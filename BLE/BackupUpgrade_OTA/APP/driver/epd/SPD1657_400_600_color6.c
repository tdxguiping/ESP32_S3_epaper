#include "app_cfg.h"
#include "CONFIG.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"
#include "epd_busy.h"
#include "release_trace.h"
#include "img_perf.h"
#include "img_perf.h"

#ifdef ENABLE_INK_SCREEN_SPD1657_800X480_COLOR_6
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
    cfg->busy_b.port = EPD_BUSY_PORT_A;
    cfg->busy_b.pin = GPIO_Pin_9;
    cfg->single_a_target = EPD_BUSY_SIDE_B;
    cfg->single_b_target = EPD_BUSY_SIDE_A;
    cfg->ab_target = EPD_BUSY_SIDE_AB;
    cfg->diff_target = EPD_BUSY_SIDE_AB;
    cfg->debug_enabled = Is_Yes;

    return Is_Yes;
}

uint16  EPD_Check_Busy(void)
{
    Print_I3("---SPD1657 Busy--");
    return EPD_Busy_WaitCurrent(200, 10000);
}

void SPD1657_InitRegisters(void)
{          
	int i;
	//IMAGE_LOG_TEXT("IMG driver-init\r\n");
	
    /* Reset and BUSY wait belong to the caller. */
	EPD_W21_WriteCMD(0xAA);    // CMDH
	EPD_W21_WriteDATA(0x49);
	EPD_W21_WriteDATA(0x55);
	EPD_W21_WriteDATA(0x20);
	EPD_W21_WriteDATA(0x08);
	EPD_W21_WriteDATA(0x09);
	EPD_W21_WriteDATA(0x18);
	
	EPD_W21_WriteCMD(0x01);//
	EPD_W21_WriteDATA(0x3F);
	
	EPD_W21_WriteCMD(PSR);	
	EPD_W21_WriteDATA(0x5F);
	EPD_W21_WriteDATA(0x69);
	
	EPD_W21_WriteCMD(POFS);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x54);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x44); 
	
	EPD_W21_WriteCMD(BTST1);
	EPD_W21_WriteDATA(0x40);
	EPD_W21_WriteDATA(0x1F);
	EPD_W21_WriteDATA(0x1F);
	EPD_W21_WriteDATA(0x2C);
	
	EPD_W21_WriteCMD(BTST2);
	EPD_W21_WriteDATA(0x6F);
	EPD_W21_WriteDATA(0x1F);
	EPD_W21_WriteDATA(0x17);
	EPD_W21_WriteDATA(0x49);
	
	EPD_W21_WriteCMD(BTST3);
	EPD_W21_WriteDATA(0x6F);
	EPD_W21_WriteDATA(0x1F);
	EPD_W21_WriteDATA(0x1F);
	EPD_W21_WriteDATA(0x22);
	EPD_W21_WriteCMD(PLL);
	EPD_W21_WriteDATA(0x08);
	EPD_W21_WriteCMD(CDI);
	EPD_W21_WriteDATA(0x3F);
	
	EPD_W21_WriteCMD(TCON);
	EPD_W21_WriteDATA(0x02);
	EPD_W21_WriteDATA(0x00);
	
	EPD_W21_WriteCMD(TRES);
	EPD_W21_WriteDATA(0x03);
	EPD_W21_WriteDATA(0x20);
	EPD_W21_WriteDATA(0x01); 
	EPD_W21_WriteDATA(0xE0);
	
	EPD_W21_WriteCMD(T_VDCS);
	EPD_W21_WriteDATA(0x01);
	
	EPD_W21_WriteCMD(PWS);
	EPD_W21_WriteDATA(0x2F);
	
	EPD_W21_WriteCMD(0x04); 	//PWR on  
}

void Init_EPD_Driver(void)
{
    EPD_W21_Reset();
    SPD1657_InitRegisters();
    EPD_Check_Busy();
}

void Display_EPD_Driver(void)
{
    Print_I3("Display_update_SPD1657_6Color_400_600 --");
	/*EPD_W21_WriteCMD(PON);
	EPD_Check_Busy();

	//20211212
	//Second setting
	EPD_W21_WriteCMD(BTST2);
	EPD_W21_WriteDATA(0x6F);
	EPD_W21_WriteDATA(0x1F);
	EPD_W21_WriteDATA(0x17);
	EPD_W21_WriteDATA(0x49);


	EPD_W21_WriteCMD(DRF);
	EPD_W21_WriteDATA(0x00);
	EPD_Check_Busy();


	EPD_W21_WriteCMD(POF);
	EPD_W21_WriteDATA(0x00);
	EPD_Check_Busy();*/

    IP_Refresh();
    IP_Refresh();
	EPD_W21_WriteCMD(0x12);   //DISPLAY REFRESH   
    EPD_W21_WriteDATA(0x00);   
	EPD_Busy_PrepareObserve();
	{
		EPD_BUSY_STATUS busy_status;
		if(EPD_Busy_GetStatus(&busy_status) == Is_Yes)
		{
		}
		else
		{
			FAULT_LOG_TEXT("FAULT busy-status\r\n");
		}
	}

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
	return SCREEN_800X480_COLOR_6_MAX;
}

UINT8 EPD_GetScreenType()
{	
#ifdef EPD_SCREEN_TYPE_A
	return '{'; 
#else
	return '@'; 
#endif
}

#endif

