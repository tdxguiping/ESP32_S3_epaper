/********************************** (C) COPYRIGHT *******************************
 * File Name          : devinfoservice.h
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2018/12/11
 * Description        :
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *******************************************************************************/

#ifndef XTINFOSERVICE_H
#define XTINFOSERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CONFIG.h"
#include "app_cfg.h"
#include "stdint.h"

/*********************************************************************
 * INCLUDES
 */

/*********************************************************************
 * CONSTANTS
 */
//#define BOEINFO_USE_BOND
// Device Information Service Parameters
#ifdef ENABLE_SOFTWARE_TO_XT

#define XTINFO_RECEIVE_DATA       				2
#define XTINFO_TX_VALUE_HANDLE   				4
#define XTINFO_NOTITY_ENABLE           			5
//#define XTINFO_RECEIVE_DATA       				14

#define BOE_DATA_FIRST				0		//DATA SEND FIRST PACKAGE	
#define BOE_DATA_FIRST_COLOR		1		//DATA SEND FIRST COLOR(WHITE AND BLACK DATA)
#define BOE_DATA_SECOND_COLOR		2		//DATA SEND SECOND COLOR(WHITE AND RED DATA)
#define BOE_DATA_THIRD_COLOR		3		//DATA SEND THIRD COLOR(WHITE AND RED DATA)
#define BOE_DATA_FOURTH_COLOR		4		//DATA SEND FOURTH COLOR(WHITE AND RED DATA)
#define BOE_DATA_FIFTH_COLOR		5		//DATA SEND FIFTH COLOR(WHITE AND RED DATA)
#define BOE_DATA_SIXTH_COLOR		6		//DATA SEND SIXTH COLOR(WHITE AND RED DATA)

#define BOE_DATA_FIRST_COLOR_END	7		//DATA SEND FIRST COLOR END(WHITE AND BLACK DATA)
#define BOE_DATA_SECOND_COLOR_END	8		//DATA SEND SECOND COLOR END(WHITE AND RED DATA)
#define BOE_DATA_THIRD_COLOR_END	9		//DATA SEND THIRD COLOR END(WHITE AND RED DATA)
#define BOE_DATA_FOURTH_COLOR_END	10		//DATA SEND FOURTH COLOR END(WHITE AND RED DATA)
#define BOE_DATA_FIFTH_COLOR_END	11		//DATA SEND FIFTH COLOR END(WHITE AND RED DATA)
#define BOE_DATA_SIXTH_COLOR_END	12		//DATA SEND SIXTH COLOR END(WHITE AND RED DATA)

#define BLE_UART_RX_BUFF_SIZE    1


/*********************************************************************
 * TYPEDEFS
 */

typedef enum
{
    BLE_UART_EVT_TX_NOTI_DISABLED = 1,
    BLE_UART_EVT_TX_NOTI_ENABLED,
    BLE_UART_EVT_BLE_DATA_RECIEVED,
} ble_uart_evt_type_t;

typedef struct
{
    uint8_t *p_data; /**< A pointer to the buffer with received data. */
    uint16_t       length; /**< Length of received data. */
} ble_uart_evt_rx_data_t;

typedef struct
{
    ble_uart_evt_type_t    type;
    ble_uart_evt_rx_data_t data;
} ble_uart_evt_t;

typedef void (*ble_uart_ProfileChangeCB_t)(uint16_t connection_handle, ble_uart_evt_t *p_evt);
typedef void (*XtNotifyDataCallback_t)(uint16_t connHandle,UINT8 *data,UINT8 dlen);

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * Profile Callbacks
 */

/*********************************************************************
 * API FUNCTIONS
 */
/*********************************************************************
 * @fn      BoeInfo_SetParameter
 *
 * @brief   Set a Device Information parameter.
 *
 * @param   param - Profile parameter ID
 * @param   len - length of data to right
 * @param   value - pointer to data to write.  This is dependent on
 *          the parameter ID and WILL be cast to the appropriate
 *          data type (example: data type of uint16_t will be cast to
 *          uint16_t pointer).
 *
 * @return  bStatus_t
 */
bStatus_t XtInfo_SetParameter(uint8_t param, uint8_t len, void *value);

/*
 * BoeInfo_GetParameter - Get a Device Information parameter.
 *
 *    param - Profile parameter ID
 *    value - pointer to data to write.  This is dependent on
 *          the parameter ID and WILL be cast to the appropriate
 *          data type (example: data type of uint16_t will be cast to
 *          uint16_t pointer).
 */
extern bStatus_t XtInfo_GetParameter(uint8_t param, void *value);
/*********************************************************************
*********************************************************************/
extern bStatus_t ble_send_boeInfo( uint16_t connHandle, uint8_t *data, uint16_t length);

extern void on_bleuartServiceEvt(UINT16 connection_handle, ble_uart_evt_t *p_evt);
extern void setXtNofityCallBack(uint16_t connHandle, XtNotifyDataCallback_t rCb);
extern void intBoardCastData();
#endif
#ifdef __cplusplus
}
#endif
#endif /* BOENFOSERVICE_H */
