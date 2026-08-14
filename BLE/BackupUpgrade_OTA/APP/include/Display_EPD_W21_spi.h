#include "app_cfg.h"
#ifndef _DISPLAY_EPD_W21_SPI_
#define _DISPLAY_EPD_W21_SPI_

void EPD_W21_Init(void);
void SPI_Write(unsigned char value);
void EPD_W21_WriteDATA(unsigned char data);
void EPD_W21_WriteCMD(unsigned char command);
void EPD_GPIO_Init(void);
void Set_Spi0_output_init(void);
void  SPI4W_READDATA(UINT8 *u8tmp);
void  SPI4W_WRITECOM(unsigned char byte);
void  SPI4W_WRITEDATA(unsigned char byte);
void  Spi_Write_1byte(UINT8 i);
UINT8 Spi_Read_1byte(void);
UINT8 Spi_Read_nbyte(UINT8* _Spi_Buf,UINT16 Len);
void SPI0_MasterDefInit_output();
void EPD_W21_Reset(void);
void EPD_W21_WriteCMD(unsigned char byte);
void EPD_W21_WriteDATA(unsigned char data);
void SPI0_MasterTrans_0xFF(uint8_t uchr,uint16_t len);
#if (defined(ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79686BB_1360X480_COLOR_3))|| \
	(defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3))
void Epaper_Write_Command_HS(u8 data,u8 cs_num);
void Epaper_Write_Data_HS(u8 data,u8 cs_num);
#endif

#endif  //#ifndef _MCU_SPI_H_
