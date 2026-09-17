/********************************** (C) COPYRIGHT *******************************
 * File Name          : CH58x_SPI0.c
 * Author             : WCH
 * Version            : V1.2
 * Date               : 2021/11/17
 * Description
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *******************************************************************************/

#include "CH58x_common.h"
#if defined(ENABLE_SOFTWARE_TO_TDX) && defined(ENABLE_INK_SCREEN_SPD1657_800X480_COLOR_6)
#include "img_perf.h"
#endif

static UINT16 counter=0;

/*********************************************************************
 * @fn      SPI0_MasterDefInit
 *
 * @brief   主机模式默认初始化：模式0+3线全双工+8MHz
 *
 * @param   none
 *
 * @return  none
 */
 




void SPI0_MasterDefInit(void)
{
#if 1    
    //(5)、设置 R8_SPIx_CTRL_MOD 的 RB_SPI_MOSI_OE 和 RB_SPI_SCK_OE 为 1，RB_SPI_MISO_OE 为 0，
    //并 设置 GPIO 方向配置寄存器(R32_PA/PB_DIR)使 MOSI 引脚和 SCK 引脚为输出，MISO 引脚为输入；
    
    R8_SPI0_CLOCK_DIV = 240; // 主频时钟4分频
    R8_SPI0_CTRL_MOD = RB_SPI_ALL_CLEAR;
    R8_SPI0_CTRL_MOD |= RB_SPI_2WIRE_MOD;  // 2 line mode
    // R8_SPI0_CTRL_MOD |= RB_SPI_MST_SCK_MOD;
    R8_SPI0_CTRL_MOD =  RB_SPI_SCK_OE | RB_SPI_MOSI_OE | RB_SPI_MISO_OE;
    R8_SPI0_CTRL_CFG |= RB_SPI_AUTO_IF;     // 访问BUFFER/FIFO自动清除IF_BYTE_END标志
    R8_SPI0_CTRL_CFG &= ~RB_SPI_DMA_ENABLE; // 不启动DMA方式
#else
    //(6)、2 线模式下 SCK 不变， RB_SPI_MOSI_OE =0，不用 MOSI，由 MISO 半双工实现输入（同 3 线模式，
    // RB_SPI_MISO_OE=0 且引脚置为输入）和输出（ RB_SPI_MISO_OE =1     且引脚置为输出），手工切换方向；

    R8_SPI0_CLOCK_DIV = 4; // 主频时钟4分频
    R8_SPI0_CTRL_MOD = RB_SPI_ALL_CLEAR;
    R8_SPI0_CTRL_MOD = RB_SPI_MOSI_OE | RB_SPI_SCK_OE;
    R8_SPI0_CTRL_CFG |= RB_SPI_AUTO_IF;     // 访问BUFFER/FIFO自动清除IF_BYTE_END标志
    R8_SPI0_CTRL_CFG &= ~RB_SPI_DMA_ENABLE; // 不启动DMA方式

#endif   
}





/*********************************************************************
 * @fn      SPI0_MasterDefInit_Input
 *
 * @brief   主机模式默认初始化：模式0+2线全双工+8MHz
 *
 * @param   none
 *
 * @return  none
 */



void SPI0_MasterDefInit_output(void)
{
    //(6)、2 线模式下 SCK 不变， RB_SPI_MOSI_OE =0，不用 MOSI，由 MISO 半双工实现输入（同 3 线模式，
    // RB_SPI_MISO_OE=0 且引脚置为输入）和输出（ RB_SPI_MISO_OE =1     且引脚置为输出），手工切换方向；

    R8_SPI0_CLOCK_DIV = 16; // 16  128 ; // 主频时钟4分频        0xFF  40  (16 140ns , 这是最快的速度， 不可以再快。 否则读取数会出错)
    R8_SPI0_CTRL_MOD = RB_SPI_ALL_CLEAR;
    R8_SPI0_CTRL_MOD |= RB_SPI_2WIRE_MOD;  // 2 line mode
    R8_SPI0_CTRL_MOD = RB_SPI_SCK_OE | RB_SPI_MISO_OE;
    R8_SPI0_CTRL_CFG |= RB_SPI_AUTO_IF;     // 访问BUFFER/FIFO自动清除IF_BYTE_END标志
    R8_SPI0_CTRL_CFG &= ~RB_SPI_DMA_ENABLE; // 不启动DMA方式

}


/*********************************************************************
 * @fn      SPI0_CLKCfg
 *
 * @brief   SPI0 基准时钟配置，= d*Tsys
 *
 * @param   c       - 时钟分频系数
 *
 * @return  none
 */
void SPI0_CLKCfg(uint8_t c)
{
    if(c == 2)
    {
        R8_SPI0_CTRL_CFG |= RB_SPI_MST_DLY_EN;
    }
    else
    {
        R8_SPI0_CTRL_CFG &= ~RB_SPI_MST_DLY_EN;
    }
    R8_SPI0_CLOCK_DIV = c;
}

/*********************************************************************
 * @fn      SPI0_DataMode
 *
 * @brief   设置数据流模式
 *
 * @param   m       - 数据流模式 refer to ModeBitOrderTypeDef
 *
 * @return  none
 */
void SPI0_DataMode(ModeBitOrderTypeDef m)
{
    switch(m)
    {
        case Mode0_LowBitINFront:
            R8_SPI0_CTRL_MOD &= ~RB_SPI_MST_SCK_MOD;
            R8_SPI0_CTRL_CFG |= RB_SPI_BIT_ORDER;
            break;
        case Mode0_HighBitINFront:
            R8_SPI0_CTRL_MOD &= ~RB_SPI_MST_SCK_MOD;
            R8_SPI0_CTRL_CFG &= ~RB_SPI_BIT_ORDER;
            break;
        case Mode3_LowBitINFront:
            R8_SPI0_CTRL_MOD |= RB_SPI_MST_SCK_MOD;
            R8_SPI0_CTRL_CFG |= RB_SPI_BIT_ORDER;
            break;
        case Mode3_HighBitINFront:
            //R8_SPI0_CTRL_MOD |= RB_SPI_MST_SCK_MOD;
            R8_SPI0_CTRL_MOD &= ~RB_SPI_MST_SCK_MOD;
            R8_SPI0_CTRL_CFG &= ~RB_SPI_BIT_ORDER;
            break;
        default:
            break;
    }
}

/*********************************************************************
 * @fn      SPI0_MasterSendByte
 *
 * @brief   发送单字节 (buffer)
 *
 * @param   d       - 发送字节
 *
 * @return  none
 */
void SPI0_MasterSendByte(uint8_t d)
{
    UINT8 x;
//    if(counter >42000)
//    {
//      //x=  (~d);
//      x=d;
//      printf("0X%02x,",x);counter++;
//        if(counter%16==0)
//        {
//         printf("\r\n");
//        }      
//    }
//    else
//    {
//        counter++;
//    }

    x=d;
    R8_SPI0_CTRL_MOD &= ~RB_SPI_FIFO_DIR;
    R8_SPI0_BUFFER =x;// d;
    //while(!(R8_SPI0_INT_FLAG & RB_SPI_FREE));
	uint32_t timeout = 0;
	while(!(R8_SPI0_INT_FLAG & RB_SPI_FREE)) {
	    timeout++;
	    if(timeout > 10000) { // 超时阈值根据SPI速率设置
	        // 处理超时（如复位SPI模块）
	        break;
	    }
	}
}

/*********************************************************************
 * @fn      SPI0_MasterRecvByte
 *
 * @brief   接收单字节 (buffer)
 *
 * @param   none
 *
 * @return  接收到的字节
 */
uint8_t SPI0_MasterRecvByte(void)
{
    R8_SPI0_CTRL_MOD &= ~RB_SPI_FIFO_DIR;
    R8_SPI0_BUFFER = 0xFF; // 启动传输
    while(!(R8_SPI0_INT_FLAG & RB_SPI_FREE));
    return (R8_SPI0_BUFFER);
}

/*********************************************************************
 * @fn      SPI0_MasterTrans
 *
 * @brief   使用FIFO连续发送多字节
 *
 * @param   pbuf    - 待发送的数据内容首地址
 * @param   len     - 请求发送的数据长度，最大4095
 *
 * @return  none
 */
void SPI0_MasterTrans(uint8_t *pbuf, uint16_t len)
{
#if defined(ENABLE_SOFTWARE_TO_TDX) && defined(ENABLE_INK_SCREEN_SPD1657_800X480_COLOR_6)
    IP_SpiCall(len);
#endif
    uint16_t sendlen;

    sendlen = len;
    R8_SPI0_CTRL_MOD &= ~RB_SPI_FIFO_DIR; // 设置数据方向为输出
    R16_SPI0_TOTAL_CNT = sendlen;         // 设置要发送的数据长度
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END;
    while(sendlen)
    {
        if(R8_SPI0_FIFO_COUNT < SPI_FIFO_SIZE)
        {
            R8_SPI0_FIFO = *pbuf;
            pbuf++;
            sendlen--;
        }
    }
    while(R8_SPI0_FIFO_COUNT != 0); // 等待FIFO中的数据全部发送完成
}


void SPI0_MasterTrans_inverted(uint8_t *pbuf, uint16_t len)
{
    uint16_t sendlen;

    sendlen = len;
    R8_SPI0_CTRL_MOD &= ~RB_SPI_FIFO_DIR; // 设置数据方向为输出
    R16_SPI0_TOTAL_CNT = sendlen;         // 设置要发送的数据长度
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END;
    while(sendlen)
    {
        if(R8_SPI0_FIFO_COUNT < SPI_FIFO_SIZE)
        {
            R8_SPI0_FIFO = (~(*pbuf));//*pbuf;    // (~(*picData_old))
            pbuf++;
            sendlen--;
        }
    }
    while(R8_SPI0_FIFO_COUNT != 0); // 等待FIFO中的数据全部发送完成
}


extern UINT32  image_size_bw_add;
extern UINT32  Total_data;

void SPI0_MasterTrans_0x(uint8_t uchr,uint16_t len)
{
    uint16_t sendlen;

    Total_data+=2;

    //printf(">%02x-%d>",uchr,len);    
    //printf("%d-%d=%d>",len,image_size_bw_add,Total_data);


    sendlen = len;
    R8_SPI0_CTRL_MOD &= ~RB_SPI_FIFO_DIR; // 设置数据方向为输出
    R16_SPI0_TOTAL_CNT = sendlen;         // 设置要发送的数据长度
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END;
    while(sendlen)
    {
        if(R8_SPI0_FIFO_COUNT < SPI_FIFO_SIZE)
        {
//  use this will be problem            
//            if(uchr==0xFF)
//                {R8_SPI0_FIFO = 0xFF;}
//            else
//                {R8_SPI0_FIFO = 0x00;}
            R8_SPI0_FIFO = uchr;
            sendlen--;
        }
    }
    while(R8_SPI0_FIFO_COUNT != 0); // 等待FIFO中的数据全部发送完成
}


// 只写 0xFF 或 0x00

void SPI0_MasterTrans_0xFF(uint8_t uchr,uint16_t len)
{
    uint16_t sendlen;

//      for(sendlen=0;sendlen<len;sendlen++)  
//       {
//        counter++;
//        if(counter >42000)    
//        {
//          //printf("0X%02x,",(~uchr));
//          printf("0X%02x,",uchr);
//          if(counter%16==0)
//          {
//           printf("\r\n");
//          }
//        }
//      } 

    



    sendlen = len;
    R8_SPI0_CTRL_MOD &= ~RB_SPI_FIFO_DIR; // 设置数据方向为输出
    R16_SPI0_TOTAL_CNT = sendlen;         // 设置要发送的数据长度
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END;
    while(sendlen)
    {
        if(R8_SPI0_FIFO_COUNT < SPI_FIFO_SIZE)
        {
//  use this will be problem            
//            if(uchr==0xFF)
//                {R8_SPI0_FIFO = 0xFF;}
//            else
//                {R8_SPI0_FIFO = 0x00;}
            R8_SPI0_FIFO = uchr;
            sendlen--;
        }
    }
    while(R8_SPI0_FIFO_COUNT != 0); // 等待FIFO中的数据全部发送完成
}


/*********************************************************************
 * @fn      SPI0_MasterRecv
 *
 * @brief   使用FIFO连续接收多字节
 *
 * @param   pbuf    - 待接收的数据首地址
 * @param   len     - 待接收的数据长度，最大4095
 *
 * @return  none
 */
void SPI0_MasterRecv(uint8_t *pbuf, uint16_t len)
{
    uint16_t readlen;

    readlen = len;
    R8_SPI0_CTRL_MOD |= RB_SPI_FIFO_DIR; // 设置数据方向为输入  
    R16_SPI0_TOTAL_CNT = len;            // 设置需要接收的数据长度，FIFO方向为输入长度不为0则会启动传输 */
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END;
    while(readlen)
    {
        if(R8_SPI0_FIFO_COUNT)
        {
            *pbuf = R8_SPI0_FIFO;
            pbuf++;
            readlen--;
        }
    }
}

/*********************************************************************
 * @fn      SPI0_MasterDMATrans
 *
 * @brief   DMA方式连续发送数据
 *
 * @param   pbuf    - 待发送数据起始地址,需要四字节对其
 * @param   len     - 待发送数据长度
 *
 * @return  none
 */
void SPI0_MasterDMATrans(uint8_t *pbuf, uint16_t len)
{
    R8_SPI0_CTRL_MOD &= ~RB_SPI_FIFO_DIR;
    R32_SPI0_DMA_BEG = (uint32_t)pbuf;
    R32_SPI0_DMA_END = (uint32_t)(pbuf + len);
    R16_SPI0_TOTAL_CNT = len;
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END;
    R8_SPI0_CTRL_CFG |= RB_SPI_DMA_ENABLE;
    while(!(R8_SPI0_INT_FLAG & RB_SPI_IF_CNT_END));
    R8_SPI0_CTRL_CFG &= ~RB_SPI_DMA_ENABLE;
}

/*********************************************************************
 * @fn      SPI0_MasterDMARecv
 *
 * @brief   DMA方式连续接收数据
 *
 * @param   pbuf    - 待接收数据存放起始地址,需要四字节对其
 * @param   len     - 待接收数据长度
 *
 * @return  none
 */
void SPI0_MasterDMARecv(uint8_t *pbuf, uint16_t len)
{
    R8_SPI0_CTRL_MOD |= RB_SPI_FIFO_DIR;
    R32_SPI0_DMA_BEG = (uint32_t)pbuf;
    R32_SPI0_DMA_END = (uint32_t)(pbuf + len);
    R16_SPI0_TOTAL_CNT = len;
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END;
    R8_SPI0_CTRL_CFG |= RB_SPI_DMA_ENABLE;
    while(!(R8_SPI0_INT_FLAG & RB_SPI_IF_CNT_END));
    R8_SPI0_CTRL_CFG &= ~RB_SPI_DMA_ENABLE;
}

/*********************************************************************
 * @fn      SPI0_SlaveInit
 *
 * @brief   设备模式默认初始化，建议设置MISO的GPIO对应为输入模式
 *
 * @return  none
 */
void SPI0_SlaveInit(void)
{
    R8_SPI0_CTRL_MOD = RB_SPI_ALL_CLEAR;
    R8_SPI0_CTRL_MOD = RB_SPI_MISO_OE | RB_SPI_MODE_SLAVE;
    R8_SPI0_CTRL_CFG |= RB_SPI_AUTO_IF;
}

/*********************************************************************
 * @fn      SPI0_SlaveRecvByte
 *
 * @brief   从机模式，接收一字节数据
 *
 * @return  接收到数据
 */
uint8_t SPI0_SlaveRecvByte(void)
{
    R8_SPI0_CTRL_MOD |= RB_SPI_FIFO_DIR;
    while(R8_SPI0_FIFO_COUNT == 0);
    return R8_SPI0_FIFO;
}

/*********************************************************************
 * @fn      SPI0_SlaveSendByte
 *
 * @brief   从机模式，发送一字节数据
 *
 * @param   d       - 待发送数据
 *
 * @return  none
 */
void SPI0_SlaveSendByte(uint8_t d)
{
    R8_SPI0_CTRL_MOD &= ~RB_SPI_FIFO_DIR;
    R8_SPI0_FIFO = d;
    while(R8_SPI0_FIFO_COUNT != 0); // 等待发送完成
}

/*********************************************************************
 * @fn      SPI0_SlaveRecv
 *
 * @brief   从机模式，接收多字节数据
 *
 * @param   pbuf    - 接收收数据存放起始地址
 * @param   len     - 请求接收数据长度
 *
 * @return  none
 */
void SPI0_SlaveRecv(uint8_t *pbuf, uint16_t len)
{
    uint16_t revlen;

    revlen = len;
    R8_SPI0_CTRL_MOD |= RB_SPI_FIFO_DIR;
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END;
    while(revlen)
    {
        if(R8_SPI0_FIFO_COUNT)
        {
            *pbuf = R8_SPI0_FIFO;
            pbuf++;
            revlen--;
        }
    }
}

/*********************************************************************
 * @fn      SPI0_SlaveTrans
 *
 * @brief   从机模式，发送多字节数据
 *
 * @param   pbuf    - 待发送的数据内容首地址
 * @param   len     - 请求发送的数据长度，最大4095
 *
 * @return  none
 */
void SPI0_SlaveTrans(uint8_t *pbuf, uint16_t len)
{
    uint16_t sendlen;

    sendlen = len;
    R8_SPI0_CTRL_MOD &= ~RB_SPI_FIFO_DIR; // 设置数据方向为输出
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END;
    while(sendlen)
    {
        if(R8_SPI0_FIFO_COUNT < SPI_FIFO_SIZE)
        {
            R8_SPI0_FIFO = *pbuf;
            pbuf++;
            sendlen--;
        }
    }
    while(R8_SPI0_FIFO_COUNT != 0); // 等待FIFO中的数据全部发送完成
}

/*********************************************************************
 * @fn      SPI0_SlaveDMARecv
 *
 * @brief   DMA方式连续接收数据
 *
 * @param   pbuf    - 待接收数据存放起始地址,需要四字节对其
 * @param   len     - 待接收数据长度
 *
 * @return  none
 */
void SPI0_SlaveDMARecv(uint8_t *pbuf, uint16_t len)
{
    R8_SPI0_CTRL_MOD |= RB_SPI_FIFO_DIR;
    R32_SPI0_DMA_BEG = (uint32_t)pbuf;
    R32_SPI0_DMA_END = (uint32_t)(pbuf + len);
    R16_SPI0_TOTAL_CNT = len;
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END;
    R8_SPI0_CTRL_CFG |= RB_SPI_DMA_ENABLE;
    while(!(R8_SPI0_INT_FLAG & RB_SPI_IF_CNT_END));
    R8_SPI0_CTRL_CFG &= ~RB_SPI_DMA_ENABLE;
}

/*********************************************************************
 * @fn      SPI0_SlaveDMATrans
 *
 * @brief   DMA方式连续发送数据
 *
 * @param   pbuf    - 待发送数据起始地址,需要四字节对其
 * @param   len     - 待发送数据长度
 *
 * @return  none
 */
void SPI0_SlaveDMATrans(uint8_t *pbuf, uint16_t len)
{
    R8_SPI0_CTRL_MOD &= ~RB_SPI_FIFO_DIR;
    R32_SPI0_DMA_BEG = (uint32_t)pbuf;
    R32_SPI0_DMA_END = (uint32_t)(pbuf + len);
    R16_SPI0_TOTAL_CNT = len;
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END;
    R8_SPI0_CTRL_CFG |= RB_SPI_DMA_ENABLE;
    while(!(R8_SPI0_INT_FLAG & RB_SPI_IF_CNT_END));
    R8_SPI0_CTRL_CFG &= ~RB_SPI_DMA_ENABLE;
}



