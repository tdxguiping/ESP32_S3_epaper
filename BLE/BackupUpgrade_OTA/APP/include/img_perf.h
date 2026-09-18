#ifndef IMG_PERF_H
#define IMG_PERF_H
#include "CH58x_common.h"
#include "app_cfg.h"

/* Build overrides allow measuring each optimization separately. */
#ifndef IMG_PERF_ENABLE
#if TDX_STORE_ZLIB && defined(ENABLE_SCREEN_COLOR_6)
#define IMG_PERF_ENABLE 1
#else
#define IMG_PERF_ENABLE 0
#endif
#endif
#ifndef IMG_PANEL_SPI_DIV
#define IMG_PANEL_SPI_DIV 8U
#endif
#ifndef IMG_ZLIB_BATCH
#define IMG_ZLIB_BATCH 4095U
#endif
#if TDX_STORE_ZLIB && (IMG_ZLIB_BATCH < 1 || IMG_ZLIB_BATCH > 32768U)
#error IMG_ZLIB_BATCH_must_fit_imageBatchBuffer
#endif
#ifndef IMG_COLOR_LUT
#define IMG_COLOR_LUT 1
#endif
#ifndef TDX_LARGE_RAW_COLOR_DISABLE
#define TDX_LARGE_RAW_COLOR_DISABLE 0
#endif
#ifndef TDX_LARGE_RAW_DIRECT
#define TDX_LARGE_RAW_DIRECT 0
#endif
#ifndef TDX_DISABLE_EPD_DELAY
#define TDX_DISABLE_EPD_DELAY 0
#endif
#ifndef TDX_DISABLE_FLASH_DELAY
#define TDX_DISABLE_FLASH_DELAY 0
#endif
#ifndef TDX_FLASH_POWERUP_DELAY_MS
#define TDX_FLASH_POWERUP_DELAY_MS 10U
#endif
#ifndef TDX_FLASH_WIP_LEGACY_DELAY
#define TDX_FLASH_WIP_LEGACY_DELAY 0
#endif

/* P id is hexadecimal; P ms is elapsed milliseconds, also hexadecimal.
 * Parent totals overlap their children. DECODE excludes OUTPUT callbacks.
 * Counts are completed timed spans, not BLE radio packets or CPU cycles. */
enum {
    IP_RX_SPAN, IP_RX_WORK, IP_AES, IP_COPY, IP_HEADER, IP_COMMIT,
    IP_NOTIFY, IP_QUEUE, IP_REPLAY, IP_READ, IP_ERASE, IP_WRITE, IP_VERIFY,
    IP_DECODE, IP_COLOR, IP_INIT, IP_OUTPUT, IP_SPI, IP_REFRESH, IP_ACTIVE,
    IP_TAIL_WAIT, IP_SAVE, IP_TOTAL, IP_SIDE_WAIT, IP_FLASH_INIT, IP_COUNT
};
/* Result: 0=complete, 1=error, 2=BUSY timeout, 3=interrupted,
 *         4=stored without refresh, 5=BUSY unsupported (time-based finish). */
#if IMG_PERF_ENABLE
UINT32 IP_Now(void);
UINT32 IP_Start(UINT8 id);
UINT32 IP_Diff(UINT32 end, UINT32 start);
UINT32 IP_Value(UINT8 id);
void IP_Add(UINT8 id, UINT32 ticks);
void IP_Toc(UINT8 id, UINT32 start);
void IP_RxEnter(UINT8 first, UINT16 bytes);
void IP_RxExit(UINT8 error);
void IP_RxDone(void);
void IP_ZipBytes(UINT16 bytes);
void IP_Queue(void);
void IP_Dequeue(void);
void IP_ReplayBegin(UINT8 side);
void IP_ReplayEnd(void);
void IP_Pixels(UINT8 enabled);
void IP_SpiCall(UINT16 bytes);
void IP_Refresh(void);
void IP_BusySeen(void);
void IP_BusyDone(UINT8 status);
void IP_Tail(void);
void IP_Stop(UINT8 status);
void IP_FinishTail(void);
void IP_Disconnect(void);
void IP_FlowFreeze(void);
void IP_FlowReport(void);
void IP_FlowMark(void);
#else
#define IP_Now() 0U
#define IP_Start(i) 0U
#define IP_Diff(e,s) 0U
#define IP_Value(i) 0U
#define IP_Add(i,t) ((void)0)
#define IP_Toc(i,s) ((void)0)
#define IP_RxEnter(f,b) ((void)0)
#define IP_RxExit(e) ((void)0)
#define IP_RxDone() ((void)0)
#define IP_ZipBytes(b) ((void)0)
#define IP_Queue() ((void)0)
#define IP_Dequeue() ((void)0)
#define IP_ReplayBegin(s) ((void)0)
#define IP_ReplayEnd() ((void)0)
#define IP_Pixels(e) ((void)0)
#define IP_SpiCall(b) ((void)0)
#define IP_Refresh() ((void)0)
#define IP_BusySeen() ((void)0)
#define IP_BusyDone(s) ((void)0)
#define IP_Tail() ((void)0)
#define IP_Stop(s) ((void)0)
#define IP_FinishTail() ((void)0)
#define IP_Disconnect() ((void)0)
#define IP_FlowFreeze() ((void)0)
#define IP_FlowReport() ((void)0)
#define IP_FlowMark() ((void)0)
#endif
#endif
