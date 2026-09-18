#include <string.h>
#include "tdx_image_stream.h"
#if TDX_SMALL_STREAM_ENABLE
#include "commoninfo.h"
#include "peripheral.h"
#include "epd_driver.h"
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
    UINT32 packets, received, bytes, batches, last_rx;
    UINT32 flow_start, final_at, rx_ticks, decode_ticks, decode_only_ticks, spi_ticks;
    UINT32 finish_ticks, refresh_ticks, pump_max_ticks;
    UINT8 active, final, side, type, frame, refreshing;
    UINT8 volatile_image, connected, fail_reported;
    UINT32 pump_calls, input_chunks, event_calls, max_used, process_ticks, event_slices;
    UINT32 pump_input, yield_calls, stalled_calls;
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
UINT8 TIS_Locked(void) { return stream.active || stream.refreshing; }
UINT8 TIS_HoldPower(void)
{
    /* Keep the panel powered only while receiving/decoding.  Once the
     * refresh command is issued, the normal BUSY monitor may run. */
    return stream.active;
}
UINT8 TIS_VolatileImage(void) { return stream.volatile_image; }

void TIS_Connect(void)
{
    stream.connected = 1;
}

void TIS_UseLarge(void)
{
    /* Called only with no active stream or physical refresh. */
    tmos_stop_task(Peripheral_TaskID, SBP_IMAGE_STREAM_EVT);
    stream.active = stream.refreshing = stream.frame = stream.volatile_image = 0;
}

void TIS_Abort(UINT8 error, UINT8 notify)
{
    UINT8 active = stream.active;
    UINT8 frame = stream.frame;
    if(stream.refreshing) return; /* Never power down a refresh on disconnect. */
    tmos_stop_task(Peripheral_TaskID, SBP_IMAGE_STREAM_EVT);
    if(active) zlib_image_codec_abort();
    stream.active = stream.final = stream.frame = 0;
    stream.used = stream.read = 0;
    if(frame) {
        EPD_SetRefreshDeferred(0);
        global_DEVICE_STATUS.fInitDriver = Is_Yes;
    }
    if(active) {
        global_DEVICE_STATUS.fDataSendSuccess = Is_No;
        BOOT_LOG_HEX8("S err=", error);
        IP_Stop(error == 3 ? 3 : 1);
        TdxInfo_StreamResult(stream.conn, stream.type, notify ? error : 0xff);
    }
}

void TIS_Disconnect(void)
{
    stream.connected = 0;
    TIS_Abort(3, 0);
}

void TIS_RefreshComplete(void)
{
    if(!stream.refreshing) return;
    /* BUSY completion ends the stream lock; the shared large-path EPD flow
     * initializes the next frame when its first output batch arrives. */
    stream.refreshing = 0;
    global_DEVICE_STATUS.fInitDriver = Is_No;
    BOOT_LOG_TEXT("S refresh-done\r\n");
    /* volatile_image stays set until a real stored-image operation starts. */
}

static int pixel_sink(void *context, const UINT8 *data, UINT16 length)
{
    UINT32 spi_start;
    (void)context;
    if(!stream.active || !data || !length || stream.bytes + length > EPD_GetDisplayMaxBuf()) return -1;
    if(!stream.frame) {
        global_DEVICE_STATUS.fScreenType = stream.side;
        global_DEVICE_STATUS.fInitDriver = Is_Yes;
        global_DEVICE_STATUS.fImageDataLen = 0;
        global_DEVICE_STATUS.fIsNeedStandby = 1;
        EPD_SetRefreshDeferred(1);
        stream.frame = 1;
    }
    global_DEVICE_STATUS.fScreenType = stream.side;
    spi_start = RTC_GetCycle32k();
    if(refreshScreenColor((UINT8 *)data, length, IS_NONEED_DECMPRESS) < 0) return -1;
    stream.spi_ticks += elapsed(spi_start);
    stream.bytes += length; stream.batches++; stream.last = length;
    return 0;
}

int TIS_Begin(UINT16 conn, UINT8 type, UINT8 side, UINT32 packets)
{
    UINT8 fail_reason = 0;
    UINT8 state = 0;
    if(stream.active) fail_reason = 1;
    else if(stream.refreshing) fail_reason = 2;
    else if(TdxInfo_DisplayBusy()) fail_reason = 3;
    else if(!packets) fail_reason = 4;
    else if(side < SCREEN_TYPE_IMG_A || side > SCREEN_TYPE_IMG_AB) fail_reason = 5;
    if(stream.active) state |= 0x01;
    if(stream.refreshing) state |= 0x02;
    if(TdxInfo_DisplayBusy()) state |= 0x04;
    if(stream.frame) state |= 0x08;
    if(fail_reason) {
        BOOT_LOG_HEX8("S begin=", fail_reason);
        BOOT_LOG_HEX8("S state=", state);
        BOOT_LOG_HEX8("S side=", side);
        BOOT_LOG_HEX32("S pk=", packets);
        return -1;
    }
    if(zlib_image_codec_begin(pixel_sink, NULL)) {
        BOOT_LOG_HEX8("S begin=", 7);
        BOOT_LOG_HEX8("S state=", state);
        return -1;
    }
    BOOT_LOG_HEX32("S begin-ok=", packets);
    stream.conn = conn; stream.type = type; stream.side = side; stream.packets = packets;
    stream.read = stream.used = stream.last = 0;
    stream.received = stream.bytes = stream.batches = 0;
    stream.rx_ticks = stream.decode_ticks = stream.decode_only_ticks = stream.spi_ticks = 0;
    stream.final_at = 0;
    stream.finish_ticks = stream.refresh_ticks = stream.pump_max_ticks = 0;
    stream.pump_calls = stream.input_chunks = 0;
    stream.event_calls = stream.max_used = stream.process_ticks = stream.event_slices = 0;
    stream.pump_input = stream.yield_calls = stream.stalled_calls = 0;
    stream.final = stream.frame = 0;
    stream.fail_reported = 0;
    stream.active = stream.volatile_image = 1;
    stream.flow_start = stream.last_rx = RTC_GetCycle32k();
    global_DEVICE_STATUS.fDataSendSuccess = Is_No;
    global_DEVICE_STATUS.fPackageCnt = global_DEVICE_STATUS.fImageDataLen = 0;
    global_DEVICE_STATUS.fPackageCount = packets;
    global_DEVICE_STATUS.fIsNeedStandby = 0;
    global_DEVICE_STATUS.fImageType = 0;
    global_DEVICE_STATUS.fRefreshType = DEVICE_OP_COMMON;
    global_DEVICE_STATUS.fOtaStatus = RTN_OTA_COMMON;
    schedule(1);
    return 0;
}

int TIS_Push(const UINT8 *data, UINT16 length)
{
    UINT16 write, first;
    UINT8 fail_reason = 0;
    if(!stream.active) fail_reason = 1;
    else if(!data || !length) fail_reason = 2;
    else if(stream.final) fail_reason = 3;
    else if(length > TDX_SMALL_ZIP_BUFFER_SIZE - stream.used) fail_reason = 4;
    if(fail_reason) {
        if(!stream.fail_reported) {
            stream.fail_reported = 1;
            BOOT_LOG_HEX8("S why=", fail_reason);
            BOOT_LOG_HEX32("S used=", stream.used);
            BOOT_LOG_HEX32("S len=", length);
            BOOT_LOG_HEX32("S rx=", stream.received);
            BOOT_LOG_HEX32("S pk=", stream.packets);
            BOOT_LOG_HEX8("S final=", stream.final);
            BOOT_LOG_HEX32("S maxq=", stream.max_used);
        }
        if(stream.active) TIS_Abort(1, 1); /* No overwrite, truncation, or Flash fallback. */
        return -1;
    }
    write = (stream.read + stream.used) % TDX_SMALL_ZIP_BUFFER_SIZE;
    first = TDX_SMALL_ZIP_BUFFER_SIZE - write;
    if(first > length) first = length;
    memcpy(zip_fifo + write, data, first);
    memcpy(zip_fifo, data + first, length - first);
    stream.used += length;
    if(stream.used > stream.max_used) stream.max_used = stream.used;
    stream.last_rx = RTC_GetCycle32k();
    stream.received++;
    stream.input_chunks++;
    if(stream.received == 1) BOOT_LOG_HEX32("S push-len=", length);
    global_DEVICE_STATUS.fPackageCnt = stream.received;
    IP_ZipBytes(length);
    if(stream.received == stream.packets) {
        stream.final = 1;
        stream.final_at = RTC_GetCycle32k();
        stream.rx_ticks = elapsed(stream.flow_start);
        IP_RxDone();
    }
    schedule(1);
    return 0;
}

UINT16 TIS_Process(void)
{
    UINT16 length, consumed = 0;
    int rc = 0;
    UINT8 slices = 0;
    UINT32 process_start = RTC_GetCycle32k();
    stream.event_calls++;
    if(!stream.active) return 0;
    if(elapsed(stream.last_rx) > 30U * CAB_LSIFQ && !stream.final) { TIS_Abort(3, 1); return 0; }
    while(slices < TDX_STREAM_SLICES_PER_EVENT) {
        length = TDX_SMALL_ZIP_BUFFER_SIZE - stream.read;
        if(length > stream.used) length = stream.used;
        if(!length) break;
        {
            UINT32 decode_start = RTC_GetCycle32k();
            UINT32 spi_before = stream.spi_ticks;
            UINT32 decode_elapsed;
            UINT32 spi_elapsed;
            stream.pump_calls++;
            rc = zlib_image_codec_pump(zip_fifo + stream.read, length, &consumed);
            stream.pump_input += consumed;
            if(rc == 2) stream.yield_calls++;
            if(!consumed) stream.stalled_calls++;
            decode_elapsed = elapsed(decode_start);
            spi_elapsed = stream.spi_ticks - spi_before;
            stream.decode_ticks += decode_elapsed;
            if(decode_elapsed > stream.pump_max_ticks) stream.pump_max_ticks = decode_elapsed;
            if(decode_elapsed >= spi_elapsed) stream.decode_only_ticks += decode_elapsed - spi_elapsed;
        }
        stream.read = (stream.read + consumed) % TDX_SMALL_ZIP_BUFFER_SIZE;
        stream.used -= consumed;
        slices++;
        if(rc < 0 || rc == 1 || !consumed || rc == 0) break;
    }
    stream.event_slices += slices;
    stream.process_ticks += elapsed(process_start);
    if(rc < 0 || (rc == 1 && stream.used)) {
        BOOT_LOG_HEX8("S rc=", rc);
        BOOT_LOG_HEX32("S used=", stream.used);
        BOOT_LOG_HEX32("S out=", stream.bytes);
        TIS_Abort(4, 1); return 0;
    }
    if(rc == 1 && stream.final) {
        {
            UINT32 finish_start = RTC_GetCycle32k();
            int finish_result = zlib_image_codec_finish();
            stream.finish_ticks = elapsed(finish_start);
            if(finish_result || stream.bytes != EPD_GetDisplayMaxBuf()) {
                BOOT_LOG_HEX8("S fin=", finish_result);
                BOOT_LOG_HEX32("S out=", stream.bytes);
                TIS_Abort(4, 1); return 0;
            }
        }
        stream.active = 0; stream.refreshing = 1;
        global_DEVICE_STATUS.fScreenType = stream.side;
        global_DEVICE_STATUS.fIsNeedStandby = 1;
        global_DEVICE_STATUS.fDataSendSuccess = Is_Yes;
        EPD_SetRefreshDeferred(0);
        IP_FlowFreeze();
        {
            UINT32 refresh_start = RTC_GetCycle32k();
            Display_EPD_AB();
            stream.refresh_ticks = elapsed(refresh_start);
        }
        /* Printing and BLE notifications cannot delay the measured endpoint. */
        BOOT_LOG_TEXT("IMG SW=V35\r\n");
        BOOT_LOG_HEX8("MODE=", TDX_IMAGE_SMALL_STREAM);
        BOOT_LOG_HEX8("SIDE=", stream.side);
        BOOT_LOG_HEX32("T RX=", milliseconds(stream.rx_ticks));
        BOOT_LOG_HEX32("T ZIP+OUT=", milliseconds(stream.decode_ticks));
        BOOT_LOG_HEX32("T DECODE=", milliseconds(stream.decode_only_ticks));
        BOOT_LOG_HEX32("T PMAX=", milliseconds(stream.pump_max_ticks));
        BOOT_LOG_HEX32("T FINISH=", milliseconds(stream.finish_ticks));
        BOOT_LOG_HEX32("T SPI=", milliseconds(stream.spi_ticks));
        BOOT_LOG_HEX32("T EPD=", milliseconds(stream.refresh_ticks));
        BOOT_LOG_HEX32("T FLOW=", milliseconds(elapsed(stream.flow_start)));
        BOOT_LOG_HEX32("T FINAL=", milliseconds(elapsed(stream.final_at)));
        BOOT_LOG_HEX32("T PUMP=", stream.pump_calls);
        BOOT_LOG_HEX32("T ZIN=", stream.pump_input);
        BOOT_LOG_HEX32("T YLD=", stream.yield_calls);
        BOOT_LOG_HEX32("T STP=", stream.stalled_calls);
        BOOT_LOG_HEX32("T INN=", stream.input_chunks);
        BOOT_LOG_HEX32("T EVT=", stream.event_calls);
        BOOT_LOG_HEX32("T SLICE=", stream.event_slices);
        BOOT_LOG_HEX32("T AVG=", stream.event_calls ? stream.event_slices / stream.event_calls : 0);
        BOOT_LOG_HEX32("T MAXQ=", stream.max_used);
        BOOT_LOG_HEX32("T PROC=", milliseconds(stream.process_ticks));
        BOOT_LOG_HEX32("IMG batch=", IMG_ZLIB_BATCH);
        BOOT_LOG_TEXT("IMG done\r\n");
        BOOT_LOG_HEX32("IMG n=", stream.batches);
        BOOT_LOG_HEX32("IMG bytes=", stream.bytes);
        BOOT_LOG_HEX32("IMG last=", stream.last);
        IP_FlowReport();
        TdxInfo_StreamResult(stream.conn, stream.type, 0);
        return 0;
    }
    if(rc == 0 && stream.final && !stream.used) {
        BOOT_LOG_TEXT("S empty\r\n");
        TIS_Abort(4, 1); return 0;
    }
    return (rc == 2 || stream.used) ? 1 : 16;
}
#endif
