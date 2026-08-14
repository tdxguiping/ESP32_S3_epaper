#include "app_cfg.h"
#include "CONFIG.h"
#include "commoninfo.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"

#ifdef ENABLE_INK_SCREEN_SSD2683_400X300_COLOR_4
uint16  EPD_Check_Busy(void)
{
    unsigned int c;
	unsigned char busy;
    
    Print_I3("---JD79665 Busy--");
    c=0;
	do
	{  
        WWDG_SetCounter(0);//喂狗 , 不可以在这里， 没有效果
		busy = is_Busy();

        if(busy==0)
            break;
        else
          delay_xms(2);        
        c++;
    }
    while(c<70); // 实测 60

    if(c>=70)
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

void Init_EPD_Driver()
{           
    EPD_W21_Reset();                     // reset               
    Print_I3("---");
	EPD_Check_Busy(); 

	SPI4W_WRITECOM(0x00);     
   	SPI4W_WRITEDATA(0x2F);
   	SPI4W_WRITEDATA(0x29);
    SPI4W_WRITECOM(0x50);  //   CDI Booder 设置   
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
	delay_ms(10);
    EPD_Check_Busy();
    
    EPD_W21_WriteCMD(R02_POF);
    EPD_W21_WriteDATA(0x00);
    EPD_Check_Busy();
	delay_ms(10);
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

