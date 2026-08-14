#include "app_cfg.h"
#include "CONFIG.h"
#include "commoninfo.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"

#ifdef ENABLE_INK_SCREEN_SSD1677_960X640_COLOR_3
#define   LCD_XSIZE                    960     /* Horizontal Active Period           */
#define   LCD_YSIZE                    640     /* Vertical Active Period             */

UINT16  EPD_Check_Busy(void)
{
    unsigned int c;
	unsigned char busy;
    
    Print_I3("-----SSD1677 Busy");
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
	//unsigned	x_size=(LCD_XSIZE%8)?(LCD_XSIZE/8):(LCD_XSIZE/8-1);

	Print_I3("1");
	EPD_W21_Reset();					 // reset			
	EPD_Check_Busy();					  // 判断屏是否完成更新图像？	 
	EPD_W21_WriteCMD(0x12);   
	EPD_Check_Busy();					  // 判断屏是否完成更新图像？	 

	EPD_W21_WriteCMD(0x01); 		  //Driver output control
	EPD_W21_WriteDATA((unsigned char)(LCD_YSIZE-1));		  //Gate setting_A[8:0]= 0x127 [POR], 296 MUX 208:
	EPD_W21_WriteDATA((unsigned char)((LCD_YSIZE-1)>>8));		  //296:0127 208:00CF	152:0097 250:00F9	
	EPD_W21_WriteDATA(0x00);		 //Gate scanning sequence and direction  00:[POR]
			
	EPD_W21_WriteCMD(0x11); 			//数据进入模式设置	
	EPD_W21_WriteDATA(0x03);		 //数据更新方向，以及递增递减(建议固定使用01)					  
			
	EPD_W21_WriteCMD(0x44); 		 //设置Source范围--开始、结束		
	EPD_W21_WriteDATA(0x00);		 //Source 起点	
	EPD_W21_WriteDATA(0x00);		 //Source 起点	
	EPD_W21_WriteDATA((unsigned char)(LCD_XSIZE-1));		//Gate 起点 数值0x(Gate-1)	//296:0127 152:0097  250:00F9
	EPD_W21_WriteDATA((unsigned char)((LCD_XSIZE-1)>>8));		 //数值  0x(Gate-1) 
	
	EPD_W21_WriteCMD(0x45); 		 //设置Gate范围--开始、结束 
	EPD_W21_WriteDATA(0x00);				
	EPD_W21_WriteDATA(0x00);		 //Gate 终点（起始点根据数据进入模式选择）					  
	EPD_W21_WriteDATA((unsigned char)(LCD_YSIZE-1));		//Gate 起点 数值0x(Gate-1)	//296:0127 152:0097  250:00F9
	EPD_W21_WriteDATA((unsigned char)((LCD_YSIZE-1)>>8));		 //数值  0x(Gate-1) 
												
	EPD_W21_WriteCMD(0x3C); 		 //边界控制 	
	EPD_W21_WriteDATA(0x05); 
	EPD_W21_WriteCMD(0x0C);
	EPD_W21_WriteDATA(0xAE);	
	EPD_W21_WriteDATA(0xC7);
	EPD_W21_WriteDATA(0xC3);
	EPD_W21_WriteDATA(0xC0);
	EPD_W21_WriteDATA(0x40);
	EPD_W21_WriteCMD(0x21); //Display Update Control
	EPD_W21_WriteDATA(0x00);
	
	EPD_W21_WriteCMD(0x4E); //Set RAM X address counter
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteCMD(0x4F); //Set RAM Y address counter
	EPD_W21_WriteDATA(0x00);		 
	EPD_W21_WriteDATA(0x00); 
	EPD_Check_Busy();					  // 判断屏是否完成更新图像？	 
}

void Display_EPD_Driver(void)
{
    Print_I3("2");
    
    EPD_W21_WriteCMD(0x18);         //温度传感器选择
    EPD_W21_WriteDATA(0X80);        //0x80:内部  0x48:外部 
    EPD_W21_WriteCMD(0x22);
    EPD_W21_WriteDATA(0XF7);        //不同的值不同操作执行流程    
    EPD_W21_WriteCMD(0x20);         //主动激活
    //delay_ms(10);
    EPD_Check_Busy();           //读取工作状态 
    //SSD1677_sleep();     //   在刚刚开机时处理
}

void Init_display_Bw(void)
{
    EPD_W21_WriteCMD(0x24);    

    SPI_DC_A_1;// data write
    SPI_DC_B_1;// data write        
}

void Init_display_Red(void)
{
    EPD_W21_WriteCMD(0x26);

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
	return ':'; 
#else
	return '*'; 
#endif
}

#endif

