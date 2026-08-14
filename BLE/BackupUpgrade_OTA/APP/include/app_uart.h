/********************************** (C) COPYRIGHT *******************************
 * File Name          : app_uart.h
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2018/12/11
 * Description        :
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

#ifndef app_uart_H
#define app_uart_H

#ifdef __cplusplus
extern "C" {
#endif
#ifdef ENABLE_SOFTWARE_TO_XT

/*********************************************************************
 * INCLUDES
 */
#include "xtinfoservice.h"

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * MACROS
 */
#define PACKHEAD  			"XTE"        /**<数据\指令包头部*/ 
#define FILEHEAD  			"XTEK"        /**<数据\指令包头部*/ 
#if 0 
#define PACKHEAD  "XTE"        /**<数据\指令包头部*/ 
>>>>>>> 3b39af9a7212b117e89bd93e5b39565b889dd7fa

#define TYPE_SLAVEPACK   	0x01  /**<从机指令包*/
#define TYPE_DATAPACK     	0x02  /**<数据包*/
#define TYPE_MASTEPACK    	0x03  /**<主机指令包*/
#define TYPE_HOSTPACK     	0x04  /**<串口主返回指令包*/

#define PACKHEADLEN			3
#define FILEHEADLEN			4
#define PACKDATALEN  		12 	  // 1211         /**<数据包数据长度*/
#define CPARAMLEN 			80 	  /**<指令包参数长度*/
#define CPHLEN 				7     /**<指令头部长度*/
#define DPHLEN 				9     /**<数据头部长度*/

#define CMD_EREFLASH  		0x01  /**<申请flash  */
#define CMD_GETBMAP   		0x02  /**<获得bitmap */
#define CMD_DEVINFO   		0x03  /**< 获取设备信息*/
#define CMD_PICDIS    		0x04  /**<显示图片*/
#define CMD_OTA       		0x05  /**< OTA */
#define CMD_ACESS     		0x06  /**<入网**/
#define CMD_SYNCTM    		0x07  /**<时间同步*/
#define CMD_DISCOVDEV 		0x08  /**<开始探索设备*/
#define CMD_GETBUFF   		0x09  /**<获取数据buff大小*/
#define CMD_SCANDEV   		0x0A  /**<标签上报*/
//#define CMD_DISCON    	0x0B  /**<断开连接*/
#define CMD_LEDCONTROL		0x0B  /**<LED控制*/
#define CMD_SETMODE  		0x0D  /**<设置广播模式*/


#define RECMDSUCCESS 	 	255  /**<指令执行成功*/
#define RECMDFAILED   		104  /**<指令执行失败*/
#define RECMDADDSUC   		105  /**<指令添加成功*/

typedef struct _dataPack{
    UINT8  head[PACKHEADLEN]   ; /**<包头*/
	UINT8  type      ; /**<数据的类型*/
	UINT8  dataLen[2] ; /**<数据长度  */
	UINT8  checkSum  ; /**<校验和*/
	UINT8  totalPack ; /**<总包数*/
	UINT8  packNumber; /**<包编号*/
	UINT8  data[PACKDATALEN]; /**<指令包参数长度*/
	//UINT8*  pdata;
}DATA_PACK_S;

typedef struct _cmdPack{
    UINT8 head[PACKHEADLEN] ; /**<包头*/
	UINT8 type    ; /**<数据的类型*/
	UINT8 cmdLen  ; /**<指令长度  */
	UINT8 checkSum; /**<校验和*/
	UINT8 cmd     ; /**<指令码*/
	UINT8 param[CPARAMLEN]; /**<指令包参数长度*/
}CMD_PACK_S;

typedef struct CompFileHead { /**<压缩文件头部结构体  */  
    uint8_t  FHead[FILEHEADLEN];        /**<文件头部            */
    uint8_t  CheckSum;        /**<文件累加和，取最低位*/
    uint8_t  FileQutity;      /**<文件数量            */
    uint8_t  recv;            /**<保留                */
    uint32_t FileSize;        /**<文件大小            */
}stCFHead;

typedef struct CompPictHead { /**<压缩图片头部*/
	uint8_t  FileOffset;      /**<文件偏移            */
	uint32_t X_pos;
	uint32_t Y_pos;
	uint32_t Width;
	uint32_t Heigth;
	uint32_t DataSize;
	uint8_t  CompType;
} stCPHead;

#endif

/*********************************************************************
 * FUNCTIONS
 */
void on_bleuartServiceEvt(uint16_t connection_handle, ble_uart_evt_t *p_evt);

/*********************************************************************
*********************************************************************/
#endif
#ifdef __cplusplus
}
#endif

#endif
