#include "app_cfg.h"
#include "CONFIG.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"

#ifdef ENABLE_INK_SCREEN_JD79686BB_1360X480_COLOR_3
UINT16  EPD_Check_Busy(void)
{
    unsigned int c;
	unsigned char busy;
    
    Print_I3("-----JD79686BB Busy");
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
	Print_I3("Init_EPD_Driver @@@@@@@@@@@@"); 	
 	EPD_W21_Reset();					 // reset	
	EPD_Check_Busy();

	Epaper_Write_Command_HS(0x41, Is_HOST);
	Epaper_Write_Data_HS(0x00,Is_HOST);	
	
  	Epaper_Write_Command_HS(0xE5,Is_ALL);  
  	Epaper_Write_Data_HS(0x1a,Is_ALL);

  	Epaper_Write_Command_HS(0xE0,Is_ALL);  
  	Epaper_Write_Data_HS(0x03,Is_ALL);
	
	Epaper_Write_Command_HS(0x4d,Is_ALL);
	Epaper_Write_Data_HS(0x55,Is_ALL);
	Epaper_Write_Command_HS(0xa6,Is_ALL);
	Epaper_Write_Data_HS(0x38,Is_ALL);
	Epaper_Write_Command_HS(0xb4,Is_ALL);
	Epaper_Write_Data_HS(0x5d,Is_ALL);
	Epaper_Write_Command_HS(0xb6,Is_ALL);
	Epaper_Write_Data_HS(0x80,Is_ALL);
	Epaper_Write_Command_HS(0xb7,Is_ALL);
	Epaper_Write_Data_HS(0x00,Is_ALL);
	Epaper_Write_Command_HS(0xf7,Is_ALL);
	Epaper_Write_Data_HS(0x02,Is_ALL);  

	Epaper_Write_Command_HS(0xAE,Is_SLAVE);
	Epaper_Write_Data_HS(0xA0,Is_SLAVE);  

	Epaper_Write_Command_HS(0x00,Is_ALL);
	Epaper_Write_Data_HS(0x8f,Is_ALL);    
}

void Display_EPD_Driver(void)
{
    Print_I3("Display_EPD_Driver @@@@@@@@@@@@"); 
	Epaper_Write_Command_HS(0x04,Is_ALL);  	//power on  
   	delay_ms(200); 	
  	EPD_Check_Busy();
	
   	Epaper_Write_Command_HS(0x12,Is_ALL);    	//display      
   	EPD_Check_Busy();	
		
   	Epaper_Write_Command_HS(0x02,Is_ALL);     //power off 
   	delay_ms(60);
   	EPD_Check_Busy();

	Epaper_Write_Command_HS(0x07,Is_ALL); 	//deep sleep
  	Epaper_Write_Data_HS(0xa5,Is_ALL);  
  	delay_ms(500);
}

void Init_display_Bw(void)
{
	if(global_DEVICE_STATUS.fisHost == IS_HOST){
		Print_I3("Init_display_Bw Is_HOST @@@@@@@@@@@@@@@@@@@@");
		SPI_CS_A_0;
		SPI_CS_A_SLAVE_1;
		SPI_CS_B_0;
		SPI_CS_B_SLAVE_1;
	}
	else{
		Print_I3("Init_display_Bw Is_SLAVE @@@@@@@@@@@@@@@@@@@@");
		SPI_CS_A_1;
		SPI_CS_A_SLAVE_0;
		SPI_CS_B_1;
		SPI_CS_B_SLAVE_0;
	}
	EPD_W21_WriteCMD(0x10); 
	
	SPI_DC_A_1;// data write
	SPI_DC_B_1;// data write        
}

void Init_display_Red(void)
{
	if(global_DEVICE_STATUS.fisHost == IS_HOST){
		//write  red and white data
		Print_I3("Init_display_Red Is_HOST @@@@@@@@@@@@@@@@@@@@");
		SPI_CS_A_0;
		SPI_CS_A_SLAVE_1;
		SPI_CS_B_0;
		SPI_CS_B_SLAVE_1;
	}
	else{
		Print_I3("Init_display_Red Is_SLAVE @@@@@@@@@@@@@@@@@@@@");
		SPI_CS_A_1;
		SPI_CS_A_SLAVE_0;
		SPI_CS_B_1;
		SPI_CS_B_SLAVE_0;
	}
	EPD_W21_WriteCMD(0x13); 

	SPI_DC_A_1;// data write
	SPI_DC_B_1;// data write	
}

UINT32 EPD_Display_Time()
{	
	return 40;
}

UINT32 EPD_GetDisplayMaxBuf()
{	
	return SCREEN_1360X480_COLOR_3_MAX;
}

UINT8 EPD_GetScreenType()
{	
#ifdef EPD_SCREEN_TYPE_A
	return ';'; 
#else
	return '/'; 
#endif
}

UINT8 EPD_GetBoardInfo(void)
{
	return EPD_MAKE_BOARD_INFO(0, 0);
}

#endif
