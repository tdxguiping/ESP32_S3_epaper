#include "app_cfg.h"
#include "CONFIG.h"
#include "commoninfo.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"

#ifdef ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4
#define	 X_Addr_Start_H  0x01 
#define	 X_Addr_Start_L  0x90 
#define  Y_Addr_Start_H  0x01   
#define  Y_Addr_Start_L  0x10
#define  HTOTL_DATA1     0x6F
#define  HTOTL_DATA2     0x5C


UINT16  EPD_Check_Busy(void)
{
    unsigned int c;
	unsigned char busy;
    
    Print_I3("-----SSD2683ZA Busy");
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
    while(c<40);   //ʵ�� 4

    if(c>=40)
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
	EPD_W21_Reset();					 // reset			
	EPD_Check_Busy();					  // �ж����Ƿ���ɸ���ͼ��	 
	delay_ms(200); 
	
	EPD_W21_WriteCMD(R00_PSR);
 	EPD_W21_WriteDATA(0x27);
 	EPD_W21_WriteDATA(0x69);
 	EPD_W21_WriteCMD(R65_GSST); //resolution 
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x01); 

	EPD_W21_WriteCMD(0xEE);
	EPD_W21_WriteDATA(0x06);
	EPD_W21_WriteCMD(0X00);
	EPD_W21_WriteDATA(0x23);
	EPD_W21_WriteDATA(0x69);
	EPD_W21_WriteCMD(R65_GSST); //resolution 
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
  	EPD_W21_WriteDATA(0x00);
  	EPD_W21_WriteDATA(0x00);
	
  	EPD_W21_WriteCMD(0xEE);
  	EPD_W21_WriteDATA(0x04);
	
  	EPD_W21_WriteCMD(0xE0);
	EPD_W21_WriteDATA(0x71);
 
	EPD_W21_WriteCMD(R01_PWR);
	EPD_W21_WriteDATA(0x07);
	EPD_W21_WriteDATA(0xF0);
  
	EPD_W21_WriteCMD(R50_CDI);
	EPD_W21_WriteDATA(0x37);  //border white
  
	EPD_W21_WriteCMD(R61_TRES); //resolution 
	EPD_W21_WriteDATA(X_Addr_Start_H);
	EPD_W21_WriteDATA(X_Addr_Start_L);
	EPD_W21_WriteDATA(Y_Addr_Start_H);
	EPD_W21_WriteDATA(Y_Addr_Start_L);  
	

	EPD_W21_WriteCMD(R65_GSST); //resolution 
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);
	EPD_W21_WriteDATA(0x00);  
	
	EPD_W21_WriteCMD(0x62);
	EPD_W21_WriteDATA(HTOTL_DATA1);
	EPD_W21_WriteDATA(HTOTL_DATA2);
	EPD_W21_WriteCMD(0xE9);
	EPD_W21_WriteDATA(0x01); 
}

void Display_EPD_Driver(void)
{
    EPD_W21_WriteCMD(0xEE);
	EPD_W21_WriteDATA(0x04);

	EPD_W21_WriteCMD(0x04);   //Power on
	EPD_Check_Busy();   //??busy????
	delay_ms(200); 	

	EPD_W21_WriteCMD(0x12); //Update  
	EPD_W21_WriteDATA(0x00); 	
	EPD_Check_Busy();
	
	EPD_W21_WriteCMD(0x02); //Power off
	EPD_W21_WriteDATA(0x00);
	EPD_Check_Busy();
	delay_ms(10);
	
	EPD_W21_WriteCMD(0x07); //Power off
	EPD_W21_WriteDATA(0xA5);
	delay_ms(50); 
}

void Init_display_Bw(void)
{

}

void Init_display_Red(void)
{
	if(global_DEVICE_STATUS.fisHost == IS_HOST){
		Print_I3("============== Is_HOST  ===================");
		EPD_W21_WriteCMD(0xEE);
		EPD_W21_WriteDATA(0x05);
	}
	else{
		Print_I3("============== Is_SLAVE ===================");
		EPD_W21_WriteCMD(0xEE);
		EPD_W21_WriteDATA(0x06);
	}

	EPD_W21_WriteCMD(0x10);  // DTM命令 - 开始数据传输
	//EPD_Check_Busy();

    SPI_DC_A_1;// data write
    SPI_DC_B_1;// data write    
}

UINT32 EPD_Display_Time()
{	
	return 65;
}

UINT32 EPD_GetDisplayMaxBuf()
{	
	return SCREEN_272X792_COLOR_4_MAX;
}

UINT8 EPD_GetScreenType()
{	
#ifdef EPD_SCREEN_TYPE_A
	return '_'; 
#else
	return ':'; 
#endif
}

UINT8 EPD_GetBoardInfo(void)
{
	return EPD_MAKE_BOARD_INFO(0, 0);
}

#endif
