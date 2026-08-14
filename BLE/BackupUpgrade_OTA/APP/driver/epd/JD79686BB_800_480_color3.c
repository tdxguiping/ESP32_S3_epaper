#include "app_cfg.h"
#include "CONFIG.h"
#include "commoninfo.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"

#ifdef ENABLE_INK_SCREEN_JD79686BB_800X480_COLOR_3
UINT16  EPD_Check_Busy(void)
{
    unsigned int c;
	unsigned char busy;

    Print_I3("------JD79686BB Busy--");
    c=0;
	do
	{  
        WWDG_SetCounter(0);//ι�� , ����������� û��Ч��
		busy = is_Busy();
        if(busy==0)
            break;
        else
          	delay_xms(2);
        c++;
    }
    while(c<60);  // ʵ���� 50

    if(c>=60)
    {
        printf("er=%d\r\n",c);
        return Is_Er;
    }
    else
    {
        printf("OK=%d",c);
        return Is_OK;
    }
}

void Init_EPD_Driver(void)
{          
    EPD_W21_Reset();                     // reset           
	EPD_Check_Busy(); 

	EPD_W21_WriteCMD(0x4D);			//PWD After OTP
	EPD_W21_WriteDATA(0x55);
	
	EPD_W21_WriteCMD(0xA6);			
	EPD_W21_WriteDATA(0x38);

	EPD_W21_WriteCMD(0xB4);			
	EPD_W21_WriteDATA(0x5D);      //PSR setting

	EPD_W21_WriteCMD(0xB6);			
	EPD_W21_WriteDATA(0x80);      //PWR setting
	
	EPD_W21_WriteCMD(0xB7);			//Power off Sequence Setting
	EPD_W21_WriteDATA(0x00);

	EPD_W21_WriteCMD(0xF7);			//Booster Soft Start	Setting			
	EPD_W21_WriteDATA(0x02);                            //47uh���ʹ��

	EPD_W21_WriteCMD(0x57);				
	EPD_W21_WriteDATA(0x87);

	EPD_W21_WriteCMD(0x04);
	delay_ms(100);
	EPD_Check_Busy(); 
}

void Display_EPD_Driver(void)
{
    Print_I3("111");
    EPD_W21_WriteCMD(0x12);
	delay_ms(300);	        //!!!The delay here is necessary, 200uS at least!!! 
    EPD_Check_Busy();
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
	return 30;
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

UINT8 EPD_GetBoardInfo(void)
{
	return EPD_MAKE_BOARD_INFO(0, 0);
}

#endif
