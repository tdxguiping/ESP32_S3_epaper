#ifndef EPD_BUSY_H
#define EPD_BUSY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CONFIG.h"

#define EPD_BUSY_SIDE_NONE          0x00
#define EPD_BUSY_SIDE_A             0x01
#define EPD_BUSY_SIDE_B             0x02
#define EPD_BUSY_SIDE_AB            (EPD_BUSY_SIDE_A | EPD_BUSY_SIDE_B)

#define EPD_BUSY_ACTIVE_LOW         0
#define EPD_BUSY_ACTIVE_HIGH        1

#define EPD_BUSY_PORT_NONE          0
#define EPD_BUSY_PORT_A             1
#define EPD_BUSY_PORT_B             2

typedef struct
{
    UINT8 port;
    UINT32 pin;
} EPD_BUSY_PIN;

typedef struct
{
    UINT8 supported;
    UINT8 active_level;
    EPD_BUSY_PIN busy_a;
    EPD_BUSY_PIN busy_b;
    UINT8 single_a_target;
    UINT8 single_b_target;
    UINT8 ab_target;
    UINT8 diff_target;
    UINT8 debug_enabled;
} EPD_BUSY_CONFIG;

typedef struct
{
    UINT8 supported;
    UINT8 target_sides;
    UINT8 raw_a;
    UINT8 raw_b;
    UINT8 busy_a;
    UINT8 busy_b;
    UINT8 any_busy;
    UINT8 all_idle;
} EPD_BUSY_STATUS;

UINT8 EPD_Driver_GetBusyConfig(EPD_BUSY_CONFIG *cfg);
UINT8 EPD_Busy_IsSupported(void);
void EPD_Busy_PrepareObserve(void);
UINT8 EPD_Busy_GetCurrentTargetSides(void);
UINT8 EPD_Busy_GetLowPowerTargetSides(void);
UINT8 EPD_Busy_GetTargetSides(void);
UINT8 EPD_Busy_GetStatus(EPD_BUSY_STATUS *status);
UINT16 EPD_Busy_WaitCurrent(UINT16 active_timeout_ms, UINT16 release_timeout_ms);
char is_Busy(void);

#ifdef __cplusplus
}
#endif

#endif
