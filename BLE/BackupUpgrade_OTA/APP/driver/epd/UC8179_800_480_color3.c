#include "app_cfg.h"
#include "CONFIG.h"
#include "commoninfo.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"

#ifdef ENABLE_INK_SCREEN_UC8179_800X480_COLOR_3
UINT16  EPD_Check_Busy(void)
{
    unsigned int c;
	unsigned char busy;

    Print_I3("------UC8179 Busy--");
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

void Display_EPD_Driver(void)
{
	EPD_W21_WriteCMD(0x17);
	EPD_W21_WriteDATA(0xA5);
	/* 5.等待芯片空闲 */
	EPD_Check_Busy();
 	/* 6.深度睡眠 */
	/*EPD_W21_WriteCMD(0x07);
	EPD_W21_WriteDATA(0xA5);*/
}

void Init_EPD_Driver(void)
{
	/* 1. 硬件复位 */
	EPD_W21_Reset(); 
	EPD_Check_Busy();
	/* 2. 初始化代码 */
	/* 2.1 BOOSTER软件启动设置 */
	EPD_W21_WriteCMD(0x06);
	EPD_W21_WriteDATA(0x17);
	EPD_W21_WriteDATA(0x17);
	EPD_W21_WriteDATA(0x28);
	EPD_W21_WriteDATA(0x17);
	EPD_W21_WriteCMD(0x01);
	EPD_W21_WriteDATA(0x07);
	EPD_W21_WriteDATA(0x07);
	EPD_W21_WriteDATA(0x3F);
	EPD_W21_WriteDATA(0x3F);
	EPD_W21_WriteDATA(0x0F);
  	/* 2.2 设置寄存器 */
  	/*  像素设置 160*296 */
    /*  扫描方向 G0-GN S0-SN*/
    /*  模式选择 BW */
    /*  LUT选择REG 等 */
	EPD_W21_WriteCMD(0x00);
	EPD_W21_WriteDATA(0x0F);
	EPD_W21_WriteDATA(0x0C);
	/* 2.3 电压设置 */
	/* VG和VS由内部DC输出 */
	/* VGH 20V,VGL-20V */
	/* VSH 15V,VSL-15V */
	/* VDHR 5.4V */
	/* 2.4 设置锁相环频率 */
	/* 2.4.1 设置锁相环频率为50HZ 即20ms*/
	EPD_W21_WriteCMD(0x30);
	EPD_W21_WriteDATA(0x06);
	/* 2.5 设置border颜色 */
  	EPD_W21_WriteCMD(0x50);
  	EPD_W21_WriteDATA(0x11);
  	EPD_W21_WriteDATA(0x07);
	/* 2.6 设置分辨率为800x480 */
  	EPD_W21_WriteCMD(0x61);
  	EPD_W21_WriteDATA(0x03);
  	EPD_W21_WriteDATA(0x20);
  	EPD_W21_WriteDATA(0x01);
  	EPD_W21_WriteDATA(0xE0);
	/* 2.2.7设置VCOM_DC-0.1V */
  	EPD_W21_WriteCMD(0x82);
  	EPD_W21_WriteDATA(0x00);
	/* 2.2.8设置G2S,S2G非重叠时钟周期 */
  	EPD_W21_WriteCMD(0x60);
  	EPD_W21_WriteDATA(0x22);
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
