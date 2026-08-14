#include "app_cfg.h"
#include "CONFIG.h"
#include "commoninfo.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"

#ifdef ENABLE_INK_SCREEN_UC8276_400X300_COLOR_3
#define Source_Pixel 400
#define Source_Pixel_high Source_Pixel/256  // Source�ֱ��ʵĸ�8λ
#define Source_Pixel_low  Source_Pixel%256  // Source�ֱ��ʵĵ�8λ
#define Gate_Pixel 300
#define Gate_Pixel_high   Gate_Pixel/256    // Gate�ֱ��ʵĸ�8λ
#define Gate_Pixel_low    Gate_Pixel%256    // Gate�ֱ��ʵĵ�8λ

UINT16  EPD_Check_Busy(void)
{
    unsigned int c;
	unsigned char busy;
    
    Print_I3("-----UC8276 Busy");
    c=0;
	do
	{  
        WWDG_SetCounter(0);//ι�� , ����������� û��Ч��
		busy = is_Busy();
        if(busy==1)
            break;
        else
          	delay_xms(2);
        
        c++;
    }
    while(c<20);   //ʵ�� 4

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
	Print_I3("1");	
	EPD_W21_Reset();                     // reset
	/*EPD_W21_WriteCMD(0x00);        // PANEL SETTING
	EPD_W21_WriteDATA(0x37);     // [5]REG=1(LUT from register); [4]BWR=1 �ڰ����У�[3]UD=0 Gn-1->G0��[2]SHL=1 S0->Sn-1��[1]SHD_N=1 DC-DC�򿪣�RST_N=1 no reset
	EPD_W21_WriteDATA(0x01);     // 

	EPD_W21_WriteCMD(0x01);        // Power setting
	EPD_W21_WriteDATA(0x03);     // [2]=0:VDHR disable  [1]VDS_EN: Source power selection =1: Internal DC/DC function for generating VDH/VDL     [0]VDG_EN: Gate power selection =1: Internal DC/DC function for generating VGH/VGL  ����ʹ���ڲ���ѹ 
	EPD_W21_WriteDATA(0x10);     // VGH=20V, VGL= -20V
	EPD_W21_WriteDATA(0x3f);     // +15V
	EPD_W21_WriteDATA(0x3f);     // -15V
	EPD_W21_WriteDATA(0x03);     //   3V

	EPD_W21_WriteCMD(0x06);        // Booster Soft Start�趨
	EPD_W21_WriteDATA(0xE7);     // 
	EPD_W21_WriteDATA(0xE7);     // 
	EPD_W21_WriteDATA(0x3D);     

	EPD_W21_WriteCMD(0X60);        // TCON SETTING  (TCON) 
	EPD_W21_WriteDATA(0x22);     // 0010=12 (Default)  

	EPD_W21_WriteCMD(0x82);        // vcom�趨
	EPD_W21_WriteDATA (0x0E);    // -1.5V

	EPD_W21_WriteCMD(0x30);        // ֡Ƶ�趨
	EPD_W21_WriteDATA (0x09);    //   50HZ       

	EPD_W21_WriteCMD(0X50);        // VCOM AND DATA INTERVAL SETTING   
	//    EPD_W21_WriteDATA(0x09);     // border=KK D7=BDZ=1(Border output Hi-Z enabled) =0(Border output Hi-Z disabled)  D5:4=BDV[1:0]   D3=N2OCP=1(Copy NEW data to OLD data enabled)  D1:0=01(Default)  ע�⣺���ֵ��ϵ�����α�
	//    EPD_W21_WriteDATA(0x29);     // border=KW D7=BDZ=1(Border output Hi-Z enabled) =0(Border output Hi-Z disabled)  D5:4=BDV[1:0]   D3=N2OCP=1(Copy NEW data to OLD data enabled)  D1:0=01(Default)  ע�⣺���ֵ��ϵ�����α�
	//    EPD_W21_WriteDATA(0x39);     // border=LUTBD D7=BDZ=1(Border output Hi-Z enabled) =0(Border output Hi-Z disabled)  D5:4=BDV[1:0]   D3=N2OCP=1(Copy NEW data to OLD data enabled)  D1:0=01(Default)  ע�⣺���ֵ��ϵ�����α�
	EPD_W21_WriteDATA(0x17);     // CDI[3:0]:VCOM and data interval  =0111��10 (Default) 

	EPD_W21_WriteCMD(0xE3);        // 
	EPD_W21_WriteDATA (0x88);    //          

	EPD_W21_WriteCMD(0x61);                  // Resolution setting
	EPD_W21_WriteDATA(Source_Pixel_high);  // Horizontal Display Resolution=800  High 8bit            
	EPD_W21_WriteDATA(Source_Pixel_low);   // Horizontal Display Resolution=800  Low  8bit  D0~D2Ϊ0
	EPD_W21_WriteDATA(Gate_Pixel_high);    //   Vertical Display Resolution=480  High 8bit
	EPD_W21_WriteDATA(Gate_Pixel_low);     //   Vertical Display Resolution=480  Low  8bit
	UC8276_Check_Busy();*/					  // �ж����Ƿ���ɸ���ͼ��	
}

void Display_EPD_Driver(void)
{
    Print_I3("2");
    
    EPD_W21_WriteCMD(0x17);        //  �Զ�   
	EPD_W21_WriteDATA(0xA5);     // PON DRF POF     
    EPD_Check_Busy();           //��ȡ����״̬ 
    //UC8276_sleep();     //   �ڸոտ���ʱ����
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
	return SCREEN_400X300_COLOR_3_MAX;
}

UINT8 EPD_GetScreenType()
{	
#ifdef EPD_SCREEN_TYPE_A
	return '}'; 
#else
	return '&'; 
#endif
}

UINT8 EPD_GetBoardInfo(void)
{
	return EPD_MAKE_BOARD_INFO(0, 0);
}

#endif
