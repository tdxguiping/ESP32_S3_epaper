/*********************************************************************
 * INCLUDES
 */
#include <stdlib.h>
#include "app_cfg.h"

#ifdef ENABLE_SOFTWARE_TO_XT
#include "xtinfoservice.h"
#include "rledecode.h"
#include "commoninfo.h"
#include "aes_util.h"
#include "base64.h"
#include "flash_api.h"

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * CONSTANTS
 */
/*********************************************************************
 * TYPEDEFS
 */

//#define FALSH_TEST_DEBUG
/*********************************************************************
 * GLOBAL VARIABLES
 */
// ble_uart GATT Profile Service UUID
CONST uint8 ble_uart_ServiceUUID[ATT_UUID_SIZE] = {0x01,0x10,0x2E,0xC7,0x8a,0x0E,  0x73,0x90,  0xE1,0x11,  0xC2,0x08,  0x60,0x27,0x00,0x00};		/*!< Service UUID */
// Characteristic rx uuid
CONST uint8 ble_uart_RxCharUUID[ATT_UUID_SIZE] = {0x01,0x00,0x2E,0xC7,0x8a,0x0E,  0x73,0x90,  0xE1,0x11,  0xC2,0x08,  0x60,0x27,0x00,0x00}; 	  /*!< Characteristic value UUID */
// Characteristic tx uuid
CONST uint8 ble_uart_TxCharUUID[ATT_UUID_SIZE] = {0x02,0x00,0x2E,0xC7,0x8a,0x0E,  0x73,0x90,  0xE1,0x11,  0xC2,0x08,  0x60,0x27,0x00,0x00}; 	  /*!< Characteristic value UUID */

/*********************************************************************
 * EXTERNAL VARIABLES
 */
/*********************************************************************
 * EXTERNAL FUNCTIONS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */
static ble_uart_ProfileChangeCB_t ble_uart_AppCBs = NULL;

/*********************************************************************
 * Profile Attributes - variables
 */

// boe Information Service attribute
static const gattAttrType_t XtInfoSer = {ATT_UUID_SIZE, ble_uart_ServiceUUID};

// Profile Characteristic 1 Properties
//static uint8 ble_uart_RxCharProps = GATT_PROP_WRITE_NO_RSP| GATT_PROP_WRITE;
static uint8 ble_uart_RxCharProps = GATT_PROP_WRITE_NO_RSP;

// Characteristic 1 Value
static uint8 ble_uart_RxCharValue[BLE_UART_RX_BUFF_SIZE];
//static uint8 ble_uart_RxCharValue[1];

// Profile Characteristic 2 Properties
//static uint8 ble_uart_TxCharProps = GATT_PROP_NOTIFY| GATT_PROP_INDICATE;
static uint8 ble_uart_TxCharProps = GATT_PROP_NOTIFY;

// Characteristic 2 Value
static uint8 ble_uart_TxCharValue = 0;

// Simple Profile Characteristic 2 User Description
static gattCharCfg_t ble_uart_TxCCCD[PERIPHERAL_MAX_CONNECTION];


/*********************************************************************
 * Profile Attributes - Table
 */
static gattAttribute_t ble_uart_ProfileAttrTbl[] = {
	// Simple Profile Service
	{
		{ATT_BT_UUID_SIZE, primaryServiceUUID}, /* type */
		GATT_PERMIT_READ,						/* permissions */
		0,										/* handle */
		(uint8 *)&XtInfoSer						/* pValue */
	},

	// Characteristic 1 Declaration
	{
		{ATT_BT_UUID_SIZE, characterUUID},
		GATT_PERMIT_READ,
		0,
		&ble_uart_RxCharProps},

	// Characteristic Value 1
	{
		{ATT_UUID_SIZE, ble_uart_RxCharUUID},
		GATT_PERMIT_WRITE,
		0,
		&ble_uart_RxCharValue[0]},

	// Characteristic 2 Declaration
	{
		{ATT_BT_UUID_SIZE, characterUUID},
		GATT_PERMIT_READ,
		0,
		&ble_uart_TxCharProps},

	// Characteristic Value 2
	{
		{ATT_UUID_SIZE, ble_uart_TxCharUUID},
		0,
		0,
		(uint8 *)&ble_uart_TxCharValue},

	// Characteristic 2 User Description
	{
		{ATT_BT_UUID_SIZE, clientCharCfgUUID},
		GATT_PERMIT_READ | GATT_PERMIT_WRITE,
		0,
		(uint8 *)ble_uart_TxCCCD},
};

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static bStatus_t xtInfo_ReadAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
                                    uint8_t *pValue, uint16_t *pLen, uint16_t offset, uint16_t maxLen, uint8_t method);
static bStatus_t xtInfo_WriteAttrCB( uint16 connHandle, gattAttribute_t *pAttr,
                                      uint8 *pValue, uint16 len, uint16 offset,uint8 method );
bStatus_t ble_send_xtInfo( uint16_t connHandle, uint8_t *data, uint16_t length, uint8_t id,  gattCharCfg_t *xtInfo_cccd);
/*********************************************************************
 * PROFILE CALLBACKS
 */
// Boe Info Service Callbacks
gattServiceCBs_t xtInfoCBs = {
    xtInfo_ReadAttrCB, // Read callback function pointer
    xtInfo_WriteAttrCB,               // Write callback function pointer
    NULL                // Authorization callback function pointer
};

/*********************************************************************
 * NETWORK LAYER CALLBACKS
 */

/*********************************************************************
 * PUBLIC FUNCTIONS
 */
static void XtInfo_HandleConnStatusCB ( uint16 connHandle, uint8 changeType )
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
            GATTServApp_InitCharCfg( INVALID_CONNHANDLE, ble_uart_TxCCCD );
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
bStatus_t CustomerInfo_AddService(ble_uart_ProfileChangeCB_t cb)
{
    uint8_t ret;

    GATTServApp_InitCharCfg( INVALID_CONNHANDLE, ble_uart_TxCCCD );

    // Register with Link DB to receive link status change callback
    linkDB_Register( XtInfo_HandleConnStatusCB  );

    // Register GATT attribute list and CBs with GATT Server App
    ret =  GATTServApp_RegisterService(ble_uart_ProfileAttrTbl,
                                       GATT_NUM_ATTRS(ble_uart_ProfileAttrTbl),
                                       GATT_MAX_ENCRYPT_KEY_SIZE,
                                       &xtInfoCBs);
    PRINT("ADD xt service:%02x\r\n",ret);
	ble_uart_AppCBs = cb;
    return ret;
}

void NotifyCallBack(uint16_t connHandle,UINT8 *data,UINT8 dlen) {
	PRINT("NotifyCallBack aaaaaaaaaaaaaa dlen=%d\r\n",dlen);
	ble_send_boeInfo(connHandle, data, dlen);
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
static bStatus_t xtInfo_ReadAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
                                    uint8_t *pValue, uint16_t *pLen, uint16_t offset, uint16_t maxLen, uint8_t method)
{
	bStatus_t status = SUCCESS;
	PRINT("ReadAttrCB\n");

	// Make sure it's not a blob operation (no attributes in the profile are long)
	if(pAttr->type.len == ATT_BT_UUID_SIZE)
	{
		// 16-bit UUID
		uint16 uuid = BUILD_UINT16(pAttr->type.uuid[0], pAttr->type.uuid[1]);
		if(uuid == GATT_CLIENT_CHAR_CFG_UUID)
		{
			*pLen = 2;
			tmos_memcpy(pValue, pAttr->pValue, 2);
		}
	}
	else
	{
		if(tmos_memcmp(pAttr->type.uuid, ble_uart_TxCharUUID, 16))
		{
			*pLen = 1;
			pValue[0] = '1';
		}
		else if(tmos_memcmp(pAttr->type.uuid, ble_uart_RxCharUUID, 16))
		{
			PRINT("read tx char\n");
		}
	}

	return (status);
}

static bStatus_t xtInfo_WriteAttrCB( uint16 connHandle, gattAttribute_t *pAttr,
                                      uint8 *pValue, uint16 len, uint16 offset,uint8 method )
{
    bStatus_t status = SUCCESS;
    //uint8 notifyApp = 0xFF;
    // If attribute permissions require authorization to write, return error
    if(gattPermitAuthorWrite(pAttr->permissions))
    {
        // Insufficient authorization
        return (ATT_ERR_INSUFFICIENT_AUTHOR);
    }

	global_DEVICE_STATUS.fisHaveData= Is_Yes;
	uint16_t gatt_handle = pAttr->handle - ble_uart_ProfileAttrTbl[0].handle;
	//PRINT("write gatt handle:%08x\r\n",gatt_handle);
    switch (gatt_handle) {
		case XTINFO_RECEIVE_DATA:{
<<<<<<< HEAD
			PRINT("XTINFO_RECEIVE_DATA 0000000000000000\r\n");
			hex_dump(pValue,len);
=======
			//PRINT("XTINFO_RECEIVE_DATA 0000000000000000\r\n");
			//hex_dump(pValue,len);
>>>>>>> 3b39af9a7212b117e89bd93e5b39565b889dd7fa
            if(ble_uart_AppCBs)
            {
                ble_uart_evt_t evt;
                evt.type = BLE_UART_EVT_BLE_DATA_RECIEVED;
                evt.data.length = (uint16_t)len;
                evt.data.p_data = pValue;
                ble_uart_AppCBs(connHandle, &evt);
            }
		}
		break;
		case XTINFO_NOTITY_ENABLE:{
			PRINT("XTINFO_NOTITY_ENABLE 0000000000000000\r\n");
			status = GATTServApp_ProcessCCCWriteReq( connHandle, pAttr, pValue, len,
                         offset, GATT_CLIENT_CFG_NOTIFY );

			setXtNofityCallBack(connHandle,NotifyCallBack);
		}
		break;
    }

#if 0
    if(pAttr->type.len == ATT_BT_UUID_SIZE)
    {
        // 16-bit UUID
        uint16 uuid = BUILD_UINT16(pAttr->type.uuid[0], pAttr->type.uuid[1]);
        if(uuid == GATT_CLIENT_CHAR_CFG_UUID)
        {
            status = GATTServApp_ProcessCCCWriteReq(connHandle, pAttr, pValue, len,
                                                    offset, GATT_CLIENT_CFG_NOTIFY);
            if(status == SUCCESS && ble_uart_AppCBs)
            {
                uint16         charCfg = BUILD_UINT16(pValue[0], pValue[1]);
                ble_uart_evt_t evt;

                //PRINT("CCCD set: [%d]\n\r", charCfg);
                evt.type = (charCfg == GATT_CFG_NO_OPERATION) ? BLE_UART_EVT_TX_NOTI_DISABLED : BLE_UART_EVT_TX_NOTI_ENABLED;
                ble_uart_AppCBs(connHandle, &evt);
            }
        }
    }
    else
    {
        // 128-bit UUID
        if(pAttr->handle == ble_uart_ProfileAttrTbl[RAWPASS_RX_VALUE_HANDLE].handle)
        {
        	PRINT("xtInfo_WriteAttrCB RAWPASS_RX_VALUE_HANDLE len=%d\n",len);
			hex_dump(pValue,len);
            if(ble_uart_AppCBs)
            {
                ble_uart_evt_t evt;
                evt.type = BLE_UART_EVT_BLE_DATA_RECIEVED;
                evt.data.length = (uint16_t)len;
                evt.data.p_data = pValue;
                ble_uart_AppCBs(connHandle, &evt);
            }
        }
    }
#endif
	return (status);
}

static bStatus_t ble_notify_info( uint16 connHandle, attHandleValueNoti_t *pNoti)
{
    uint16 value = GATTServApp_ReadCharCfg( connHandle, ble_uart_TxCCCD );
    // If notifications enabled
    if ( value & GATT_CLIENT_CFG_NOTIFY )
    {
        // Set the handle
        pNoti->handle = ble_uart_ProfileAttrTbl[XTINFO_TX_VALUE_HANDLE].handle;

        // Send the Indication
        return GATT_Notification( connHandle, pNoti, FALSE);
    }
    return bleIncorrectMode;
}

bStatus_t ble_send_boeInfo( uint16_t connHandle, uint8_t *data, uint16_t length) {
    attHandleValueNoti_t notify_pram;
    uint8_t result = 0;
    notify_pram.len = length;
    notify_pram.pValue = GATT_bm_alloc( connHandle, ATT_HANDLE_VALUE_NOTI, length, NULL, 0 );
    if(notify_pram.pValue != NULL){
        tmos_memcpy( notify_pram.pValue, data, length );
        result = ble_notify_info( connHandle, &notify_pram);
        if(result != 0 ) {
            GATT_bm_free( (gattMsg_t *)&notify_pram, ATT_HANDLE_VALUE_NOTI );
        }
        return result;
    }
    return bleNoResources;
}

static uint8 scanRspData[] = {
    // complete name
    15, // length of this data
    GAP_ADTYPE_LOCAL_NAME_COMPLETE,
    'c', 'h', '5', '8', '3', '_', 'b', 'l', 'e', '_', 'u', 'a', 'r', 't',
    // connection interval range
    0x05, // length of this data
    GAP_ADTYPE_SLAVE_CONN_INTERVAL_RANGE,
    LO_UINT16(DEFAULT_DESIRED_MIN_CONN_INTERVAL), // 100ms
    HI_UINT16(DEFAULT_DESIRED_MIN_CONN_INTERVAL),
    LO_UINT16(DEFAULT_DESIRED_MAX_CONN_INTERVAL), // 1s
    HI_UINT16(DEFAULT_DESIRED_MAX_CONN_INTERVAL),

    // Tx power level
    0x02, // length of this data
    GAP_ADTYPE_POWER_LEVEL,
    0 // 0dBm
};

static UINT8 advertData[11] = {
    10,//12, //17, // 实际 是 14
    GAP_ADTYPE_MANUFACTURER_SPECIFIC, // manufacturer specific advertisement data type
    1,2,3,4,5,6,7,8,9,
};

void intBoardCastData()
{
	//1.关 键 字: 蓝牙主机的过滤条件，也可以作为蓝牙系列标签唯一标识
	scanRspData[27-25] = 'X';
	advertData[27-25] = 'X';
	scanRspData[28-25] = 'R';
	advertData[28-25] = 'R';
	
	//2.上报类型: 0x03：漫游上报,0x04:标签上报（带唤醒） 
	//			  0xFE：可连接广播 0xFD：标签上报 0xFC:重连广播 
	scanRspData[29-25] = 0xFD;
	advertData[29-25] = 0xFD;
		
	//3.硬件版本: 产品的硬件版本,高4位表示主版本号(0-F)，低4位表示次版本号(0-F)
	scanRspData[30-25] = 0x01;
	advertData[30-25] = 0x01;
		
	//4.软件版本: 左高右低，高字节高4位主版本号(0-F)，低4位次版本号(0-F)，低字节位(0-255)。 			 例如：0x1002，表示版本1.0.2
	scanRspData[31-25] = 0x00;
	scanRspData[32-25] = 0x20;
	
	advertData[31-25] = 0x00;
	advertData[32-25] = 0x20;
		
	//5.设备编码：设备的编码ID，作为查询产品信息的标识
	scanRspData[33-25] = 0;
	advertData[33-25] = 0;
	
	scanRspData[34-25] = 0x20;
	advertData[34-25] = 0x20;
		
	//6.电	  量：设备电池电量百分比，0~100%
	scanRspData[35-25] =30;
	advertData[35-25] =30;	
	Print_I3("@@@@@@@@@@@@@@@@@@@@@@@@@@Peripheral_Init version=%d \n",scanRspData[36-12]);

	// Setup the GAP Peripheral Role Profile
    {
        uint8_t  initial_advertising_enable = TRUE;                                                 //开启广播使能
        // Set the GAP Role Parameters                                                              //设置GAP层参数
        GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &initial_advertising_enable);
        GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);
        GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData), advertData);
    }
}
#endif
/*********************************************************************
*********************************************************************/

