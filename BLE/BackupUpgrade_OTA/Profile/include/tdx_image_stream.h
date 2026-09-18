#ifndef TDX_IMAGE_STREAM_H
#define TDX_IMAGE_STREAM_H
#include "CONFIG.h"
#include "app_cfg.h"

#ifndef TDX_SMALL_STREAM_ENABLE
#if TDX_STORE_ZLIB && defined(ENABLE_INK_SCREEN_SPD1657_800X480_COLOR_6) && defined(ENABLE_SCREEN_COLOR_6)
#define TDX_SMALL_STREAM_ENABLE 1
#else
#define TDX_SMALL_STREAM_ENABLE 0
#endif
#endif

#if TDX_SMALL_STREAM_ENABLE
typedef enum {
    TDX_IMAGE_SMALL_STREAM = 0,
    TDX_IMAGE_LARGE_FLASH = 1
} TDX_IMAGE_MODE;
extern TDX_IMAGE_MODE g_tdx_image_transfer_mode;
#define TDX_SMALL_ZIP_BUFFER_SIZE 55296U
#define TDX_STREAM_WORK_LIMIT 4095U
#define TDX_STREAM_SLICES_PER_EVENT 3U

void TIS_Connect(void);
void TIS_Disconnect(void);
UINT16 TIS_Process(void); /* next event delay in TMOS ticks, zero=idle */
int TIS_Begin(UINT16 conn, UINT8 type, UINT8 side, UINT32 packets);
int TIS_Push(const UINT8 *data, UINT16 length);
void TIS_Abort(UINT8 error, UINT8 notify);
void TIS_UseLarge(void);
UINT8 TIS_Active(void);
UINT8 TIS_Locked(void);
UINT8 TIS_HoldPower(void);
UINT8 TIS_VolatileImage(void);
void TIS_RefreshComplete(void);

/* Service owns BLE notification formatting and the legacy receive flags. */
UINT8 TdxInfo_DisplayBusy(void);
void TdxInfo_StreamResult(UINT16 conn, UINT8 type, UINT8 error);
#else
#define TIS_Connect() ((void)0)
#define TIS_Disconnect() ((void)0)
#define TIS_Locked() 0
#define TIS_HoldPower() 0
#define TIS_VolatileImage() 0
#define TIS_RefreshComplete() ((void)0)
#define TIS_UseLarge() ((void)0)
#endif
#endif
