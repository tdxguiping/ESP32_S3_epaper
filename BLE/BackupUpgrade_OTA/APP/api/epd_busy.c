#include "CONFIG.h"
#include "app_cfg.h"
#include "commoninfo.h"
#include "epd_busy.h"

static UINT32 EPD_Busy_GetLegacyBusyGpio(void)
{
#ifndef ENABLE_SCREEN_COLOR_6
    return GPIO_Pin_16;
#else
    return GPIO_Pin_9;
#endif
}

static void EPD_Busy_ClearConfig(EPD_BUSY_CONFIG *cfg)
{
    if(cfg == NULL)
    {
        return;
    }

    cfg->supported = Is_No;
    cfg->active_level = EPD_BUSY_ACTIVE_HIGH;
    cfg->busy_a.port = EPD_BUSY_PORT_NONE;
    cfg->busy_a.pin = 0;
    cfg->busy_b.port = EPD_BUSY_PORT_NONE;
    cfg->busy_b.pin = 0;
    cfg->single_a_target = EPD_BUSY_SIDE_A;
    cfg->single_b_target = EPD_BUSY_SIDE_B;
    cfg->ab_target = EPD_BUSY_SIDE_AB;
    cfg->diff_target = EPD_BUSY_SIDE_AB;
    cfg->debug_enabled = Is_No;
}

__attribute__((weak)) UINT8 EPD_Driver_GetBusyConfig(EPD_BUSY_CONFIG *cfg)
{
    EPD_Busy_ClearConfig(cfg);
    return Is_No;
}

static UINT8 EPD_Busy_LoadConfig(EPD_BUSY_CONFIG *cfg)
{
    EPD_Busy_ClearConfig(cfg);
    if(EPD_Driver_GetBusyConfig(cfg) != Is_Yes)
    {
        return Is_No;
    }
    return (cfg->supported == Is_Yes) ? Is_Yes : Is_No;
}

static UINT8 EPD_Busy_RawToBusy(UINT8 raw_level, UINT8 active_level)
{
    if(active_level == EPD_BUSY_ACTIVE_LOW)
    {
        return (raw_level == 0) ? Is_Yes : Is_No;
    }
    return (raw_level != 0) ? Is_Yes : Is_No;
}

static void EPD_Busy_ModeInputPullup(EPD_BUSY_PIN pin)
{
    if(pin.pin == 0)
    {
        return;
    }

    if(pin.port == EPD_BUSY_PORT_A)
    {
        GPIOA_ModeCfg(pin.pin, GPIO_ModeIN_PU);
    }
    else if(pin.port == EPD_BUSY_PORT_B)
    {
        GPIOB_ModeCfg(pin.pin, GPIO_ModeIN_PU);
    }
}

static UINT8 EPD_Busy_ReadPinRaw(EPD_BUSY_PIN pin)
{
    UINT32 port_value;

    if(pin.pin == 0)
    {
        return 0;
    }

    if(pin.port == EPD_BUSY_PORT_A)
    {
        port_value = GPIOA_ReadPort();
        return ((port_value & pin.pin) == pin.pin) ? 1 : 0;
    }
    if(pin.port == EPD_BUSY_PORT_B)
    {
        port_value = GPIOB_ReadPort();
        return ((port_value & pin.pin) == pin.pin) ? 1 : 0;
    }

    return 0;
}

UINT8 EPD_Busy_IsSupported(void)
{
    EPD_BUSY_CONFIG cfg;
    return EPD_Busy_LoadConfig(&cfg);
}

void EPD_Busy_PrepareObserve(void)
{
    EPD_BUSY_CONFIG cfg;

    if(EPD_Busy_LoadConfig(&cfg) != Is_Yes)
    {
        return;
    }

    EPD_Busy_ModeInputPullup(cfg.busy_a);
    EPD_Busy_ModeInputPullup(cfg.busy_b);
    mDelayuS(10);
}

UINT8 EPD_Busy_GetCurrentTargetSides(void)
{
    EPD_BUSY_CONFIG cfg;

    if(EPD_Busy_LoadConfig(&cfg) != Is_Yes)
    {
        return EPD_BUSY_SIDE_NONE;
    }

    if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_A)
    {
        return cfg.single_a_target;
    }
    if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_B)
    {
        return cfg.single_b_target;
    }
    if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_AB)
    {
        return cfg.ab_target;
    }

    return EPD_BUSY_SIDE_NONE;
}

UINT8 EPD_Busy_GetLowPowerTargetSides(void)
{
    EPD_BUSY_CONFIG cfg;

    if(EPD_Busy_LoadConfig(&cfg) != Is_Yes)
    {
        return EPD_BUSY_SIDE_NONE;
    }

    if(global_DEVICE_STATUS.fImageType == 1)
    {
        return cfg.diff_target;
    }

    if((global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_B) &&
       (global_EXTERN_FLASH_INFO.fImageIndex == SCREEN_A_COMMON_INDEX))
    {
        return cfg.diff_target;
    }

    return EPD_Busy_GetCurrentTargetSides();
}

UINT8 EPD_Busy_GetTargetSides(void)
{
    return EPD_Busy_GetLowPowerTargetSides();
}

static UINT8 EPD_Busy_GetStatusByTarget(EPD_BUSY_STATUS *status, UINT8 target_sides)
{
    EPD_BUSY_CONFIG cfg;

    if(status == NULL)
    {
        return Is_No;
    }

    if(EPD_Busy_LoadConfig(&cfg) != Is_Yes)
    {
        status->supported = Is_No;
        status->target_sides = EPD_BUSY_SIDE_NONE;
        status->raw_a = 0;
        status->raw_b = 0;
        status->busy_a = Is_No;
        status->busy_b = Is_No;
        status->any_busy = Is_No;
        status->all_idle = Is_No;
        return Is_No;
    }

    status->supported = Is_Yes;
    status->target_sides = target_sides;
    status->raw_a = EPD_Busy_ReadPinRaw(cfg.busy_a);
    status->raw_b = EPD_Busy_ReadPinRaw(cfg.busy_b);
    status->busy_a = EPD_Busy_RawToBusy(status->raw_a, cfg.active_level);
    status->busy_b = EPD_Busy_RawToBusy(status->raw_b, cfg.active_level);
    status->any_busy = Is_No;
    status->all_idle = (target_sides == EPD_BUSY_SIDE_NONE) ? Is_No : Is_Yes;

    if((target_sides & EPD_BUSY_SIDE_A) && (status->busy_a == Is_Yes))
    {
        status->any_busy = Is_Yes;
        status->all_idle = Is_No;
    }
    if((target_sides & EPD_BUSY_SIDE_B) && (status->busy_b == Is_Yes))
    {
        status->any_busy = Is_Yes;
        status->all_idle = Is_No;
    }

    return Is_Yes;
}

UINT8 EPD_Busy_GetStatus(EPD_BUSY_STATUS *status)
{
    return EPD_Busy_GetStatusByTarget(status, EPD_Busy_GetLowPowerTargetSides());
}

UINT16 EPD_Busy_WaitCurrent(UINT16 active_timeout_ms, UINT16 release_timeout_ms)
{
    EPD_BUSY_STATUS status;
    UINT16 active_count;
    UINT16 release_count;
    UINT16 active_limit;
    UINT16 release_limit;
    UINT8 had_active;
    UINT8 target_sides;

    EPD_Busy_PrepareObserve();
    target_sides = EPD_Busy_GetCurrentTargetSides();
    if(target_sides == EPD_BUSY_SIDE_NONE)
    {
        Print_I3("BW noT");
        return Is_OK;
    }

    active_limit = active_timeout_ms / 2;
    if(active_limit == 0)
    {
        active_limit = 1;
    }
    release_limit = release_timeout_ms / 2;
    if(release_limit == 0)
    {
        release_limit = 1;
    }

    had_active = Is_No;
    for(active_count = 0; active_count < active_limit; active_count++)
    {
        if(EPD_Busy_GetStatusByTarget(&status, target_sides) != Is_Yes)
        {
            Print_I3("BW uns");
            return Is_Er;
        }
        if(status.any_busy == Is_Yes)
        {
            had_active = Is_Yes;
            break;
        }
        DelayMs(2);
    }

    if(had_active == Is_No)
    {
        Print_I3("BW idle t=%x rA=%d rB=%d bA=%d bB=%d",
                 target_sides, status.raw_a, status.raw_b, status.busy_a, status.busy_b);
        return Is_OK;
    }

    for(release_count = 0; release_count < release_limit; release_count++)
    {
        if(EPD_Busy_GetStatusByTarget(&status, target_sides) != Is_Yes)
        {
            Print_I3("BW unsR");
            return Is_Er;
        }
        if(status.all_idle == Is_Yes)
        {
            Print_I3("BW ok t=%x a=%d r=%d rA=%d rB=%d",
                     target_sides, active_count, release_count, status.raw_a, status.raw_b);
            return Is_OK;
        }
        DelayMs(2);
    }

    Print_I3("BW to t=%x r=%d rA=%d rB=%d bA=%d bB=%d",
             target_sides, release_count, status.raw_a, status.raw_b, status.busy_a, status.busy_b);
    return Is_Er;
}

char is_Busy(void)
{
    UINT32 key;
    EPD_BUSY_STATUS busy_status;

    if(EPD_Busy_GetStatusByTarget(&busy_status, EPD_Busy_GetCurrentTargetSides()) == Is_Yes)
    {
        return (busy_status.any_busy == Is_Yes) ? Is_Busying : Is_Over;
    }

#if defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4)
    if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_AB)
    {
        UINT32 key_a = GPIOA_ReadPort();
        UINT32 key_b = GPIOB_ReadPort();
        if ((key_a & epaper_BUSY) == 0)
        {
            return 0;
        }
        if ((key_b & EPD_Busy_GetLegacyBusyGpio()) == 0)
        {
            return 0;
        }
        return 1;
    }
    if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_B)
    {
        key = GPIOA_ReadPort();
        if ((key & epaper_BUSY) == 0)
        {
            return 0;
        }
        return 1;
    }
    key = GPIOB_ReadPort();
    if ((key & EPD_Busy_GetLegacyBusyGpio()) == 0)
    {
        return 0;
    }
    return 1;
#endif

    if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_B)
    {
        key = GPIOA_ReadPort();
#if (defined(ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4))
        if ((key & epaper_BUSY) == 0)
#else
        if ((key & epaper_BUSY) == epaper_BUSY)
#endif
        {
            return 0;
        }
    }
    else
    {
#ifndef ENABLE_SCREEN_COLOR_6
        key = GPIOB_ReadPort();
#else
        key = GPIOA_ReadPort();
#endif
#if (defined(ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4))
        if ((key & EPD_Busy_GetLegacyBusyGpio()) == 0)
#else
        if ((key & EPD_Busy_GetLegacyBusyGpio()) == EPD_Busy_GetLegacyBusyGpio())
#endif
        {
            return 0;
        }
    }

    return 1;
}
