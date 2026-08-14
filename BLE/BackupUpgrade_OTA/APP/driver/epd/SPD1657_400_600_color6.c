#include "app_cfg.h"
#include "CONFIG.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"

#ifdef ENABLE_INK_SCREEN_SPD1657_800X480_COLOR_6
uint16  EPD_Check_Busy(void)
{
    unsigned int c;
	unsigned char busy;
    
    Print_I3("---EL073TF1 Busy--");
    c=0;
	do
	{  
        //WWDG_SetCounter(0);//ι�� , ����������� û��Ч��
		busy = is_Busy();

        if(busy==0)
            break;
        else
          delay_xms(2);        
        c++;
    }
    while(c<60); // ʵ�� 60

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

void Init_EPD_Driver()
{          
	int i;

	for(i=0; i<3; i++){
    	EPD_W21_Reset();                     // reset  
	}
    Print_I3("---");
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
	EPD_Check_Busy();          //waiting for the electronic paper IC to release the idle signal
}

void Display_EPD_Driver(void)
{
    Print_I3("Display_update_EL073TF1_6Color_400_600 --");
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

	EPD_W21_WriteCMD(0x12);   //DISPLAY REFRESH   
    EPD_W21_WriteDATA(0x00);   
	EPD_Check_Busy();

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
	//return 'a';//7.5寸 HD 6色
	//return 'b';//7.5寸 6色
	return 'd';//13.3寸 HD 6色
	//return 'e';//7.09寸 HD 6色
	//return 'f';//10.85 4色
	//return 'g';//7.5寸 4色
	//return '@'; 
#endif
}

UINT8 EPD_GetBoardInfo(void)
{
	return EPD_MAKE_BOARD_INFO(0, EPD_BOARD_VENDOR_DKE);
}

#endif
