/********************************** (C) COPYRIGHT *******************************
 * File Name          : devinfoservice.h
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2018/12/11
 * Description        :
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *******************************************************************************/

#ifndef BOEINFOSERVICE_H
#define BOEINFOSERVICE_H

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
//#define BOEINFO_USE_BOND
// Device Information Service Parameters
#ifdef ENABLE_SOFTWARE_TO_BOE

#define BOEINFO_NOTITY_ERROR_INFO              	3
#ifdef BOEINFO_USE_BOND
#define BOEINFO_NOTITY_BOND_INFO           		9
#define BOEINFO_SEND_DATA       				11
#define BOEINFO_NOTITY_SEND_RESULT_INFO       	14
#define BOEINFO_SEND_AES_IV       				16

#define BOEINFO_SEND_PHONE_ID          			23
#define BOEINFO_NOTITY_SEND_END          		31
#define BOEINFO_OTA_INFO          				33
#define BOEINFO_SEND_PRE_SAVE         			35
#define BOEINFO_NOTITY_SAVE_STATUS          	36
#define BOEINFO_SEND_CLEAN         				38
#define BOEINFO_SWITCH_MODE         			40
#else
#define BOEINFO_SEND_DATA       				9
#define BOEINFO_NOTITY_SEND_RESULT_INFO       	12
#define BOEINFO_SEND_AES_IV       				14

#define BOEINFO_SEND_PHONE_ID          			21
#define BOEINFO_NOTITY_SEND_END          		29
#define BOEINFO_OTA_INFO          				31
#define BOEINFO_SEND_PRE_SAVE         			33
#define BOEINFO_NOTITY_SAVE_STATUS          	34
#define BOEINFO_SEND_CLEAN         				36
#define BOEINFO_SWITCH_MODE         			38
#endif

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

#define AES_KEY_IV_LENGTH			16		//AES KEY IV LENGTH
#define PBONE_ID_LENGTH				30		//PHONE ID LENGTH
#define MD5_LENGTH					6

#define OP_TYPE_OTA_FILE			0     	//OTA upgrade
#define OP_TYPE_IMG_A				1		//IMAGE A PAGE	
#define OP_TYPE_IMG_B				2		//IMAGE B PAGE
#define OP_TYPE_IMG_AB				3		//IMAGE AB PAGE
#define OP_TYPE_IMG_DIFF_A			4		//IMAGE AB PAGE DIFF CONETENT, A PAGE CONTENT
#define OP_TYPE_IMG_DIFF_B			5		//IMAGE AB PAGE DIFF CONETENT, B PAGE CONTENT
#define OP_TYPE_SAVE_IMG_A			6		//SAVE IMAGE A PAGE	
#define OP_TYPE_SAVE_IMG_B			7		//SAVE IMAGE B PAGE
#define OP_TYPE_SAVE_IMG_AB			8		//SAVE IMAGE AB PAGE

#define BROADCAST_COMMAND_PRESAVE   			'B'
#define BROADCAST_COMMAND_CLEAN_ALL   			'C'
#define BROADCAST_COMMAND_CLEAN_ROOM   			'D'
#define BROADCAST_COMMAND_CLEAN_ROOM_AND_GROUP	'E'
#define BROADCAST_COMMAND_CLEAN_ALL_SCREEN		'F'

#define BROADCAST_PREFIX_CHAR2   				'%'
#define BROADCAST_PREFIX_CHAR3   				'!'
#define BROADCAST_PREFIX_CHAR4   				'#'

/*********************************************************************
 * TYPEDEFS
 */

struct _BOE_AES_INFO {
	unsigned char szAesKey[AES_KEY_IV_LENGTH];
	unsigned char szPhoneId[PBONE_ID_LENGTH];
    unsigned char szAesIv[AES_KEY_IV_LENGTH];
};

struct _BOE_AES_DATA_INFO {
	unsigned char  fOpType;				//SCREEN OPRATE TYPE A, B, OR AB 
	unsigned char  szMd5[MD5_LENGTH];	//MD5 VALUE
    unsigned int   fPackageNum;			//DATA TOTAL PACKAGE ,BUT NO CONTAIN THIS PACKAGE
	unsigned int   fBeforeDataLength;	//
	unsigned char  fIsSecret;			// SECRET OR NOT
	unsigned char  fIsZip;				// ZIP OR NOT
	unsigned char  fGroupInfo;			// Save Group info
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
bStatus_t BoeInfo_SetParameter(uint8_t param, uint8_t len, void *value);

/*
 * BoeInfo_GetParameter - Get a Device Information parameter.
 *
 *    param - Profile parameter ID
 *    value - pointer to data to write.  This is dependent on
 *          the parameter ID and WILL be cast to the appropriate
 *          data type (example: data type of uint16_t will be cast to
 *          uint16_t pointer).
 */
extern bStatus_t BoeInfo_GetParameter(uint8_t param, void *value);

/*********************************************************************
*********************************************************************/
#endif
#ifdef __cplusplus
}
#endif

#endif /* BOENFOSERVICE_H */
