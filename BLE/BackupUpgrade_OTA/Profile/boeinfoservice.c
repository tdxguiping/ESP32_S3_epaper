/*********************************************************************
 * INCLUDES
 */
#include <stdlib.h>
#include "app_cfg.h"

#ifdef ENABLE_SOFTWARE_TO_BOE
#include "boenfoservice.h"
#include "rledecode.h"
#include "commoninfo.h"
#include "aes_util.h"
#include "base64.h"
#include "flash_api.h"
#include "util.h"
#include "epd_driver.h"

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * CONSTANTS
 */
struct _BOE_AES_INFO global_BOE_AES_INFO;
struct _BOE_AES_DATA_INFO global_BOE_AES_DATA_INFO;

/*********************************************************************
 * TYPEDEFS
 */

//#define FALSH_TEST_DEBUG
/*********************************************************************
 * GLOBAL VARIABLES
 */
// Boe information service
const uint8_t boeInfoSerUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x00,0xff,0x12,0x6b};
	
// 1 Boe error information service
const uint8_t boeInfoErrorUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x01,0xff,0x12,0x6b};

// 2 get adc ID
const uint8_t boeInfoGetAdcUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x02,0xff,0x12,0x6b};

// 3 get ver
const uint8_t boeInfoGetVerUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x03,0xff,0x12,0x6b};

#ifdef BOEINFO_USE_BOND
// 4 get bond
const uint8_t boeInfoBondUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x05,0xff,0x12,0x6b};
#endif

// 5 send data
const uint8_t boenfoSendDataUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x06,0xff,0x12,0x6b};

// 6  Bind
const uint8_t boeInfoBindUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x07,0xff,0x12,0x6b};

// 7 send aes iv
const uint8_t boeInfoSendAesIvUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x08,0xff,0x12,0x6b};

// 8 bind iv
const uint8_t boeInfoBindIvUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x09,0xff,0x12,0x6b};

// 9 send aes key
const uint8_t boeInfoSendAesKeyUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x10,0xff,0x12,0x6b};

// 10 send phone id
const uint8_t boeInfoSendPhoneIdUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x11,0xff,0x12,0x6b};

// 11 get board model
const uint8_t boeInfoGetBoardModelUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x12,0xff,0x12,0x6b};

// 12 get board ver
const uint8_t boeInfoGetBoardVerUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x13,0xff,0x12,0x6b};

// 13 get cpu type
const uint8_t boeInfoGetCpuTypeUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x14,0xff,0x12,0x6b};

// 14 notify dev
const uint8_t boeInfoNotifyDevUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x15,0xff,0x12,0x6b};

// 15 notify ota 
const uint8_t boeInfoNotifyOtaUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x16,0xff,0x12,0x6b};

// 16 pre save 
const uint8_t boeInfoSendPreSaveUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x17,0xff,0x12,0x6b};

// 17 clean screen 
const uint8_t boeInfoSendCleanUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x18,0xff,0x12,0x6b};

// 18 switch work mod 
const uint8_t boeInfoSwitchModeUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x19,0xff,0x12,0x6b};

/*********************************************************************
 * EXTERNAL VARIABLES
 */
/*********************************************************************
 * EXTERNAL FUNCTIONS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */

/*********************************************************************
 * Profile Attributes - variables
 */

// boe Information Service attribute
static const gattAttrType_t boeInfoSer = {ATT_UUID_SIZE, boeInfoSerUUID};

//1 Boe error information service
static uint8_t boeInfoErrorProps = GATT_PROP_NOTIFY;
static gattCharCfg_t boeInfoError_cccd[4];

//2 get adc ID
static uint8_t boeInfoGetAdcProps = GATT_PROP_READ;

//3 get ver
static uint8_t boeInfoGetVerProps = GATT_PROP_READ;

#ifdef BOEINFO_USE_BOND
//4 get bond
static uint8_t boeInfoBondProps = GATT_PROP_READ | GATT_PROP_WRITE;
#endif

//5 send data
static uint8_t boenInfoSendDataProps = GATT_PROP_WRITE | GATT_PROP_WRITE_NO_RSP;

//6 Bind
static uint8_t boeInfoBindProps = GATT_PROP_NOTIFY;
static gattCharCfg_t boeInfoBind_cccd[4];

//7 send aes iv
static uint8_t boeInfoSendAesIvProps = GATT_PROP_WRITE;

//8 bind iv
static uint8_t boeInfoBindIvProps = GATT_PROP_NOTIFY;
static gattCharCfg_t boeInfoBindIv_cccd[4];

//9 send aes key
static uint8_t boeInfoSendAesKeyProps = GATT_PROP_WRITE;

//10 send phone id
static uint8_t boeInfoSendPhoneIdProps = GATT_PROP_WRITE;

//11 get board model
static uint8_t boeInfoGetBoardModelProps = GATT_PROP_READ;

//12  get board ver
static uint8_t boeInfoGetBoardVerProps = GATT_PROP_READ;

// 13 get cpu type
static uint8_t boeInfoGetCpuTypeProps = GATT_PROP_READ;

// 14 notify dev
static uint8_t boeInfoNotifyDevProps = GATT_PROP_READ | GATT_PROP_WRITE;

// 15 notify ota
static uint8_t boeInfoNotifyOtaProps = GATT_PROP_READ | GATT_PROP_WRITE | GATT_PROP_WRITE_NO_RSP;//GATT_PROP_READ | GATT_PROP_WRITE;//

//16 presave 
static uint8_t boeInfoSendPreSaveProps = GATT_PROP_NOTIFY | GATT_PROP_WRITE | GATT_PROP_WRITE_NO_RSP;

//17 clean 
static uint8_t boeInfoSendCleanProps = GATT_PROP_WRITE;

// 18 switch 
static uint8_t boeInfoSwitchModeProps = GATT_PROP_READ | GATT_PROP_WRITE;

static gattCharCfg_t boeInfoPreSave_cccd[4];

/*********************************************************************
 * Profile Attributes - Table
 */
static gattAttribute_t boeInfoAttrTbl[] = {
    // Device Information Service index0
    {
        {ATT_BT_UUID_SIZE, primaryServiceUUID}, /* type */
        GATT_PERMIT_READ,                       /* permissions */
        0,                                      /* handle */
        (uint8_t *)&boeInfoSer              	/* pValue */
    },

#if 1
    //CHAR 1,boeInfoErrorUUID
    // System ID Declaration index1
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &boeInfoErrorProps
    },

    // System ID Value index2
    {
        {ATT_UUID_SIZE, boeInfoErrorUUID},
        0,
        0,
        NULL
    },
    //index3
    {
        { ATT_BT_UUID_SIZE, clientCharCfgUUID },
        GATT_PERMIT_READ|GATT_PERMIT_WRITE,
        0,
        (uint8*)boeInfoError_cccd
    },
#endif

#if 1
#if 1
    //CHAR 2 ,boeInfoGetAdcUUID
    // System ID Declaration index4
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &boeInfoGetAdcProps
    },

    // System ID Value index5
    {
        {ATT_UUID_SIZE, boeInfoGetAdcUUID},
        GATT_PERMIT_READ,
        0,
        NULL
    },
#endif

#if 1
    //CHAR 3,boeInfoGetVerUUID
    // System ID Declaration  index06
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &boeInfoGetVerProps
    },

    // System ID Value index7
    {
        {ATT_UUID_SIZE, boeInfoGetVerUUID},
        GATT_PERMIT_READ,
        0,
        NULL
    },
#endif

#ifdef BOEINFO_USE_BOND
    //CHAR 4,boeInfoBondUUID
    // System ID Declaration index8
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &boeInfoBondProps
    },

    // System ID Value index9
    {
        {ATT_UUID_SIZE, boeInfoBondUUID},
        GATT_PERMIT_READ|GATT_PERMIT_WRITE,
        0,
        NULL
    },
#endif
#endif

#if 1
#if 1
    //CHAR 5,boenfoSendDataUUID
    // System ID Declaration index10
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &boenInfoSendDataProps
    },

    // System ID Value index11
    {
        {ATT_UUID_SIZE, boenfoSendDataUUID},
        GATT_PERMIT_WRITE,
        0,
        NULL
    },
#endif

#if 1
    //CHAR 6,boeInfoBindUUID
    // System ID Declaration index12
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &boeInfoBindProps
    },

    // System ID Value index13
    {
        {ATT_UUID_SIZE, boeInfoBindUUID},
        0,
        0,
        NULL
    },
    { //index14
        { ATT_BT_UUID_SIZE, clientCharCfgUUID },
        GATT_PERMIT_READ|GATT_PERMIT_WRITE,
        0,
        (uint8*)boeInfoBind_cccd
    },
#endif

#if 1
    //CHAR 7,boeInfoSendAesIvUUID
    // System ID Declaration index15
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &boeInfoSendAesIvProps
    },

    // System ID Value index16
    {
        {ATT_UUID_SIZE, boeInfoSendAesIvUUID},
        GATT_PERMIT_WRITE,
        0,
        NULL
    },
#endif

#if 1
    //CHAR 8,boeInfoBindIvUUID
    // System ID Declaration index17
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &boeInfoBindIvProps
    },

    // System ID Value index18
    {
        {ATT_UUID_SIZE, boeInfoBindIvUUID},
        0,
        0,
        NULL
    },
    { //index 19
        { ATT_BT_UUID_SIZE, clientCharCfgUUID },
        GATT_PERMIT_READ|GATT_PERMIT_WRITE,
        0,
        (uint8*)boeInfoBindIv_cccd
    },
#endif

#if 1
    //CHAR 9,boeInfoSendAesKeyUUID
    // System ID Declaration index20
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &boeInfoSendAesKeyProps
    },

    // System ID Value index21
    {
        {ATT_UUID_SIZE, boeInfoSendAesKeyUUID},
        GATT_PERMIT_WRITE,
        0,
        NULL
    },
#endif

#if 1
    //CHAR 10,boeInfoSendPhoneIdUUID
    // System ID Declaration index22
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &boeInfoSendPhoneIdProps
    },

    // System ID Value index23
    {
        {ATT_UUID_SIZE, boeInfoSendPhoneIdUUID},
        GATT_PERMIT_WRITE,
        0,
        NULL
    },
#endif

#if 1
    //CHAR 11,boeInfoGetBoardModelUUID
    // System ID Declaration index24
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &boeInfoGetBoardModelProps
    },

    // System ID Value index25
    {
        {ATT_UUID_SIZE, boeInfoGetBoardModelUUID},
        GATT_PERMIT_READ,
        0,
        NULL
    },
#endif

#if 1
    //CHAR 12,boeInfoGetBoardVerUUID
    // System ID Declaration index26
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &boeInfoGetBoardVerProps
    },

    // System ID Value index27
    {
        {ATT_UUID_SIZE, boeInfoGetBoardVerUUID},
        GATT_PERMIT_READ,
        0,
        NULL
    },
#endif

#if 1
    //CHAR 13,boeInfoGetCpuTypeUUID
    // System ID Declaration index28
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &boeInfoGetCpuTypeProps
    },

    // System ID Value index29
    {
        {ATT_UUID_SIZE, boeInfoGetCpuTypeUUID},
        GATT_PERMIT_READ,
        0,
        NULL
    },
#endif

#if 1
    //CHAR 14,boeInfoNotifyDevUUID
    // System ID Declaration index30
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ|GATT_PERMIT_WRITE,
        0,
        &boeInfoNotifyDevProps
    },

    // System ID Value 31
    {
        {ATT_UUID_SIZE, boeInfoNotifyDevUUID},
        GATT_PERMIT_READ|GATT_PERMIT_WRITE,
        0,
        NULL
    },

	//CHAR 6,boeInfoBindUUID
	// System ID Declaration index32
	{
		{ATT_BT_UUID_SIZE, characterUUID},
		GATT_PERMIT_READ|GATT_PERMIT_WRITE,
		0,
		&boeInfoNotifyOtaProps
	},
	
	// System ID Value index33
	{
		{ATT_UUID_SIZE, boeInfoNotifyOtaUUID},
		GATT_PERMIT_READ|GATT_PERMIT_WRITE,
		0,
		NULL
	},

#if 1
	//CHAR 15,boeInfoSendPreSaveUUID
	// System ID Declaration index34
	{
		{ATT_BT_UUID_SIZE, characterUUID},
		GATT_PERMIT_READ|GATT_PERMIT_WRITE,
		0,
		&boeInfoSendPreSaveProps
	},

	// System ID Value index35
	{
		{ATT_UUID_SIZE, boeInfoSendPreSaveUUID},
		GATT_PERMIT_READ|GATT_PERMIT_WRITE,
		0,
		NULL
	},

	{ //index36
		{ ATT_BT_UUID_SIZE, clientCharCfgUUID },
		GATT_PERMIT_READ|GATT_PERMIT_WRITE,
		0,
		(uint8*)boeInfoPreSave_cccd
	},
#endif

#if 1
	//CHAR 16,boeInfoSendPreSaveUUID
	// System ID Declaration index37
	{
		{ATT_BT_UUID_SIZE, characterUUID},
		GATT_PERMIT_READ,
		0,
		&boeInfoSendCleanProps
	},

	// System ID Value index38
	{
		{ATT_UUID_SIZE, boeInfoSendCleanUUID},
		GATT_PERMIT_WRITE,
		0,
		NULL
	},
#endif

#if 1
	//CHAR 16,boeInfoSwitchModeProps
	// System ID Declaration index39
	{
		{ATT_BT_UUID_SIZE, characterUUID},
		GATT_PERMIT_READ|GATT_PERMIT_WRITE,
		0,
		&boeInfoSwitchModeProps
	},

	// System ID Value index40
	{
		{ATT_UUID_SIZE, boeInfoSwitchModeUUID},
		GATT_PERMIT_READ|GATT_PERMIT_WRITE,
		0,
		NULL
	},
#endif
#endif
#endif
};

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static bStatus_t boeInfo_ReadAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
                                    uint8_t *pValue, uint16_t *pLen, uint16_t offset, uint16_t maxLen, uint8_t method);
static bStatus_t boeInfo_WriteAttrCB( uint16 connHandle, gattAttribute_t *pAttr,
                                      uint8 *pValue, uint16 len, uint16 offset,uint8 method );
bStatus_t ble_send_boeInfo( uint16_t connHandle, uint8_t *data, uint16_t length, uint8_t id,  gattCharCfg_t *boeInfo_cccd);
/*********************************************************************
 * PROFILE CALLBACKS
 */
// Boe Info Service Callbacks
gattServiceCBs_t boeInfoCBs = {
    boeInfo_ReadAttrCB, // Read callback function pointer
    boeInfo_WriteAttrCB,               // Write callback function pointer
    NULL                // Authorization callback function pointer
};

static int phoneid_len = 0;
static unsigned char u8IsFirstPackage = BOE_DATA_FIRST;

/*********************************************************************
 * NETWORK LAYER CALLBACKS
 */

/*********************************************************************
 * PUBLIC FUNCTIONS
 */
static void BoeInfo_HandleConnStatusCB ( uint16 connHandle, uint8 changeType )
{
    // Make sure this is not loopback connection
    if ( connHandle != LOOPBACK_CONNHANDLE )
    {
        // Reset Client Char Config if connection has dropped
        if ( ( changeType == LINKDB_STATUS_UPDATE_REMOVED )      ||
                ( ( changeType == LINKDB_STATUS_UPDATE_STATEFLAGS ) &&
                  ( !linkDB_Up( connHandle ) ) ) )
        {
            //ble_uart_TxCCCD[0].value = 0;
            GATTServApp_InitCharCfg( INVALID_CONNHANDLE, boeInfoError_cccd );
            GATTServApp_InitCharCfg( INVALID_CONNHANDLE, boeInfoBind_cccd );
            GATTServApp_InitCharCfg( INVALID_CONNHANDLE, boeInfoBindIv_cccd );
			GATTServApp_InitCharCfg( INVALID_CONNHANDLE, boeInfoPreSave_cccd );
        }
    }
}

/*********************************************************************
 * @fn      BoeInfo_AddService
 *
 * @brief   Initializes the Device Information service by registering
 *          GATT attributes with the GATT server.
 *
 * @return  Success or Failure
 */
bStatus_t CustomerInfo_AddService(void)
{
    uint8_t ret;

    GATTServApp_InitCharCfg( INVALID_CONNHANDLE, boeInfoError_cccd );
    GATTServApp_InitCharCfg( INVALID_CONNHANDLE, boeInfoBind_cccd );
    GATTServApp_InitCharCfg( INVALID_CONNHANDLE, boeInfoBindIv_cccd );
	GATTServApp_InitCharCfg( INVALID_CONNHANDLE, boeInfoPreSave_cccd );

    // Register with Link DB to receive link status change callback
    linkDB_Register( BoeInfo_HandleConnStatusCB  );

    // Register GATT attribute list and CBs with GATT Server App
    ret =  GATTServApp_RegisterService(boeInfoAttrTbl,
                                       GATT_NUM_ATTRS(boeInfoAttrTbl),
                                       GATT_MAX_ENCRYPT_KEY_SIZE,
                                       &boeInfoCBs);
    PRINT("ADD boe service:%02x\r\n",ret);
    return ret;
}

/*********************************************************************
 * @fn          boeInfo_ReadAttrCB
 *
 * @brief       Read an attribute.
 *
 * @param       connHandle - connection message was received on
 * @param       pAttr - pointer to attribute
 * @param       pValue - pointer to data to be read
 * @param       pLen - length of data to be read
 * @param       offset - offset of the first octet to be read
 * @param       maxLen - maximum length of data to be read
 *
 * @return      Success or Failure
 */
static bStatus_t boeInfo_ReadAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
                                    uint8_t *pValue, uint16_t *pLen, uint16_t offset, uint16_t maxLen, uint8_t method)
{
    bStatus_t status = SUCCESS;

    uint16_t gatt_handle = pAttr->handle - boeInfoAttrTbl[0].handle;
    PRINT("read gatt handle:%08x\r\n",gatt_handle);
    switch (gatt_handle) {
#ifdef BOEINFO_USE_BOND
		case BOEINFO_NOTITY_BOND_INFO:{
			PRINT("ReadAttrCB BOEINFO_NOTITY_BOND_INFO 0000000000000000000\r\n");
			memcpy(pValue,global_BOE_AES_INFO.szPhoneId,phoneid_len);
			hex_dump(pValue, phoneid_len);
            *pLen = phoneid_len;
			break;
		}
#endif
		case BOEINFO_NOTITY_SEND_END:{
			PRINT("ReadAttrCB BOEINFO_NOTITY_SEND_END 0000000000000000000\r\n");
			uint8_t finish_data[] = {0};
			uint8_t finish_data2[] = {2};
			if(global_DEVICE_STATUS.fDataSendSuccess == Is_Yes){
				memcpy(pValue,finish_data,1);
				*pLen = 1;
			}else{
				memcpy(pValue,finish_data2,1);
				*pLen = 1;
			}
			break;
		}
		case BOEINFO_OTA_INFO:{
			PRINT("ReadAttrCB BOEINFO_OTA_INFO 0000000000000000000 fOtaStatus:%d\r\n",global_DEVICE_STATUS.fOtaStatus);
			uint8_t ota_data[] = {0};
			ota_data[0] = global_DEVICE_STATUS.fOtaStatus;
			memcpy(pValue,ota_data,1);
			*pLen = 1;
			break;
		}
		case BOEINFO_SWITCH_MODE:{
			uint8_t mode[] = {0};
			uint8_t mode1[] = {0};
			Get_EEPROM_Flag(mode,WORKMODE_Position,WORKMODE_Len);
			PRINT("ReadAttrCB BOEINFO_SWITCH_MODE 0000000000000000 mode:%d\r\n",mode[0]);
			if(mode[0] == DEVICE_MODE_HIGH){
				mode1[0] = 1;
			}
			else{
				mode1[0] = 0;
			}
			memcpy(pValue,mode1,WORKMODE_Len);
            *pLen = WORKMODE_Len;
		}
		break;
        default:

            break;
    }
    return (status);
}

void DataCallBack(unsigned char *data, int length) {
	int i;
	unsigned char PREFIXSTR[5] = {0x42,0x4f,0x45,0x44,0x54};
	unsigned char phoneid[PBONE_ID_LENGTH];

	memcpy(global_BOE_AES_INFO.szAesKey,data,16);
	phoneid_len = length-21;
	memcpy(phoneid,data+21,phoneid_len);
	//hex_dump(phoneid, phoneid_len);
	Get_EEPROM_Flag(global_BOE_AES_INFO.szPhoneId,PHONEID_Position,PHONEID_Len);

	if (strncmp(global_BOE_AES_INFO.szPhoneId, phoneid, phoneid_len) == 0){
		global_DEVICE_STATUS.fIsBonded[0] = DEVICE_BONDED;
	}else{
		memcpy(global_BOE_AES_INFO.szPhoneId,phoneid,phoneid_len);
		Save_EEPROM_Flag(global_BOE_AES_INFO.szPhoneId,PHONEID_Position,PHONEID_Len);
		//hex_dump(global_BOE_AES_INFO.szPhoneId, phoneid_len);
		global_DEVICE_STATUS.fIsBonded[0] = DEVICE_BONDED;
	}
	Save_EEPROM_Flag(global_DEVICE_STATUS.fIsBonded,BONDED_Position,BONDED_Len);
	P_scanBondRspData(global_DEVICE_STATUS.fIsBonded[0]);
}

static bStatus_t boeInfo_WriteAttrCB( uint16 connHandle, gattAttribute_t *pAttr,
                                      uint8 *pValue, uint16 len, uint16 offset,uint8 method )
{
	int i, ret;
    bStatus_t status = SUCCESS;
    //uint8 notifyApp = 0xFF;
    // If attribute permissions require authorization to write, return error
    if ( gattPermitAuthorWrite( pAttr->permissions ) )
    {
        // Insufficient authorization
        return ( ATT_ERR_INSUFFICIENT_AUTHOR );
    }

	global_DEVICE_STATUS.fisHaveData= Is_Yes;
    uint16_t gatt_handle = pAttr->handle - boeInfoAttrTbl[0].handle;
    //PRINT("write gatt handle:%08x\r\n",gatt_handle);
    switch (gatt_handle) {
        case BOEINFO_NOTITY_ERROR_INFO:
        case BOEINFO_NOTITY_SEND_RESULT_INFO:
		case BOEINFO_NOTITY_SAVE_STATUS:
        case 17: {//19
				PRINT("WriteAttrCB BOEINFO_NOTITY_ERROR_INFO BOEINFO_NOTITY_SEND_RESULT_INFO BOEINFO_NOTITY_SAVE_STATUS connHandle=%x\r\n",connHandle);	
				//hex_dump(pValue, len);
                status = GATTServApp_ProcessCCCWriteReq( connHandle, pAttr, pValue, len,
                         offset, GATT_CLIENT_CFG_NOTIFY );
            }
            break;
		case BOEINFO_SEND_PHONE_ID:
			PRINT("send phone id\r\n");			
			//hex_dump(pValue, len);
			global_DEVICE_STATUS.fSystemTimeOut = 0;
		    rle_reverse_decrypt(pValue, len, DataCallBack);
			break;
#ifdef BOEINFO_USE_BOND
		case BOEINFO_NOTITY_BOND_INFO:
			{
				PRINT("WriteAttrCB BOEINFO_NOTITY_BOND_INFO\r\n");
				//hex_dump(pValue, len);
				if(pValue[0] == 0x01){
					global_DEVICE_STATUS.fIsBonded[0] = DEVICE_NOBONDED;
					Save_EEPROM_Flag(global_DEVICE_STATUS.fIsBonded,BONDED_Position,BONDED_Len);
					P_scanBondRspData(global_DEVICE_STATUS.fIsBonded[0]);
				}
			}
			break;
#endif
		case BOEINFO_SEND_AES_IV:
			PRINT("WriteAttrCB BOEINFO_SEND_AES_IV\r\n");
			u8IsFirstPackage = BOE_DATA_FIRST;
			//hex_dump(pValue, len);
			memcpy(global_BOE_AES_INFO.szAesIv,pValue,len);
			break;
		case BOEINFO_SEND_DATA:
			{
				//PRINT("WriteAttrCB BOEINFO_SEND_DATA\r\n");
				//PRINT("WriteAttrCB BOEINFO_SEND_DATA data len=%d\r\n",len);
				//hex_dump(pValue, len);
				char *b64_cipher_text = b64_encode(pValue, len);
	    		//printf("base64 result:\t%s\n", b64_cipher_text);
				
				size_t de_size = 0;
	    		unsigned char *deb64_orig_data = b64_decode_ex(b64_cipher_text, strlen(b64_cipher_text), &de_size);
				
				unsigned int out_len2 = 0;
	    		unsigned char *out_decrypted = decrypt(deb64_orig_data, de_size, &out_len2, global_BOE_AES_INFO.szAesKey, global_BOE_AES_INFO.szAesIv);
	   			//hex_dump(out_decrypted, out_len2);
				if(u8IsFirstPackage == BOE_DATA_FIRST){
					global_DEVICE_STATUS.fInitDriver = Is_Yes;
					global_BOE_AES_DATA_INFO.fOpType = out_decrypted[0];
					if(global_BOE_AES_DATA_INFO.fOpType == OP_TYPE_IMG_A || global_BOE_AES_DATA_INFO.fOpType == OP_TYPE_SAVE_IMG_A 
						|| global_BOE_AES_DATA_INFO.fOpType == OP_TYPE_IMG_DIFF_A)
						global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_A;
					if(global_BOE_AES_DATA_INFO.fOpType == OP_TYPE_IMG_B || global_BOE_AES_DATA_INFO.fOpType == OP_TYPE_SAVE_IMG_B 
						|| global_BOE_AES_DATA_INFO.fOpType == OP_TYPE_IMG_DIFF_B)
						global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_B;
					if(global_BOE_AES_DATA_INFO.fOpType == OP_TYPE_IMG_AB || global_BOE_AES_DATA_INFO.fOpType == OP_TYPE_SAVE_IMG_AB)
						global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_AB;
					memcpy(global_BOE_AES_DATA_INFO.szMd5,out_decrypted+1,MD5_LENGTH);
					global_BOE_AES_DATA_INFO.fPackageNum = (out_decrypted[7] << 24) | (out_decrypted[8] << 16) | (out_decrypted[9] << 8) | out_decrypted[10];
					global_BOE_AES_DATA_INFO.fBeforeDataLength = (out_decrypted[11] << 24) | (out_decrypted[12] << 16) | (out_decrypted[13] << 8) | out_decrypted[14];
					global_BOE_AES_DATA_INFO.fIsSecret = out_decrypted[15];
					global_BOE_AES_DATA_INFO.fIsZip = out_decrypted[16];
					global_BOE_AES_DATA_INFO.fGroupInfo= out_decrypted[17];
					global_BOE_AES_DATA_INFO.fRoomNum= out_decrypted[18];
					global_BOE_AES_DATA_INFO.fGroupNum= out_decrypted[19];
					global_DEVICE_STATUS.fPackageCnt = 0;
					global_DEVICE_STATUS.fDataSendSuccess = Is_No;
					global_DEVICE_STATUS.fOtaStatus = RTN_OTA_COMMON;
					global_EXTERN_FLASH_INFO.fBlockNum = EXTERN_FLASH_BLOCK_FIRST_ADDR;
					global_EXTERN_FLASH_INFO.fImageIndex = 0;
					global_DEVICE_STATUS.fRefreshType = DEVICE_OP_COMMON;
					global_DEVICE_STATUS.fPackageCount = global_BOE_AES_DATA_INFO.fPackageNum;
					global_DEVICE_STATUS.fIsNeedStandby = 0;
					global_DEVICE_STATUS.fImageDataLen = 0;

					if(global_DEVICE_STATUS.fScreenType != SCREEN_TYPE_IMG_AB){
						if(global_BOE_AES_DATA_INFO.fOpType != OP_TYPE_IMG_DIFF_A){
							global_DEVICE_STATUS.fIsNeedStandby = 1;
						}
					}else{
						if(global_BOE_AES_DATA_INFO.fOpType == OP_TYPE_IMG_DIFF_B
							|| global_BOE_AES_DATA_INFO.fOpType == OP_TYPE_IMG_AB
							|| global_BOE_AES_DATA_INFO.fOpType == OP_TYPE_SAVE_IMG_AB){
							global_DEVICE_STATUS.fIsNeedStandby = 1;
						}
					}

					clearBoardcastData();

					PRINT("BOE INFO DATA:\r\n");
					PRINT("global_BOE_AES_DATA_INFO.fOpType:%d\r\n",global_BOE_AES_DATA_INFO.fOpType);
					PRINT("global_BOE_AES_DATA_INFO.fPackageNum:%d\r\n",global_BOE_AES_DATA_INFO.fPackageNum);
					PRINT("global_BOE_AES_DATA_INFO.fBeforeDataLength:%d\r\n",global_BOE_AES_DATA_INFO.fBeforeDataLength);
					PRINT("global_BOE_AES_DATA_INFO.fIsSecret:%d\r\n",global_BOE_AES_DATA_INFO.fIsSecret);
					PRINT("global_BOE_AES_DATA_INFO.fIsZip:%d\r\n",global_BOE_AES_DATA_INFO.fIsZip);
					PRINT("global_BOE_AES_DATA_INFO.fGroupInfo:%d\r\n",global_BOE_AES_DATA_INFO.fGroupInfo);
					PRINT("global_BOE_AES_DATA_INFO.fRoomNum:%d\r\n",global_BOE_AES_DATA_INFO.fRoomNum);
					PRINT("global_BOE_AES_DATA_INFO.fGroupNum:%d\r\n",global_BOE_AES_DATA_INFO.fGroupNum);
					PRINT("BOE INFO DATA END\r\n");

					if(global_BOE_AES_DATA_INFO.fOpType == OP_TYPE_SAVE_IMG_A || global_BOE_AES_DATA_INFO.fOpType == OP_TYPE_SAVE_IMG_B
						|| global_BOE_AES_DATA_INFO.fOpType == OP_TYPE_SAVE_IMG_AB){
						setWorkMode(DEVICE_MODE_HIGH);
						
						InitFlashDriver();

						ret = EraseSaveBlock(connHandle,global_BOE_AES_DATA_INFO.fGroupNum,global_BOE_AES_DATA_INFO.fRoomNum,global_BOE_AES_DATA_INFO.fIsZip);
						if(ret == -1){
							//return ( ATT_ERR_INSUFFICIENT_AUTHOR );
							PRINT("WriteAttrCB BOEINFO_SEND_DATA error 00000000000000000000000000000\r\n");
							uint8_t finish_data[] = {10};
							ble_send_boeInfo(connHandle,finish_data,1,BOEINFO_NOTITY_SEND_RESULT_INFO-1, boeInfoBind_cccd) ;
							global_DEVICE_STATUS.fWorked =Is_No;
							tmos_start_task(main_task_ID,EVENT_Low_Power ,500);
							return ( ATT_ERR_INSUFFICIENT_AUTHOR );
						}
						PRINT("WriteAttrCB BOEINFO_SEND_DATA 666 fImageIndex:%d\r\n",ret);
						global_EXTERN_FLASH_INFO.fImageIndex = ret;
						//TDX_SPI_FLASH_E_4096Bytes(global_EXTERN_FLASH_INFO.fImageIndex,0,187);
					}
					u8IsFirstPackage = BOE_DATA_FIRST_COLOR;
				}else{
					if(global_DEVICE_STATUS.fScreenType != OP_TYPE_OTA_FILE){
						if(global_DEVICE_STATUS.fPackageCnt % 50 == 0)
							PRINT("WriteAttrCB TDXINFO_SEND_DATA fPackageCnt:%d fPackageNum:%d\r\n",global_DEVICE_STATUS.fPackageCnt,global_BOE_AES_DATA_INFO.fPackageNum);
						//PRINT("WriteAttrCB BOEINFO_SEND_DATA 0000000 out_len2=%d\r\n",out_len2);
						//hex_dump(out_decrypted, out_len2);
						ret = refreshScreenColor(out_decrypted,out_len2,global_BOE_AES_DATA_INFO.fIsZip);
						if(ret >= EPD_GetDisplayMaxBuf()){
							uint8_t finish_data1[] = {0};
							ble_send_boeInfo(connHandle,finish_data1,1,BOEINFO_NOTITY_SEND_RESULT_INFO-1, boeInfoBind_cccd) ;
						}
#ifdef ENABLE_INK_SCREEN_SPD1657_800X480_COLOR_6
						/*if(ret == -2){
							PRINT("send data error\r\n");
							uint8_t finish_data[] = {1};
							ble_send_boeInfo(connHandle,finish_data,1,BOEINFO_NOTITY_SEND_RESULT_INFO-1, boeInfoBind_cccd) ;
							global_DEVICE_STATUS.fWorked =Is_No;
							tmos_start_task(main_task_ID,EVENT_Low_Power ,500);
						}else if(ret == 0){
							uint8_t finish_data1[] = {0};
							ble_send_boeInfo(connHandle,finish_data1,1,BOEINFO_NOTITY_SEND_RESULT_INFO-1, boeInfoBind_cccd) ;
						}*/
#endif
						if(global_BOE_AES_DATA_INFO.fOpType == OP_TYPE_SAVE_IMG_A || global_BOE_AES_DATA_INFO.fOpType == OP_TYPE_SAVE_IMG_B
							|| global_BOE_AES_DATA_INFO.fOpType == OP_TYPE_SAVE_IMG_AB)
						{
							if(Save256DataToFlash(out_decrypted,out_len2,global_EXTERN_FLASH_INFO.fImageIndex,global_EXTERN_FLASH_INFO.fBlockNum) == 1){
								//global_EXTERN_FLASH_INFO.fBlockNum++;
							}
						}
					}
					
					global_DEVICE_STATUS.fPackageCnt++;
					//if(global_DEVICE_STATUS.fPackageCnt%400 == 0){ //timeout 400 package timeout reset 0
					//	global_DEVICE_STATUS.fSystemTimeOut = 0;
					//}
					//PRINT("WriteAttrCB BOEINFO_SEND_DATA img_data_len:%d\r\n",img_data_len);
					if(global_DEVICE_STATUS.fPackageCnt == global_BOE_AES_DATA_INFO.fPackageNum){
						PRINT("WriteAttrCB BOEINFO_SEND_DATA send finish\r\n");
						u8IsFirstPackage = BOE_DATA_FIRST;
						global_DEVICE_STATUS.fPackageCnt=0;
						global_DEVICE_STATUS.fDataSendSuccess = Is_Yes;
						rle_un_decrypt();
						//if(global_BOE_AES_DATA_INFO.fOpType != OP_TYPE_OTA_FILE){
							//All_Data_black_white_and_red_had_transferred_screen_A_or_B(global_DEVICE_STATUS.fScreenType);
						//}
						//uint8_t finish_data[] = {0};
						//ble_send_boeInfo(connHandle,finish_data,1,BOEINFO_NOTITY_SEND_RESULT_INFO-1, boeInfoBind_cccd) ;
						//ble_send_boeInfoError(connHandle,finish_data,1,BOEINFO_NOTITY_ERROR_INFO-1, boeInfoError_cccd);

						if(global_BOE_AES_DATA_INFO.fOpType == OP_TYPE_SAVE_IMG_A || global_BOE_AES_DATA_INFO.fOpType == OP_TYPE_SAVE_IMG_B
							|| global_BOE_AES_DATA_INFO.fOpType == OP_TYPE_SAVE_IMG_AB)
						{
							DeInitFlashDriver();
							global_EXTERN_FLASH_INFO.fBlockNum = EXTERN_FLASH_BLOCK_FIRST_ADDR;
						}
						//ble_send_boeInfo(connHandle,finish_data,1,BOEINFO_NOTITY_SEND_RESULT_INFO-1, boeInfoBind_cccd) ;
						//ble_send_boeInfo(connHandle,finish_data,1,BOEINFO_NOTITY_SEND_RESULT_INFO-1, boeInfoBind_cccd) ;
					}
				}
				free(b64_cipher_text);
			    free(deb64_orig_data);
			    free(out_decrypted);
			}
			break;
		case BOEINFO_NOTITY_SEND_END:
			PRINT("WriteAttrCB BOEINFO_NOTITY_SEND_END\r\n");
			//hex_dump(pValue, len);
			//uint8_t finish_data[] = {0};
			//ble_send_boeInfo(connHandle,finish_data,1,BOEINFO_NOTITY_SEND_RESULT_INFO-1, boeInfoBind_cccd) ;
			//tmos_set_event(main_task_ID,EVENT_Low_Power);
			break;
		case BOEINFO_OTA_INFO:
			global_DEVICE_STATUS.fisOtaed = 1;
			//PRINT("WriteAttrCB BOEINFO_OTA_INFO\r\n");
			//hex_dump(pValue, len);
			Rec_OTA_Data(pValue,len);
			break;
		case BOEINFO_SEND_PRE_SAVE:
			global_DEVICE_STATUS.fInitDriver = Is_Yes;
			global_BOE_AES_DATA_INFO.fOpType = OP_TYPE_SAVE_IMG_A;
			global_EXTERN_FLASH_INFO.fBlockNum = EXTERN_FLASH_BLOCK_FIRST_ADDR;
			global_DEVICE_STATUS.fRefreshType = DEVICE_OP_PRESAVE;
			global_DEVICE_STATUS.fIsNeedStandby = 1;
			PRINT("send pre save len:%d\r\n",len);			
			hex_dump(pValue, len);
			if(pValue[0] == 0x0b){ //pre save
				if(pValue[3] == 0x0a){
					global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_A;
				}
				if(pValue[3] == 0x0b){
					global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_B;
				}
				if(pValue[3] == 0x0c){
					global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_AB;
					global_BOE_AES_DATA_INFO.fOpType = OP_TYPE_SAVE_IMG_AB;
				}
				
				clearBoardcastData();
				InitFlashDriver();
				mDelayuS(50);
				if(preSaveDisplayColor(pValue[2], pValue[1], DEVICE_PRE_SAVE) == -1){
					PRINT("send pre save  error \r\n");	
					uint8_t finish_data1[] = {1};
					ble_send_boeInfo(connHandle,finish_data1,1,BOEINFO_SEND_PRE_SAVE, boeInfoPreSave_cccd) ;
					global_DEVICE_STATUS.fWorked =Is_No;
					tmos_start_task(main_task_ID,EVENT_Low_Power ,500);
				}
				mDelayuS(50);
				DeInitFlashDriver();
			}
			if(pValue[0] == 0x0c){ //clean
				if(len == 1){
					PRINT("send pre clean all\r\n");
					//global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_AB;
					initPicSave(SCREEN_CLEAN_ALL,0xff,0xff);
				}
				else if(len == 2){
					PRINT("send pre clean all room:%d\r\n",pValue[1]);
					initPicSave(SCREEN_CLEAN_ROOM,pValue[1],0xff);
				}else if(len == 3){
					PRINT("send pre clean all room:%d group:%d\r\n",pValue[1],pValue[2]);
					initPicSave(SCREEN_CLEAN_ROOM_AND_GROUP,pValue[1],pValue[2]);
				}
				global_DEVICE_STATUS.fWorked =Is_No;
				tmos_start_task(main_task_ID,EVENT_Low_Power ,500);
			}
			break;
		case BOEINFO_SEND_CLEAN:
			PRINT("send pre clean\r\n");	
			hex_dump(pValue, len);
			global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_AB;
			global_DEVICE_STATUS.fInitDriver = Is_Yes;
			global_EXTERN_FLASH_INFO.fBlockNum = EXTERN_FLASH_BLOCK_FIRST_ADDR;
			global_DEVICE_STATUS.fIsNeedStandby = 1;
			preSaveDisplayColor(0, 0, DEVICE_CLEAN_SCREEN);
			break;

		case BOEINFO_SWITCH_MODE:
			PRINT("WriteAttrCB BOEINFO_SWITCH_MODE\r\n");
			unsigned char workmode[WORKMODE_Len] = {0x01};
			hex_dump(pValue, len);
			if(pValue[0] == DEVICE_MODE_LOW){
				workmode[0] = DEVICE_MODE_LOW;
				P_scanModeRspData(DEVICE_MODE_LOW);
			}
			else{
				workmode[0] = DEVICE_MODE_HIGH;
				P_scanModeRspData(DEVICE_MODE_HIGH);
			}
			//PRINT("WriteAttrCB BOEINFO_SWITCH_MODE 0000000000000000000\r\n");
			Save_EEPROM_Flag(workmode,WORKMODE_Position,WORKMODE_Len);
			//global_DEVICE_STATUS.fRefreshPic = INK_SCREEN_REFRESH_OTHER;
			global_DEVICE_STATUS.fWorked=Is_No;
			tmos_start_task(main_task_ID,EVENT_Low_Power ,500);
			break;

        default:
			PRINT("other ..........\r\n");
            hex_dump(pValue, len);
		    //rle_test();
            break;
    }
    return ( status );
} 

static bStatus_t ble_notify_info( uint16 connHandle, attHandleValueNoti_t *pNoti, uint8 id, gattCharCfg_t *boeInfo_cccd)
{
    uint16 value = GATTServApp_ReadCharCfg( connHandle, boeInfo_cccd );
    // If notifications enabled
    if ( value & GATT_CLIENT_CFG_NOTIFY )
    {
        // Set the handle
        pNoti->handle = boeInfoAttrTbl[id].handle;

        // Send the Indication
        return GATT_Notification( connHandle, pNoti, FALSE);
    }
    return bleIncorrectMode;
}

bStatus_t ble_send_boeInfo( uint16_t connHandle, uint8_t *data, uint16_t length, uint8_t id,  gattCharCfg_t *boeInfo_cccd) {
    attHandleValueNoti_t notify_pram;
    uint8_t result = 0;
    notify_pram.len = length;
    notify_pram.pValue = GATT_bm_alloc( connHandle, ATT_HANDLE_VALUE_NOTI, length, NULL, 0 );
    if(notify_pram.pValue != NULL){
        tmos_memcpy( notify_pram.pValue, data, length );
        result = ble_notify_info( connHandle, &notify_pram, id, boeInfo_cccd);
        if(result != 0 ) {
            GATT_bm_free( (gattMsg_t *)&notify_pram, ATT_HANDLE_VALUE_NOTI );
        }
        return result;
    }
    return bleNoResources;
}

//boardcast data
static uint8 scanRspData[] = {
    // complete name
 	21, 
    GAP_ADTYPE_LOCAL_NAME_COMPLETE,

#ifdef ENABLE_INK_SCREEN_UC8279_800X480_COLOR_3
	')','(','#','2','F','7','8','2','D','4','A','A','D','F','A','S','1','F','@','!',
#endif
#ifdef ENABLE_INK_SCREEN_SPD1657_800X480_COLOR_6
	')','(','"','2','F','7','8','2','D','4','A','A','D','F','A','S','1','F','@','!',
#endif
};

static uint8_t advertData[] = {
    0x02, // length of this data
    GAP_ADTYPE_FLAGS,
    GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,

    // service UUID, to notify central devices what services are included
    // in this peripheral
    17,                  // length of this data
    GAP_ADTYPE_128BIT_MORE, // some of the UUID's, but not all
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x00,0xff,0x12,0x6b
};

void intBoardCastData()
{
	unsigned char szVerInfo[VERINFO_Len];
	char char1;
    char char2;
	unsigned char bond1[2];
	Get_EEPROM_Flag(bond1,WORKMODE_Position,WORKMODE_Len);
	
	Print_I3("@@intBoardCastData mac=%x %x %x %x %x %x \n",Mac[0],Mac[1],Mac[2],Mac[3],Mac[4],Mac[5]);
	scanRspData[5] = hexToChar((Mac[5] & 0xf0) >> 4);
	scanRspData[6] = hexToChar(Mac[5] & 0x0f);
	scanRspData[7] = hexToChar((Mac[4] & 0xf0) >> 4);
	scanRspData[8] = hexToChar(Mac[4] & 0x0f);
	scanRspData[9] = hexToChar((Mac[3] & 0xf0) >> 4);
	scanRspData[10] = hexToChar(Mac[3] & 0x0f);
	scanRspData[11] = hexToChar((Mac[2] & 0xf0) >> 4);
	scanRspData[12] = hexToChar(Mac[2] & 0x0f);
	scanRspData[13] = hexToChar((Mac[1] & 0xf0) >> 4);
	scanRspData[14] = hexToChar(Mac[1] & 0x0f);
	scanRspData[15] = hexToChar((Mac[0] & 0xf0) >> 4);
	scanRspData[16] = hexToChar(Mac[0] & 0x0f);
	scanRspData[17] = 33 + (global_DEVICE_STATUS.fAdcValue / 2);

	convert_number(VER, &char1, &char2);
	Print_I3("Peripheral_Init %d -> '%c', '%c'\n", VER, char1, char2);
	scanRspData[18] = char1;
	scanRspData[19] = char2;

	scanRspData[20] = getAdcAndWorkMode(global_DEVICE_STATUS.fIsCharg,bond1[0]);
	
	extern int getPicSaveNum();
	printf("P_scanPreNumRspData 111 getPicSaveNum :%d\r\n",getPicSaveNum());
	scanRspData[21] = '!' + getPicSaveNum();
	
	Get_EEPROM_Flag(szVerInfo,VERINFO_Position,VERINFO_Len);
	//hex_dump(szVerInfo, VERINFO_Len);
	if((szVerInfo[0] != char1) || (szVerInfo[1] != char2)){
		Print_I3("@@@@@@@@@@@@@@@@@ diff ver need  factory \n");
		extern void initPicSave(int type, int room, int group);
		initPicSave(SCREEN_CLEAN_ALL,0xff,0xff);
		szVerInfo[0] = char1;
		szVerInfo[1] = char2;
		Save_EEPROM_Flag(szVerInfo,VERINFO_Position,VERINFO_Len);	
	}

	// Setup the GAP Peripheral Role Profile
    {
        uint8_t  initial_advertising_enable = TRUE;                                                 //开启广播使能
        // Set the GAP Role Parameters                                                              //设置GAP层参数
        GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &initial_advertising_enable);
        GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);
        GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData), advertData);
    }
}

void  P_scanBondRspData(UINT8 bond_status)
{
	printf("P_scanRspData 111 bond_status :%d\r\n",bond_status);
	if(bond_status == DEVICE_BONDED)
		scanRspData[3] = '(';
	else
		scanRspData[3] = ')';
	
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);// 用这个会
}

void  P_scanModeRspData(UINT8 work_status)
{
	printf("P_scanRspData 111 work_status :%d\r\n",work_status);
	/*if(work_status == DEVICE_MODE_HIGH)
		scanRspData[20] = 'P';
	else
		scanRspData[20] = '@';*/
	scanRspData[20] = getAdcAndWorkMode(global_DEVICE_STATUS.fIsCharg,work_status);
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);// 用这个会
}

void  P_scanAdcRspData(UINT8 work_status)
{
	//printf("P_scanRspData 111 fAdcValue :%d\r\n",global_DEVICE_STATUS.fAdcValue);
	scanRspData[17] = 33 + (global_DEVICE_STATUS.fAdcValue / 2);//pre num  start '!'
	
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);// 用这个会
}

void  P_scanPreNumRspData(UINT8 pre_num)
{
	printf("P_scanPreNumRspData 111 pre_num :%d\r\n",pre_num);
	scanRspData[21] = '!' + pre_num;   //pre num  start '!'
	
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);// 用这个会
}

void  P_scanIsChargeRspData()
{
	printf("P_scanIsChargeRspData 00000000000000000000000\r\n");
	unsigned char mode1[2];
	Get_EEPROM_Flag(mode1,WORKMODE_Position,WORKMODE_Len);

	scanRspData[20] = getAdcAndWorkMode(global_DEVICE_STATUS.fIsCharg, mode1[0]);
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);
}

static uint8_t firstAdvertisementData[BOARDCAST_Len];  // 存储首次广播数据的缓冲区
static uint8_t firstAdvertisementLen[BOARDLEN_Len];

void receiveGapBroadcastAdData(UINT8 *adData, UINT32 dataLen)
{
	if((adData[0] == BROADCAST_COMMAND_PRESAVE && adData[1] == BROADCAST_PREFIX_CHAR2 && adData[2] == BROADCAST_PREFIX_CHAR3 && adData[3] == BROADCAST_PREFIX_CHAR4) || 
		(adData[0] == BROADCAST_COMMAND_CLEAN_ALL && adData[1] == BROADCAST_PREFIX_CHAR2 && adData[2] == BROADCAST_PREFIX_CHAR3 && adData[3] == BROADCAST_PREFIX_CHAR4) ||
		(adData[0] == BROADCAST_COMMAND_CLEAN_ROOM && adData[1] == BROADCAST_PREFIX_CHAR2 && adData[2] == BROADCAST_PREFIX_CHAR3 && adData[3] == BROADCAST_PREFIX_CHAR4) ||
		(adData[0] == BROADCAST_COMMAND_CLEAN_ROOM_AND_GROUP && adData[1] == BROADCAST_PREFIX_CHAR2 && adData[2] == BROADCAST_PREFIX_CHAR3 && adData[3] == BROADCAST_PREFIX_CHAR4)){
		Print_I3("	boardcast data: %.*s\n", dataLen, adData);
		// 打印设备地址和RSSI
		/*Print_I3("find device: ");
		for (i = 0; i < B_ADDR_LEN; i++) {
			Print_I3("%02X", pEvent->deviceInfo.addr[i]);
			if (i < B_ADDR_LEN - 1) PRINT(":");
		}
		Print_I3(", RSSI: %d dBm\n", pEvent->deviceInfo.rssi);*/
		if(global_DEVICE_STATUS.fWorked == Is_No){
			if((adData[1]==BROADCAST_PREFIX_CHAR2) && (adData[2]==BROADCAST_PREFIX_CHAR3) && (adData[3]==BROADCAST_PREFIX_CHAR4)){
				Get_EEPROM_Flag(firstAdvertisementData,BOARDCAST_Position,BOARDCAST_Len);
				Get_EEPROM_Flag(firstAdvertisementLen,BOARDLEN_Position,BOARDLEN_Len);
				if((firstAdvertisementLen[0] != dataLen) || (memcmp(adData, firstAdvertisementData, dataLen) != 0)){
					memcpy(firstAdvertisementData, adData, dataLen);
					firstAdvertisementLen[0] = dataLen;
					Save_EEPROM_Flag(firstAdvertisementData,BOARDCAST_Position,BOARDCAST_Len);
					Save_EEPROM_Flag(firstAdvertisementLen,BOARDLEN_Position,BOARDLEN_Len);
					Print_I3("	boardcast data the diff @@@@@@@\n");
					if(adData[0] == BROADCAST_COMMAND_PRESAVE){
						global_DEVICE_STATUS.fBoardCastType = SCREEN_PRESAVE_OP;
						if(adData[6] == 'A'){
							global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_A;
						}else if(adData[6] == 'B'){
							global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_B;
						}else if(adData[6] == 'C'){
							global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_AB;
						}
					}else if(adData[0] == BROADCAST_COMMAND_CLEAN_ALL){
						global_DEVICE_STATUS.fBoardCastType = SCREEN_CLEAN_ALL;
					}else if(adData[0] == BROADCAST_COMMAND_CLEAN_ROOM){
						global_DEVICE_STATUS.fBoardCastType = SCREEN_CLEAN_ROOM;
					}else if(adData[0] == BROADCAST_COMMAND_CLEAN_ROOM_AND_GROUP){
						global_DEVICE_STATUS.fBoardCastType = SCREEN_CLEAN_ROOM_AND_GROUP;
					}
	
					global_DEVICE_STATUS.fBoardCastRoom = adData[4] - 33;
					global_DEVICE_STATUS.fBoardCastGroup= adData[5] - 33;
	
					//Print_I3("boardcast info type:%d room:%d group:%d ab:%d",global_DEVICE_STATUS.fBoardCastType,global_BOE_AES_DATA_INFO.fRoomNum,
					//	global_BOE_AES_DATA_INFO.fGroupNum,global_DEVICE_STATUS.fScreenType);
					tmos_start_task(main_task_ID,EVENT_Boardcast_Op ,200);
				}else{
					Print_I3("	boardcast data the same @@@@@@@\n");
				}
			}
		}
	}
}

void processGapBroadcastAdData(){
	Print_I3("boardcast info type:%d room:%d group:%d ab:%d",global_DEVICE_STATUS.fBoardCastType,global_DEVICE_STATUS.fBoardCastRoom,
		global_DEVICE_STATUS.fBoardCastGroup,global_DEVICE_STATUS.fScreenType);

	if(global_DEVICE_STATUS.fBoardCastType == SCREEN_PRESAVE_OP){
		global_DEVICE_STATUS.fInitDriver = Is_Yes;
		//global_BOE_AES_DATA_INFO.fOpType = OP_TYPE_SAVE_IMG_A;
		global_EXTERN_FLASH_INFO.fBlockNum = EXTERN_FLASH_BLOCK_FIRST_ADDR;
		global_DEVICE_STATUS.fRefreshType = DEVICE_OP_PRESAVE;
		global_DEVICE_STATUS.fIsNeedStandby = 1;
		InitFlashDriver();
		mDelayuS(50);

		if(preSaveDisplayColor(global_DEVICE_STATUS.fBoardCastGroup, global_DEVICE_STATUS.fBoardCastRoom, DEVICE_PRE_SAVE) == -1){
			PRINT("send pre save  error \r\n");	
			global_DEVICE_STATUS.fWorked =Is_No;
		}
		mDelayuS(50);
		DeInitFlashDriver();
	}

	if(global_DEVICE_STATUS.fBoardCastType == SCREEN_CLEAN_ALL){
		initPicSave(SCREEN_CLEAN_ALL,0xff,0xff);
		global_DEVICE_STATUS.fWorked =Is_No;
	}

	if(global_DEVICE_STATUS.fBoardCastType == SCREEN_CLEAN_ROOM){
		initPicSave(SCREEN_CLEAN_ROOM,global_DEVICE_STATUS.fBoardCastRoom,0xff);
		global_DEVICE_STATUS.fWorked =Is_No;
	}

	if(global_DEVICE_STATUS.fBoardCastType == SCREEN_CLEAN_ROOM_AND_GROUP){
		initPicSave(SCREEN_CLEAN_ROOM_AND_GROUP,global_DEVICE_STATUS.fBoardCastRoom,global_DEVICE_STATUS.fBoardCastGroup);
		global_DEVICE_STATUS.fWorked =Is_No;
	}

	tmos_start_task(main_task_ID,EVENT_Low_Power ,500);
}
#endif
/*********************************************************************
*********************************************************************/

