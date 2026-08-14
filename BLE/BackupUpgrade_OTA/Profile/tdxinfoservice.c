/*********************************************************************
 * INCLUDES
 */
#include <stdlib.h>
#include "app_cfg.h"

#ifdef ENABLE_SOFTWARE_TO_TDX
#include "tdxinfoservice.h"
#include "rledecode.h"
#include "commoninfo.h"
#include "aes_util.h"
#include "base64.h"
#include "flash_api.h"
#include "util.h"
#include "epd_driver.h"
#include "ch583_secure.h"
#include "ota.h"
#include "uart1_wifi_passthrough.h"

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * UTILITY FUNCTIONS
 */

/**
 * �?5 �?ASCII(49�?26) 字符压缩�?4 字节�?uint32_t
 *
 * 原理�? * - 每个字符映射�?0�?7
 * - �?5 �?78 进制�?合成为一�?32 位无符号整数
 *
 * @param str        输入字符串，必须至少�?5 个字�? * @param out_value  输出uint32_t值的指针
 * @return 0 成功�?1 表示错误（非法字符）
 */
static int encode5To4Bytes(const char str[5], uint32_t *out_value) {
    uint32_t value = 0;

    for (int i = 0; i < 5; i++) {
        unsigned char c = (unsigned char)str[i];

        if (c < 49 || c > 126) {
            return -1; // 非法字符
        }

        // 相当�?78 进制左移一�?        value = value * 78 + (uint32_t)(c - 49);
    }

    // 输出最终的32位值（大端序）
    *out_value = value;

    return 0;
}

/**
 * 核心函数：验证接收到的user_id是否与flash中存储的匹配
 * 通用接口，所有场景都可以复用
 *
 * @param received_user_id  接收到的4字节user_id
 * @return 0 验证通过或跳过验证，-1 验证失败
 */
static int verifyUserId(uint32_t received_user_id) {
#ifdef DISABLE_USER_LOCK_FEATURE
    (void)received_user_id;
    return 0;
#else
    uint32_t stored_user_id = getUserId();
    if(stored_user_id == 0x00000000 || stored_user_id == 0xFFFFFFFF){
        return 0;
    }

    if(received_user_id != stored_user_id){
        PRINT("User_id mismatch! (Recv:0x%08X != Stored:0x%08X)\n", received_user_id, stored_user_id);
        return -1;
    }

    return 0;
#endif
}

static int isOtaIapFrame(uint8_t *data, uint16_t len)
{
	if(data == NULL || len == 0)
	{
		return 0;
	}

	switch(data[0])
	{
		case CMD_IAP_PROM:
		case CMD_IAP_VERIFY:
			return (len >= 4 && data[1] > 0 && data[1] <= (len - 4) && data[1] <= (IAP_LEN - 4)) ? 1 : 0;
		case CMD_IAP_ERASE:
			return (len >= 6 && data[1] == 4) ? 1 : 0;
		case CMD_IAP_END:
			return (len >= 4 && data[1] <= (len - 2)) ? 1 : 0;
		case CMD_IAP_INFO:
			return (len >= 2 && data[1] <= (len - 2)) ? 1 : 0;
		case CMD_IAP_PROM_END:
		case CMD_IAP_VERIFY_END:
			return (len <= 20) ? 1 : 0;
		default:
			return 0;
	}
}

static uint8_t s_tdx_ota_guarded = 0;
static uint16_t s_tdx_ota_program_log_count = 0;
static uint16_t s_tdx_ota_verify_log_count = 0;

static void TdxInfo_PrepareOtaWrite(uint8_t *data, uint16_t len, const char *source)
{
	uint8_t cmd;
	uint8_t force_guard;

	if(data == NULL || len == 0)
	{
		return;
	}

	cmd = data[0];
	force_guard = (cmd == CMD_IAP_INFO || cmd == CMD_IAP_ERASE) ? 1 : 0;
	global_DEVICE_STATUS.fisOtaed = 1;

#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
	if(force_guard || s_tdx_ota_guarded != 1 ||
	   WifiPassthrough_IsWifiAwake() == Is_Yes ||
	   WifiPassthrough_IsWakePending() == Is_Yes)
	{
		WifiPassthrough_EnterOtaGuard();
		s_tdx_ota_guarded = 1;
	}
#else
	if(force_guard || s_tdx_ota_guarded != 1)
	{
		s_tdx_ota_guarded = 1;
	}
#endif

	switch(cmd)
	{
		case CMD_IAP_INFO:
		case CMD_IAP_ERASE:
			s_tdx_ota_program_log_count = 0;
			s_tdx_ota_verify_log_count = 0;
			PRINT("[OTA] %s len=%d cmd=0x%02x\r\n", source, len, cmd);
			break;
		case CMD_IAP_PROM:
			if(s_tdx_ota_program_log_count < 3)
			{
				PRINT("[OTA] %s program len=%d\r\n", source, len);
			}
			s_tdx_ota_program_log_count++;
			break;
		case CMD_IAP_VERIFY:
			if(s_tdx_ota_verify_log_count < 3)
			{
				PRINT("[OTA] %s verify len=%d\r\n", source, len);
			}
			s_tdx_ota_verify_log_count++;
			break;
		case CMD_IAP_END:
		case CMD_IAP_PROM_END:
		case CMD_IAP_VERIFY_END:
			PRINT("[OTA] %s len=%d cmd=0x%02x\r\n", source, len, cmd);
			break;
		default:
			break;
	}
}

static int verifyUserIdInData(UINT8 *data, UINT32 dataLen, int search_start_pos) {
    int percent_pos = -1;
    uint32_t received_user_id = 0x00000000;

    for(int i = search_start_pos; i < dataLen; i++){
        if(data[i] == '%'){
            percent_pos = i;
            break;
        }
    }

    if(percent_pos >= 0){
        if((dataLen - percent_pos - 1) < 5){
            PRINT("Broadcast format error: insufficient user_id length\n");
            return -1;
        }

        char user_id_str[5];
        memcpy(user_id_str, &data[percent_pos + 1], 5);
        if(encode5To4Bytes(user_id_str, &received_user_id) != 0){
            PRINT("Broadcast format error: invalid user_id characters\n");
            return -1;
        }
    }

    return verifyUserId(received_user_id);
}
/**
 * 处理user_id验证和lock标志
 *
 * @param received_user_id  从蓝牙接收到的user_id
 * @param lock              lock标志�?=锁定�?=不锁定）
 * @return 0 成功继续�?1 拒绝操作
 */
static int processUserIdAndLock(uint32_t received_user_id, uint8_t lock) {
#ifdef DISABLE_USER_LOCK_FEATURE
    (void)received_user_id;
    (void)lock;
    return 0;
#else
    uint32_t stored_user_id = getUserId();

    if(stored_user_id == 0x00000000 || stored_user_id == 0xFFFFFFFF){
        if(lock == 1){
            setUserId(received_user_id);
            PRINT("User ID saved: 0x%08X\r\n", received_user_id);
        }
        return 0;
    }

    if(received_user_id != stored_user_id){
        PRINT("User ID mismatch! (Recv:0x%08X != Stored:0x%08X) Rejected.\r\n", received_user_id, stored_user_id);
        return -1;
    }

    if(lock == 0){
        clearUserId();
        PRINT("User ID cleared\r\n");
    }

    return 0;
#endif
}

static int verifyBroadcastAndParseParams(UINT8 *adData, UINT32 dataLen,
                                         uint8_t *out_room, uint8_t *out_group,
                                         uint8_t *out_screen_mode, int *out_param_count) {
    int percent_pos = -1;

    if(verifyUserIdInData(adData, dataLen, 4) != 0){
        return -1;
    }

    for(int i = 4; i < dataLen; i++){
        if(adData[i] == '%'){
            percent_pos = i;
            break;
        }
    }
    if(percent_pos == -1){
        percent_pos = dataLen;
    }

    int param_count = percent_pos - 4;
    *out_param_count = param_count;
    *out_room = 0;
    *out_group = 0;
    *out_screen_mode = 0;

    if(param_count == 0){
        PRINT("No room/group specified\n");
    }else if(param_count == 1){
        *out_room = adData[4] - 33;
        PRINT("Room only: %d\n", *out_room);
    }else if(param_count == 2){
        *out_room = adData[4] - 33;
        *out_group = adData[5] - 33;
        PRINT("Room: %d, Group: %d\n", *out_room, *out_group);
    }else if(param_count == 3){
        *out_room = adData[4] - 33;
        *out_group = adData[5] - 33;
        *out_screen_mode = adData[6];
        PRINT("Room: %d, Group: %d, Screen mode: %c\n", *out_room, *out_group, *out_screen_mode);
    }else{
        PRINT("Invalid param format (too many chars: %d)\n", param_count);
        return -1;
    }

    return 0;
}
/*********************************************************************
 * CONSTANTS
 */
struct _TDX_AES_INFO global_TDX_AES_INFO;
struct _TDX_AES_DATA_INFO global_TDX_AES_DATA_INFO;
uint8 	gVerM;
uint8 	gVerS;
uint8 	gScreenType;
uint8 	IsDecryptFlag = Is_No;
uint8_t data_aes_key[16] = {0};
/*********************************************************************
 * TYPEDEFS
 */

//#define FALSH_TEST_DEBUG
/*********************************************************************
 * GLOBAL VARIABLES
 */
// Tdx information service
const uint8_t tdxInfoSerUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x00,0xff,0x12,0x7b};

// 1 get adc ID
const uint8_t tdxInfoGetInfoUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x01,0xff,0x12,0x7b};

// 2 get ver
const uint8_t tdxInfoGetVerUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x02,0xff,0x12,0x7b};

// 3 send data
const uint8_t tdxnfoSendDataUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x03,0xff,0x12,0x7b};

// 4  Bind
const uint8_t tdxInfoBindUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x04,0xff,0x12,0x7b};

// 5 send aes iv
const uint8_t tdxInfoSendAesIvUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x05,0xff,0x12,0x7b};

// 6 bind iv
const uint8_t tdxInfoBindIvUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x06,0xff,0x12,0x7b};

// 7 send aes key
const uint8_t tdxInfoSendAesKeyUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x07,0xff,0x12,0x7b};

// 8 send phone id
const uint8_t tdxInfoSendPhoneIdUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x08,0xff,0x12,0x7b};

// 9 get board model
const uint8_t tdxInfoGetBoardModelUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x09,0xff,0x12,0x7b};

// 10 notify dev
const uint8_t tdxInfoNotifyDevUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x10,0xff,0x12,0x7b};

// 11 notify ota 
const uint8_t tdxInfoNotifyOtaUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x11,0xff,0x12,0x7b};

// 12 pre save 
const uint8_t tdxInfoSendPreSaveUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x12,0xff,0x12,0x7b};

// 13 clean screen 
const uint8_t tdxInfoSendCleanUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x13,0xff,0x12,0x7b};

// 14 switch work mod 
const uint8_t tdxInfoSwitchModeUUID[ATT_UUID_SIZE] = {
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x14,0xff,0x12,0x7b};

/*********************************************************************
 * EXTERNAL VARIABLES
 */
/*********************************************************************
 * EXTERNAL FUNCTIONS
 */
int InitFirstPackage(uint16 connHandle);
int InitOtherPackage(uint16 connHandle, UINT8 *adData, UINT32 dataLen);
void notitySendEnd(uint16 connHandle, uint8 status, uint8 type, uint8_t *out_buf, uint16_t *out_len);
void notitySendFunc(uint16 connHandle, uint8_t *out_buf, uint16_t *out_len);
void notifyDeviceBound(uint16 connHandle);
#endif

void TdxInfo_ClearDisplayBusyProtect(void);

/*********************************************************************
 * LOCAL VARIABLES
 */
static uint8_t s_tdx_display_busy_protect = 0;
static uint32_t s_tdx_busy_drop_packets = 0;

/*********************************************************************
 * Profile Attributes - variables
 */

// tdx Information Service attribute
static const gattAttrType_t tdxInfoSer = {ATT_UUID_SIZE, tdxInfoSerUUID};

//1 get info
static uint8_t tdxInfoGetInfoProps = GATT_PROP_NOTIFY;
static gattCharCfg_t tdxInfoGet_cccd[4];

//2 get ver
static uint8_t tdxInfoGetVerProps = GATT_PROP_READ;

//3 send data
static uint8_t tdxnInfoSendDataProps = GATT_PROP_WRITE | GATT_PROP_WRITE_NO_RSP;

//4 Bind
static uint8_t tdxInfoBindProps = GATT_PROP_NOTIFY;
static gattCharCfg_t tdxInfoBind_cccd[4];

//5 send aes iv
static uint8_t tdxInfoSendAesIvProps = GATT_PROP_WRITE;

//6 bind iv
static uint8_t tdxInfoBindIvProps = GATT_PROP_NOTIFY;
static gattCharCfg_t tdxInfoBindIv_cccd[4];

//7 send aes key
static uint8_t tdxInfoSendAesKeyProps = GATT_PROP_WRITE;

//8 send phone id
static uint8_t tdxInfoSendPhoneIdProps = GATT_PROP_WRITE;

//9 get board model
static uint8_t tdxInfoGetBoardModelProps = GATT_PROP_READ;

// 10 notify dev
static uint8_t tdxInfoNotifyDevProps = GATT_PROP_READ | GATT_PROP_WRITE;

// 11 notify ota
static uint8_t tdxInfoNotifyOtaProps = GATT_PROP_READ | GATT_PROP_WRITE | GATT_PROP_WRITE_NO_RSP;//GATT_PROP_READ | GATT_PROP_WRITE;//

//12 presave 
static uint8_t tdxInfoSendPreSaveProps = GATT_PROP_NOTIFY | GATT_PROP_WRITE | GATT_PROP_WRITE_NO_RSP;

//13 clean 
static uint8_t tdxInfoSendCleanProps = GATT_PROP_WRITE;

//14 switch
static uint8_t tdxInfoSwitchModeProps = GATT_PROP_NOTIFY | GATT_PROP_READ | GATT_PROP_WRITE | GATT_PROP_WRITE_NO_RSP;

static gattCharCfg_t tdxInfoPreSave_cccd[4];
static gattCharCfg_t tdxInfoSwitchMode_cccd[4];

/*********************************************************************
 * Profile Attributes - Table
 */
static gattAttribute_t tdxInfoAttrTbl[] = {
    // Device Information Service index0
    {
        {ATT_BT_UUID_SIZE, primaryServiceUUID}, /* type */
        GATT_PERMIT_READ,                       /* permissions */
        0,                                      /* handle */
        (uint8_t *)&tdxInfoSer              	/* pValue */
    },
    
#if 0
    //CHAR 2 ,tdxInfoGetAdcUUID
    // System ID Declaration index1
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &tdxInfoGetAdcProps
    },

    // System ID Value index2
    {
        {ATT_UUID_SIZE, tdxInfoGetAdcUUID},
        GATT_PERMIT_READ,
        0,
        NULL
    },
#endif
#if 1
    //CHAR 6,boeInfoBindUUID
    // System ID Declaration index1
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &tdxInfoGetInfoProps
    },

    // System ID Value index2
    {
        {ATT_UUID_SIZE, tdxInfoGetInfoUUID},
        0,
        0,
        NULL
    },
    { //index3
        { ATT_BT_UUID_SIZE, clientCharCfgUUID },
        GATT_PERMIT_READ|GATT_PERMIT_WRITE,
        0,
        (uint8*)tdxInfoGet_cccd
    },
#endif

#if 1
    //CHAR 3,tdxInfoGetVerUUID
    // System ID Declaration  index4
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &tdxInfoGetVerProps
    },

    // System ID Value index5
    {
        {ATT_UUID_SIZE, tdxInfoGetVerUUID},
        GATT_PERMIT_READ,
        0,
        NULL
    },
#endif

#if 1
    //CHAR 5,tdxnfoSendDataUUID
    // System ID Declaration index6
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &tdxnInfoSendDataProps
    },

    // System ID Value index7
    {
        {ATT_UUID_SIZE, tdxnfoSendDataUUID},
        GATT_PERMIT_WRITE,
        0,
        NULL
    },
#endif

#if 1
    //CHAR 6,tdxInfoBindUUID
    // System ID Declaration index8
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &tdxInfoBindProps
    },

    // System ID Value index9
    {
        {ATT_UUID_SIZE, tdxInfoBindUUID},
        0,
        0,
        NULL
    },
    
    { //index10
        { ATT_BT_UUID_SIZE, clientCharCfgUUID },
        GATT_PERMIT_READ|GATT_PERMIT_WRITE,
        0,
        (uint8*)tdxInfoBind_cccd
    },
#endif

#if 1
    //CHAR 7,tdxInfoSendAesIvUUID
    // System ID Declaration index11
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &tdxInfoSendAesIvProps
    },

    // System ID Value index12
    {
        {ATT_UUID_SIZE, tdxInfoSendAesIvUUID},
        GATT_PERMIT_WRITE,
        0,
        NULL
    },
#endif

#if 1
    //CHAR 8,tdxInfoBindIvUUID
    // System ID Declaration index13
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &tdxInfoBindIvProps
    },

    // System ID Value index14
    {
        {ATT_UUID_SIZE, tdxInfoBindIvUUID},
        0,
        0,
        NULL
    },
    { //index 15
        { ATT_BT_UUID_SIZE, clientCharCfgUUID },
        GATT_PERMIT_READ|GATT_PERMIT_WRITE,
        0,
        (uint8*)tdxInfoBindIv_cccd
    },
#endif

#if 1
    //CHAR 9,tdxInfoSendAesKeyUUID
    // System ID Declaration index16
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &tdxInfoSendAesKeyProps
    },

    // System ID Value index17
    {
        {ATT_UUID_SIZE, tdxInfoSendAesKeyUUID},
        GATT_PERMIT_WRITE,
        0,
        NULL
    },
#endif

#if 1
    //CHAR 10,tdxInfoSendPhoneIdUUID
    // System ID Declaration index18
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &tdxInfoSendPhoneIdProps
    },

    // System ID Value index19
    {
        {ATT_UUID_SIZE, tdxInfoSendPhoneIdUUID},
        GATT_PERMIT_WRITE,
        0,
        NULL
    },
#endif

#if 1
    //CHAR 11,tdxInfoGetBoardModelUUID
    // System ID Declaration index20
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ,
        0,
        &tdxInfoGetBoardModelProps
    },

    // System ID Value index21
    {
        {ATT_UUID_SIZE, tdxInfoGetBoardModelUUID},
        GATT_PERMIT_READ,
        0,
        NULL
    },
#endif

#if 1
    //CHAR 14,tdxInfoNotifyDevUUID
    // System ID Declaration index22
    {
        {ATT_BT_UUID_SIZE, characterUUID},
        GATT_PERMIT_READ|GATT_PERMIT_WRITE,
        0,
        &tdxInfoNotifyDevProps
    },

    // System ID Value 23
    {
        {ATT_UUID_SIZE, tdxInfoNotifyDevUUID},
        GATT_PERMIT_READ|GATT_PERMIT_WRITE,
        0,
        NULL
    },
#endif

#if 1
	//CHAR 6,tdxInfoBindOta
	// System ID Declaration index24
	{
		{ATT_BT_UUID_SIZE, characterUUID},
		GATT_PERMIT_READ|GATT_PERMIT_WRITE,
		0,
		&tdxInfoNotifyOtaProps
	},
	
	// System ID Value index25
	{
		{ATT_UUID_SIZE, tdxInfoNotifyOtaUUID},
		GATT_PERMIT_READ|GATT_PERMIT_WRITE,
		0,
		NULL
	},
#endif

#if 1
	//CHAR 15,tdxInfoSendPreSaveUUID
	// System ID Declaration index26
	{
		{ATT_BT_UUID_SIZE, characterUUID},
		GATT_PERMIT_READ|GATT_PERMIT_WRITE,
		0,
		&tdxInfoSendPreSaveProps
	},

	// System ID Value index27
	{
		{ATT_UUID_SIZE, tdxInfoSendPreSaveUUID},
		GATT_PERMIT_READ|GATT_PERMIT_WRITE,
		0,
		NULL
	},

	{ //index28
		{ ATT_BT_UUID_SIZE, clientCharCfgUUID },
		GATT_PERMIT_READ|GATT_PERMIT_WRITE,
		0,
		(uint8*)tdxInfoPreSave_cccd
	},
#endif

#if 1
	//CHAR 16,tdxInfoSendPreSaveUUID
	// System ID Declaration index29
	{
		{ATT_BT_UUID_SIZE, characterUUID},
		GATT_PERMIT_READ,
		0,
		&tdxInfoSendCleanProps
	},

	// System ID Value index30
	{
		{ATT_UUID_SIZE, tdxInfoSendCleanUUID},
		GATT_PERMIT_WRITE,
		0,
		NULL
	},
#endif

#if 1
	//CHAR 16,tdxInfoSwitchModeProps
	// System ID Declaration index30
	{
		{ATT_BT_UUID_SIZE, characterUUID},
		GATT_PERMIT_READ|GATT_PERMIT_WRITE,
		0,
		&tdxInfoSwitchModeProps
	},

	// System ID Value index31
	{
		{ATT_UUID_SIZE, tdxInfoSwitchModeUUID},
		GATT_PERMIT_READ|GATT_PERMIT_WRITE,
		0,
		NULL
	},
	{ //index33
		{ ATT_BT_UUID_SIZE, clientCharCfgUUID },
		GATT_PERMIT_READ|GATT_PERMIT_WRITE,
		0,
		(uint8*)tdxInfoSwitchMode_cccd
	},
#endif
};

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static bStatus_t tdxInfo_ReadAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
                                    uint8_t *pValue, uint16_t *pLen, uint16_t offset, uint16_t maxLen, uint8_t method);
static bStatus_t tdxInfo_WriteAttrCB( uint16 connHandle, gattAttribute_t *pAttr,
                                      uint8 *pValue, uint16 len, uint16 offset,uint8 method );
bStatus_t ble_send_tdxInfo( uint16_t connHandle, uint8_t *data, uint16_t length, uint8_t id,  gattCharCfg_t *tdxInfo_cccd);
/*********************************************************************
 * PROFILE CALLBACKS
 */
// Tdx Info Service Callbacks
gattServiceCBs_t tdxInfoCBs = {
    tdxInfo_ReadAttrCB, // Read callback function pointer
    tdxInfo_WriteAttrCB,               // Write callback function pointer
    NULL                // Authorization callback function pointer
};

static int phoneid_len = 0;
static unsigned char u8IsFirstPackage = TDX_DATA_FIRST;

/*********************************************************************
 * NETWORK LAYER CALLBACKS
 */

/*********************************************************************
 * PUBLIC FUNCTIONS
 */
static void TdxInfo_HandleConnStatusCB ( uint16 connHandle, uint8 changeType )
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
            GATTServApp_InitCharCfg( INVALID_CONNHANDLE, tdxInfoBind_cccd );
            GATTServApp_InitCharCfg( INVALID_CONNHANDLE, tdxInfoBindIv_cccd );
			GATTServApp_InitCharCfg( INVALID_CONNHANDLE, tdxInfoPreSave_cccd );
			GATTServApp_InitCharCfg( INVALID_CONNHANDLE, tdxInfoGet_cccd );
			GATTServApp_InitCharCfg( INVALID_CONNHANDLE, tdxInfoSwitchMode_cccd );
        }
    }
}

/*********************************************************************
 * @fn      TdxInfo_AddService
 *
 * @brief   Initializes the Device Information service by registering
 *          GATT attributes with the GATT server.
 *
 * @return  Success or Failure
 */
bStatus_t CustomerInfo_AddService(void)
{
    uint8_t ret;

    GATTServApp_InitCharCfg( INVALID_CONNHANDLE, tdxInfoBind_cccd );
    GATTServApp_InitCharCfg( INVALID_CONNHANDLE, tdxInfoBindIv_cccd );
	GATTServApp_InitCharCfg( INVALID_CONNHANDLE, tdxInfoPreSave_cccd );
	GATTServApp_InitCharCfg( INVALID_CONNHANDLE, tdxInfoGet_cccd );
	GATTServApp_InitCharCfg( INVALID_CONNHANDLE, tdxInfoSwitchMode_cccd );

    // Register with Link DB to receive link status change callback
    linkDB_Register( TdxInfo_HandleConnStatusCB  );

    // Register GATT attribute list and CBs with GATT Server App
    ret =  GATTServApp_RegisterService(tdxInfoAttrTbl,
                                       GATT_NUM_ATTRS(tdxInfoAttrTbl),
                                       GATT_MAX_ENCRYPT_KEY_SIZE,
                                       &tdxInfoCBs);
    //PRINT("ADD tdx service:%02x\r\n",ret);
    return ret;
}

/*********************************************************************
 * @fn          tdxInfo_ReadAttrCB
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
static bStatus_t tdxInfo_ReadAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
                                    uint8_t *pValue, uint16_t *pLen, uint16_t offset, uint16_t maxLen, uint8_t method)
{
    bStatus_t status = SUCCESS;

    uint16_t gatt_handle = pAttr->handle - tdxInfoAttrTbl[0].handle;
    //PRINT("read gatt handle:%08x\r\n",gatt_handle);
    switch (gatt_handle) {
		case TDXINFO_NOTITY_SEND_END:{
			//PRINT("ReadAttrCB TDXINFO_NOTITY_SEND_END 0000000000000000000\r\n");
			uint8_t send_status = (global_DEVICE_STATUS.fDataSendSuccess == Is_Yes) ? 0x01 : 0x00;

			uint8_t notify_buf[6];  // ���������㹻�������ַ�֧����󳤶�?��
    		uint16_t notify_len = 0;  // ��ʼ�����ȱ���
			notitySendEnd(connHandle, send_status, global_TDX_AES_DATA_INFO.fScreenType, notify_buf, &notify_len);

			// �������ݵ�pValue��ʹ����ȷ�ĳ���notify_len��
		    // ��ȫУ�飺����pValue/pLenΪ�գ��򳤶ȳ�������������
		    if (pValue != NULL && pLen != NULL && notify_len <= sizeof(notify_buf)) {
		        memcpy(pValue, notify_buf, notify_len);
		        *pLen = notify_len;
		    } else {
		        // ������������־��ӡ������Ĭ��ֵ
		        *pLen = 0;
		    }
			break;
		}
		case TDXINFO_OTA_INFO:{
			//PRINT("ReadAttrCB TDXINFO_OTA_INFO 0000000000000000000 fOtaStatus:%d\r\n",global_DEVICE_STATUS.fOtaStatus);
			uint8_t ota_data[] = {0};
			ota_data[0] = global_DEVICE_STATUS.fOtaStatus;
			memcpy(pValue,ota_data,1);
			*pLen = 1;
			break;
		}
		case TDXINFO_SWITCH_MODE:{
			uint8_t mode[] = {0};
			uint8_t mode1[] = {0};
			Get_EEPROM_Flag(mode,WORKMODE_Position,WORKMODE_Len);
			//PRINT("ReadAttrCB TDXINFO_SWITCH_MODE 0000000000000000 mode:%d\r\n",mode[0]);
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

	memcpy(global_TDX_AES_INFO.szAesKey,data,16);
	phoneid_len = length-21;
	memcpy(phoneid,data+21,phoneid_len);
	//hex_dump(phoneid, phoneid_len);
	Get_EEPROM_Flag(global_TDX_AES_INFO.szPhoneId,PHONEID_Position,PHONEID_Len);

	if (strncmp(global_TDX_AES_INFO.szPhoneId, phoneid, phoneid_len) == 0){
		global_DEVICE_STATUS.fIsBonded[0] = DEVICE_BONDED;
	}else{
		memcpy(global_TDX_AES_INFO.szPhoneId,phoneid,phoneid_len);
		Save_EEPROM_Flag(global_TDX_AES_INFO.szPhoneId,PHONEID_Position,PHONEID_Len);
		//hex_dump(global_TDX_AES_INFO.szPhoneId, phoneid_len);
		global_DEVICE_STATUS.fIsBonded[0] = DEVICE_BONDED;
	}
	Save_EEPROM_Flag(global_DEVICE_STATUS.fIsBonded,BONDED_Position,BONDED_Len);
	P_scanBondRspData(global_DEVICE_STATUS.fIsBonded[0]);
}

static bStatus_t tdxInfo_WriteAttrCB( uint16 connHandle, gattAttribute_t *pAttr,
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
    uint16_t gatt_handle = pAttr->handle - tdxInfoAttrTbl[0].handle;
	if(global_DEVICE_STATUS.fisVaildDevice == Is_No){
		if((gatt_handle == TDXINFO_SEND_PHONE_ID) || (gatt_handle == TDXINFO_SEND_AES_IV)
			||((gatt_handle == TDXINFO_SEND_DATA) && !isOtaIapFrame(pValue, len)) || (gatt_handle == TDXINFO_NOTITY_SEND_END)
			|| (gatt_handle == TDXINFO_SEND_PRE_SAVE) || (gatt_handle == TDXINFO_SEND_CLEAN)){
			//PRINT("invaild device cannot use handle:%08x\r\n",gatt_handle);
			return ( ATT_ERR_INSUFFICIENT_AUTHOR );
		}
	}
    switch (gatt_handle) {
		case TDXINFO_GET_DATA:
        case TDXINFO_NOTITY_SEND_RESULT_INFO:
		case TDXINFO_NOTITY_SAVE_STATUS:
		case TDXINFO_SWITCH_MODE + 1:
        case 14: {//19
				//PRINT("WriteAttrCB TDXINFO_GET_DATA TDXINFO_NOTITY_ERROR_INFO TDXINFO_NOTITY_SEND_RESULT_INFO TDXINFO_NOTITY_SAVE_STATUS connHandle=%x\r\n",connHandle);	
				//hex_dump(pValue, len);
                status = GATTServApp_ProcessCCCWriteReq( connHandle, pAttr, pValue, len,
                         offset, GATT_CLIENT_CFG_NOTIFY );
            }
            break;
		case TDXINFO_SEND_PHONE_ID:
			//PRINT("send phone id\r\n");			
			//hex_dump(pValue, len);
			global_DEVICE_STATUS.fSystemTimeOut = 0;
		    rle_reverse_decrypt(pValue, len, DataCallBack);
			break;
		case TDXINFO_SEND_AES_IV:
			//PRINT("WriteAttrCB TDXINFO_SEND_AES_IV\r\n");
			u8IsFirstPackage = TDX_DATA_FIRST;
			//hex_dump(pValue, len);
			memcpy(global_TDX_AES_INFO.szAesIv,pValue,len);
			break;
		case TDXINFO_SEND_DATA:
			{
				if(isOtaIapFrame(pValue, len))
				{
					TdxInfo_PrepareOtaWrite(pValue, len, "TDXINFO_SEND_DATA");
					Rec_OTA_Data(pValue, len);
					return SUCCESS;
				}
#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
				WifiPassthrough_BleWrite(pValue, len);
				return SUCCESS;
#endif
				//Print_I3("decrypt_ecb data:\n");
	   			//hex_dump(pValue, len);

				if(s_tdx_display_busy_protect && s_tdx_busy_drop_packets > 0){
					s_tdx_busy_drop_packets--;
					if(s_tdx_busy_drop_packets == 0){
						u8IsFirstPackage = TDX_DATA_FIRST;
					}
					return ( SUCCESS );
				}
	   		
	   			unsigned int decrypted_len;
			    unsigned char *decrypted;
				if(IsDecryptFlag == Is_Yes)
				{
					//Print_I3("ENCRYPTED YES YES YES YES YES");
					decrypted = decrypt_ecb(pValue, len, &decrypted_len, data_aes_key);
				}
				else
				{
					//Print_I3("ENCRYPTED NO NO NO NO NO NO");
					decrypted = (unsigned char *)malloc(len * sizeof(uint8_t));
					decrypted_len = len;
					memcpy(decrypted, pValue, len);
				}
			    if (!decrypted) {
			    	free(decrypted);
			        Print_I3("decrypt_ecb failure\n");
			       	return ( ATT_ERR_INSUFFICIENT_AUTHOR );
			    } 		

				//PRINT("WriteAttrCB TDXINFO_SEND_DATA aaaaaaaaaaaaaaaa decrypted_len=%d\r\n",decrypted_len);
				//hex_dump(decrypted, decrypted_len);
				if(u8IsFirstPackage == TDX_DATA_FIRST){
					if(s_tdx_display_busy_protect){
						uint8_t busy_screen_type = decrypted[1];
						uint8_t notify_buf[6];
						uint16_t notify_len = 0;
						uint8_t notify_retry;

						s_tdx_busy_drop_packets = (decrypted[2] << 24) | (decrypted[3] << 16) | (decrypted[4] << 8) | decrypted[5];
						u8IsFirstPackage = (s_tdx_busy_drop_packets == 0) ? TDX_DATA_FIRST : TDX_DATA_FIRST_COLOR;
						notitySendEnd(connHandle, 0x03, busy_screen_type, notify_buf, &notify_len);
						for(notify_retry = 0; notify_retry < 3; notify_retry++){
							PRINT("TDX send notify 0x03 repeat %d/3\r\n", notify_retry + 1);
							notitySendFunc(connHandle, notify_buf, &notify_len);
							if(notify_retry < 2){
								delay_ms(50);
							}
						}
						free(decrypted);
						return ( SUCCESS );
					}

					//PRINT("WriteAttrCB TDXINFO_SEND_DATA aaaaaaaaaaaaaaaa decrypted_len=%d\r\n",decrypted_len);
					//hex_dump(decrypted, decrypted_len);
					global_TDX_AES_DATA_INFO.fOpType = decrypted[0];
					global_TDX_AES_DATA_INFO.fScreenType = decrypted[1];
					if(global_TDX_AES_DATA_INFO.fScreenType == OP_TYPE_IMG_A || global_TDX_AES_DATA_INFO.fScreenType == OP_TYPE_SAVE_IMG_A 
						|| global_TDX_AES_DATA_INFO.fScreenType == OP_TYPE_IMG_DIFF_A)
						global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_A;
					if(global_TDX_AES_DATA_INFO.fScreenType == OP_TYPE_IMG_B || global_TDX_AES_DATA_INFO.fScreenType == OP_TYPE_SAVE_IMG_B 
						|| global_TDX_AES_DATA_INFO.fScreenType == OP_TYPE_IMG_DIFF_B)
						global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_B;
					if(global_TDX_AES_DATA_INFO.fScreenType == OP_TYPE_IMG_AB || global_TDX_AES_DATA_INFO.fScreenType == OP_TYPE_SAVE_IMG_AB)
						global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_AB;
					global_TDX_AES_DATA_INFO.fPackageNum = (decrypted[2] << 24) | (decrypted[3] << 16) | (decrypted[4] << 8) | decrypted[5];
					
					uint32_t received_user_id = (decrypted[6] << 24) | (decrypted[7] << 16) | (decrypted[8] << 8) | decrypted[9];
					uint8_t user_id_lock = decrypted[14];
					global_TDX_AES_DATA_INFO.fUserId = received_user_id;
					
					if(processUserIdAndLock(received_user_id, user_id_lock) != 0){
						// user_id验证失败，设备已被其他用户绑定，通知前端
						u8IsFirstPackage = TDX_DATA_FIRST_COLOR;//第二个包就不进来�?						notifyDeviceBound(connHandle);
						free(decrypted);
						return ( SUCCESS );  // 返回成功，避免前端重试，通过notify告知真实状�?					}
					}
					
					global_TDX_AES_DATA_INFO.fIsSecret = decrypted[10];
					global_TDX_AES_DATA_INFO.fIsZip = decrypted[11];
					global_TDX_AES_DATA_INFO.fRoomNum= decrypted[12];
					global_TDX_AES_DATA_INFO.fGroupNum= decrypted[13];
					global_DEVICE_STATUS.fBoardCastGroup = global_TDX_AES_DATA_INFO.fGroupNum;
					global_DEVICE_STATUS.fBoardCastRoom = global_TDX_AES_DATA_INFO.fRoomNum;

					if(InitFirstPackage(connHandle) == -1){
						free(decrypted);
						return ( ATT_ERR_INSUFFICIENT_AUTHOR );
					}
					u8IsFirstPackage = TDX_DATA_FIRST_COLOR;
				}else{
					InitOtherPackage(connHandle, decrypted, decrypted_len);
				}
				free(decrypted);
			}
			break;
		case TDXINFO_NOTITY_SEND_END:
			//PRINT("WriteAttrCB TDXINFO_NOTITY_SEND_END\r\n");
			//hex_dump(pValue, len);
			//uint8_t finish_data[] = {0};
			//ble_send_tdxInfo(connHandle,finish_data,1,TDXINFO_NOTITY_SEND_RESULT_INFO-1, tdxInfoBind_cccd) ;
			//tmos_set_event(main_task_ID,EVENT_Low_Power);
			break;
		case TDXINFO_OTA_INFO:
			TdxInfo_PrepareOtaWrite(pValue, len, "TDXINFO_OTA_INFO");
			//hex_dump(pValue, len);
			Rec_OTA_Data(pValue,len);
			break;
		case TDXINFO_SEND_PRE_SAVE:
#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
			return SUCCESS;
#endif
			global_DEVICE_STATUS.fInitDriver = Is_Yes;
			global_TDX_AES_DATA_INFO.fOpType = OP_TYPE_SAVE_IMG_A;
			global_EXTERN_FLASH_INFO.fBlockNum = EXTERN_FLASH_BLOCK_FIRST_ADDR;
			global_DEVICE_STATUS.fRefreshType = DEVICE_OP_COMMON_PRESAVE;
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
					global_TDX_AES_DATA_INFO.fOpType = OP_TYPE_SAVE_IMG_AB;
				}
				
				clearBoardcastData();
				InitFlashDriver();
				mDelayuS(50);
				if(preSaveDisplayColor(pValue[2], pValue[1], DEVICE_PRE_SAVE) == -1){
					PRINT("send pre save  error \r\n");	
					uint8_t finish_data1[] = {1};
					ble_send_tdxInfo(connHandle,finish_data1,1,TDXINFO_SEND_PRE_SAVE, tdxInfoPreSave_cccd) ;
					global_DEVICE_STATUS.fWorked =Is_No;
					tmos_start_task(main_task_ID,EVENT_Low_Power ,500);
				}
				else{
					// 预存刷屏成功，标记成功（在EVENT_Low_Power统一保存�?					global_DEVICE_STATUS.fDataSendSuccess = Is_Yes;
				}
				mDelayuS(50);
				DeInitFlashDriver();
			}
			if(pValue[0] == 0x0c){ //clean
				if(len == 1){
					PRINT("send pre clean all\r\n");
					//global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_AB;
			#ifndef DISABLE_EPD_IMAGE_FEATURES
		initPicSave(SCREEN_CLEAN_ALL,0xff,0xff);
#endif
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
		case TDXINFO_SEND_CLEAN:
#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
			return SUCCESS;
#endif
			PRINT("send pre clean\r\n");	
			hex_dump(pValue, len);
			global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_AB;
			global_DEVICE_STATUS.fInitDriver = Is_Yes;
			global_EXTERN_FLASH_INFO.fBlockNum = EXTERN_FLASH_BLOCK_FIRST_ADDR;
			global_DEVICE_STATUS.fIsNeedStandby = 1;
			preSaveDisplayColor(0, 0, DEVICE_CLEAN_SCREEN);
			break;

		case TDXINFO_SWITCH_MODE:
#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
			PRINT("TDXINFO_SWITCH_MODE passthrough len=%d\r\n", len);
			hex_dump(pValue, len);
			WifiPassthrough_BleWrite(pValue, len);
			return SUCCESS;
#endif
			PRINT("WriteAttrCB TDXINFO_SWITCH_MODE len:%d pValue=%d\r\n",len,pValue[0]);
			uint8_t modePrefix[4] = {'M', 'O', 'D', 'E'};    
			uint8_t epdaPrefix[4] = {'E', 'P', 'D', 'A'};
			uint8_t otpPrefix[3] = {'O', 'T', 'P'};
			uint8_t keyPrefix[3] = {'K', 'E', 'Y'};
			uint8_t timePrefix[4] = {'T', 'I', 'M', 'E'};

			if (memcmp(pValue, modePrefix, 4) == 0) {
				uint32_t received_user_id = 0x00000000;
				unsigned char workmode[WORKMODE_Len] = {0x01};
			
				// 如果数据长度包含user_id�?=9字节），提取user_id
				if(len >= 9){
					received_user_id = (pValue[5] << 24) | (pValue[6] << 16) | (pValue[7] << 8) | pValue[8];
				}
				
				// 验证user_id（新旧指令都需要验证）
				if(verifyUserId(received_user_id) != 0){
					PRINT("MODE command rejected: user_id verification failed\r\n");
					workmode[0] = 2;
				}
				else
				{

					//hex_dump(pValue, len);
					if(pValue[4] == DEVICE_MODE_LOW){
						workmode[0] = DEVICE_MODE_LOW;
						P_scanModeRspData(DEVICE_MODE_LOW);
					}
					else if(pValue[4] == FRAME_WORK_MODE_DAILY_UPDATE){
						workmode[0] = FRAME_WORK_MODE_DAILY_UPDATE;
						P_scanModeRspData(FRAME_WORK_MODE_DAILY_UPDATE);
					}
					else{
						workmode[0] = DEVICE_MODE_HIGH;
						P_scanModeRspData(DEVICE_MODE_HIGH);
					}
					Save_EEPROM_Flag(workmode,WORKMODE_Position,WORKMODE_Len);

				}			
				uint8_t finish_data1[5] = {0};
				finish_data1[0] = 'M';
				finish_data1[1] = 'O';
				finish_data1[2] = 'D';
				finish_data1[3] = 'E';
				finish_data1[4] = workmode[0];
				ble_send_tdxInfo(connHandle,finish_data1,5,TDXINFO_GET_DATA-1, tdxInfoGet_cccd) ;
			}
			else if (memcmp(pValue, epdaPrefix, 4) == 0){
				uint8_t finish_data[14] = {0};
				uint32_t received_user_id = 0x00000000;	
				//hex_dump(pValue, len);
				// 如果数据长度包含user_id�?=8字节），提取user_id
				if(len >= 8){
					received_user_id = (pValue[4] << 24) | (pValue[5] << 16) | (pValue[6] << 8) | pValue[7];
				}
				
				// 验证user_id（新旧指令都需要验证）
				if(verifyUserId(received_user_id) != 0){
					PRINT("MODE command rejected: user_id verification failed\r\n");
					finish_data[0] = 'E';
					finish_data[1] = 'P';
					finish_data[2] = 'D';
					finish_data[3] = 'A';
					finish_data[13] = 2;
				}
					//return ( SUCCESS );  // 返回成功，避免前端重试，通过notify告知真实状�?				}
				else
				{
					uint8_t mode[] = {0};
					Get_EEPROM_Flag(mode,WORKMODE_Position,WORKMODE_Len);
				
					finish_data[0] = 'E';
					finish_data[1] = 'P';
					finish_data[2] = 'D';
					finish_data[3] = 'A';
					finish_data[4] = gVerM;
					finish_data[5] = gVerS;
					finish_data[6] = mode[0];
					finish_data[7] = ADC();
					finish_data[8] = gScreenType;
					finish_data[9] = INK_SCREEN_CUSTOMER;
					finish_data[10] = INK_SCREEN_CHIP;
					finish_data[11] = INK_DEVICE_CHIP;
#ifdef DISABLE_USER_LOCK_FEATURE
					finish_data[12] = 0;
#else
					uint32_t stored_user_id = getUserId();//whether locked
					if(stored_user_id == 0x00000000 || stored_user_id == 0xFFFFFFFF){
						finish_data[12] = 0;
					}else{
						finish_data[12] = 1;
					}
#endif
					finish_data[13] = 0;
				}
				ble_send_tdxInfo(connHandle,finish_data,14,TDXINFO_GET_DATA-1, tdxInfoGet_cccd) ;
			}
			else if (memcmp(pValue, otpPrefix, 3) == 0){
				uint8_t ret_write;
				PRINT("Burn key.......\r\n");
#ifdef ENABLE_BOARD_ENCRYPT
				if(pValue[3] == 'F'){
					ret_write = write_key_to_epprom(pValue+4, Is_Yes);
				}else{
					ret_write = write_key_to_epprom(pValue+4, Is_No);
				}
				uint8_t finish_data2[4] = {0};
		
				finish_data2[0] = 'O';
				finish_data2[1] = 'T';
				finish_data2[2] = 'P';
				finish_data2[3] = ret_write;
				ble_send_tdxInfo(connHandle,finish_data2,4,TDXINFO_GET_DATA-1, tdxInfoGet_cccd) ;
#endif
			}
			else if (memcmp(pValue, keyPrefix, 3) == 0){
				//PRINT("aes key.......\r\n");
				IsDecryptFlag = Is_Yes;
				memcpy(data_aes_key, pValue+3, len-3);
				//hex_dump(data_aes_key, len-3);
				reverseData(data_aes_key, len-3);
				//PRINT("aes key 11.......\r\n");
				//hex_dump(data_aes_key, len-3);
			}
		else if (memcmp(pValue, timePrefix, 4) == 0){
			// TIME指令格式：T, I, M, E, 开关（�?个字节）
			// �?个字�?pValue[4])：开关标志，0x00=关闭�?x01=开�?			// 注意：小时数已写死在代码中（REFRESH_TIMER_FIXED_HOURS），不再通过命令传�?			hex_dump(pValue, len);
			if(len >= 5){
				uint8_t new_enable = pValue[4]; // 获取开关标�?
				PRINT("Received TIME command: enable=%d (fixed hours=%d)\r\n", new_enable, REFRESH_TIMER_FIXED_HOURS);

				if(new_enable == 0x00 || new_enable == 0x01){
					saveLastRefreshInfo(
						SAVE_KEEP_U8,  // type keep
						SAVE_KEEP_U8,  // group keep
						SAVE_KEEP_U8,  // room keep
						SAVE_KEEP_U8,  // screen_mode keep
						SAVE_KEEP_U8,  // zip keep
						new_enable,    // enabled set
						SAVE_KEEP_U8   // screen_cleared keep
					);
					PRINT("TIME config saved to flash: enable=%d (fixed hours=%d, take effect after reboot)\r\n",
						  new_enable, REFRESH_TIMER_FIXED_HOURS);
				}else{
					PRINT("Invalid enable flag: 0x%02X (should be 0x00 or 0x01)\r\n", new_enable);
				}
			}
			else{
				PRINT("TIME command length error: received %d bytes, expected 5\r\n", len);
			}
		}
			mDelaymS(100);
			global_DEVICE_STATUS.fWorked=Is_No;
			//tmos_start_task(main_task_ID,EVENT_Low_Power ,500);		
			tmos_start_task(main_task_ID,EVENT_Check_TimeOut,500);
			break;

        default:
			PRINT("other ..........\r\n");
            hex_dump(pValue, len);
		    //rle_test();
            break;
    }
    return ( status );
} 

static bStatus_t ble_notify_info( uint16 connHandle, attHandleValueNoti_t *pNoti, uint8 id, gattCharCfg_t *tdxInfo_cccd)
{
    uint16 value = GATTServApp_ReadCharCfg( connHandle, tdxInfo_cccd );
    // If notifications enabled
    if ( value & GATT_CLIENT_CFG_NOTIFY )
    {
        // Set the handle
        pNoti->handle = tdxInfoAttrTbl[id].handle;

        // Send the Indication
        return GATT_Notification( connHandle, pNoti, FALSE);
    }
    return bleIncorrectMode;
}

bStatus_t ble_send_tdxInfo( uint16_t connHandle, uint8_t *data, uint16_t length, uint8_t id,  gattCharCfg_t *tdxInfo_cccd) {
    attHandleValueNoti_t notify_pram;
    uint8_t result = 0;
    notify_pram.len = length;
    notify_pram.pValue = GATT_bm_alloc( connHandle, ATT_HANDLE_VALUE_NOTI, length, NULL, 0 );
    if(notify_pram.pValue != NULL){
        tmos_memcpy( notify_pram.pValue, data, length );
        result = ble_notify_info( connHandle, &notify_pram, id, tdxInfo_cccd);
        if(result != 0 ) {
            GATT_bm_free( (gattMsg_t *)&notify_pram, ATT_HANDLE_VALUE_NOTI );
        }
        return result;
    }
    return bleNoResources;
}

bStatus_t TdxInfo_SendWifiDataToFrontend(uint16_t connHandle, uint8_t *data, uint16_t length)
{
    return ble_send_tdxInfo(connHandle, data, length, TDXINFO_SWITCH_MODE, tdxInfoSwitchMode_cccd);
}

#ifdef OLD_ADVERTDATA
#define DEFAULT_DESIRED_MIN_CONN_INTERVAL     6//320
#define DEFAULT_DESIRED_MIN_CONN_INTERVAL_v2  6//320 // (80)     // �ӿ������ٶ�
#define DEFAULT_DESIRED_MAX_CONN_INTERVAL     100//320
#define DEFAULT_DESIRED_MAX_CONN_INTERVAL_v2  100//320     // �ӿ������ٶ�

static uint8 scanRspData[] = {
    // complete name
 	3+1, // length of this data
    GAP_ADTYPE_LOCAL_NAME_COMPLETE,
    
	'T', '%', '!',
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
    0, // 0dBm
    
  	0x0D,                             // length of this data including the data type byte
	GAP_ADTYPE_MANUFACTURER_SPECIFIC, // manufacturer specific advertisement data type
	'b', 'l', 'e', '1', '2', '3', '4', '5', '6', 
	'7', '0', '0'//, '0', '0', // m_ver s_ver work_mode

};

static uint8_t advertData[] = {
    // Flags; this sets the device to use limited discoverable
    // mode (advertises for 30 seconds at a time) instead of general
    // discoverable mode (advertises indefinitely)
    0x02, // length of this data
    GAP_ADTYPE_FLAGS,
    GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,

    // service UUID, to notify central devices what services are included
    // in this peripheral
    0x03,                  // length of this data
    GAP_ADTYPE_16BIT_MORE, // some of the UUID's, but not all
    LO_UINT16(SIMPLEPROFILE_SERV_UUID),
    HI_UINT16(SIMPLEPROFILE_SERV_UUID)
};
#else
//boardcast data
static uint8 scanRspData[] = {
	// complete name
	23, 
	GAP_ADTYPE_LOCAL_NAME_COMPLETE,
	'T',
	'%',
	'2','F','7','8','2','D','4','A','A','D','F','A','S','1','F','@','@','A','E','0'
};

static uint8_t advertData[] = {
    0x02, // length of this data
    GAP_ADTYPE_FLAGS,
    GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,

    // service UUID, to notify central devices what services are included
    // in this peripheral
    17,                  // length of this data
    GAP_ADTYPE_128BIT_MORE, // some of the UUID's, but not all
    0x41,0x59,0x8b,0x7b,0x99,0x74,0x07,0xa3,0xc1,0x49,0x13,0x44,0x00,0xff,0x12,0x6b,

    0x06,
    GAP_ADTYPE_MANUFACTURER_SPECIFIC,
    0xFF,0xFF,
    0x00,0x00,0x00
};
#endif

static uint8_t P_normalizeFrameWorkMode(uint8_t mode)
{
	if(mode > WIFI_COMPOSITE_WORK_MODE_MAX)
	{
		return FRAME_WORK_MODE_NORMAL;
	}

	return mode;
}

static uint8_t P_getValidFrameWorkMode(void)
{
	uint8_t mode = FRAME_WORK_MODE_NORMAL;

	Get_EEPROM_Flag(&mode, WORKMODE_Position, WORKMODE_Len);
	return P_normalizeFrameWorkMode(mode);
}

static uint8_t P_getWifiProvisionNameNibble(void)
{
	return (getWifiProvisionStatus() == WIFI_PROVISIONED) ? WIFI_PROVISIONED_NIBBLE : WIFI_UNPROVISIONED_NIBBLE;
}

static uint8_t P_getWifiProvisionNameNibbleByStatus(uint8_t status)
{
	return (status == WIFI_PROVISIONED) ? WIFI_PROVISIONED_NIBBLE : WIFI_UNPROVISIONED_NIBBLE;
}

static uint8_t P_getTdxCompositeStatusByteByMode(uint8_t mode)
{
	return (uint8_t)(((P_getWifiProvisionNameNibble() & 0x0F) << 4) | (P_normalizeFrameWorkMode(mode) & 0x0F));
}

static uint8_t P_getTdxCompositeStatusByteByStatus(uint8_t status, uint8_t mode)
{
	return (uint8_t)(((P_getWifiProvisionNameNibbleByStatus(status) & 0x0F) << 4) | (P_normalizeFrameWorkMode(mode) & 0x0F));
}

static uint8_t P_getTdxCompositeStatusByte(void)
{
	return P_getTdxCompositeStatusByteByMode(P_getValidFrameWorkMode());
}

#ifndef OLD_ADVERTDATA
#define TDX_WIFI_VERSION_ADV_OFFSET    (sizeof(advertData) - WIFI_COMPOSITE_VERSION_LEN)
static void P_setWifiVersionAdvertData(uint8_t version[WIFI_COMPOSITE_VERSION_LEN])
{
	memcpy(&advertData[TDX_WIFI_VERSION_ADV_OFFSET], version, WIFI_COMPOSITE_VERSION_LEN);
}
#endif

void intBoardCastData()
{
	unsigned char szVerInfo[VERINFO_Len];
	char char1;
    char char2;
	unsigned char bond1[2];
#ifndef OLD_ADVERTDATA
	uint8_t wifi_version[WIFI_COMPOSITE_VERSION_LEN];
#endif
	Get_EEPROM_Flag(bond1,WORKMODE_Position,WORKMODE_Len);
	
	//Print_I3("@@intBoardCastData mac=%x %x %x %x %x %x \n",Mac[0],Mac[1],Mac[2],Mac[3],Mac[4],Mac[5]);
#ifdef OLD_ADVERTDATA
	scanRspData[27-12] = 0;
    scanRspData[28-12] = 0;
    scanRspData[29-12] = 33 + (global_DEVICE_STATUS.fAdcValue / 2);
	scanRspData[30-12] = Mac[0];
    scanRspData[31-12] = Mac[1];
    scanRspData[32-12] = Mac[2];
    scanRspData[33-12] = Mac[3];
    scanRspData[34-12] = Mac[4];
    scanRspData[35-12] = Mac[5];

	convert_number(VER, &char1, &char2);
	Print_I3("Peripheral_Init %d -> '%c', '%c'\n", VER, char1, char2);
	scanRspData[36-12] = char1;
	scanRspData[37-12] = char2;
#else
	scanRspData[3] = EPD_GetScreenType();
	scanRspData[4] = hexToChar((Mac[5] & 0xf0) >> 4);
	scanRspData[5] = hexToChar(Mac[5] & 0x0f);
	scanRspData[6] = hexToChar((Mac[4] & 0xf0) >> 4);
	scanRspData[7] = hexToChar(Mac[4] & 0x0f);
	scanRspData[8] = hexToChar((Mac[3] & 0xf0) >> 4);
	scanRspData[9] = hexToChar(Mac[3] & 0x0f);
	scanRspData[10] = hexToChar((Mac[2] & 0xf0) >> 4);
	scanRspData[11] = hexToChar(Mac[2] & 0x0f);
	scanRspData[12] = hexToChar((Mac[1] & 0xf0) >> 4);
	scanRspData[13] = hexToChar(Mac[1] & 0x0f);
	scanRspData[14] = hexToChar((Mac[0] & 0xf0) >> 4);
	scanRspData[15] = hexToChar(Mac[0] & 0x0f);
	scanRspData[16] = 33 + (global_DEVICE_STATUS.fAdcValue / 2);

	convert_number(VER, &char1, &char2);
	Print_I3("Peripheral_Init %d -> '%c', '%c'\n", VER, char1, char2);
	scanRspData[17] = char1;
	scanRspData[18] = char2;
	scanRspData[19] = EPD_GetBoardInfo();

	scanRspData[20] = getAdcAndWorkMode(Is_No, bond1[0]);
	if(global_DEVICE_STATUS.fisVaildDevice == Is_Yes){
		scanRspData[21] = 'A';
	}else{
		scanRspData[21] = 'B';
	}
	scanRspData[22] = P_getTdxCompositeStatusByte();
	getWifiCompositeVersion(wifi_version);
	wifi_version[0] = (uint8_t)VER;
	P_setWifiVersionAdvertData(wifi_version);
#ifdef DISABLE_USER_LOCK_FEATURE
	scanRspData[23] = '0';
#else
	uint32_t stored_user_id = getUserId();//whether locked
	if(stored_user_id == 0x00000000 || stored_user_id == 0xFFFFFFFF){
		scanRspData[23] = '0';
	}else{
		scanRspData[23] = '1';
	}
#endif
#endif

	gVerM = scanRspData[17];
	gVerS = scanRspData[18];
	gScreenType = scanRspData[3];

	Get_EEPROM_Flag(szVerInfo,VERINFO_Position,VERINFO_Len);
	//hex_dump(szVerInfo, VERINFO_Len);
	if((szVerInfo[0] != char1) || (szVerInfo[1] != char2)){
		Print_I3("@@@@@@@@@@@@@@@@@ diff ver need  factory \n");
		extern void initPicSave(int type, int room, int group);
#ifndef DISABLE_EPD_IMAGE_FEATURES
		initPicSave(SCREEN_CLEAN_ALL,0xff,0xff);
#endif
		szVerInfo[0] = char1;
		szVerInfo[1] = char2;
		Save_EEPROM_Flag(szVerInfo,VERINFO_Position,VERINFO_Len);	
	}

	// Setup the GAP Peripheral Role Profile
    {
        uint8_t  initial_advertising_enable = TRUE;                                                 //�����㲥ʹ��
        // Set the GAP Role Parameters                                                              //����GAP�����?        GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &initial_advertising_enable);
        GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);
        GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData), advertData);
    }
}

void  P_scanBondRspData(UINT8 bond_status)
{
	printf("P_scanRspData 111 bond_status :%d\r\n",bond_status);
#if 0
	if(bond_status == DEVICE_BONDED)
		scanRspData[3] = '(';
	else
		scanRspData[3] = ')';
	
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);// �������?
#endif
}

void  P_scanModeRspData(UINT8 work_status)
{
	printf("P_scanRspData 111 work_status :%d\r\n",work_status);

	if(work_status == DEVICE_MODE_HIGH)
		scanRspData[20] = 'P';
	else
		scanRspData[20] = '@';
	scanRspData[20] = getAdcAndWorkMode(global_DEVICE_STATUS.fIsCharg,work_status);
	scanRspData[22] = P_getTdxCompositeStatusByteByMode(work_status);
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);// �������?}
}

void  P_scanAdcRspData(UINT8 work_status)
{
	//printf("P_scanRspData 111 fAdcValue :%d\r\n",global_DEVICE_STATUS.fAdcValue);
	scanRspData[16] = 33 + (global_DEVICE_STATUS.fAdcValue / 2);//pre num  start '!'
	
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);// �������?}
}

void  P_scanPreNumRspData(UINT8 pre_num)
{
#if 0
	printf("P_scanPreNumRspData 111 pre_num :%d\r\n",pre_num);
	scanRspData[21] = '!' + pre_num;   //pre num  start '!'
	
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);// �������?
#endif
}

void  P_scanIsChargeRspData()
{
	//printf("P_scanIsChargeRspData 00000000000000000000000\r\n");
	unsigned char mode1[2];
	Get_EEPROM_Flag(mode1,WORKMODE_Position,WORKMODE_Len);

	scanRspData[20] = getAdcAndWorkMode(global_DEVICE_STATUS.fIsCharg, mode1[0]);
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);
}

void P_scanWifiProvisionRspData(uint8_t status)
{
#ifndef OLD_ADVERTDATA
	(void)status;
	scanRspData[22] = P_getTdxCompositeStatusByte();
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);
#else
	(void)status;
#endif
}

void P_scanWifiCompositeRspData(uint8_t status, uint8_t mode)
{
#ifndef OLD_ADVERTDATA
	scanRspData[22] = P_getTdxCompositeStatusByteByStatus(status, mode);
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);
#else
	(void)status;
	(void)mode;
#endif
}

void P_scanWifiVersionRspData(uint8_t version[WIFI_COMPOSITE_VERSION_LEN])
{
#ifndef OLD_ADVERTDATA
	P_setWifiVersionAdvertData(version);
	GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData), advertData);
#else
	(void)version;
#endif
}
static uint8_t firstAdvertisementData[BOARDCAST_Len];  // �洢�״ι㲥���ݵĻ�����
static uint8_t firstAdvertisementLen[BOARDLEN_Len];

void receiveGapBroadcastAdData(UINT8 *adData, UINT32 dataLen)
{
#ifdef DISABLE_EPD_IMAGE_FEATURES
    (void)adData;
    (void)dataLen;
    return;
#else
	if((adData[0] == BROADCAST_COMMAND_PRESAVE && adData[1] == BROADCAST_PREFIX_CHAR2 && adData[2] == BROADCAST_PREFIX_CHAR3 && adData[3] == BROADCAST_PREFIX_CHAR4) || 
		(adData[0] == BROADCAST_COMMAND_CLEAN_SCREEN && adData[1] == BROADCAST_PREFIX_CHAR2 && adData[2] == BROADCAST_PREFIX_CHAR3 && adData[3] == BROADCAST_PREFIX_CHAR4) ||
		(adData[0] == BROADCAST_COMMAND_CLEAN_PRESAVE && adData[1] == BROADCAST_PREFIX_CHAR2 && adData[2] == BROADCAST_PREFIX_CHAR3 && adData[3] == BROADCAST_PREFIX_CHAR4)){
		Print_I3("	boardcast data: %.*s\n", dataLen, adData);
		
		// ��ӡ�豸��ַ��RSSI
		/*Print_I3("find device: ");
		for (i = 0; i < B_ADDR_LEN; i++) {
			Print_I3("%02X", pEvent->deviceInfo.addr[i]);
			if (i < B_ADDR_LEN - 1) PRINT(":");
		}
		Print_I3(", RSSI: %d dBm\n", pEvent->deviceInfo.rssi);*/
		// 如果设备即将重启，不处理新的广播数据，避免误保存
        if(global_DEVICE_STATUS.fWillReboot == Is_Yes){
            Print_I3("    Device will reboot, ignore new broadcast\n");
            return;
        }
		if(global_DEVICE_STATUS.fWorked == Is_No){
			if((adData[1]==BROADCAST_PREFIX_CHAR2) && (adData[2]==BROADCAST_PREFIX_CHAR3) && (adData[3]==BROADCAST_PREFIX_CHAR4)){
				Get_EEPROM_Flag(firstAdvertisementData,BOARDCAST_Position,BOARDCAST_Len);
				Get_EEPROM_Flag(firstAdvertisementLen,BOARDLEN_Position,BOARDLEN_Len);
				if((firstAdvertisementLen[0] != dataLen) || (memcmp(adData, firstAdvertisementData, dataLen) != 0)){
					uint8_t room_num = 0;
					uint8_t group_num = 0;
					uint8_t screen_mode_char = 0;
					int param_count = 0;
					
					// 验证user_id并解析广播参�?					if(verifyBroadcastAndParseParams(adData, dataLen, &room_num, &group_num, &screen_mode_char, &param_count) != 0){
						// 验证失败或解析失败，直接返回
						return;
					}
					memcpy(firstAdvertisementData, adData, dataLen);
					firstAdvertisementLen[0] = dataLen;
					Save_EEPROM_Flag(firstAdvertisementData,BOARDCAST_Position,BOARDCAST_Len);
					Save_EEPROM_Flag(firstAdvertisementLen,BOARDLEN_Position,BOARDLEN_Len);
					Print_I3("	boardcast data the diff @@@@@@@\n");
					if(adData[0] == BROADCAST_COMMAND_PRESAVE){
						// G命令：预存刷屏（格式：G@!#11C%xxxxx，必须是3位参数）
						if(param_count != 3){
							PRINT("G command requires 3 params (room+group+mode)\n");
							return;
						}
						global_DEVICE_STATUS.fBoardCastType = SCREEN_PRESAVE_OP;
						// 解析屏幕模式（第三位字符�?						if(screen_mode_char == 'a'){
							global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_A;
						}else if(screen_mode_char == 'b'){
							global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_B;
						}else if(screen_mode_char == 'C'){
							global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_AB;
						}else{
							PRINT("Invalid screen mode: %c\n", screen_mode_char);
							return;
						}
						global_DEVICE_STATUS.fBoardCastRoom = room_num;
						global_DEVICE_STATUS.fBoardCastGroup = group_num;
					}else if(adData[0] == BROADCAST_COMMAND_CLEAN_SCREEN){
						// C命令：清屏（支持0/1/2位参数）
						if(param_count == 0){
							// 没有room/group参数，清空所有（格式：C@!#%xxxxx�?							global_DEVICE_STATUS.fBoardCastType = SCREEN_CLEAN_SCRREN_ALL;
						}else if(param_count == 1){
							// 只有room参数（格式：C@!#1%xxxxx�?							global_DEVICE_STATUS.fBoardCastType = SCREEN_CLEAN_SCRREN_ROOM;
							global_DEVICE_STATUS.fBoardCastRoom = room_num;
						}else if(param_count == 2 || param_count == 3){//3是兼容旧协议
							// 有room和group参数（格式：C@!#12%xxxxx�?							global_DEVICE_STATUS.fBoardCastType = SCREEN_CLEAN_SCRREN_ROOM;
							global_DEVICE_STATUS.fBoardCastRoom = room_num;
							global_DEVICE_STATUS.fBoardCastGroup = group_num;
						}else{
							PRINT("C command invalid param count: %d\n", param_count);
							return;
						}
					}else if(adData[0] == BROADCAST_COMMAND_CLEAN_PRESAVE){
						// D命令：清预存（支�?/1/2位参数）
						if(param_count == 0){
							// 没有room/group参数，清空所有（格式：D@!#%xxxxx�?							global_DEVICE_STATUS.fBoardCastType = SCREEN_CLEAN_ALL;
						}else if(param_count == 1){
							// 只有room参数（格式：D@!#1%xxxxx�?							global_DEVICE_STATUS.fBoardCastType = SCREEN_CLEAN_ROOM;
							global_DEVICE_STATUS.fBoardCastRoom = room_num;
						}else if(param_count == 2){
							// 有room和group参数（格式：D@!#12%xxxxx�?							global_DEVICE_STATUS.fBoardCastType = SCREEN_CLEAN_ROOM_AND_GROUP;
							global_DEVICE_STATUS.fBoardCastRoom = room_num;
							global_DEVICE_STATUS.fBoardCastGroup = group_num;
						}else{
							PRINT("D command invalid param count: %d\n", param_count);
							return;
						}
					}
	
					//Print_I3("boardcast info type:%d room:%d group:%d ab:%d",global_DEVICE_STATUS.fBoardCastType,global_BOE_AES_DATA_INFO.fRoomNum,
					//	global_BOE_AES_DATA_INFO.fGroupNum,global_DEVICE_STATUS.fScreenType);
					tmos_start_task(main_task_ID,EVENT_Boardcast_Op ,200);
				}else{
					Print_I3("	boardcast data the same @@@@@@@\n");
				}
			}
		}
	}
#endif
}

void processGapBroadcastAdData(){
#ifdef DISABLE_EPD_IMAGE_FEATURES
    return;
#else
	//PRINT("boardcast info type:%d room:%d group:%d ab:%d\r\n",global_DEVICE_STATUS.fBoardCastType,global_DEVICE_STATUS.fBoardCastRoom,
	//	global_DEVICE_STATUS.fBoardCastGroup,global_DEVICE_STATUS.fScreenType);

	if(global_DEVICE_STATUS.fBoardCastType == SCREEN_PRESAVE_OP){
		global_DEVICE_STATUS.fInitDriver = Is_Yes;
		//global_BOE_AES_DATA_INFO.fOpType = OP_TYPE_SAVE_IMG_A;
		global_EXTERN_FLASH_INFO.fBlockNum = EXTERN_FLASH_BLOCK_FIRST_ADDR;
		global_DEVICE_STATUS.fRefreshType = DEVICE_OP_PRESAVE;
		global_DEVICE_STATUS.fIsNeedStandby = 1;
		InitFlashDriver();
		mDelayuS(50);

		if(preSaveDisplayColor(global_DEVICE_STATUS.fBoardCastRoom, global_DEVICE_STATUS.fBoardCastGroup, DEVICE_PRE_SAVE) == -1){
			PRINT("send pre save 11  error \r\n");	
			global_DEVICE_STATUS.fWorked =Is_No;
		}
		else{
			// 蓝牙广播切换预存刷屏成功，标记成功（在EVENT_Low_Power统一保存�?			int Room, Group;
			Group = global_DEVICE_STATUS.fBoardCastRoom; //预存切换协议里的group和room是反着的，所以要反过来保�?			Room = global_DEVICE_STATUS.fBoardCastGroup;
			global_DEVICE_STATUS.fBoardCastRoom = Room;
			global_DEVICE_STATUS.fBoardCastGroup = Group;
			global_DEVICE_STATUS.fDataSendSuccess = Is_Yes;
		}
		mDelayuS(50);
		DeInitFlashDriver();
	}

	if(global_DEVICE_STATUS.fBoardCastType == SCREEN_CLEAN_ALL){
#ifndef DISABLE_EPD_IMAGE_FEATURES
		initPicSave(SCREEN_CLEAN_ALL,0xff,0xff);
#endif
		global_DEVICE_STATUS.fWorked =Is_No;
	}

	if(global_DEVICE_STATUS.fBoardCastType == SCREEN_CLEAN_ROOM){
		cleanPresaveScreen(SCREEN_CLEAN_ROOM,global_DEVICE_STATUS.fBoardCastRoom,0xff);
		initPicSave(SCREEN_CLEAN_ROOM,global_DEVICE_STATUS.fBoardCastRoom,0xff);
	}

	if(global_DEVICE_STATUS.fBoardCastType == SCREEN_CLEAN_ROOM_AND_GROUP){
		cleanPresaveScreen(SCREEN_CLEAN_ROOM_AND_GROUP,global_DEVICE_STATUS.fBoardCastRoom,global_DEVICE_STATUS.fBoardCastGroup);
		initPicSave(SCREEN_CLEAN_ROOM_AND_GROUP,global_DEVICE_STATUS.fBoardCastRoom,global_DEVICE_STATUS.fBoardCastGroup);
	}

	if(global_DEVICE_STATUS.fBoardCastType == SCREEN_CLEAN_SCRREN_ALL){
		cleanPresaveScreen(SCREEN_CLEAN_SCRREN_ALL,0,0);
	}

	if(global_DEVICE_STATUS.fBoardCastType == SCREEN_CLEAN_SCRREN_ROOM){
		cleanPresaveScreen(SCREEN_CLEAN_SCRREN_ROOM,global_DEVICE_STATUS.fBoardCastRoom,0xff);
	}

	tmos_start_task(main_task_ID,EVENT_Low_Power ,500);
#endif
}

void TdxInfo_ClearDisplayBusyProtect(void)
{
	s_tdx_display_busy_protect = 0;
	s_tdx_busy_drop_packets = 0;
	u8IsFirstPackage = TDX_DATA_FIRST;
}

int getPresaveImageIndex(uint16 connHandle){
	int ret;
	
	ret = EraseSaveBlock(connHandle,global_TDX_AES_DATA_INFO.fGroupNum,global_TDX_AES_DATA_INFO.fRoomNum,global_TDX_AES_DATA_INFO.fIsZip);
	if(ret == -1){
		PRINT("WriteAttrCB TDXINFO_SEND_DATA error 00000000000000000000000000000\r\n");
		uint8_t finish_data[] = {10};
		ble_send_tdxInfo(connHandle,finish_data,1,TDXINFO_NOTITY_SEND_RESULT_INFO-1, tdxInfoBind_cccd) ;
		global_DEVICE_STATUS.fWorked =Is_No;
		tmos_start_task(main_task_ID,EVENT_Low_Power ,500);
		return -1;
	}
	return ret;
}

int InitFirstPackage(uint16 connHandle){
	global_DEVICE_STATUS.fInitDriver = Is_Yes;
	global_DEVICE_STATUS.fPackageCnt = 0;
	global_DEVICE_STATUS.fDataSendSuccess = Is_No;
	global_DEVICE_STATUS.fOtaStatus = RTN_OTA_COMMON;
	global_EXTERN_FLASH_INFO.fBlockNum = EXTERN_FLASH_BLOCK_FIRST_ADDR;
	global_EXTERN_FLASH_INFO.fImageIndex = 0;
	global_EXTERN_FLASH_INFO.fZip = global_TDX_AES_DATA_INFO.fIsZip;
	global_DEVICE_STATUS.fRefreshType = global_TDX_AES_DATA_INFO.fOpType;
	global_DEVICE_STATUS.fPackageCount = global_TDX_AES_DATA_INFO.fPackageNum;
	global_DEVICE_STATUS.fIsNeedStandby = 0;
	global_DEVICE_STATUS.fImageDataLen = 0;
	
	InitFlashDriver();
	if(global_TDX_AES_DATA_INFO.fOpType == DEVICE_OP_COMMON){	
		if(global_TDX_AES_DATA_INFO.fScreenType == OP_TYPE_IMG_DIFF_B){
			global_EXTERN_FLASH_INFO.fImageIndex =SCREEN_A_COMMON_INDEX;//SCREEN_B_COMMON_INDEX;
		}
		else{
			global_EXTERN_FLASH_INFO.fImageIndex =SCREEN_B_COMMON_INDEX;//SCREEN_A_COMMON_INDEX;
		}
		// When writing a common image into a shared slot, also save that slot zip so later timed refresh can read it per side
		saveCommonImageZip(global_EXTERN_FLASH_INFO.fImageIndex, global_TDX_AES_DATA_INFO.fIsZip);
	}else{
		global_EXTERN_FLASH_INFO.fImageIndex = getPresaveImageIndex(connHandle);
	}

	clearBoardcastData();
	PRINT("TDX INFO DATA:\r\n");
	PRINT("global_TDX_AES_DATA_INFO.fOpType:%d\r\n",global_TDX_AES_DATA_INFO.fOpType);
	PRINT("global_TDX_AES_DATA_INFO.fScreenType:%d\r\n",global_TDX_AES_DATA_INFO.fScreenType);
	PRINT("global_TDX_AES_DATA_INFO.fPackageNum:%d\r\n",global_TDX_AES_DATA_INFO.fPackageNum);
	PRINT("global_TDX_AES_DATA_INFO.fUserId:0x%08X\r\n", global_TDX_AES_DATA_INFO.fUserId);
	PRINT("global_TDX_AES_DATA_INFO.fIsSecret:%d\r\n",global_TDX_AES_DATA_INFO.fIsSecret);
	PRINT("global_TDX_AES_DATA_INFO.fIsZip:%d\r\n",global_TDX_AES_DATA_INFO.fIsZip);
	PRINT("global_TDX_AES_DATA_INFO.fRoomNum:%d\r\n",global_TDX_AES_DATA_INFO.fRoomNum);
	PRINT("global_TDX_AES_DATA_INFO.fGroupNum:%d\r\n",global_TDX_AES_DATA_INFO.fGroupNum);
	PRINT("TDX INFO DATA END\r\n");

	return 0;
}

int InitOtherPackage(uint16 connHandle, UINT8 *adData, UINT32 dataLen)
{
	int ret ;
	uint8_t notify_buf[6];
	uint16_t notify_len;
	uint8_t notify_retry;
			
	if(global_DEVICE_STATUS.fPackageCnt % 50 == 0)
		PRINT("WriteAttrCB TDXINFO_SEND_DATA fPackageCnt:%d fPackageNum:%d\r\n",global_DEVICE_STATUS.fPackageCnt,global_TDX_AES_DATA_INFO.fPackageNum);


		if(Save256DataToFlash(adData,dataLen,global_EXTERN_FLASH_INFO.fImageIndex,global_EXTERN_FLASH_INFO.fBlockNum) == 1){
			//global_EXTERN_FLASH_INFO.fBlockNum++;
		}
		
		global_DEVICE_STATUS.fPackageCnt++;
		if(global_DEVICE_STATUS.fPackageCnt == global_TDX_AES_DATA_INFO.fPackageNum){
			PRINT("WriteAttrCB TDXINFO_SEND_DATA send finish\r\n");
			//hex_dump(decrypted, decrypted_len);
			u8IsFirstPackage = TDX_DATA_FIRST;
			IsDecryptFlag = Is_No;
			global_DEVICE_STATUS.fPackageCnt=0;
			global_DEVICE_STATUS.fRefreshType = global_TDX_AES_DATA_INFO.fOpType;
			DeInitFlashDriver();
			global_EXTERN_FLASH_INFO.fBlockNum = EXTERN_FLASH_BLOCK_FIRST_ADDR;
					
			global_DEVICE_STATUS.fDataSendSuccess = Is_Yes;
			notitySendEnd(connHandle, 0x01, global_TDX_AES_DATA_INFO.fScreenType,notify_buf, &notify_len);
			//hex_dump(notify_buf, notify_len);
			for(notify_retry = 0; notify_retry < 5; notify_retry++){
				PRINT("TDX send notify 0x01 repeat %d/5\r\n", notify_retry + 1);
				notitySendFunc(connHandle, notify_buf, &notify_len);
				if(notify_retry < 4){
					delay_ms(50);
				}
			}
			if(global_TDX_AES_DATA_INFO.fScreenType != OP_TYPE_IMG_DIFF_A)
			{
				s_tdx_display_busy_protect = 1;
				s_tdx_busy_drop_packets = 0;

				InitFlashDriver();
				mDelayuS(10);
				if(global_TDX_AES_DATA_INFO.fScreenType == OP_TYPE_IMG_DIFF_B){
					PRINT("need refresh ab diffrent @@@@@@@@@@@@@@@@@@@@@@@@\r\n");
					global_DEVICE_STATUS.fImageType = 1;
				}
				else{
					PRINT("need refresh ab same @@@@@@@@@@@@@@@@@@@@@@@@\r\n");
					global_DEVICE_STATUS.fImageType = 0;
				}
				tmos_start_task(main_task_ID,EVENT_Get_Battle_Charge ,100);
				mDelayuS(10);
				DeInitFlashDriver();
			}
		}

}

void notitySendEnd(uint16 connHandle, uint8 status, uint8 type, uint8_t *out_buf, uint16_t *out_len)
{
#if(INK_SCREEN_CUSTOMER != INK_SCREEN_CUSTOMER_TY)
	uint8_t notify_code = 'A';

	if((type == OP_TYPE_IMG_B) || (type == OP_TYPE_SAVE_IMG_B) || (type == OP_TYPE_IMG_DIFF_B)){
		notify_code = 'B';
	}
	else if((type == OP_TYPE_IMG_A) || (type == OP_TYPE_SAVE_IMG_A) || (type == OP_TYPE_IMG_DIFF_A)
		|| (type == OP_TYPE_IMG_AB) || (type == OP_TYPE_SAVE_IMG_AB)){
		notify_code = 'A';
	}
	else{
		notify_code = (global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_B) ? 'B' : 'A';
	}

	*out_len = 6;

	out_buf[0] = 'C';
	out_buf[1] = 'O';
	out_buf[2] = 'D';
	out_buf[3] = 'E';
	out_buf[4] = notify_code;
    out_buf[5] = status;
#else
	*out_len = 1;

	out_buf[0] = status;
#endif
}

void notitySendFunc(uint16 connHandle, uint8_t *out_buf, uint16_t *out_len)
{
	// 修复：带重试机制的notify发送，避免应用层收不到消息
	bStatus_t send_ret;
	uint8_t send_retry = 0;
	const uint8_t SEND_MAX_RETRY = 3;

	if (out_len == NULL) {
        return;
    }
	
	do {
		send_ret = ble_send_tdxInfo(connHandle, out_buf, *out_len, TDXINFO_NOTITY_SEND_RESULT_INFO-1, tdxInfoBind_cccd);
		if (send_ret == SUCCESS) {
			PRINT("Notify send success on attempt %d\r\n", send_retry + 1);
			break;  // 发送成功，退出重�?		}
		}
		else if (send_ret == blePending || send_ret == bleNoResources || send_ret == bleTimeout) {
			PRINT("Notify pending/busy (0x%02x), retry %d/%d\r\n", send_ret, send_retry + 1, SEND_MAX_RETRY);
			mDelayuS(5000);  // 延时5ms等待BLE栈处�?		}
		}
		else if (send_ret == bleNotConnected) {
			PRINT("BLE not connected, stop retry\r\n");
			break;  // 连接断开，停止重�?		}
		}
		else {
			PRINT("Notify failed with error 0x%02x\r\n", send_ret);
		}
		send_retry++;
	} while (send_retry < SEND_MAX_RETRY);

	if (send_ret != SUCCESS) {
		PRINT("WARNING: Notify failed after %d attempts, error=0x%02x\r\n", send_retry, send_ret);
	}
}

/*********************************************************************
 * @fn      notifyDeviceBound
 *
 * @brief   通知前端设备已被其他用户绑定（状态码0x02�? *
 * @param   connHandle - 连接句柄
 *
 * @return  none
 */
void notifyDeviceBound(uint16 connHandle)
{
	uint8_t notify_buf[6];
	uint16_t notify_len = 0;
	uint8_t notify_retry;
	
	PRINT("Notify device bound to another user (status=0x02)\r\n");
	
	// 使用0x02状态码表示设备已被绑定�?x01=成功, 0x02=已绑定）
	notitySendEnd(connHandle, 0x02, global_TDX_AES_DATA_INFO.fScreenType, notify_buf, &notify_len);
	
	for(notify_retry = 0; notify_retry < 3; notify_retry++){
		PRINT("TDX send notify 0x02 repeat %d/3\r\n", notify_retry + 1);
		notitySendFunc(connHandle, notify_buf, &notify_len);
		if(notify_retry < 2){
			delay_ms(50);
		}
	}
}
/*********************************************************************
*********************************************************************/
