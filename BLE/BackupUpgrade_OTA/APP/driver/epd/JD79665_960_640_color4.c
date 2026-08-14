#include "app_cfg.h"
#include "CONFIG.h"
#include "commoninfo.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"

#ifdef ENABLE_INK_SCREEN_JD79665_960x640_COLOR_4
UINT16  EPD_Check_Busy(void)
{
    unsigned int c;
	unsigned char busy;

    Print_I3("------JD79665 Busy--");
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

	EPD_W21_WriteCMD(0x4D);
	EPD_W21_WriteDATA(0x78);

	EPD_W21_WriteCMD(0xE9);
	EPD_W21_WriteDATA(0x01);   

}

void Display_EPD_Driver(void)
{
    //Print_I3("111");
	EPD_W21_WriteCMD(0x04);   //Power on
	EPD_Check_Busy();	 //��busy�ź�
	delay_ms(100) ;

	EPD_W21_WriteCMD(0x12); //Update  
	EPD_W21_WriteDATA(0x00);	  
	EPD_Check_Busy();

	EPD_W21_WriteCMD(0x02); //Power off
	EPD_W21_WriteDATA(0x00);
	EPD_Check_Busy();

	EPD_W21_WriteCMD(0x07); //Power off
	EPD_W21_WriteDATA(0xA5);
	delay_ms(50);	

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

UINT8 EPD_GetBoardInfo(void)
{
	return EPD_MAKE_BOARD_INFO(0, 0);
}

#endif
