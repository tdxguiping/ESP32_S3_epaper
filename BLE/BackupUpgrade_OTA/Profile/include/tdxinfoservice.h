/********************************** (C) COPYRIGHT *******************************
 * File Name          : devinfoservice.h
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2018/12/11
 * Description        :
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *******************************************************************************/

#ifndef TDXINFOSERVICE_H
#define TDXINFOSERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CONFIG.h"
#include "app_cfg.h"

/*********************************************************************
 * INCLUDES
 */

/*********************************************************************
 * CONSTANTS
 */
//#define TDXINFO_USE_BOND
// Device Information Service Parameters
#ifdef ENABLE_SOFTWARE_TO_TDX

//uuID
#define TDXINFO_GET_DATA       					3
#define TDXINFO_SEND_DATA       				7
#define TDXINFO_NOTITY_SEND_RESULT_INFO       	10
#define TDXINFO_SEND_AES_IV       				12
#define TDXINFO_SEND_PHONE_ID          			19
#define TDXINFO_NOTITY_SEND_END          		23
#define TDXINFO_OTA_INFO          				25
#define TDXINFO_SEND_PRE_SAVE         			27
#define TDXINFO_NOTITY_SAVE_STATUS          	28
#define TDXINFO_SEND_CLEAN         				30
#define TDXINFO_SWITCH_MODE         			32

#define TDX_DATA_FIRST				0		//DATA SEND FIRST PACKAGE	
#define TDX_DATA_FIRST_COLOR		1		//DATA SEND FIRST COLOR(WHITE AND BLACK DATA)
#define TDX_DATA_SECOND_COLOR		2		//DATA SEND SECOND COLOR(WHITE AND RED DATA)
#define TDX_DATA_THIRD_COLOR		3		//DATA SEND THIRD COLOR(WHITE AND RED DATA)
#define TDX_DATA_FOURTH_COLOR		4		//DATA SEND FOURTH COLOR(WHITE AND RED DATA)
#define TDX_DATA_FIFTH_COLOR		5		//DATA SEND FIFTH COLOR(WHITE AND RED DATA)
#define TDX_DATA_SIXTH_COLOR		6		//DATA SEND SIXTH COLOR(WHITE AND RED DATA)

#define TDX_DATA_FIRST_COLOR_END	7		//DATA SEND FIRST COLOR END(WHITE AND BLACK DATA)
#define TDX_DATA_SECOND_COLOR_END	8		//DATA SEND SECOND COLOR END(WHITE AND RED DATA)
#define TDX_DATA_THIRD_COLOR_END	9		//DATA SEND THIRD COLOR END(WHITE AND RED DATA)
#define TDX_DATA_FOURTH_COLOR_END	10		//DATA SEND FOURTH COLOR END(WHITE AND RED DATA)
#define TDX_DATA_FIFTH_COLOR_END	11		//DATA SEND FIFTH COLOR END(WHITE AND RED DATA)
#define TDX_DATA_SIXTH_COLOR_END	12		//DATA SEND SIXTH COLOR END(WHITE AND RED DATA)

#define AES_KEY_IV_LENGTH			16		//AES KEY IV LENGTH
#define PBONE_ID_LENGTH				30		//PHONE ID LENGTH
#define MD5_LENGTH					6


#define OP_TYPE_IMG_A				0		//IMAGE A PAGE	
#define OP_TYPE_IMG_B				OP_TYPE_IMG_A+1		//IMAGE B PAGE
#define OP_TYPE_IMG_AB				OP_TYPE_IMG_B+1		//IMAGE AB PAGE
#define OP_TYPE_IMG_DIFF_A			OP_TYPE_IMG_AB+1		//IMAGE AB PAGE DIFF CONETENT, A PAGE CONTENT
#define OP_TYPE_IMG_DIFF_B			OP_TYPE_IMG_DIFF_A+1		//IMAGE AB PAGE DIFF CONETENT, B PAGE CONTENT
#define OP_TYPE_SAVE_IMG_A			OP_TYPE_IMG_DIFF_B+1		//SAVE IMAGE A PAGE	
#define OP_TYPE_SAVE_IMG_B			OP_TYPE_SAVE_IMG_A+1		//SAVE IMAGE B PAGE
#define OP_TYPE_SAVE_IMG_AB			OP_TYPE_SAVE_IMG_B+1		//SAVE IMAGE AB PAGE

#define BROADCAST_COMMAND_PRESAVE   			'G'
#define BROADCAST_COMMAND_CLEAN_SCREEN   		'C'
#define BROADCAST_COMMAND_CLEAN_PRESAVE   		'E'

#define BROADCAST_PREFIX_CHAR2   				'@'
#define BROADCAST_PREFIX_CHAR3   				'!'
#define BROADCAST_PREFIX_CHAR4   				'#'

/*********************************************************************
 * TYPEDEFS
 */

struct _TDX_AES_INFO {
	unsigned char szAesKey[AES_KEY_IV_LENGTH];
	unsigned char szPhoneId[PBONE_ID_LENGTH];
    unsigned char szAesIv[AES_KEY_IV_LENGTH];
};

struct _TDX_AES_DATA_INFO {
	unsigned char  fOpType;				//SCREEN OPRATE TYPE A, B, OR AB 
	unsigned char  fScreenType;				//SCREEN OPRATE TYPE A, B, OR AB 
    unsigned int   fPackageNum;			//DATA TOTAL PACKAGE ,BUT NO CONTAIN THIS PACKAGE
	unsigned int   fUserId;				// User ID (4 bytes)
	unsigned char  fIsSecret;			// SECRET OR NOT
	unsigned char  fIsZip;				// ZIP OR NOT
	unsigned char  fRoomNum;			// Save room num
	unsigned char  fGroupNum;			// Save Group num
};

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
 * @fn      TdxInfo_SetParameter
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
bStatus_t TdxInfo_SetParameter(uint8_t param, uint8_t len, void *value);

/*
 * TdxInfo_GetParameter - Get a Device Information parameter.
 *
 *    param - Profile parameter ID
 *    value - pointer to data to write.  This is dependent on
 *          the parameter ID and WILL be cast to the appropriate
 *          data type (example: data type of uint16_t will be cast to
 *          uint16_t pointer).
 */
extern bStatus_t TdxInfo_GetParameter(uint8_t param, void *value);

extern void TdxInfo_ClearDisplayBusyProtect(void);
extern bStatus_t TdxInfo_SendWifiDataToFrontend(uint16_t connHandle, uint8_t *data, uint16_t length);

/*********************************************************************
*********************************************************************/

#endif
#ifdef __cplusplus
}
#endif

#endif /* TDXNFOSERVICE_H */
