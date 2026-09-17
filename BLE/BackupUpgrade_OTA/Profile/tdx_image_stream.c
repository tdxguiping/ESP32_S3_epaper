#include <string.h>
#include "tdx_image_stream.h"
#if TDX_SMALL_STREAM_ENABLE
#include "commoninfo.h"
#include "peripheral.h"
#include "epd_driver.h"
#include "epd_busy.h"
#include "Display_EPD_W21_spi.h"
#include "zlib_image_codec.h"
#include "img_perf.h"
#include "release_trace.h"

TDX_IMAGE_MODE g_tdx_image_transfer_mode = TDX_IMAGE_SMALL_STREAM;
extern uint8_t Peripheral_TaskID;

/* The inflater has its own 4 KiB history and 2 KiB output. This is only the
 * compressed-input FIFO. All producers/consumers run cooperatively in TMOS. */
static UINT8 zip_fifo[TDX_SMALL_ZIP_BUFFER_SIZE];
static struct {
    UINT16 read, used, conn, last;
    UINT32 packets, received, bytes, batches, last_rx, pre_start, wait_start;
    UINT8 active, final, side, type, phase, ready, owned, frame, refreshing;
    UINT8 volatile_image, connected, idle_samples;
} stream;

static UINT32 elapsed(UINT32 start)
{
    UINT32 now = RTC_GetCycle32k();
    return now >= start ? now - start : RTC_MAX_COUNT - start + now;
}

static UINT32 milliseconds(UINT32 ticks)
{
    return (ticks / CAB_LSIFQ) * 1000U + ((ticks % CAB_LSIFQ) * 1000U) / CAB_LSIFQ;
}

static void schedule(UINT16 ticks)
{
    tmos_start_task(Peripheral_TaskID, SBP_IMAGE_STREAM_EVT, ticks);
}

UINT8 TIS_Active(void) { return stream.active; }
UINT8 TIS_Locked(void) { return stream.active || stream.phase || stream.refreshing; }
UINT8 TIS_HoldPower(void)
{
    return !stream.refreshing && stream.connected &&
           (stream.owned || global_DEVICE_STATUS.fWorked != Is_Yes) &&
           (stream.active || stream.phase || stream.ready);
}
UINT8 TIS_VolatileImage(void) { return stream.volatile_image; }
void TIS_InvalidatePanel(void) { stream.ready = 0; }

void TIS_Connect(void)
{
    stream.connected = 1;
    if(g_tdx_image_transfer_mode != TDX_IMAGE_SMALL_STREAM || stream.refreshing || stream.active) return;
    if(!stream.ready || global_DEVICE_STATUS.fInitDriver == Is_Yes) {
        stream.phase = 1;
        schedule(1);
    }
}

void TIS_UseLarge(void)
{
    /* Called only with no active stream or physical refresh. */
    tmos_stop_task(Peripheral_TaskID, SBP_IMAGE_STREAM_EVT);
    if(stream.owned) EPD_StreamRelease();
    stream.phase = stream.ready = stream.owned = stream.volatile_image = 0;
}

void TIS_Abort(UINT8 error, UINT8 notify)
{
    UINT8 active = stream.active;
    if(stream.refreshing) return; /* Never power down a refresh on disconnect. */
    tmos_stop_task(Peripheral_TaskID, SBP_IMAGE_STREAM_EVT);
    if(active) zlib_image_codec_abort();
    stream.active = stream.final = stream.phase = stream.ready = stream.frame = 0;
    stream.used = stream.read = 0;
    if(stream.owned) {
        EPD_StreamRelease();
        ControlEPDPower(Is_Off);
        global_DEVICE_STATUS.fWorked = Is_No;
        global_DEVICE_STATUS.fInitDriver = Is_Yes;
    }
    stream.owned = 0;
    if(active) {
        global_DEVICE_STATUS.fDataSendSuccess = Is_No;
        BOOT_LOG_HEX8("S err=", error);
        IP_Stop(error == 3 ? 3 : 1);
        TdxInfo_StreamResult(stream.conn, stream.type, notify ? error : 0xff);
    }
}

void TIS_Disconnect(void)
{
    UINT8 preparing = stream.owned && !stream.active && !stream.refreshing;
    stream.connected = 0;
    TIS_Abort(3, 0);
    if(preparing) {
        global_DEVICE_STATUS.fDataSendSuccess = Is_No;
        TdxInfo_StreamResult(0, 0, 0xff);
    }
}

void TIS_RefreshComplete(void)
{
    if(!stream.refreshing) return;
    stream.refreshing = stream.ready = stream.owned = 0;
    global_DEVICE_STATUS.fInitDriver = Is_Yes;
    /* volatile_image stays set until a real stored-image operation starts. */
}

static int pixel_sink(void *context, const UINT8 *data, UINT16 length)
{
    (void)context;
    if(!stream.active || !stream.ready || global_DEVICE_STATUS.fInitDriver == Is_Yes ||
       stream.bytes + length > EPD_GetDisplayMaxBuf()) return -1;
    if(!stream.frame) {
        EPD_StreamFrameBegin(stream.side);
        stream.frame = 1;
    }
    global_DEVICE_STATUS.fScreenType = stream.side;
    if(EPD_StreamWrite(data, length, stream.bytes)) return -1;
    stream.bytes += length; stream.batches++; stream.last = length;
    global_DEVICE_STATUS.fImageDataLen = stream.bytes;
    return 0;
}

int TIS_Begin(UINT16 conn, UINT8 type, UINT8 side, UINT32 packets)
{
    if(stream.active || stream.refreshing || TdxInfo_DisplayBusy() || !packets ||
       side < SCREEN_TYPE_IMG_A || side > SCREEN_TYPE_IMG_AB ||
       (global_DEVICE_STATUS.fWorked == Is_Yes && !stream.owned)) return -1;
    if(zlib_image_codec_begin(pixel_sink, NULL)) return -1;
    stream.conn = conn; stream.type = type; stream.side = side; stream.packets = packets;
    stream.read = stream.used = stream.last = 0;
    stream.received = stream.bytes = stream.batches = 0;
    stream.final = stream.frame = 0;
    stream.active = stream.volatile_image = 1;
    stream.last_rx = RTC_GetCycle32k();
    global_DEVICE_STATUS.fDataSendSuccess = Is_No;
    global_DEVICE_STATUS.fPackageCnt = global_DEVICE_STATUS.fImageDataLen = 0;
    global_DEVICE_STATUS.fPackageCount = packets;
    global_DEVICE_STATUS.fIsNeedStandby = 0;
    global_DEVICE_STATUS.fImageType = 0;
    global_DEVICE_STATUS.fRefreshType = DEVICE_OP_COMMON;
    global_DEVICE_STATUS.fOtaStatus = RTN_OTA_COMMON;
    if(!stream.ready || global_DEVICE_STATUS.fInitDriver == Is_Yes) {
        if(!stream.phase) stream.phase = 1;
    }
    if(stream.phase <= 1) schedule(1);
    return 0;
}

int TIS_Push(const UINT8 *data, UINT16 length)
{
    UINT16 write, first;
    if(!stream.active) return -1;
    if(!data || !length || stream.final || length > TDX_SMALL_ZIP_BUFFER_SIZE - stream.used) {
        TIS_Abort(1, 1); /* No overwrite, truncation, or Flash fallback. */
        return -1;
    }
    write = (stream.read + stream.used) % TDX_SMALL_ZIP_BUFFER_SIZE;
    first = TDX_SMALL_ZIP_BUFFER_SIZE - write;
    if(first > length) first = length;
    memcpy(zip_fifo + write, data, first);
    memcpy(zip_fifo, data + first, length - first);
    stream.used += length;
    stream.last_rx = RTC_GetCycle32k();
    stream.received++;
    global_DEVICE_STATUS.fPackageCnt = stream.received;
    IP_ZipBytes(length);
    if(stream.received == stream.packets) { stream.final = 1; IP_RxDone(); }
    /* Do not shorten a pending power/reset settling timer. */
    if(!stream.phase) schedule(1);
    return 0;
}

static UINT16 prepare_step(void)
{
    EPD_BUSY_STATUS busy;
    switch(stream.phase) {
    case 1:
        if(TdxInfo_DisplayBusy() || (global_DEVICE_STATUS.fWorked == Is_Yes && !stream.owned)) return 16;
        stream.owned = 1; stream.ready = 0;
        stream.pre_start = RTC_GetCycle32k();
        EPD_StreamPrepare(0); /* Power on; preserve the existing 200 ms settle. */
        stream.phase = 2; return 320;
    case 2:
        EPD_StreamPrepare(1); /* GPIO/SPI and simultaneous reset assertion. */
        stream.phase = 3; return 8;
    case 3:
        EPD_StreamPrepare(2); /* Release both reset pins. */
        stream.phase = 4; return 32;
    case 4:
        EPD_StreamPrepare(3); /* Broadcast register setup and power-on command. */
        stream.wait_start = RTC_GetCycle32k();
        stream.idle_samples = 0; stream.phase = 5; return 16;
    default:
        if(EPD_Busy_GetStatus(&busy) != Is_Yes) break;
        if(busy.busy_a == Is_No && busy.busy_b == Is_No) stream.idle_samples++;
        else stream.idle_samples = 0;
        if(stream.idle_samples >= 2 && elapsed(stream.wait_start) >= CAB_LSIFQ / 10U) {
            stream.ready = 1; stream.phase = 0;
            global_DEVICE_STATUS.fInitDriver = Is_No;
            BOOT_LOG_HEX32("PRE ms=", milliseconds(elapsed(stream.pre_start)));
            BOOT_LOG_HEX8("PRE result=", 0);
            return stream.active ? 1 : 0;
        }
        if(elapsed(stream.wait_start) < 10U * CAB_LSIFQ) return 16;
        break;
    }
    BOOT_LOG_HEX8("PRE result=", 1);
    TIS_Abort(2, 1);
    return 0;
}

UINT16 TIS_Process(void)
{
    UINT16 length, consumed = 0;
    int rc;
    if(stream.phase) return prepare_step();
    if(!stream.active) return 0;
    if(!stream.ready || global_DEVICE_STATUS.fInitDriver == Is_Yes) { TIS_Abort(2, 1); return 0; }
    if(elapsed(stream.last_rx) > 30U * CAB_LSIFQ && !stream.final) { TIS_Abort(3, 1); return 0; }
    length = TDX_SMALL_ZIP_BUFFER_SIZE - stream.read;
    if(length > stream.used) length = stream.used;
    rc = zlib_image_codec_pump(zip_fifo + stream.read, length, &consumed);
    stream.read = (stream.read + consumed) % TDX_SMALL_ZIP_BUFFER_SIZE;
    stream.used -= consumed;
    if(rc < 0 || (rc == 1 && stream.used)) { TIS_Abort(4, 1); return 0; }
    if(rc == 1 && stream.final) {
        if(zlib_image_codec_finish() || stream.bytes != EPD_GetDisplayMaxBuf()) {
            TIS_Abort(4, 1); return 0;
        }
        stream.active = 0; stream.refreshing = 1;
        global_DEVICE_STATUS.fScreenType = stream.side;
        global_DEVICE_STATUS.fIsNeedStandby = 1;
        global_DEVICE_STATUS.fDataSendSuccess = Is_Yes;
        EPD_SetRefreshDeferred(0);
        IP_FlowFreeze();
        Display_EPD_AB();
        /* Printing and BLE notifications cannot delay the measured endpoint. */
        BOOT_LOG_HEX8("MODE=", TDX_IMAGE_SMALL_STREAM);
        BOOT_LOG_HEX8("SIDE=", stream.side);
        BOOT_LOG_HEX32("IMG batch=", IMG_ZLIB_BATCH);
        BOOT_LOG_TEXT("IMG done\r\n");
        BOOT_LOG_HEX32("IMG n=", stream.batches);
        BOOT_LOG_HEX32("IMG bytes=", stream.bytes);
        BOOT_LOG_HEX32("IMG last=", stream.last);
        IP_FlowReport();
        TdxInfo_StreamResult(stream.conn, stream.type, 0);
        return 0;
    }
    if(rc == 0 && stream.final && !stream.used) { TIS_Abort(4, 1); return 0; }
    return (rc == 2 || stream.used) ? 1 : 16;
}
#endif
