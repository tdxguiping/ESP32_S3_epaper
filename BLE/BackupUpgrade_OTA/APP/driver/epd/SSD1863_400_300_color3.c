#include "app_cfg.h"
#include "CONFIG.h"
#include "commoninfo.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"

#ifdef ENABLE_INK_SCREEN_SSD1863_400X300_COLOR_3
#define   LCD_XSIZE                    400     /* Horizontal Active Period           */
#define   LCD_YSIZE                    300     /* Vertical Active Period             */

UINT16  EPD_Check_Busy(void)
{
    unsigned int c;
	unsigned char busy;
    
    Print_I3("-----SSD1863 Busy");
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
	unsigned	x_size=(LCD_XSIZE%8)?(LCD_XSIZE/8):(LCD_XSIZE/8-1);

	Print_I3("1");
	EPD_W21_Reset();					 // reset			
	EPD_Check_Busy();					  // �ж����Ƿ���ɸ���ͼ��	 
	EPD_W21_WriteCMD(0x12);   
	EPD_Check_Busy();					  // �ж����Ƿ���ɸ���ͼ��	 

	EPD_W21_WriteCMD(0x01); 		  //Driver output control
	EPD_W21_WriteDATA((unsigned char)(LCD_YSIZE-1));		  //Gate setting_A[8:0]= 0x127 [POR], 296 MUX 208:
	EPD_W21_WriteDATA((unsigned char)((LCD_YSIZE-1)>>8));		  //296:0127 208:00CF	152:0097 250:00F9	
	EPD_W21_WriteDATA(0x00);		 //Gate scanning sequence and direction  00:[POR]
			
	EPD_W21_WriteCMD(0x11); 			//���ݽ���ģʽ����	
	EPD_W21_WriteDATA(0x03);		 //���ݸ��·����Լ������ݼ�(����̶�ʹ��01)					  
			
	EPD_W21_WriteCMD(0x44); 		 //����Source��Χ--��ʼ������		
	EPD_W21_WriteDATA(0x00);		 //Source ���		
	EPD_W21_WriteDATA(x_size);		   //128:0f Source �յ㣨��ʼ��������ݽ���ģʽѡ��
										  // ��ֵ0x((Source/8)-1) 128:0f 122:0f   152��12  160:13	176��15  
	EPD_W21_WriteCMD(0x45); 		 //����Gate��Χ--��ʼ������ 
	EPD_W21_WriteDATA(0x00);				
	EPD_W21_WriteDATA(0x00);		 //Gate �յ㣨��ʼ��������ݽ���ģʽѡ��					  
	EPD_W21_WriteDATA((unsigned char)(LCD_YSIZE-1));		//Gate ��� ��ֵ0x(Gate-1)	//296:0127 152:0097  250:00F9
	EPD_W21_WriteDATA((unsigned char)((LCD_YSIZE-1)>>8));		 //��ֵ  0x(Gate-1) 
												
	EPD_W21_WriteCMD(0x3C); 		 //�߽���� 	
	EPD_W21_WriteDATA(0x01);			
	EPD_W21_WriteCMD(0x4E); //Set RAM X address counter
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteCMD(0x4F); //Set RAM Y address counter
	EPD_W21_WriteDATA(0x00);		 
	EPD_W21_WriteDATA(0x00); 
	EPD_Check_Busy();					  // �ж����Ƿ���ɸ���ͼ��	 
}

void Display_EPD_Driver(void)
{
    Print_I3("2");
    
    EPD_W21_WriteCMD(0x18);         //�¶ȴ�����ѡ��
    EPD_W21_WriteDATA(0X80);        //0x80:�ڲ�  0x48:�ⲿ 
    EPD_W21_WriteCMD(0x22);
    EPD_W21_WriteDATA(0XF7);        //��ͬ��ֵ��ͬ����ִ������    
    EPD_W21_WriteCMD(0x20);         //��������
    //delay_ms(10);
    EPD_Check_Busy();           //��ȡ����״̬ 
    //SSD1863_sleep();     //   �ڸոտ���ʱ����
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
