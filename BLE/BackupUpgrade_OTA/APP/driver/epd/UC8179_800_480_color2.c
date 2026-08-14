#include "app_cfg.h"
#include "CONFIG.h"
#include "commoninfo.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"

#ifdef ENABLE_INK_SCREEN_UC8179_800X480_COLOR_2
uint16  EPD_Check_Busy(void)
{
    unsigned int c;
	unsigned char busy;
    
    Print_I3("---UC8179 Busy--");
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
    while(c<70); // ʵ�� 60

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

/*void Init_EPD_Driver(void)
{           
	Print_I3("==========================Init_UC8179_2Color_800_480.....");
	EPD_W21_Reset();					 // reset	
}

void Display_EPD_Driver(void)
{
    Print_I3("--");
    EPD_W21_WriteCMD(0x17);        //  �Զ�   
	EPD_W21_WriteDATA(0xA5);     // PON DRF POF    
}*/

void Display_EPD_Driver(void)
{
    EPD_W21_WriteCMD(0x17);         // �Զ�����ָ��   
    EPD_W21_WriteDATA(0xA5);        // PON DRF POF DSLP ���Զ����º�������˯��    
    EPD_Check_Busy();                     // �ж����Ƿ���ɸ���ͼ��    
}

void Init_EPD_Driver(void)
{
#if 0
    EPD_W21_Reset();                     // reset
    //EPD_W21_WriteCMD(0x00);        // PANEL SETTING  0
    //EPD_W21_WriteDATA(0x07);    //if cf from OTP   if ef from regitser �ӼĴ��������� [7:6]RES=11b:160x296��[5]REG_EN: LUT selection=1(LUT from register); [4]BWR=1 �ڰ����У�[3]UD=0 Gn-1->G0��[2]SHL=1 S0->Sn-1��[1]SHD_N=1 DC-DC�򿪣�RST_N=1 no reset
    Print_I3("  \r\n");    
//    EPD_W21_Reset();        //  RESET        
//    Print_I3("---");    
//    EPD_W21_WriteCMD(0x00); // PANEL SETTING  0
//    //EPD_W21_WriteDATA(0x0F); // 0x0f by otp,0x2F from register lut by mcu 
//    EPD_W21_WriteDATA(0x07);

//    // ��һ�����ң� ��ɫ����������
//    EPD_W21_WriteCMD(0x50);
//    EPD_W21_WriteDATA(0x11);
//    EPD_W21_WriteDATA(0x07);
#else
	EPD_W21_Reset(); 
	EPD_W21_WriteCMD(0x01);		  //POWER SETTING 
	EPD_W21_WriteDATA (0x07); 			
	EPD_W21_WriteDATA (0x17);
	EPD_W21_WriteDATA (0x3F);
	EPD_W21_WriteDATA (0x3F); 	  // 15V
	//		  EPD_W21_WriteDATA (0x28); 	  // red
	EPD_W21_WriteCMD(0x06);		  //boost soft start
	EPD_W21_WriteDATA (0x17); 	  //A
	EPD_W21_WriteDATA (0x17); 	  //B
	EPD_W21_WriteDATA (0x27); 	  //C 
	EPD_W21_WriteDATA (0x17); 	  //C 
	EPD_W21_WriteCMD(0x04);
	delay_xms(100);
	EPD_Check_Busy(); 
	EPD_W21_WriteCMD(0x00);		  //panel setting	   
	EPD_W21_WriteDATA(0x17);				   
	EPD_W21_WriteCMD(0x30);		  //PLL setting
	EPD_W21_WriteDATA (0x06);		  // 3C  50HZ
	EPD_W21_WriteCMD(0x61);		  //resolution setting	  
	EPD_W21_WriteDATA (0x03); 	  //gate 800
	EPD_W21_WriteDATA (0x20);
	EPD_W21_WriteDATA (0x01); 	  //source 480	   
	EPD_W21_WriteDATA (0xe0);
	EPD_W21_WriteCMD(0x15);		  
	EPD_W21_WriteDATA (0x00);
	EPD_W21_WriteCMD(0x60);		  
	EPD_W21_WriteDATA (0x22); 
	EPD_W21_WriteCMD(0x82);		  //vcom_DC setting
	EPD_W21_WriteDATA (0x1c);		  //2c -2.3V  /26 -2V	 /1c -1.5V
	EPD_W21_WriteCMD(0X50);		  //VCOM AND DATA INTERVAL SETTING			  
	EPD_W21_WriteDATA(0x11);		  //VBDF 00  VBDW 01 VBDB 11	  VBDF F7 VBDW 77 VBDB 37  VBDR B7
	EPD_W21_WriteDATA(0x07);
#endif
}


void ENTER_DEEP_SLEEP()       // ע�⣺�������˯��ģʽ�󣬲��������ֻ����Ӳ��RESET�������²��������RESETҲ���С�Ȼ����Ҫ���³�ʼ�����á�
{
	EPD_W21_WriteCMD(0x07);
	EPD_W21_WriteDATA(0xA5);
}

void Init_display_Bw(void)
{

}

void Init_display_Red(void)
{
    EPD_W21_WriteCMD(0x13);

    SPI_DC_A_1;// data write
    SPI_DC_B_1;// data write  
}

UINT32 EPD_Display_Time()
{	
	return 20;
}

UINT32 EPD_GetDisplayMaxBuf()
{	
	return SCREEN_800X480_COLOR_2_MAX;//96000;//
}

UINT8 EPD_GetScreenType()
{	
#ifdef EPD_SCREEN_TYPE_A
	return ']'; 
#else
	return '?'; 
#endif
}

UINT8 EPD_GetBoardInfo(void)
{
	return EPD_MAKE_BOARD_INFO(0, 0);
}

#endif
