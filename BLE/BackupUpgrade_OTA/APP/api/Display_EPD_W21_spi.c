#include "Display_EPD_W21_spi.h"

void SPI_Delay(unsigned char xrate)
{
   while(xrate--);
}

void SPI_Write(unsigned char value)                                    
{
    Spi_Write_1byte(value);
}

void EPD_W21_WriteCMD(unsigned char command)
{
    SPI_DC_A_0;   // command write
    SPI_DC_B_0; // command write    
    SPI_Write(command);
}

void EPD_W21_WriteDATA(unsigned char data)
{
    SPI_DC_A_1;		// data write
    SPI_DC_B_1;		// data write    
    SPI_Write(data);
}

#if (defined(ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79686BB_1360X480_COLOR_3))|| \
	(defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3))
void Epaper_Write_Command_HS(u8 data,u8 cs_num)
{
	SPI_CS_A_1;
	SPI_CS_B_1;
 	SPI_CS_A_SLAVE_1;	
	SPI_CS_B_SLAVE_1;

	if(cs_num == Is_HOST ){
		SPI_CS_A_0;
		SPI_CS_B_0;
	}
	else if(cs_num == Is_SLAVE ){
		SPI_CS_A_SLAVE_0;	
		SPI_CS_B_SLAVE_0;
	}
	else
	{
		SPI_CS_A_0;
		SPI_CS_B_0;
	 	SPI_CS_A_SLAVE_0;	
		SPI_CS_B_SLAVE_0;
	}

    SPI_DC_A_0;   // command write
    SPI_DC_B_0; // command write   

	mDelayuS(5) ;
	SPI_Write(data);
	mDelayuS(5) ;

	SPI_CS_A_1;
	SPI_CS_B_1;
 	SPI_CS_A_SLAVE_1;	
	SPI_CS_B_SLAVE_1;	 
}

void Epaper_Write_Data_HS(u8 data,u8 cs_num)
{
	SPI_CS_A_1;
	SPI_CS_B_1;
 	SPI_CS_A_SLAVE_1;	
	SPI_CS_B_SLAVE_1;

	if(cs_num == Is_HOST){
		SPI_CS_A_0;
		SPI_CS_B_0;
	}
	else if(cs_num == Is_SLAVE ){
		SPI_CS_A_SLAVE_0;	
		SPI_CS_B_SLAVE_0;
	}
	else
	{
		SPI_CS_A_0;
		SPI_CS_B_0;
	 	SPI_CS_A_SLAVE_0;	
		SPI_CS_B_SLAVE_0;
	}

    SPI_DC_A_1;		// data write
    SPI_DC_B_1;		// data write 

	mDelayuS(5) ;
	SPI_Write(data);
	mDelayuS(5) ;

	SPI_CS_A_1;
	SPI_CS_B_1;
 	SPI_CS_A_SLAVE_1;	
	SPI_CS_B_SLAVE_1;		
}
#endif

void DELAY_100nS(int delaytime)    // 30us 
{
   	mDelayuS(delaytime);
}

void DELAY_mS(int delaytime)       // 1ms
{
  	DelayMs(delaytime);
}

void DELAY_S(int delaytime)        // 1s
{
    DelayMs(100 * delaytime);
}

void SPI4W_WRITECOM(unsigned char byte)
{
    EPD_W21_WriteCMD(byte);
}

void SPI4W_WRITEDATA(unsigned char byte)
{
    EPD_W21_WriteDATA(byte);
}

void EPD_WriteInst(unsigned char byte)
{
    EPD_W21_WriteCMD(byte);
}

void EPD_WriteData(unsigned char byte)
{
    EPD_W21_WriteDATA(byte);
}

void RESET_x(void)
{    
  	EPD_W21_Init();
}

void SPI4W_READDATA(UINT8 *u8tmp)
{
   *u8tmp=Spi_Read_1byte();
}

void SPI4W_READDATA_n(UINT8 *u8tmp,UINT8 Len)
{
    Spi_Read_nbyte(u8tmp,Len);
}

