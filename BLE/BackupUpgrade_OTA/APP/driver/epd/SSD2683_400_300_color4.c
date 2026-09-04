#include "app_cfg.h"
#include "CONFIG.h"
#include "commoninfo.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"
#include "epd_busy.h"

#ifdef ENABLE_INK_SCREEN_SSD2683_400X300_COLOR_4
UINT8 EPD_Driver_GetBusyConfig(EPD_BUSY_CONFIG *cfg)
{
    if(cfg == NULL)
    {
        return Is_No;
    }

    cfg->supported = Is_Yes;
    cfg->active_level = EPD_BUSY_ACTIVE_LOW;
    cfg->busy_a.port = EPD_BUSY_PORT_A;
    cfg->busy_a.pin = epaper_BUSY;
    cfg->busy_b.port = EPD_BUSY_PORT_B;
    cfg->busy_b.pin = GPIO_Pin_16;
    cfg->single_a_target = EPD_BUSY_SIDE_B;
    cfg->single_b_target = EPD_BUSY_SIDE_A;
    cfg->ab_target = EPD_BUSY_SIDE_AB;
    cfg->diff_target = EPD_BUSY_SIDE_AB;
    cfg->debug_enabled = Is_Yes;

    return Is_Yes;
}

uint16  EPD_Check_Busy(void)
{
    Print_I3("---SSD2683 Busy--");
    return EPD_Busy_WaitCurrent(200, 10000);
}

void Init_EPD_Driver()
{           
    EPD_W21_Reset();                     // reset               
    Print_I3("---");
	EPD_Check_Busy(); 

	SPI4W_WRITECOM(0x00);     
   	SPI4W_WRITEDATA(0x2F);
   	SPI4W_WRITEDATA(0x29);
    SPI4W_WRITECOM(0x50);  //   CDI Booder …Ë÷√   
    SPI4W_WRITEDATA(0x37); 
    SPI4W_WRITECOM(0xE9);
    SPI4W_WRITEDATA(0x01);
}

void Display_EPD_Driver(void)
{
    Print_I3("--");
    EPD_W21_WriteCMD(R04_PON);
    EPD_Check_Busy();
	delay_ms(10);
    
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

    SPI_DC_A_1;// data write
    SPI_DC_B_1;// data write    
}

UINT32 EPD_Display_Time()
{	
	return 40;
}

UINT32 EPD_GetDisplayMaxBuf()
{	
	return SCREEN_400X300_COLOR_4_MAX;
}

UINT8 EPD_GetScreenType()
{	
#ifdef EPD_SCREEN_TYPE_A
	return '<'; 
#else
	return '$'; 
#endif
}

#endif

