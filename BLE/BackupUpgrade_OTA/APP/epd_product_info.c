#include "epd_driver.h"
#include "epd_product_info.h"

void Init_EPD_Driver(void)
{
}

void Display_EPD_Driver(void)
{
}

void Init_display_Bw(void)
{
}

void Init_display_Red(void)
{
}

UINT32 EPD_Display_Time(void)
{
	return (UINT32)EPD_PRODUCT_DISPLAY_TIME;
}

UINT32 EPD_GetDisplayMaxBuf(void)
{
	return (UINT32)EPD_PRODUCT_DISPLAY_MAX_BUF;
}

UINT8 EPD_GetScreenType(void)
{
	return (UINT8)EPD_PRODUCT_SCREEN_TYPE_CHAR;
}

UINT8 EPD_GetBoardInfo(void)
{
	return (UINT8)EPD_PRODUCT_BOARD_INFO;
}
