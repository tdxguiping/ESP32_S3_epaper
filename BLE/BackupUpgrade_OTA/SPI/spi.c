/********************************** (C) COPYRIGHT *******************************
 * File Name          : Main.c
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2020/08/06
 * Description        : SPI0演示 Master/Slave 模式数据收发
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *******************************************************************************/

#include "CH58x_common.h"
#include "app_cfg.h"
#include "Display_EPD_W21_spi.h"
#include "commoninfo.h"


//__attribute__((aligned(4))) UINT8 spiBuff[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6};
//__attribute__((aligned(4))) UINT8 spiBuffrev[16];
//├────────────────────────────────────────────────────────────────────────

#define SPI_CLK     GPIO_Pin_13
#define SPI_CLK_L	R32_PA_CLR |= SPI_CLK
#define SPI_CLK_H	R32_PA_OUT |= SPI_CLK

void delay_ms(UINT16 x)
{
   	DelayMs(x);    
}
//├────────────────────────────────────────────────────────────────────────

void delay_s(UINT16 x)
{
    DelayMs(100*x);  
}

void delay_xms(unsigned int xms)
{
    DelayMs(xms);    
}

//├────────────────────────────────────────────────────────────────────────
//├────────────────────────────────────────────────────────────────────────
// 单字节发送
void Spi_Write_1byte(UINT8 i)
{
  	SPI0_MasterSendByte(i);
}
//├────────────────────────────────────────────────────────────────────────
//├────────────────────────────────────────────────────────────────────────
UINT8 Spi_Read_1byte(void)
{
    UINT8 i;
    i = SPI0_MasterRecvByte();
    return i;
}

//├────────────────────────────────────────────────────────────────────────
//├────────────────────────────────────────────────────────────────────────
// FIFO 连续发送
void Spi_Write_nbyte(UINT8* _Spi_Buf,UINT16 Len)
{
    // FIFO 连续发送
    //Print_I3(" ");
    //GPIOA_ResetBits(GPIO_Pin_12);
    SPI0_MasterTrans(_Spi_Buf,Len);
    //GPIOA_SetBits(GPIO_Pin_12);
    //DelayMs(2);  
}
//├────────────────────────────────────────────────────────────────────────
//├────────────────────────────────────────────────────────────────────────
UINT8 Spi_Read_nbyte(UINT8* _Spi_Buf,UINT16 Len)
{
  	SPI0_MasterRecv(_Spi_Buf,Len);
}

void Set_Spi0_Input_all_input(void)
{
    GPIOA_ModeCfg(GPIO_Pin_13 | GPIO_Pin_15, GPIO_ModeIN_Floating); 
}

//Mode0_LowBitINFront = 0, // 模式0，低位在前
//Mode0_HighBitINFront,    // 模式0，高位在前
//Mode3_LowBitINFront,     // 模式3，低位在前
//Mode3_HighBitINFront,    // 模式3，高位在前
//RB_SPI_MST_SCK_MOD RW 主机模式时钟空闲方式选择： 1：模式 3（空闲时 SCK 为高电平）； 0：模式 0（空闲时 SCK 为低电平）。

#if (defined(ENABLE_INK_SCREEN_JD79665_800X480_COLOR_4)) || (defined(ENABLE_INK_SCREEN_JD79665_960x640_COLOR_4))
#define EPD_IS_COLOR4_PANEL 1
#else
#define EPD_IS_COLOR4_PANEL 0
#endif

void Set_Spi0_output_init(void)
{
    // Print_I3("");
    GPIOPinRemap(0,RB_PIN_SPI0);  // DISABLE  ENABLE
    GPIOA_ModeCfg(GPIO_Pin_13 | GPIO_Pin_15, GPIO_ModeOut_PP_5mA);
    GPIOB_ModeCfg(GPIO_Pin_12, GPIO_ModeOut_PP_5mA);        
    SPI0_MasterDefInit_output();    
    SPI0_DataMode(Mode0_HighBitINFront);
}


