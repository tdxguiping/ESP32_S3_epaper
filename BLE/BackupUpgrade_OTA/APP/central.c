/********************************** (C) COPYRIGHT *******************************
 * File Name          : central.c
 * Author             : WCH
 * Version            : V1.1
 * Date               : 2019/11/05
 * Description        : 主机例程，主动扫描周围设备，连接至给定的从机设备地址，
 *                      寻找自定义服务及特征，执行读写命令，需与从机例程配合使用,
 *                      并将从机设备地址修改为该例程目标地址，默认为(84:C2:E4:03:02:02)
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "CONFIG.h"
#include "gattprofile.h"
#include "peripheral.h"
#include "central.h"
#include "app_cfg.h"
#include "commoninfo.h"
#include "boardcast_data_op.h"

#ifdef ENABLE_SOFTWARE_TO_TDX
#include "tdxinfoservice.h"
#endif
#ifdef ENABLE_SOFTWARE_TO_BOE
#include "boenfoservice.h"
#endif
/*********************************************************************
 * MACROS
 */

// Length of bd addr as a string
#define B_ADDR_STR_LEN                      15

/*********************************************************************
 * CONSTANTS
 */
// Maximum number of scan responses
#define DEFAULT_MAX_SCAN_RES                10

// Scan duration in 0.625ms
#define DEFAULT_SCAN_DURATION               2400

// Connection min interval in 1.25ms
#define DEFAULT_MIN_CONNECTION_INTERVAL     20

// Connection max interval in 1.25ms
#define DEFAULT_MAX_CONNECTION_INTERVAL     100

// Connection supervision timeout in 10ms
#define DEFAULT_CONNECTION_TIMEOUT          100

// Discovey mode (limited, general, all)
#define DEFAULT_DISCOVERY_MODE              DEVDISC_MODE_ALL

// TRUE to use active scan
#define DEFAULT_DISCOVERY_ACTIVE_SCAN       TRUE

// TRUE to use white list during discovery
#define DEFAULT_DISCOVERY_WHITE_LIST        FALSE

// TRUE to use high scan duty cycle when creating link
#define DEFAULT_LINK_HIGH_DUTY_CYCLE        FALSE

// TRUE to use white list when creating link
#define DEFAULT_LINK_WHITE_LIST             FALSE

// Default read RSSI period in 0.625ms
#define DEFAULT_RSSI_PERIOD                 2400

// Minimum connection interval (units of 1.25ms)
#define DEFAULT_UPDATE_MIN_CONN_INTERVAL    20

// Maximum connection interval (units of 1.25ms)
#define DEFAULT_UPDATE_MAX_CONN_INTERVAL    100

// Slave latency to use parameter update
#define DEFAULT_UPDATE_SLAVE_LATENCY        0

// Supervision timeout value (units of 10ms)
#define DEFAULT_UPDATE_CONN_TIMEOUT         600

// Default passcode
#define DEFAULT_PASSCODE                    0

// Default GAP pairing mode
#define DEFAULT_PAIRING_MODE                GAPBOND_PAIRING_MODE_WAIT_FOR_REQ

// Default MITM mode (TRUE to require passcode or OOB when pairing)
#define DEFAULT_MITM_MODE                   TRUE

// Default bonding mode, TRUE to bond, max bonding 6 devices
#define DEFAULT_BONDING_MODE                TRUE

// Default GAP bonding I/O capabilities
#define DEFAULT_IO_CAPABILITIES             GAPBOND_IO_CAP_NO_INPUT_NO_OUTPUT

// Default service discovery timer delay in 0.625ms
#define DEFAULT_SVC_DISCOVERY_DELAY         1600

// Default parameter update delay in 0.625ms
#define DEFAULT_PARAM_UPDATE_DELAY          3200

// Default read or write timer delay in 0.625ms
#define DEFAULT_READ_OR_WRITE_DELAY         1600

// Establish link timeout in 0.625ms
#define ESTABLISH_LINK_TIMEOUT              3200

// Application states
enum
{
    BLE_STATE_IDLE,
    BLE_STATE_CONNECTING,
    BLE_STATE_CONNECTED,
    BLE_STATE_DISCONNECTING
};

// Discovery states
enum
{
    BLE_DISC_STATE_IDLE, // Idle
    BLE_DISC_STATE_SVC,  // Service discovery
    BLE_DISC_STATE_CHAR  // Characteristic discovery
};
/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL FUNCTIONS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */

// Task ID for internal task/event processing
static uint8_t centralTaskId;

// GAP GATT Attributes
static const uint8_t centralDeviceName[GAP_DEVICE_NAME_LEN] = "Central";

// Number of scan results
static uint8_t centralScanRes;

// Scan result list
static gapDevRec_t centralDevList[DEFAULT_MAX_SCAN_RES];

// Connection handle of current connection
static uint16_t centralConnHandle = GAP_CONNHANDLE_INIT;

// Application state
static uint8_t centralState = BLE_STATE_IDLE;

// Discovery state
static uint8_t centralDiscState = BLE_DISC_STATE_IDLE;

// Discovered service start and end handle
static uint16_t centralSvcStartHdl = 0;
static uint16_t centralSvcEndHdl = 0;

// Discovered characteristic handle
static uint16_t centralCharHdl = 0;

// Value to write
static uint8_t centralCharVal = 0x5A;

// Value read/write toggle
static uint8_t centralDoWrite = TRUE;

// GATT read/write procedure state
static uint8_t centralProcedureInProgress = FALSE;
/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void centralProcessGATTMsg(gattMsgEvent_t *pMsg);
static void centralRssiCB(uint16_t connHandle, int8_t rssi);
static void centralEventCB(gapRoleEvent_t *pEvent);
static void centralHciMTUChangeCB(uint16_t connHandle, uint16_t maxTxOctets, uint16_t maxRxOctets);
static void centralPasscodeCB(uint8_t *deviceAddr, uint16_t connectionHandle,
                              uint8_t uiInputs, uint8_t uiOutputs);
static void centralPairStateCB(uint16_t connHandle, uint8_t state, uint8_t status);
static void central_ProcessTMOSMsg(tmos_event_hdr_t *pMsg);
static void centralGATTDiscoveryEvent(gattMsgEvent_t *pMsg);
static void centralStartDiscovery(void);
static void centralAddDeviceInfo(uint8_t *pAddr, uint8_t addrType);
/*********************************************************************
 * PROFILE CALLBACKS
 */

// GAP Role Callbacks
static gapCentralRoleCB_t centralRoleCB = {
    centralRssiCB,        // RSSI callback
    centralEventCB,       // Event callback
    centralHciMTUChangeCB // MTU change callback
};

// Bond Manager Callbacks
static gapBondCBs_t centralBondCB = {
    centralPasscodeCB,
    centralPairStateCB
};

/*********************************************************************
 * PUBLIC FUNCTIONS
 */

/*********************************************************************
 * @fn      Central_Init
 *
 * @brief   Initialization function for the Central App Task.
 *          This is called during initialization and should contain
 *          any application specific initialization (ie. hardware
 *          initialization/setup, table initialization, power up
 *          notification).
 *
 * @param   task_id - the ID assigned by TMOS.  This ID should be
 *                    used to send messages and set timers.
 *
 * @return  none
 */
void Central_Init()
{
    centralTaskId = TMOS_ProcessEventRegister(Central_ProcessEvent);

    // Setup GAP
    GAP_SetParamValue(TGAP_DISC_SCAN, DEFAULT_SCAN_DURATION);
    GAP_SetParamValue(TGAP_CONN_EST_INT_MIN, DEFAULT_MIN_CONNECTION_INTERVAL);
    GAP_SetParamValue(TGAP_CONN_EST_INT_MAX, DEFAULT_MAX_CONNECTION_INTERVAL);
    GAP_SetParamValue(TGAP_CONN_EST_SUPERV_TIMEOUT, DEFAULT_CONNECTION_TIMEOUT);

    GGS_SetParameter(GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN, (uint8_t *)centralDeviceName);

    // Setup the GAP Bond Manager
    {
        uint32_t passkey = DEFAULT_PASSCODE;
        uint8_t  pairMode = DEFAULT_PAIRING_MODE;
        uint8_t  mitm = DEFAULT_MITM_MODE;
        uint8_t  ioCap = DEFAULT_IO_CAPABILITIES;
        uint8_t  bonding = DEFAULT_BONDING_MODE;

        GAPBondMgr_SetParameter(GAPBOND_CENT_DEFAULT_PASSCODE, sizeof(uint32_t), &passkey);
        GAPBondMgr_SetParameter(GAPBOND_CENT_PAIRING_MODE, sizeof(uint8_t), &pairMode);
        GAPBondMgr_SetParameter(GAPBOND_CENT_MITM_PROTECTION, sizeof(uint8_t), &mitm);
        GAPBondMgr_SetParameter(GAPBOND_CENT_IO_CAPABILITIES, sizeof(uint8_t), &ioCap);
        GAPBondMgr_SetParameter(GAPBOND_CENT_BONDING_ENABLED, sizeof(uint8_t), &bonding);
    }

    // Initialize GATT Client
    GATT_InitClient();
    // Register to receive incoming ATT Indications/Notifications
    GATT_RegisterForInd(centralTaskId);
    // Initialize GATT attributes
    GGS_AddService(GATT_ALL_SERVICES);         // GAP
    GATTServApp_AddService(GATT_ALL_SERVICES); // GATT attributes

    // Setup a delayed profile startup
    tmos_set_event(centralTaskId, START_DEVICE_EVT);
}

/*********************************************************************
 * @fn      Central_ProcessEvent
 *
 * @brief   Central Application Task event processor.  This function
 *          is called to process all events for the task.  Events
 *          include timers, messages and any other user defined events.
 *
 * @param   task_id  - The TMOS assigned task ID.
 * @param   events - events to process.  This is a bit map and can
 *                   contain more than one event.
 *
 * @return  events not processed
 */
uint16_t Central_ProcessEvent(uint8_t task_id, uint16_t events)
{
    WWDG_SetCounter(0);//喂狗 , 不可以在这里， 没有效果

    if(events & SYS_EVENT_MSG)
    {
        uint8_t *pMsg;

        if((pMsg = tmos_msg_receive(centralTaskId)) != NULL)
        {
            central_ProcessTMOSMsg((tmos_event_hdr_t *)pMsg);
            // Release the TMOS message
            tmos_msg_deallocate(pMsg);
        }
        // return unprocessed events
        return (events ^ SYS_EVENT_MSG);
    }

    if(events & START_DEVICE_EVT)
    {
        // Start the Device
        Print_I3("Start the Device");
        GAPRole_CentralStartDevice(centralTaskId, &centralBondCB, &centralRoleCB);
        return (events ^ START_DEVICE_EVT);
    }

    if(events & ESTABLISH_LINK_TIMEOUT_EVT)
    {
        Print_I3("Terminates the existing connection");
        GAPRole_TerminateLink(INVALID_CONNHANDLE);
        return (events ^ ESTABLISH_LINK_TIMEOUT_EVT);
    }

    if(events & START_SVC_DISCOVERY_EVT)
    {
        // start service discovery
        Print_I3("Start service discovery");
        centralStartDiscovery();
        return (events ^ START_SVC_DISCOVERY_EVT);
    }

    if(events & START_PARAM_UPDATE_EVT)
    {
        // start connect parameter update
        Print_I3("Update the link connection parameters");
        GAPRole_UpdateLink(centralConnHandle,
                           DEFAULT_UPDATE_MIN_CONN_INTERVAL,
                           DEFAULT_UPDATE_MAX_CONN_INTERVAL,
                           DEFAULT_UPDATE_SLAVE_LATENCY,
                           DEFAULT_UPDATE_CONN_TIMEOUT);
        return (events ^ START_PARAM_UPDATE_EVT);
    }

    if(events & START_READ_OR_WRITE_EVT)
    {
        if(centralProcedureInProgress == FALSE)
        {
            if(centralDoWrite)
            {
                // Do a write
                attWriteReq_t req;

                req.cmd = FALSE;
                req.sig = FALSE;
                req.handle = centralCharHdl;
                req.len = 1;
                req.pValue = GATT_bm_alloc(centralConnHandle, ATT_WRITE_REQ, req.len, NULL, 0);
                if(req.pValue != NULL)
                {
                    *req.pValue = centralCharVal;

                    if(GATT_WriteCharValue(centralConnHandle, &req, centralTaskId) == SUCCESS)
                    {
                        centralProcedureInProgress = TRUE;
                        centralDoWrite = !centralDoWrite;
                        tmos_start_task(centralTaskId, START_READ_OR_WRITE_EVT, DEFAULT_READ_OR_WRITE_DELAY);
                    }
                    else
                    {
                        GATT_bm_free((gattMsg_t *)&req, ATT_WRITE_REQ);
                    }
                }
            }
            else
            {
                // Do a read
                attReadReq_t req;

                req.handle = centralCharHdl;
                if(GATT_ReadCharValue(centralConnHandle, &req, centralTaskId) == SUCCESS)
                {
                    centralProcedureInProgress = TRUE;
                    centralDoWrite = !centralDoWrite;
                }
            }
        }
        return (events ^ START_READ_OR_WRITE_EVT);
    }

    if(events & START_READ_RSSI_EVT)
    {
        Print_I3("Read Rssi");
        GAPRole_ReadRssiCmd(centralConnHandle);
        tmos_start_task(centralTaskId, START_READ_RSSI_EVT, DEFAULT_RSSI_PERIOD);
        return (events ^ START_READ_RSSI_EVT);
    }

    // Discard unknown events
    return 0;
}

/*********************************************************************
 * @fn      central_ProcessTMOSMsg
 *
 * @brief   Process an incoming task message.
 *
 * @param   pMsg - message to process
 *
 * @return  none
 */
static void central_ProcessTMOSMsg(tmos_event_hdr_t *pMsg)
{
    switch(pMsg->event)
    {
        case GATT_MSG_EVENT:
            centralProcessGATTMsg((gattMsgEvent_t *)pMsg);
            break;
    }
}

/*********************************************************************
 * @fn      centralProcessGATTMsg
 *
 * @brief   Process GATT messages
 *
 * @return  none
 */
static void centralProcessGATTMsg(gattMsgEvent_t *pMsg)
{
    if(centralState != BLE_STATE_CONNECTED)
    {
        // In case a GATT message came after a connection has dropped,
        // ignore the message
        GATT_bm_free(&pMsg->msg, pMsg->method);
        return;
    }

    if((pMsg->method == ATT_EXCHANGE_MTU_RSP) ||
       ((pMsg->method == ATT_ERROR_RSP) &&
        (pMsg->msg.errorRsp.reqOpcode == ATT_EXCHANGE_MTU_REQ)))
    {
        if(pMsg->method == ATT_ERROR_RSP)
        {
            uint8_t status = pMsg->msg.errorRsp.errCode;

            Print_I3("Exchange MTU Error: %x\n", status);
        }
        centralProcedureInProgress = FALSE;
    }

    if(pMsg->method == ATT_MTU_UPDATED_EVENT)
    {
        Print_I3("MTU: %x\n", pMsg->msg.mtuEvt.MTU);
    }

    if((pMsg->method == ATT_READ_RSP) ||
       ((pMsg->method == ATT_ERROR_RSP) &&
        (pMsg->msg.errorRsp.reqOpcode == ATT_READ_REQ)))
    {
		if(pMsg->method == ATT_ERROR_RSP)
        {
            uint8_t status = pMsg->msg.errorRsp.errCode;

            Print_I3("Read Error: %x\n", status);
        }
        else
        {
            // After a successful read, display the read value
            Print_I3("Read rsp: %x\n", *pMsg->msg.readRsp.pValue);
        }
        centralProcedureInProgress = FALSE;
    }
    else if((pMsg->method == ATT_WRITE_RSP) ||
            ((pMsg->method == ATT_ERROR_RSP) &&
             (pMsg->msg.errorRsp.reqOpcode == ATT_WRITE_REQ)))
    {
        if(pMsg->method == ATT_ERROR_RSP)
        {
            uint8_t status = pMsg->msg.errorRsp.errCode;
            Print_I3("Write Error: %x\n", status);
        }
        else
        {
            // After a succesful write, display the value that was written and increment value
            Print_I3("Write sent: %x\n", centralCharVal);
        }
        centralProcedureInProgress = FALSE;
    }
    else if(pMsg->method == ATT_HANDLE_VALUE_NOTI)
    {
        Print_I3("Receive noti: %x\n", *pMsg->msg.handleValueNoti.pValue);
    }
    else if(centralDiscState != BLE_DISC_STATE_IDLE)
    {
        centralGATTDiscoveryEvent(pMsg);
    }
    GATT_bm_free(&pMsg->msg, pMsg->method);
}

/*********************************************************************
 * @fn      centralRssiCB
 *
 * @brief   RSSI callback.
 *
 * @param   connHandle - connection handle
 * @param   rssi - RSSI
 *
 * @return  none
 */
static void centralRssiCB(uint16_t connHandle, int8_t rssi)
{
    Print_I3("RSSI : -%d dB \n", -rssi);
}

/*********************************************************************
 * @fn      centralHciMTUChangeCB
 *
 * @brief   MTU changed callback.
 *
 * @param   maxTxOctets - Max tx octets
 * @param   maxRxOctets - Max rx octets
 *
 * @return  none
 */
static void centralHciMTUChangeCB(uint16_t connHandle, uint16_t maxTxOctets, uint16_t maxRxOctets)
{
    Print_I3(" HCI data length changed, Tx: %d, Rx: %d\n", maxTxOctets, maxRxOctets);
    centralProcedureInProgress = TRUE;
}

unsigned char HexToChar(unsigned char bChar)
{
	if( (bChar>=0x30 ) &&(bChar<=0x39))
	{  
		bChar -= 0x30;
	}  
	else if((bChar>=0x41)&&(bChar<=0x46)) // Capital
	{  
		bChar -= 0x37;
	}  
	else if((bChar>=0x61 )&&(bChar<=0x66)) //littlecase
	{  
		bChar -= 0x57;  
	}
	else
	{  
		bChar = 0xff;
	}

	return bChar;
}

void  Stop_Central_Scan(void)
{
    //Print_I3("s4");
//    Print_I3("Discovering...\n");
//    GAPRole_CentralStartDiscovery(DEFAULT_DISCOVERY_MODE,
//                                  FALSE,
//                                  DEFAULT_DISCOVERY_WHITE_LIST);
    GAPRole_CentralCancelDiscovery();
}

void  Start_Central_Scan(void)
{
    // Print_I3("k4=%d",Power_State);
    GAPRole_CentralStartDiscovery(DEFAULT_DISCOVERY_MODE,
                                  TRUE,
                                  DEFAULT_DISCOVERY_WHITE_LIST);
}

#if 0
// 回调函数：打印每个数据段
void printSegment(int segmentIndex, const DataSegment *segment, void *userData) {
    /*Print_I3("Segment %d: totalLength=%d, type=0x%02X, dataLength=%d, data=", 
           segmentIndex, segment->totalLength, segment->type, segment->dataLength);
    for (int i = 0; i < segment->dataLength; i++) {
        Print_I3("%02X ", segment->data[i]);
    }
    Print_I3("\n");*/

	if(segment->type == 0x09){
		Print_I("@@@@@@@@@@@@@@@@@@@@@@@@@@@ GAP_ADTYPE_LOCAL_NAME_COMPLETE\r\n");
		hex_dump(segment->data,segment->dataLength);
	}
}
#endif

/*********************************************************************
 * @fn      centralEventCB
 *
 * @brief   Central event callback function.
 *
 * @param   pEvent - pointer to event structure
 *
 * @return  none
 */
static void centralEventCB(gapRoleEvent_t *pEvent)
{
    UINT16  i,j,k;

    UINT8 *pData;
    UINT8 adLen;
    UINT8 adType;

    switch(pEvent->gap.opcode)
    {
        case GAP_DEVICE_INIT_DONE_EVENT:
        break;
        case GAP_DEVICE_INFO_EVENT:
        {
			// 获取广播数据指针和长度
            pData = pEvent->deviceInfo.pEvtData;
            adLen = pEvent->deviceInfo.dataLen;
            
            // 解析广播数据中的各个AD Structure
            i = 0;
            while (i < adLen) {
                // 获取当前AD Structure的长度（第一个字节）
                UINT8 len = pData[i];
                
                // 确保长度有效（至少包含类型字节）
                if (len == 0 || (i + len) > adLen) {
                    break;
                }
                
                // 获取AD Type（第二个字节）
                adType = pData[i + 1];
                
                // 获取AD Data（从第三个字节开始，长度为len-1）
                UINT8 *adData = &pData[i + 2];
                UINT8 dataLen = len - 1;
                
                // 根据AD Type处理数据
                switch (adType) {
                    case GAP_ADTYPE_FLAGS:
                        //PRINT("  FLAGS: 0x%02X\n", adData[0]);
                        break;
                        
                    case GAP_ADTYPE_LOCAL_NAME_COMPLETE:
						receiveGapBroadcastAdData(adData, dataLen);
                        break;
                        
                    case GAP_ADTYPE_LOCAL_NAME_SHORT:
                        //PRINT("  short name  %.*s\n", dataLen, adData);
                        break;
                        
                    case GAP_ADTYPE_SERVICES_LIST_16BIT:
                    //case GAP_ADTYPE_16BIT_SERVICE_UUID_CMP:
                        /*PRINT("  service UUID: ");
                        for (j = 0; j < dataLen; j += 2) {
                            PRINT("%02X%02X ", adData[j+1], adData[j]); // UUID是小端序
                        }
                        PRINT("\n");*/
                        break;
                        
                    case GAP_ADTYPE_MANUFACTURER_SPECIFIC:
                        /*PRINT("  MANUFACTURER:  FAC ID=0x%02X%02X, datalen=%d\n", 
                              adData[1], adData[0], dataLen - 2);
                        PRINT("  fac data: ");
                        for (j = 2; j < dataLen; j++) {
                            PRINT("%02X ", adData[j]);
                        }
                        PRINT("\n");*/
                        break;
                        
                    default:
                        //PRINT("  unknown type(0x%02X): len=%d\n", adType, dataLen);
                        break;
                }
                
                // 移动到下一个AD Structure
                i += len + 1;
            }
        }
        break;
        case GAP_DEVICE_DISCOVERY_EVENT:

    	break;
        case GAP_LINK_ESTABLISHED_EVENT:
        
        break;
        case GAP_LINK_TERMINATED_EVENT:
      
        break;
        case GAP_LINK_PARAM_UPDATE_EVENT:
        {
            Print_I3("Param Update...\n");
        }
        break;
        default:
            break;
    }
}

/*********************************************************************
 * @fn      pairStateCB
 *
 * @brief   Pairing state callback.
 *
 * @return  none
 */
static void centralPairStateCB(uint16_t connHandle, uint8_t state, uint8_t status)
{
    if(state == GAPBOND_PAIRING_STATE_STARTED)
    {
        Print_I3("Pairing started:%d\n", status);
    }
    else if(state == GAPBOND_PAIRING_STATE_COMPLETE)
    {
        if(status == SUCCESS)
        {
            Print_I3("Pairing success\n");
        }
        else
        {
            Print_I3("Pairing fail\n");
        }
    }
    else if(state == GAPBOND_PAIRING_STATE_BONDED)
    {
        if(status == SUCCESS)
        {
            Print_I3("Bonding success\n");
        }
    }
    else if(state == GAPBOND_PAIRING_STATE_BOND_SAVED)
    {
        if(status == SUCCESS)
        {
            Print_I3("Bond save success\n");
        }
        else
        {
            Print_I3("Bond save failed: %d\n", status);
        }
    }
}

/*********************************************************************
 * @fn      centralPasscodeCB
 *
 * @brief   Passcode callback.
 *
 * @return  none
 */
static void centralPasscodeCB(uint8_t *deviceAddr, uint16_t connectionHandle,
                              uint8_t uiInputs, uint8_t uiOutputs)
{
    uint32_t passcode;

    // Create random passcode
    passcode = tmos_rand();
    passcode %= 1000000;
    // Display passcode to user
    if(uiOutputs != 0)
    {
        Print_I3("Passcode:%06d\n", (int)passcode);
    }
    // Send passcode response
    GAPBondMgr_PasscodeRsp(connectionHandle, SUCCESS, passcode);
}

/*********************************************************************
 * @fn      centralStartDiscovery
 *
 * @brief   Start service discovery.
 *
 * @return  none
 */
static void centralStartDiscovery(void)
{
    uint8_t uuid[ATT_BT_UUID_SIZE] = {LO_UINT16(SIMPLEPROFILE_SERV_UUID),
                                      HI_UINT16(SIMPLEPROFILE_SERV_UUID)};

    // Initialize cached handles
    centralSvcStartHdl = centralSvcEndHdl = centralCharHdl = 0;

    centralDiscState = BLE_DISC_STATE_SVC;

    // Discovery simple BLE service
    Print_I3("Discovery simple BLE service");
    GATT_DiscPrimaryServiceByUUID(centralConnHandle,
                                  uuid,
                                  ATT_BT_UUID_SIZE,
                                  centralTaskId);
}

/*********************************************************************
 * @fn      centralGATTDiscoveryEvent
 *
 * @brief   Process GATT discovery event
 *
 * @return  none
 */
static void centralGATTDiscoveryEvent(gattMsgEvent_t *pMsg)
{
    attReadByTypeReq_t req;

    if(centralDiscState == BLE_DISC_STATE_SVC)
    {
        // Service found, store handles
        if(pMsg->method == ATT_FIND_BY_TYPE_VALUE_RSP &&
           pMsg->msg.findByTypeValueRsp.numInfo > 0)
        {
            centralSvcStartHdl = ATT_ATTR_HANDLE(pMsg->msg.findByTypeValueRsp.pHandlesInfo, 0);
            centralSvcEndHdl = ATT_GRP_END_HANDLE(pMsg->msg.findByTypeValueRsp.pHandlesInfo, 0);

            // Display Profile Service handle range
            Print_I3("Found Profile Service handle : %x ~ %x \n", centralSvcStartHdl, centralSvcEndHdl);
        }
        // If procedure complete
        if((pMsg->method == ATT_FIND_BY_TYPE_VALUE_RSP &&
            pMsg->hdr.status == bleProcedureComplete) ||
           (pMsg->method == ATT_ERROR_RSP))
        {
            if(centralSvcStartHdl != 0)
            {
                // Discover characteristic
                centralDiscState = BLE_DISC_STATE_CHAR;
                req.startHandle = centralSvcStartHdl;
                req.endHandle = centralSvcEndHdl;
                req.type.len = ATT_BT_UUID_SIZE;
                req.type.uuid[0] = LO_UINT16(SIMPLEPROFILE_CHAR1_UUID);
                req.type.uuid[1] = HI_UINT16(SIMPLEPROFILE_CHAR1_UUID);

                GATT_ReadUsingCharUUID(centralConnHandle, &req, centralTaskId);
            }
        }
    }
    else if(centralDiscState == BLE_DISC_STATE_CHAR)
    {
        // Characteristic found, store handle
        if(pMsg->method == ATT_READ_BY_TYPE_RSP &&
           pMsg->msg.readByTypeRsp.numPairs > 0)
        {
			centralCharHdl = BUILD_UINT16(pMsg->msg.readByTypeRsp.pDataList[0],
                                          pMsg->msg.readByTypeRsp.pDataList[1]);
            centralProcedureInProgress = FALSE;

            // Start do read or write
            tmos_start_task(centralTaskId, START_READ_OR_WRITE_EVT, DEFAULT_READ_OR_WRITE_DELAY);

            // Display Characteristic 1 handle
            Print_I3("Found Characteristic 1 handle : %x \n", centralCharHdl);
        }
        centralDiscState = BLE_DISC_STATE_IDLE;
    }
}

/*********************************************************************
 * @fn      centralAddDeviceInfo
 *
 * @brief   Add a device to the device discovery result list
 *
 * @return  none
 */
static void centralAddDeviceInfo(uint8_t *pAddr, uint8_t addrType)
{
    uint8_t i;

    // If result count not at max
    if(centralScanRes < DEFAULT_MAX_SCAN_RES)
    {
        // Check if device is already in scan results
        for(i = 0; i < centralScanRes; i++)
        {
            if(tmos_memcmp(pAddr, centralDevList[i].addr, B_ADDR_LEN))
            {
                return;
            }
        }
        // Add addr to scan result list
        tmos_memcpy(centralDevList[centralScanRes].addr, pAddr, B_ADDR_LEN);
        centralDevList[centralScanRes].addrType = addrType;
        // Increment scan result count
        centralScanRes++;
        // Display device addr
        Print_I3("Device %d - Addr %x %x %x %x %x %x \n", centralScanRes,
              centralDevList[centralScanRes - 1].addr[0],
              centralDevList[centralScanRes - 1].addr[1],
              centralDevList[centralScanRes - 1].addr[2],
              centralDevList[centralScanRes - 1].addr[3],
              centralDevList[centralScanRes - 1].addr[4],
              centralDevList[centralScanRes - 1].addr[5]);
    }
}
/************************ endfile @ central **************************/
