#include "app_cfg.h"
#include "CONFIG.h"
#include "commoninfo.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"
#include "epd_busy.h"

#ifdef ENABLE_INK_SCREEN_UC8279_800X480_COLOR_3
#define   LCD_XSIZE                    800    /* Horizontal Active Period           */
#define   LCD_YSIZE                    480       /* Vertical Active Period             */

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

void Display_EPD_Driver(void)
{
    EPD_W21_WriteCMD(0x12);
    delay_ms(100);
    EPD_Busy_PrepareObserve();
}

void Init_EPD_Driver(void)
{		

	EPD_W21_Reset(); 
	EPD_Check_Busy();		//ic entern  normal

	EPD_W21_WriteCMD(0x00); 		//(PSR): Panel Setting Register
	EPD_W21_WriteDATA(	(0<<6) | // Resolution setting
			(0<<5) | // 0: LUT from OTP. (Default)
			(0<<4) | //Color selection setting,0: Pixel with B/W/Red. Run both LU1 and LU2. (default) 1: Pixel with B/W. Run LU1 only
			(1<<3) | //1:Scan up; First line=G1→G2 →…→Gn-1→Last line=Gn. (default)
			(1<<2) | //1: Shift right: First data=S1→S2 →…→Sn-1→Last data=Sn. (default)
			(1<<1) | //1 : Booster on. (default)	
			(1<<0) ); //1: no effect. (default)

	EPD_W21_WriteCMD(0x06);	 // Booster
	EPD_W21_WriteDATA(0x27);	 
	EPD_W21_WriteDATA(0x27);	 
	EPD_W21_WriteDATA(0x36);	 
	EPD_W21_WriteDATA(0x17);	 
	EPD_W21_WriteCMD(0x50); 	 //VCOM and DATA interval setting Register
	EPD_W21_WriteDATA(0x11);
	EPD_W21_WriteDATA(0x07);
	EPD_W21_WriteCMD(0x60); 	//TCON setting
	EPD_W21_WriteDATA(0x22); 

	EPD_W21_WriteCMD(0x61); 		   //(TRES): Resolution Setting 
	EPD_W21_WriteDATA((unsigned char)(LCD_XSIZE>>8));//	
	EPD_W21_WriteDATA((unsigned char)(LCD_XSIZE & 0xFF));//
	EPD_W21_WriteDATA((unsigned char)(LCD_YSIZE>>8));
	EPD_W21_WriteDATA((unsigned char)(LCD_YSIZE & 0xFF));			

	EPD_W21_WriteCMD(0xe3);	//POWER SAVING (PWS)
	EPD_W21_WriteDATA(0x88);

	EPD_W21_WriteCMD(0x04); //R04H (PON): Power ON Command
	delay_ms(100);  
	EPD_Check_Busy();		 //waiting for the electronic paper IC to release the idle signal
}

void Init_display_Bw(void)
{
    EPD_W21_WriteCMD(0x10);     

    SPI_DC_A_1;// data write
    SPI_DC_B_1;// data write        
}

void Init_display_Red(void)
{
    EPD_W21_WriteCMD(0x13);

    SPI_DC_A_1;// data write
    SPI_DC_B_1;// data write    
}

UINT32 EPD_Display_Time()
{	
	return 40;
}

UINT32 EPD_GetDisplayMaxBuf()
{	
	return SCREEN_RED_COLOR_3_MAX;
}

UINT8 EPD_GetScreenType()
{	
#ifdef EPD_SCREEN_TYPE_A
	return '-'; 
#else
	return '%'; 
#endif
}

#endif

