#include "app_cfg.h"
#include "CONFIG.h"
#include "commoninfo.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"
#include "epd_busy.h"

#ifdef ENABLE_INK_SCREEN_JD79665_800X480_COLOR_4
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
UINT16  EPD_Check_Busy(void)
{
    Print_I3("------JD79665 Busy--");
    return EPD_Busy_WaitCurrent(200, 10000);
}

void Init_EPD_Driver(void)
{          
    EPD_W21_Reset();                     // reset           
	EPD_Check_Busy(); 

	EPD_W21_WriteCMD(0x4D);			//PWD After OTP
	EPD_W21_WriteDATA(0x78);
	
	EPD_W21_WriteCMD(0xb4);			
	EPD_W21_WriteDATA(0xd0);

	EPD_W21_WriteCMD(0x00);			
	EPD_W21_WriteDATA(0x0f);      //PSR setting
	EPD_W21_WriteDATA(0X29);

	EPD_W21_WriteCMD(0x01);			
	EPD_W21_WriteDATA(0x07);      //PWR setting
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x22);      //PWR setting
	EPD_W21_WriteDATA(0x78);
	EPD_W21_WriteDATA(0x0a);      //PWR setting
	EPD_W21_WriteDATA(0x22);	
	
	EPD_W21_WriteCMD(0x03);			//Power off Sequence Setting
	EPD_W21_WriteDATA(0x10);
	EPD_W21_WriteDATA(0x54);
	EPD_W21_WriteDATA(0x44);

	EPD_W21_WriteCMD(0x06);			//Booster Soft Start	Setting			
	EPD_W21_WriteDATA(0x0d);                            //47uh电感使用
	EPD_W21_WriteDATA(0x12);
	EPD_W21_WriteDATA(0x30);
	EPD_W21_WriteDATA(0x20);
	EPD_W21_WriteDATA(0x19);
	EPD_W21_WriteDATA(0x34);
	EPD_W21_WriteDATA(0x10);
	EPD_W21_WriteCMD(0x30);			
	EPD_W21_WriteDATA(0x02);
	EPD_W21_WriteCMD(0x41);			
	EPD_W21_WriteDATA(0x00);	
	EPD_W21_WriteCMD(0x50);			//CDI Setting
	EPD_W21_WriteDATA(0x37);	
	EPD_W21_WriteCMD(0x60);			//TCON Setting
	EPD_W21_WriteDATA(0x02);
	EPD_W21_WriteDATA(0x02);
	EPD_W21_WriteCMD(0x61);	    //resolution setting		
	EPD_W21_WriteDATA(0x03);
	EPD_W21_WriteDATA(0x20);
	EPD_W21_WriteDATA(0x01);
	EPD_W21_WriteDATA(0xe0);
	EPD_W21_WriteCMD(0x65);	    //resolution setting		
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteCMD(0xE7);	    			
	EPD_W21_WriteDATA(0x1C);
	EPD_W21_WriteCMD(0xE3);	    //PWS setting   			
	EPD_W21_WriteDATA(0x22);
	EPD_W21_WriteCMD(0xE0);	    //PWS setting   			
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteCMD(0x4D);			//PWD After OTP
	EPD_W21_WriteDATA(0x78);
	EPD_W21_WriteCMD(0x00);			
	EPD_W21_WriteDATA(0x0f);      //PSR setting
	EPD_W21_WriteDATA(0X29);
	EPD_W21_WriteCMD(0x30);			
	EPD_W21_WriteDATA(0x08);
	EPD_W21_WriteCMD(0xE9);			
	EPD_W21_WriteDATA(0x01);
}

void Display_EPD_Driver(void)
{
    //Print_I3("111");
    EPD_W21_WriteCMD(R04_PON);
    EPD_Check_Busy();
    delay_ms(50);
    EPD_W21_WriteCMD(R12_DRF);
    EPD_W21_WriteDATA(0x00);
    EPD_Busy_PrepareObserve();
    EPD_W21_WriteCMD(0x07);
    EPD_W21_WriteDATA(0xa5);
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

