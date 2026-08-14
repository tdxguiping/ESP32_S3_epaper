#include "app_cfg.h"
#include "CONFIG.h"
#include "commoninfo.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"

#ifdef ENABLE_INK_SCREEN_SSD1683_272X792_COLOR_3
#define   LCD_XSIZE                    792     /* Horizontal Active Period           */
#define   LCD_YSIZE                    272     /* Vertical Active Period             */

UINT16  EPD_Check_Busy(void)
{
    unsigned int c;
	unsigned char busy;
    
    Print_I3("-----SSD1683 Busy");
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
	EPD_W21_WriteDATA(0x0F);		 //Gate scanning sequence and direction  00:[POR]
	EPD_W21_WriteDATA(0x01);         //Gate ?? 272:012B  ??  0x(Gate-1)  272:010F
	EPD_W21_WriteDATA(0x0e);         //Gate???????  

	EPD_W21_WriteCMD(0x21);		
	EPD_W21_WriteDATA(0x00);            
	EPD_W21_WriteDATA(0x10);         //Clock select for Cascade mode
		
	EPD_W21_WriteCMD(0x3C); 
	EPD_W21_WriteDATA(0x01);			
	EPD_Check_Busy();
	EPD_Check_Busy();
	
	EPD_W21_WriteCMD(0xBF);                            //?IC(EOPT)????
	EPD_W21_WriteDATA(0x22);    
	EPD_Check_Busy();
	/*EPD_W21_WriteCMD(0x12);		
	delay_ms(20);		
	EPD_Check_Busy(); 				
	EPD_W21_Reset();	
	EPD_Check_Busy();*/
}

void Display_EPD_Driver(void)
{
    Print_I3("2");
    
    EPD_W21_WriteCMD(0x18);         //�¶ȴ�����ѡ��
    EPD_W21_WriteDATA(0X80);        //0x80:�ڲ�  0x48:�ⲿ 
    EPD_W21_WriteCMD(0x22);
    EPD_W21_WriteDATA(0XF7);        //��ͬ��ֵ��ͬ����ִ������    
    EPD_W21_WriteCMD(0x20);         //��������
    delay_ms(10);
    EPD_Check_Busy();           //��ȡ����״̬ 
    //SSD1683_sleep();     //   �ڸոտ���ʱ����
}

void Init_display_Bw(void)
{
	if(global_DEVICE_STATUS.fisHost == IS_HOST){
		Print_I3("==============Is_HOST 000000000000000000000000000 ===================");
		EPD_W21_WriteCMD(0x11);
		EPD_W21_WriteDATA(0x00); 
	
		EPD_W21_WriteCMD(0x44); 		 //??Source?????		
		EPD_W21_WriteDATA(0x31);		 //Source ??		
		EPD_W21_WriteDATA(0x00);		 //Source ??  796:0x30
	
		EPD_W21_WriteCMD(0x45); 		 //??Gate?????			
		EPD_W21_WriteDATA(0x0F);			
		EPD_W21_WriteDATA(0x01);		 //Gate ?? ??0x(Gate-1)  //296:0127 
		EPD_W21_WriteDATA(0x00);				
		EPD_W21_WriteDATA(0x00);		 //Gate ??(?????????????)	   
	
		EPD_W21_WriteCMD(0x4E); //??Source?????
		EPD_W21_WriteDATA(0x31);
		EPD_W21_WriteCMD(0x4F); //??Gate?????
		EPD_W21_WriteDATA(0x0F);		 
		EPD_W21_WriteDATA(0x01); 
		EPD_W21_WriteCMD(0x24); 
	}
	else{
		Print_I3("==============Is_SLAVE 000000000000000000000000000 ===================");
		EPD_W21_WriteCMD(0x91);
		EPD_W21_WriteDATA(0x01); 
	
		EPD_W21_WriteCMD(0xC4); 		 //??Source?????		
		EPD_W21_WriteDATA(0x00);		 //Source ??		
		EPD_W21_WriteDATA(0x31);		 //Source ??(?????????????)200:18 
	
		EPD_W21_WriteCMD(0xC5); 		 //??Gate?????			
		EPD_W21_WriteDATA(0x0F);			
		EPD_W21_WriteDATA(0x01);		 //Gate ?? ??0x(Gate-1)  //296:0127 
		EPD_W21_WriteDATA(0x00);				
		EPD_W21_WriteDATA(0x00);		 //Gate ??(?????????????)	   
	
		EPD_W21_WriteCMD(0xCE); //??Source?????
		EPD_W21_WriteDATA(0x00);
		EPD_W21_WriteCMD(0xCF); //??Gate?????
		EPD_W21_WriteDATA(0x0F);		 
		EPD_W21_WriteDATA(0x01); 
		EPD_W21_WriteCMD(0xA4);
	}

    SPI_DC_A_1;// data write
    SPI_DC_B_1;// data write        
}

void Init_display_Red(void)
{
	if(global_DEVICE_STATUS.fisHost == IS_HOST){
		//write  red and white data
		Print_I3("==============Is_HOST 111111111111111111111111111 ===================");
		EPD_W21_WriteCMD(0x4E); //??Source?????
		EPD_W21_WriteDATA(0x31);
		EPD_W21_WriteCMD(0x4F); //??Gate?????
		EPD_W21_WriteDATA(0x0F);		 
		EPD_W21_WriteDATA(0x01);
		EPD_W21_WriteCMD(0x26); 			 //write  red and white data//	EPD_RAM_XY_address_counter();
	}
	else{
		Print_I3("==============Is_SLAVE 11111111111111111111111111 ===================");
		EPD_W21_WriteCMD(0xCE); //??Source?????
		EPD_W21_WriteDATA(0x00);
		EPD_W21_WriteCMD(0xCF); //??Gate?????
		EPD_W21_WriteDATA(0x0F);		 
		EPD_W21_WriteDATA(0x01); 
		EPD_W21_WriteCMD(0xA6);
	}

    SPI_DC_A_1;// data write
    SPI_DC_B_1;// data write    
}

UINT32 EPD_Display_Time()
{	
	return 40;
}

UINT32 EPD_GetDisplayMaxBuf()
{	
	return SCREEN_272X792_COLOR_3_MAX;
}

UINT8 EPD_GetScreenType()
{	
#ifdef EPD_SCREEN_TYPE_A
	return '.'; 
#else
	return '#'; 
#endif
}

UINT8 EPD_GetBoardInfo(void)
{
	return EPD_MAKE_BOARD_INFO(0, 0);
}

#endif
