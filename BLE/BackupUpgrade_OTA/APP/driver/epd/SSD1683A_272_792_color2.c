#include "app_cfg.h"
#include "CONFIG.h"
#include "commoninfo.h"
#include "epd_driver.h"
#include "Display_EPD_W21_spi.h"

#ifdef ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2
#define LCD_XSIZE 792
#define LCD_YSIZE 272
#define HALF_SOURCE_BYTES 50
#define HALF_WINDOW_END (HALF_SOURCE_BYTES - 1)

UINT16 EPD_Check_Busy(void)
{
    unsigned int c = 0;
    unsigned char busy;

    Print_I3("-----SSD1683A Busy");
    do
    {
        WWDG_SetCounter(0);
        busy = is_Busy();
        if (busy == 1)
        {
            break;
        }
        delay_xms(2);
        c++;
    }
    while (c < 40);

    if (c >= 40)
    {
        printf("er=%d\r\n", c);
        return Is_Er;
    }

    printf("OK=%d\r\n", c);
    return Is_OK;
}

void Init_EPD_Driver(void)
{
    // Follow the 2-color reference more closely: reset the cascaded panel and
    // rely on the default RAM/write state instead of reusing the 3-color init.
    printf("EPD SSD1683A Reset\r\n");
    DelayMs(10);
    SPI_RST_A_1;
    SPI_RST_B_1;
    DelayMs(10);
    SPI_RST_A_0;
    SPI_RST_B_0;
    DelayMs(10);
    SPI_RST_A_1;
    SPI_RST_B_1;
    printf("EPD SSD1683A Reset Done\r\n");
    EPD_Check_Busy();
}

void Display_EPD_Driver(void)
{
    Print_I3("SSD1683A refresh");
    EPD_W21_WriteCMD(0x18);
    EPD_W21_WriteDATA(0x80);
    EPD_W21_WriteCMD(0x22);
    EPD_W21_WriteDATA(0xFF);
    EPD_W21_WriteCMD(0x20);
    delay_ms(10);
    EPD_Check_Busy();
}

void Init_display_Bw(void)
{
}

void Init_display_Red(void)
{
    if (global_DEVICE_STATUS.fisHost == IS_HOST)
    {
        Print_I3("SSD1683A host data window");
        EPD_W21_WriteCMD(0x11);
        EPD_W21_WriteDATA(0x01);

        EPD_W21_WriteCMD(0x44);
        EPD_W21_WriteDATA(0x00);
        EPD_W21_WriteDATA(HALF_WINDOW_END);

        EPD_W21_WriteCMD(0x45);
        EPD_W21_WriteDATA(0x0F);
        EPD_W21_WriteDATA(0x01);
        EPD_W21_WriteDATA(0x00);
        EPD_W21_WriteDATA(0x00);

        EPD_W21_WriteCMD(0x4E);
        EPD_W21_WriteDATA(0x00);
        EPD_W21_WriteCMD(0x4F);
        EPD_W21_WriteDATA(0x0F);
        EPD_W21_WriteDATA(0x01);
        EPD_W21_WriteCMD(0x24);
    }
    else
    {
        Print_I3("SSD1683A slave data window");
        EPD_W21_WriteCMD(0x91);
        EPD_W21_WriteDATA(0x00);

        EPD_W21_WriteCMD(0xC4);
        EPD_W21_WriteDATA(HALF_WINDOW_END);
        EPD_W21_WriteDATA(0x00);

        EPD_W21_WriteCMD(0xC5);
        EPD_W21_WriteDATA(0x0F);
        EPD_W21_WriteDATA(0x01);
        EPD_W21_WriteDATA(0x00);
        EPD_W21_WriteDATA(0x00);

        EPD_W21_WriteCMD(0xCE);
        EPD_W21_WriteDATA(HALF_WINDOW_END);
        EPD_W21_WriteCMD(0xCF);
        EPD_W21_WriteDATA(0x0F);
        EPD_W21_WriteDATA(0x01);
        EPD_W21_WriteCMD(0xA4);
    }

    SPI_DC_A_1;
    SPI_DC_B_1;
}

UINT32 EPD_Display_Time(void)
{
    return 20;
}

UINT32 EPD_GetDisplayMaxBuf(void)
{
    return SCREEN_272X792_COLOR_2_MAX;
}

UINT8 EPD_GetScreenType(void)
{
    return 'C';
}

UINT8 EPD_GetBoardInfo(void)
{
	return EPD_MAKE_BOARD_INFO(0, 0);
}

#endif
