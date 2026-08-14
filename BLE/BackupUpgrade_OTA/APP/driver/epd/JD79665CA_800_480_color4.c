#include "app_cfg.h"
#include "CONFIG.h"
#include "commoninfo.h"
#include "epd_driver.h"
#include "Display_EPD_W21_spi.h"
#include "epd_busy.h"

#ifdef ENABLE_INK_SCREEN_JD79665CA_800X480_COLOR_4
UINT8 EPD_Driver_GetBusyConfig(EPD_BUSY_CONFIG *cfg)
{
    cfg->supported = Is_Yes;
    cfg->active_level = EPD_BUSY_ACTIVE_LOW;
    cfg->busy_a.port = EPD_BUSY_PORT_A;
    cfg->busy_a.pin = epaper_BUSY;
    cfg->busy_b.port = EPD_BUSY_PORT_B;
    cfg->busy_b.pin = GPIO_Pin_16;
    cfg->single_a_target = EPD_BUSY_SIDE_B;
    cfg->single_b_target = EPD_BUSY_SIDE_A;

    return Is_Yes;
}

UINT16 EPD_Check_Busy(void)
{
    return EPD_Busy_WaitCurrent(200, 10000);
}

void Init_EPD_Driver(void)
{
    EPD_W21_Reset();
    delay_ms(50);
    EPD_Check_Busy();

    EPD_W21_WriteCMD(0x4D);
    EPD_W21_WriteDATA(0x78);

    EPD_W21_WriteCMD(0x00);
    EPD_W21_WriteDATA(0x2F);
    EPD_W21_WriteDATA(0x29);

    EPD_W21_WriteCMD(0xE3);
    EPD_W21_WriteDATA(0x88);

    EPD_W21_WriteCMD(0x50);
    EPD_W21_WriteDATA(0x37);

    EPD_W21_WriteCMD(0x61);
    EPD_W21_WriteDATA(0x03);
    EPD_W21_WriteDATA(0x20);
    EPD_W21_WriteDATA(0x01);
    EPD_W21_WriteDATA(0xE0);

    EPD_W21_WriteCMD(0x65);
    EPD_W21_WriteDATA(0x00);
    EPD_W21_WriteDATA(0x00);
    EPD_W21_WriteDATA(0x00);
    EPD_W21_WriteDATA(0x00);

    EPD_W21_WriteCMD(0xF0);
    EPD_W21_WriteDATA(0x5F);

    EPD_W21_WriteCMD(0xE9);
    EPD_W21_WriteDATA(0x01);

    EPD_W21_WriteCMD(0x30);
    EPD_W21_WriteDATA(0x08);
}

void Display_EPD_Driver(void)
{
    EPD_W21_WriteCMD(R04_PON);
    EPD_Check_Busy();

    EPD_W21_WriteCMD(R12_DRF);
    EPD_W21_WriteDATA(0x00);
    EPD_Busy_PrepareObserve();
}

void Init_display_Bw(void)
{

}

void Init_display_Red(void)
{
    EPD_W21_WriteCMD(DTM);

    SPI_DC_A_1;
    SPI_DC_B_1;
}

UINT32 EPD_Display_Time()
{
    return 65;
}

UINT32 EPD_GetDisplayMaxBuf()
{
    return SCREEN_800X480_COLOR_4_MAX;
}

UINT8 EPD_GetScreenType()
{
#ifdef EPD_SCREEN_TYPE_A
    return '[';
#else
    return '=';
#endif
}

#endif
