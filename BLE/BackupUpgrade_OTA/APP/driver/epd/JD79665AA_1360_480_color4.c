#include "app_cfg.h"
#include "CONFIG.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"

#ifdef ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4
#define Source_BITS    1360/2
#define Gate_BITS   480 

UINT16  EPD_Check_Busy(void)
{
    unsigned int c;
	unsigned char busy;
    
    Print_I3("-----JD79665AA Busy");
    c=0;
	do
	{  
        WWDG_SetCounter(0);//喂狗 , 不可以在这里， 没有效果
		busy = is_Busy();
        if(busy==1)
            break;
        else
          	delay_xms(2);
        
        c++;
    }
    while(c<60);   //实测 4

    if(c>=60)
    {
        printf("er=%d\r\n",c);
        return Is_Er;
    }
    else
    {
        printf("OK=%d\r\n",c);
        return Is_OK;
    }
}

void Init_EPD_Driver(void)
{

	Print_I3("Init_EPD_Driver JD79665AA_1360X480_COLOR_4");
	EPD_W21_Reset();	// reset	
	EPD_Check_Busy();

	EPD_W21_WriteCMD(0x4D);		
	EPD_W21_WriteDATA(0x78);
	
	EPD_W21_WriteCMD(0xE5);		
	EPD_W21_WriteDATA(0x08);
	
	EPD_W21_WriteCMD(0xE0); //0xE0
	EPD_W21_WriteDATA(0x01);	
	
	EPD_W21_WriteCMD(0xA2); //MASTER enable
	EPD_W21_WriteDATA(0x01);	
	
	EPD_W21_WriteCMD(0x00); //0x00 PSR
	EPD_W21_WriteDATA(0x0F);	//UD=1,SHL=0
	EPD_W21_WriteDATA(0x21);
	
	EPD_W21_WriteCMD(0xA2); //close
	EPD_W21_WriteDATA(0x00);	
		
	
	EPD_W21_WriteCMD(0x01);	// PWRR
	EPD_W21_WriteDATA(0x07);
	EPD_W21_WriteDATA(0x00);
	  
	EPD_W21_WriteCMD(0x06);	// BTST_P
	EPD_W21_WriteDATA(0x0f);		// 2025-09-02 WQY
	EPD_W21_WriteDATA(0x11);	
	EPD_W21_WriteDATA(0x2d);	
	EPD_W21_WriteDATA(0x22);	
	EPD_W21_WriteDATA(0x19);		
	EPD_W21_WriteDATA(0x39);	
	EPD_W21_WriteDATA(0x10);  
	 
		
	EPD_W21_WriteCMD(0x30);	// PLL
	EPD_W21_WriteDATA(0x08);	
		
	EPD_W21_WriteCMD(0x50);	// CDI
	EPD_W21_WriteDATA(0x37);
	  
	EPD_W21_WriteCMD(0x60);	// TCON
	EPD_W21_WriteDATA(0x02);
	EPD_W21_WriteDATA(0x02);
	  
	EPD_W21_WriteCMD(0x61); // TRES
	EPD_W21_WriteDATA(Source_BITS/256);		// Source_BITS_H
	EPD_W21_WriteDATA(Source_BITS%256);		// Source_BITS_L
	EPD_W21_WriteDATA(Gate_BITS/256); 		// Gate_BITS_H
	EPD_W21_WriteDATA(Gate_BITS%256); 		// Gate_BITS_L		
	
		
	EPD_W21_WriteCMD(0x65);	// GSST
	EPD_W21_WriteDATA(0x00);	
	EPD_W21_WriteDATA(0x00);	
	EPD_W21_WriteDATA(0x00);	
	EPD_W21_WriteDATA(0x00);
	
	//Epaper_Write_Command(0x82); // VDCS
	//Epaper_Write_Data(0x80 | LUT_VCOM[0]);	
	
	EPD_W21_WriteCMD(0xE3);	// PWS
	EPD_W21_WriteDATA(0x08);
	EPD_W21_WriteDATA(0x00);
		
	  
	EPD_W21_WriteCMD(0xE9);	// 
	EPD_W21_WriteDATA(0x01); 
		
	EPD_W21_WriteCMD(0xB8);	// 
	EPD_W21_WriteDATA(0xB5); 
	delay_ms(20); 
	
	//////////////////////////////////////////////////////////////////////////////	
	
	EPD_W21_WriteCMD(0x4D);		
	EPD_W21_WriteDATA(0x78);
	
	EPD_W21_WriteCMD(0xE5);		
	EPD_W21_WriteDATA(0x08);
	
	EPD_W21_WriteCMD(0xE0); //0xE0
	EPD_W21_WriteDATA(0x01);
				
	EPD_W21_WriteCMD(0xA2); //SLAVE enable
	EPD_W21_WriteDATA(0x02);	
	
	EPD_W21_WriteCMD(0x00); //0x00 PSR
	EPD_W21_WriteDATA(0x0F);	//UD=1,SHL=1
	EPD_W21_WriteDATA(0x21);
	
	EPD_W21_WriteCMD(0xA2); //close
	EPD_W21_WriteDATA(0x00);	
	
	
	EPD_W21_WriteCMD(0x01);	// PWRR
	EPD_W21_WriteDATA(0x07);
	EPD_W21_WriteDATA(0x00);
	  
	EPD_W21_WriteCMD(0x06);	// BTST_P
	EPD_W21_WriteDATA(0x0f);		// 2025-09-02 WQY
	EPD_W21_WriteDATA(0x11);	
	EPD_W21_WriteDATA(0x2d);	
	EPD_W21_WriteDATA(0x22);	
	EPD_W21_WriteDATA(0x19);		
	EPD_W21_WriteDATA(0x39);	
	EPD_W21_WriteDATA(0x10);  
	 
		
	EPD_W21_WriteCMD(0x30);	// PLL
	EPD_W21_WriteDATA(0x08);	
		
	EPD_W21_WriteCMD(0x50);	// CDI
	EPD_W21_WriteDATA(0x37);
	  
	EPD_W21_WriteCMD(0x60);	// TCON
	EPD_W21_WriteDATA(0x02);
	EPD_W21_WriteDATA(0x02);
	  
	EPD_W21_WriteCMD(0x61); // TRES
	EPD_W21_WriteDATA(Source_BITS/256);		// Source_BITS_H
	EPD_W21_WriteDATA(Source_BITS%256);		// Source_BITS_L
	EPD_W21_WriteDATA(Gate_BITS/256); 		// Gate_BITS_H
	EPD_W21_WriteDATA(Gate_BITS%256); 		// Gate_BITS_L		
	
		
	EPD_W21_WriteCMD(0x65);	// GSST
	EPD_W21_WriteDATA(0x00);	
	EPD_W21_WriteDATA(0x00);	
	EPD_W21_WriteDATA(0x00);	
	EPD_W21_WriteDATA(0x00);
	
	//Epaper_Write_Command(0x82); // VDCS
	//Epaper_Write_Data(0x80 | LUT_VCOM[0]);	
	
	EPD_W21_WriteCMD(0xE3);	// PWS
	EPD_W21_WriteDATA(0x08);
	EPD_W21_WriteDATA(0x00);
		
	  
	EPD_W21_WriteCMD(0xE9);	// 
	EPD_W21_WriteDATA(0x01); 
		
	EPD_W21_WriteCMD(0xB8);	// 
	EPD_W21_WriteDATA(0xB5); 
	delay_ms(20);
		
	EPD_W21_WriteCMD(0x04);
	EPD_Check_Busy();	//while(1);

}

void Display_EPD_Driver(void)
{
    Print_I3("Display_EPD_Driver JD79665AA_1360X480_COLOR_4"); 
  	EPD_W21_WriteCMD(0x12);
	EPD_W21_WriteDATA(0x00);
 	EPD_Check_Busy();
	delay_ms(20);
 
  	EPD_W21_WriteCMD(0x02);
  	EPD_W21_WriteDATA(0x00);
    EPD_Check_Busy();	
//	
////	delay_ms(30);
//	
    EPD_W21_WriteCMD(0x07);		// Deep_sleep
    EPD_W21_WriteDATA(0xa5);   		
}

void Init_display_Bw(void)
{
   
}

void Init_display_Red(void)
{
	if(global_DEVICE_STATUS.fisHost == IS_HOST){
		Print_I3("============== Is_HOST  ===================");
		EPD_W21_WriteCMD(0xA2);
		EPD_W21_WriteDATA(0x01);
	}
	else{
		Print_I3("============== Is_SLAVE ===================");
		EPD_W21_WriteCMD(0xA2);
		EPD_W21_WriteDATA(0x02);
	}

	EPD_W21_WriteCMD(DTM);
	EPD_Check_Busy();

    SPI_DC_A_1;// data write
    SPI_DC_B_1;// data write    
    delay_ms(200);
}

UINT32 EPD_Display_Time()
{	
	return 65;
}

UINT32 EPD_GetDisplayMaxBuf()
{	
	return SCREEN_1360X480_COLOR_4_MAX;
}

UINT8 EPD_GetScreenType()
{	
#ifdef EPD_SCREEN_TYPE_A
	return '|';
#else
	return '~';
#endif
}

#endif

