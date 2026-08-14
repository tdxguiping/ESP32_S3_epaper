#include "app_cfg.h"
#include "CONFIG.h"
#include "commoninfo.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"
#include "epd_busy.h"

#ifdef ENABLE_INK_SCREEN_JD79665_960x640_COLOR_4
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

UINT16  EPD_Check_Busy(void)
{
    return EPD_Busy_WaitCurrent(200, 10000);
}

void Init_EPD_Driver(void)
{          
    EPD_W21_Reset();                     // reset           
	EPD_Check_Busy(); 

	EPD_W21_WriteCMD(0x4D);
	EPD_W21_WriteDATA(0x78);

	EPD_W21_WriteCMD(0xE9);
	EPD_W21_WriteDATA(0x01);   

}

void Display_EPD_Driver(void)
{
    EPD_W21_WriteCMD(0x04);
    EPD_Check_Busy();
    delay_ms(100);

    EPD_W21_WriteCMD(0x12);
    EPD_W21_WriteDATA(0x00);
    EPD_Busy_PrepareObserve();
}

void Init_display_Bw(void)
{
 
}

void Init_display_Red(void)
{
    EPD_W21_WriteCMD(DTM);

    SPI_DC_A_1;// data write
    SPI_DC_B_1;// data write    
}

UINT32 EPD_Display_Time()
{	
	return 40;
}

UINT32 EPD_GetDisplayMaxBuf()
{	
	return SCREEN_960X640_COLOR_4_MAX;
}

UINT8 EPD_GetScreenType()
{	
#ifdef EPD_SCREEN_TYPE_A
	return '!'; 
#else
	return '!'; 
#endif
}

#endif

