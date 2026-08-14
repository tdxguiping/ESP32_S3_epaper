#include "app_cfg.h"
#include "CONFIG.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"

#ifdef ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3
UINT16  EPD_Check_Busy(void)
{
    unsigned int c;
	unsigned char busy;
    
    Print_I3("-----JD79686AB Busy");
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
    while(c<20);   //实测 4

    if(c>=20)
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
	Print_I3("Init_EPD_Driver JD79686AB_1360X480_COLOR_3");
	EPD_W21_Reset();					 // reset	
	delay_ms(50); 
	EPD_Check_Busy();

	EPD_W21_WriteCMD(0xF8);  // Enter FITI Command
	EPD_W21_WriteDATA(0x7C); 
	EPD_W21_WriteDATA(0x00); 

	EPD_W21_WriteCMD(0xF8);  // Enter FITI Command
	EPD_W21_WriteDATA(0x60); 
	EPD_W21_WriteDATA(0xA5); 

	EPD_W21_WriteCMD(0xF8);  
	EPD_W21_WriteDATA(0x92);
	EPD_W21_WriteDATA(0x08);
			  
	EPD_W21_WriteCMD(0xF8);   
	EPD_W21_WriteDATA(0x93);
	EPD_W21_WriteDATA(0x18); 
		
	EPD_W21_WriteCMD(0xF8);   
	EPD_W21_WriteDATA(0x7E);
	EPD_W21_WriteDATA(0x01);
}

void Display_EPD_Driver(void)
{
    Print_I3("Display_EPD_Driver JD79686AB_1360X480_COLOR_3"); 
	EPD_W21_WriteCMD(0x04);  //power on  
   	delay_ms(200); 	
  	EPD_Check_Busy();
	
   	EPD_W21_WriteCMD(0x12);        //display      
   	EPD_Check_Busy();	
		
   	EPD_W21_WriteCMD(0x02);        //power off 
   	delay_ms(60);
   	EPD_Check_Busy(); 

	EPD_W21_WriteCMD(0x07); //deep sleep
  	EPD_W21_WriteDATA(0xa5);  
  	delay_ms(500); 
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
	return SCREEN_1360X480_COLOR_3_MAX;
}

UINT8 EPD_GetScreenType()
{	
#ifdef EPD_SCREEN_TYPE_A
	return ';'; 
#else
	return '/'; 
#endif
}

#endif

