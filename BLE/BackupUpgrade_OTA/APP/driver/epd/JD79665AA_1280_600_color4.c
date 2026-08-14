#include "app_cfg.h"
#include "CONFIG.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"

#ifdef ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4
#define Source_BITS    1360/2
#define Gate_BITS   480 

UINT16  EPD_Check_Busy(void)
{
    unsigned int c;
	unsigned char busy;
    
    Print_I3("-----JD79665AA_1280X600_COLOR_4 Busy");
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
    while(c<60);   //实测 4

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

void Init_EPD_Driver(void)
{

	Print_I3("Init_EPD_Driver JD79665AA_1280X600_COLOR_4");
	EPD_W21_Reset();	// reset	
	EPD_Check_Busy();

	EPD_W21_WriteCMD(0x4D);	   
	EPD_W21_WriteDATA(0x78); 
	
	EPD_W21_WriteCMD(0xA2);
	EPD_W21_WriteDATA(0x01);//m enable
	
	EPD_W21_WriteCMD(0x00); //
	EPD_W21_WriteDATA(0x0B);
	EPD_W21_WriteDATA(0x21);
	
	
	EPD_W21_WriteCMD(0xA2);
	EPD_W21_WriteDATA(0x02);//s enable
	
	EPD_W21_WriteCMD(0x00); //UD=0
	EPD_W21_WriteDATA(0x0B);
	EPD_W21_WriteDATA(0x21);
	
	EPD_W21_WriteCMD(0xA2);
	EPD_W21_WriteDATA(0x00);//m  and s enable
	
	
	EPD_W21_WriteCMD(0xE9); //JD cmd entry	
	EPD_W21_WriteDATA(0x01); 
	
	EPD_W21_WriteCMD(0xB8);	  
	EPD_W21_WriteDATA(0xB5);
	delay_ms(10);
	
	EPD_W21_WriteCMD(0x04);
	delay_ms(1000); 


}

void Display_EPD_Driver(void)
{
    Print_I3("Display_EPD_Driver JD79665AA_1280X600_COLOR_4"); 
	EPD_W21_WriteCMD(0xA2);  
    EPD_W21_WriteDATA(0x00);
      
    
    EPD_W21_WriteCMD(0x04);      
    EPD_Check_Busy();
     
    EPD_W21_WriteCMD(0x12);  
    EPD_W21_WriteDATA(0x00);  
    EPD_Check_Busy();
    

    EPD_W21_WriteCMD(0x02);    
    EPD_W21_WriteDATA(0x00);  
    EPD_Check_Busy();
	delay_ms(20); 

	/*EPD_W21_WriteCMD(0x07);  //deepsleep
    EPD_W21_WriteDATA(0xA5);  */

}

void Init_display_Bw(void)
{
   
}

void Init_display_Red(void)
{
	if(global_DEVICE_STATUS.fisHost == IS_HOST){
		Print_I3("============== Is_HOST  ===================");
		EPD_W21_WriteCMD(0xA2);
		EPD_W21_WriteDATA(0x01);
	}
	else{
		Print_I3("============== Is_SLAVE ===================");
		EPD_W21_WriteCMD(0xA2);
		EPD_W21_WriteDATA(0x02);
	}

	EPD_W21_WriteCMD(DTM);
	EPD_Check_Busy();

    SPI_DC_A_1;// data write
    SPI_DC_B_1;// data write    
}

UINT32 EPD_Display_Time()
{	
	return 60;
}

UINT32 EPD_GetDisplayMaxBuf()
{	
	return SCREEN_1280X600_COLOR_4_MAX;
}

UINT8 EPD_GetScreenType()
{	
#ifdef EPD_SCREEN_TYPE_A
	return '<'; 
#else
	return '>'; 
#endif
}

#endif

