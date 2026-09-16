/********************************** (C) COPYRIGHT *******************************
 * File Name          : Peripheral.C
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2018/12/10
 * Description        : ����ӻ�Ӧ�ó��򣬳�ʼ���㲥���Ӳ�����Ȼ��㲥��ֱ������������ͨ���Զ������������
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *******************************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <stdlib.h>
#include "CONFIG.h"
#include "GATTprofile.h"
#include "Peripheral.h"
#include "OTA.h"
#include "OTAprofile.h"
#include "app_cfg.h"
#include "zlib_image_store.h"
#include "commoninfo.h"
#include "release_trace.h"

#include "rledecode.h"

// Ϊ�˵͹��ģ� ��������Ҫ����
//#define DCDC_ENABLE                         TRUE //FALSE
//#define HAL_SLEEP                           TRUE //FALSE
//CLK_OSC32K=2
//BLE_MEMHEAP_SIZE=7*1024
//BLE_BUFF_MAX_LEN=251
//HAL_SLEEP=TRUE
//DCDC_ENABLE=TRUE
UINT8   Ble_CRC;
#define EVT_PERIOD       Is_Off //   Is_On   Is_Off
#if 1
//1s 19uA
// What is the advertising interval when device is discoverable (units of 625us, 80=50ms)
//#define DEFAULT_ADVERTISING_INTERVAL         1600 //  ���� 1 ��
//#define DEFAULT_ADVERTISING_INTERVAL         2096+10 // ���� 500ms ,��û��ʹ��
//#define DEFAULT_ADVERTISING_INTERVAL         (2096+10+24+20)/2 //���� 500ms ,�����
//#define DEFAULT_ADVERTISING_INTERVAL         800-100  //���� 400ms ,�����
//  #define DEFAULT_ADVERTISING_INTERVAL         800-100  //���� 400ms ,�����
//  DCDC_ENABLE=TRUE    ��������� �ڿ��������мӼӺ�
// Limited discoverable mode advertises for 30.72s, and then stops
// General discoverable mode advertises indefinitely
// Minimum connection interval (units of 1.25ms, 6=7.5ms)
//#define DEFAULT_DESIRED_MIN_CONN_INTERVAL      6
//#define DEFAULT_DESIRED_MIN_CONN_INTERVAL_v2   6 // (80)     // �ӿ������ٶ�
#define DEFAULT_DESIRED_MIN_CONN_INTERVAL     6//320
#define DEFAULT_DESIRED_MIN_CONN_INTERVAL_v2  6//320 // (80)     // �ӿ������ٶ�
// Maximum connection interval (units of 1.25ms, 100=125ms)
//#define DEFAULT_DESIRED_MAX_CONN_INTERVAL    100
//#define DEFAULT_DESIRED_MAX_CONN_INTERVAL_v2  100     // �ӿ������ٶ�
#define DEFAULT_DESIRED_MAX_CONN_INTERVAL     100//320
#define DEFAULT_DESIRED_MAX_CONN_INTERVAL_v2  100//320     // �ӿ������ٶ�

// Slave latency to use parameter update
#define DEFAULT_DESIRED_SLAVE_LATENCY        0
// Supervision timeout value (units of 10ms, 100=1s)
#define DEFAULT_DESIRED_CONN_TIMEOUT         100
#endif
/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */

/* flash��������ʱ�洢 */
__attribute__((aligned(8))) uint8_t block_buf[EEPROM_PAGE_SIZE];
#ifdef ENABLE_BOARD_ENCRYPT
__attribute__((aligned(8))) uint8_t block_key_buf[EEPROM_PAGE_SIZE];
#endif
/*********************************************************************
 * EXTERNAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL FUNCTIONS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */
static uint8_t Peripheral_TaskID = 0xff; // Task ID for internal task/event processing

#ifdef ENABLE_BOARD_ENCRYPT
void Save_Key_EEPROM_Flag(uint8_t* new_flag,UINT16 pos,UINT16 Len)
{
	/* ??????,??????? */
	/* ?????? */
	EEPROM_READ(KEY_DATAFLASH_ADD_BAK, (uint32_t *)&block_key_buf[0], EEPROM_PAGE_SIZE);
	for(int i=0; i<Len; i++){
		block_key_buf[i+pos] = new_flag[i];
	}

	EEPROM_ERASE(KEY_DATAFLASH_ADD_BAK, EEPROM_PAGE_SIZE);
	/* ?DataFlash?? */
	EEPROM_WRITE(KEY_DATAFLASH_ADD_BAK, (uint32_t *)&block_key_buf[0], EEPROM_PAGE_SIZE);

	/* ???????,????? */
	/* ????? */
	EEPROM_READ(KEY_DATAFLASH_ADD, (uint32_t *)&block_key_buf[0], EEPROM_PAGE_SIZE);
	for(int i=0; i<Len; i++){
		block_key_buf[i+pos] = new_flag[i];
	}

	/* ????? */
	EEPROM_ERASE(KEY_DATAFLASH_ADD, EEPROM_PAGE_SIZE);
	EEPROM_WRITE(KEY_DATAFLASH_ADD, (uint32_t *)&block_key_buf[0], EEPROM_PAGE_SIZE);
}

void Get_Key_EEPROM_Flag(uint8_t* new_flag,UINT16 pos,UINT16 Len)
{
	EEPROM_READ(KEY_DATAFLASH_ADD_BAK, (uint32_t *)&block_key_buf[0], EEPROM_PAGE_SIZE);
	uint8_t all_ff = 1;
	for(int i=0; i<Len; i++){
		new_flag[i] = block_key_buf[i+pos];
		if(new_flag[i] != 0xFF){
			all_ff = 0;
		}
	}

	if(all_ff){
		/* ????????,??????????? */
		uint8_t main_buf[EEPROM_PAGE_SIZE];
		for(int i=0; i<EEPROM_PAGE_SIZE; i++){
			main_buf[i] = block_key_buf[i];
		}
		
		EEPROM_READ(KEY_DATAFLASH_ADD, (uint32_t *)&block_key_buf[0], EEPROM_PAGE_SIZE);
		uint8_t bak_valid = 0;
		for(int i=0; i<Len; i++){
			new_flag[i] = block_key_buf[i+pos];
			if(new_flag[i] != 0xFF){
				bak_valid = 1;
			}
		}
		/* ??????????,????pos?pos+Len?????????????? */
		if(bak_valid){
			/* ????????????????? */
			for(int i=0; i<Len; i++){
				main_buf[i+pos] = block_key_buf[i+pos];
			}
			EEPROM_ERASE(KEY_DATAFLASH_ADD_BAK, EEPROM_PAGE_SIZE);
			EEPROM_WRITE(KEY_DATAFLASH_ADD_BAK, (uint32_t *)&main_buf[0], EEPROM_PAGE_SIZE);
		}
	}
}
#endif

void Save_EEPROM_Flag(uint8_t* new_flag,UINT16 pos,UINT16 Len)
{
	/* ��ȡ��һ�� */
	EEPROM_READ(OTA_DATAFLASH_ADD, (uint32_t *)&block_buf[0], EEPROM_PAGE_SIZE);
	for(int i=0; i<Len; i++){
		block_buf[i+pos] = new_flag[i];
	}
	EEPROM_ERASE(OTA_DATAFLASH_ADD, EEPROM_PAGE_SIZE);
	/* ���DataFlash */
	EEPROM_WRITE(OTA_DATAFLASH_ADD, (uint32_t *)&block_buf[0], EEPROM_PAGE_SIZE);
}

void Get_EEPROM_Flag(uint8_t* new_flag,UINT16 pos,UINT16 Len)
{
	EEPROM_READ(OTA_DATAFLASH_ADD, (uint32_t *)&block_buf[0], EEPROM_PAGE_SIZE);

	for(int i=0; i<Len; i++){
		new_flag[i] = block_buf[i+pos];
	}
}

void saveCommonImageZip(uint8_t index, uint8_t zip)
{
	uint8_t zip_info[COMMON_IMG_ZIP_INFO_LEN];

	// index=0/1 maps to the two common image slots, each storing its own compression flag
	if(index >= COMMON_IMG_ZIP_INFO_LEN){
		return;
	}

	Get_EEPROM_Flag(zip_info, COMMON_IMG_ZIP_INFO_POSITION, COMMON_IMG_ZIP_INFO_LEN);
	zip_info[index] = zip; /* 0=RAW, 1=legacy RLE, 2=stored zlib */
	Save_EEPROM_Flag(zip_info, COMMON_IMG_ZIP_INFO_POSITION, COMMON_IMG_ZIP_INFO_LEN);
}

uint8_t getCommonImageZip(uint8_t index)
{
	uint8_t zip_info[COMMON_IMG_ZIP_INFO_LEN];
	uint8_t zip;

	// Return 0xFF when this slot has no valid zip yet, so callers keep using the original global fZip
	if(index >= COMMON_IMG_ZIP_INFO_LEN){
		return 0xFF;
	}

	Get_EEPROM_Flag(zip_info, COMMON_IMG_ZIP_INFO_POSITION, COMMON_IMG_ZIP_INFO_LEN);
	zip = zip_info[index];
	if(zip > IMAGE_FLASH_ZLIB){
		return 0xFF;
	}

	return zip;
}

/**
 * @brief  设置“首次开机初始化标志”字节
 * @param  value 标志值（1字节）
 *         建议：0x00 = 已完成初始化，0x01 = 需要初始化（也可按实际需求自定义）
 */
void setFirstBootFlag(uint8_t value)
{
	uint8_t flag[FIRST_BOOT_FLAG_LEN];
	flag[0] = value;
	Save_EEPROM_Flag(flag, FIRST_BOOT_FLAG_POSITION, FIRST_BOOT_FLAG_LEN);
}

/**
 * @brief  读取“首次开机初始化标志”字节
 * @return 标志值（1字节），如果Flash为擦除态(0xFF)，直接返回0xFF
 */
uint8_t getFirstBootFlag(void)
{
	uint8_t flag[FIRST_BOOT_FLAG_LEN];
	Get_EEPROM_Flag(flag, FIRST_BOOT_FLAG_POSITION, FIRST_BOOT_FLAG_LEN);
	return flag[0];
}

/**
 * @brief  设置User ID到flash（4字节）
 * @param  user_id: User ID值（大端序，即高字节在前）
 */
void setUserId(uint32_t user_id)
{
	uint8_t id_bytes[USER_ID_LEN];
	// 大端序存储：高字节在前
	id_bytes[0] = (user_id >> 24) & 0xFF;
	id_bytes[1] = (user_id >> 16) & 0xFF;
	id_bytes[2] = (user_id >> 8) & 0xFF;
	id_bytes[3] = user_id & 0xFF;
	Save_EEPROM_Flag(id_bytes, USER_ID_POSITION, USER_ID_LEN);
}

/**
 * @brief  读取flash中的User ID（4字节）
 * @return User ID值（大端序，即高字节在前）
 */
uint32_t getUserId(void)
{
	uint8_t id_bytes[USER_ID_LEN];
	Get_EEPROM_Flag(id_bytes, USER_ID_POSITION, USER_ID_LEN);
	// 大端序解析：高字节在前
	return ((uint32_t)id_bytes[0] << 24) | 
	       ((uint32_t)id_bytes[1] << 16) | 
	       ((uint32_t)id_bytes[2] << 8) | 
	       id_bytes[3];
}

/**
 * @brief  清除User ID（写全0）
 */
void clearUserId(void)
{
	uint8_t id_bytes[USER_ID_LEN] = {0, 0, 0, 0};
	Save_EEPROM_Flag(id_bytes, USER_ID_POSITION, USER_ID_LEN);
}

/**
 * @brief 保存上次刷屏信息到flash（位压缩版本，支持“局部更新”）
 * @param type 刷屏类型：LAST_REFRESH_TYPE_COMMON(0x00)=普通刷屏, LAST_REFRESH_TYPE_PRESAVE(0x01)=预存刷屏
 * @param group 预存刷屏的group编号（普通刷屏时可填0）
 * @param room 预存刷屏的room编号（普通刷屏时可填0）
 * @param screen_mode 屏幕模式：SCREEN_MODE_SINGLE_A/B/AB_SAME/AB_DIFF
 * @param zip 是否压缩：0/1
 * @param enabled 定时刷屏功能启用标志：0/1
 * @param hours 定时器小时数
 *
 * 约定：
 * - 传入 SAVE_KEEP_U8 / SAVE_KEEP_U16 表示“保持flash里原值不变”
 * - 刷图流程只更新 type/group/room/screen_mode/zip，enabled/hours 必须保持不变
 * - TIME 命令只更新 enabled/hours，其余字段保持不变
 */
void saveLastRefreshInfo(uint8_t type, uint8_t group, uint8_t room, uint8_t screen_mode, uint8_t zip, uint8_t enabled, uint8_t screen_cleared)
{
	// 先读出当前flash记录，避免刷图流程把 enabled 覆盖成0
	uint8_t refresh_info[LAST_REFRESH_Len];
	Get_EEPROM_Flag(refresh_info, LAST_REFRESH_Position, LAST_REFRESH_Len);

	// 如果是全0xFF（擦除态/未初始化），给一套安全默认值，避免误把 enabled 当成1
	if(refresh_info[0] == 0xFF && refresh_info[1] == 0xFF && refresh_info[2] == 0xFF)
	{
		refresh_info[0] = 0; // flags
		refresh_info[1] = 0; // group
		refresh_info[2] = 0; // room
	}

	uint8_t old_flags = refresh_info[0];
	uint8_t old_enabled = (old_flags >> FLAGS_BIT_TIMER_ENABLED) & 0x01;
	uint8_t old_type = (old_flags >> FLAGS_BIT_TYPE_OFFSET) & 0x03;
	uint8_t old_screen_mode = (old_flags >> FLAGS_BIT_SCREEN_MODE_OFFSET) & 0x03;
	uint8_t old_zip = (old_flags >> FLAGS_BIT_ZIP) & 0x01;
	uint8_t old_screen_cleared = (old_flags >> FLAGS_BIT_SCREEN_CLEARED) & 0x01;

	// 应用"局部更新"
	// 如果传入SAVE_KEEP_U8，使用全局变量global_screen_cleared_flag的值
	uint8_t new_enabled = (enabled == SAVE_KEEP_U8) ? old_enabled : (enabled & 0x01);
	uint8_t new_type = (type == SAVE_KEEP_U8) ? old_type : (type & 0x03);
	uint8_t new_screen_mode = (screen_mode == SAVE_KEEP_U8) ? old_screen_mode : (screen_mode & 0x03);
	uint8_t new_zip = (zip == SAVE_KEEP_U8) ? old_zip : (zip & 0x01);
    uint8_t new_screen_cleared = (screen_cleared == SAVE_KEEP_U8) ? old_screen_cleared : (screen_cleared & 0x01);
	uint8_t new_group = (group == SAVE_KEEP_U8) ? refresh_info[1] : group;
	uint8_t new_room = (room == SAVE_KEEP_U8) ? refresh_info[2] : room;

	// 重新打包 flags（bit0=enabled, bit1-2=type, bit3-4=screen_mode, bit5=zip, bit6=screen_cleared）
	uint8_t flags = 0;
	flags |= (new_enabled & 0x01) << FLAGS_BIT_TIMER_ENABLED;
	flags |= (new_type & 0x03) << FLAGS_BIT_TYPE_OFFSET;
	flags |= (new_screen_mode & 0x03) << FLAGS_BIT_SCREEN_MODE_OFFSET;
	flags |= (new_zip & 0x01) << FLAGS_BIT_ZIP;
	flags |= (new_screen_cleared & 0x01) << FLAGS_BIT_SCREEN_CLEARED;

	// 写回3字节：flags/group/room（小时数已写死在代码中）
	refresh_info[0] = flags;
	refresh_info[1] = new_group;
	refresh_info[2] = new_room;

	Save_EEPROM_Flag(refresh_info, LAST_REFRESH_Position, LAST_REFRESH_Len);
	PRINT("Saved refresh info(update): flags=0x%02X, type=%d, group=%d, room=%d, zip=%d, enabled=%d, screen_mode=%d, cleared=%d (fixed hours=%d)\r\n",
		  flags, new_type, new_group, new_room, new_zip, new_enabled, new_screen_mode, new_screen_cleared, REFRESH_TIMER_FIXED_HOURS);
}

/**
 * @brief 只读取定时刷屏功能开关状态和清屏标志（不修改任何全局变量）
 * @param enabled 输出参数：定时刷屏功能启用标志
	 * @param screen_cleared 输出参数：清屏标志（SCREEN_NOT_CLEARED=0 或 SCREEN_ALREADY_CLEARED=1）
	 * 用途：开机时检查是否需要启动定时器，避免修改全局变量影响预存刷屏功能
	 */
	void getRefreshTimerStatus(uint8_t* enabled, uint8_t* screen_cleared)
	{
		// 只读取flags字节，不修改任何全局变量
		uint8_t refresh_info[LAST_REFRESH_Len];
		Get_EEPROM_Flag(refresh_info, LAST_REFRESH_Position, LAST_REFRESH_Len);

		// 全0xFF（擦除态/未初始化）时，返回默认值
		if(refresh_info[0] == 0xFF && refresh_info[1] == 0xFF && refresh_info[2] == 0xFF)
		{
			*enabled = REFRESH_TIMER_DISABLED;
			*screen_cleared = SCREEN_NOT_CLEARED;  // 默认未清屏
			return;
		}

	// 解压flags字节
	uint8_t flags = refresh_info[0];
	*enabled = (flags >> FLAGS_BIT_TIMER_ENABLED) & 0x01;        // bit 0
	*screen_cleared = (flags >> FLAGS_BIT_SCREEN_CLEARED) & 0x01; // bit 6
}

/**
 * @brief 从flash读取上次刷屏信息（位解压版本）
 * @param type 输出参数：刷屏类型
 * @param group 输出参数：group编号
 * @param room 输出参数：room编号
 * @param enabled 输出参数：定时刷屏功能启用标志
 * @param screen_mode 输出参数：屏幕模式
 * 注意：小时数已写死在代码中（REFRESH_TIMER_FIXED_HOURS），不再从flash读取
 * 警告：此函数会修改全局变量（fZip, fImageIndex, fScreenType, fImageType），
 *       开机时请使用 getRefreshTimerEnabled() 只读取开关状态
 */
void getLastRefreshInfo(uint8_t* type, uint8_t* group, uint8_t* room, uint8_t* enabled, uint8_t* screen_mode)
{
	// 读取3字节：flags/group/room（小时数已写死在代码中）
	uint8_t refresh_info[LAST_REFRESH_Len];
	uint8_t base_index;
	Get_EEPROM_Flag(refresh_info, LAST_REFRESH_Position, LAST_REFRESH_Len);

	// 全0xFF（擦除态/未初始化）时，给安全默认值：不开启定时器
	if(refresh_info[0] == 0xFF && refresh_info[1] == 0xFF && refresh_info[2] == 0xFF)
	{
		*enabled = 0;
		*type = LAST_REFRESH_TYPE_NONE;
		*screen_mode = SCREEN_MODE_AB_SAME;
		*group = 0;
		*room = 0;
		global_EXTERN_FLASH_INFO.fZip = 0;
		global_DEVICE_STATUS.fImageType = 0;
		return;
	}

	// 解压flags字节
	uint8_t flags = refresh_info[0];
	*enabled = (flags >> FLAGS_BIT_TIMER_ENABLED) & 0x01;        // bit 0
	*type = (flags >> FLAGS_BIT_TYPE_OFFSET) & 0x03;             // bit 1-2
	*screen_mode = (flags >> FLAGS_BIT_SCREEN_MODE_OFFSET) & 0x03; // bit 3-4
	uint8_t zip = (flags >> FLAGS_BIT_ZIP) & 0x01;               // bit 5
	
	*group = refresh_info[1];
	*room = refresh_info[2];

	// 回填到全局变量（供定时刷屏流程使用）
	global_EXTERN_FLASH_INFO.fZip = zip;
	
	// 根据type和screen_mode恢复index和fScreenType
	switch(*screen_mode){
			case SCREEN_MODE_SINGLE_A:
				base_index = SCREEN_B_COMMON_INDEX; // 1
				global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_A;
				global_DEVICE_STATUS.fImageType = 0; // 普通模式
				break;
			case SCREEN_MODE_SINGLE_B:
				base_index = SCREEN_B_COMMON_INDEX; // 1
				global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_B;
				global_DEVICE_STATUS.fImageType = 0; // 普通模式
				break;
			case SCREEN_MODE_AB_SAME:
				base_index = SCREEN_B_COMMON_INDEX; // 1
				global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_AB;
				global_DEVICE_STATUS.fImageType = 0; // 普通模式
				break;
			case SCREEN_MODE_AB_DIFF:
				// 异显模式：需要刷两次（A面+B面）
				// 设置 fImageType=1，让 EVENT_Get_Battle_Charge 自动执行异显流程
				base_index = SCREEN_B_COMMON_INDEX; // 1（EVENT_Get_Battle_Charge会重设）
				global_DEVICE_STATUS.fImageType = 1; // 异显模式标志
				global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_A; // 从A面开始（EVENT_Get_Battle_Charge会重设）
				break;
			default:
				base_index = SCREEN_B_COMMON_INDEX;
				global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_AB;
				global_DEVICE_STATUS.fImageType = 0;
				break;
		}
	if(*type == LAST_REFRESH_TYPE_COMMON){
		// 普通刷屏：根据screen_mode判断基础index和fScreenType	
		
		
		// 普通刷图：不管内置还是外置flash，都直接用base_index（0或1）
		// 因为正常刷图存储时就是用0或1，没有加偏移（只有预存刷图才加偏移）
		global_EXTERN_FLASH_INFO.fImageIndex = base_index;
		PRINT("Common refresh restore: base_index=%d, fImageIndex=%d\r\n", base_index, global_EXTERN_FLASH_INFO.fImageIndex);
	} else if(*type == LAST_REFRESH_TYPE_PRESAVE){
		// 预存刷屏：通过group/room查找index（由调用方在后续处理）
		// 这里暂不处理，因为需要读取GROUPINFO_Len，调用getPicCurIndex()
		global_EXTERN_FLASH_INFO.fImageIndex = 0; // 临时值，后续会被覆盖
		//global_DEVICE_STATUS.fImageType = 0; // 预存模式
		// 关键修复：预存刷屏固定为AB同显模式，必须设置fScreenType，否则硬件初始化不会执行
		//global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_AB;
	}

	PRINT("Read refresh info: flags=0x%02X, type=%d, group=%d, room=%d, zip=%d, enabled=%d, screen_mode=%d (fixed hours=%d)\r\n",
		  flags, *type, *group, *room, zip, *enabled, *screen_mode, REFRESH_TIMER_FIXED_HOURS);
}

//risc5mcu:
//GAP_UpdateAdvertisingData(�0�20,FALSE�0�2,sizeof(�0�2scanRspData�0�2),scanRspData�0�2);�0�2�0�2//�0�2�0�2�0�2ɨ��Ӧ���
//risc5mcu:
//�����������������������������㲥
void  Stop_advertising(void)
{     
    //�����㲥
    //��Ӧ״̬�ϱ�:GAPROLE_ADVERTISING
    uint8_t advertising_enable = TRUE;
    //GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8_t ), &advertising_enable );
     
    //�رչ㲥
    //  ��Ӧ��״̬�ϱ�:
    // :GAPROLE_WAITING
    // :pEvent->gap.opcode == GAP_END_DISCOVERABLE_DONE_EVENT
    advertising_enable = FALSE;
    GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8_t ), &advertising_enable );
    //Print_I3("Stop_advertising");
}

void  Start_advertising(void)
{     
    uint8_t advertising_enable = TRUE;
    GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8_t ), &advertising_enable );
    //Print_I3("Start_advertising");
}

// GAP GATT Attributes
//static uint8_t attDeviceName[GAP_DEVICE_NAME_LEN] = "Simple Peripheral";
static uint8   attDeviceName[GAP_DEVICE_NAME_LEN] = "TDXeTable-card827";

// OTA IAP VARIABLES
/* OTAͨѶ��֡ */
OTA_IAP_CMD_t iap_rec_data;

/* OTA������� */
uint32_t OpParaDataLen = 0;
uint32_t OpAdd = 0;

/* Flash �������� */
uint32_t EraseAdd = 0;      //������ַ
uint32_t EraseBlockNum = 0; //��Ҫ�����Ŀ���
uint32_t EraseBlockCnt = 0; //�����Ŀ����
uint32_t EraseTmpBlockCnt = 0; //�����Ŀ����

#define ERASE_BLOCK_ONE		10  //ÿ�β���10��
/* FLASH У����� */
uint8_t VerifyStatus = 0;
uint8_t ResultStatus = 0;
/* Trace-only state. It must not participate in the BLE or OTA state machines. */
static UINT8 s_ble_log_connected = Is_No;
static UINT8 s_ota_program_logged = Is_No;

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void Peripheral_ProcessTMOSMsg(tmos_event_hdr_t *pMsg);
static void peripheralStateNotificationCB(gapRole_States_t newState, gapRoleEvent_t *pEvent);
static void performPeriodicTask(void);
static void simpleProfileChangeCB(uint8_t paramID);
void        OTA_IAPReadDataComplete(unsigned char index);
void        OTA_IAPWriteData(unsigned char index, unsigned char *p_data, unsigned char w_len);
void        Rec_OTA_IAP_DataDeal(void);
void        OTA_IAP_SendCMDDealSta(uint8_t deal_status);

// add by_lgp
static void peripheralRssiCB(uint16 connHandle, int8 rssi);
static void peripheralParamUpdateCB(uint16 connHandle, uint16 connInterval,
                                    uint16 connSlaveLatency, uint16 connTimeout);
/*********************************************************************
 * PROFILE CALLBACKS
 */

// GAP Role Callbacks
static gapRolesCBs_t Peripheral_PeripheralCBs = {
    peripheralStateNotificationCB, // Profile State Change Callbacks
    peripheralRssiCB,              // When a valid RSSI is read from controller (not used by application)
    peripheralParamUpdateCB};

// GAP Bond Manager Callbacks
static gapBondCBs_t Peripheral_BondMgrCBs = {
    NULL, // Passcode callback (not used by application)
    NULL  // Pairing / Bonding state Callback (not used by application)
};

// Simple GATT Profile Callbacks
static simpleProfileCBs_t Peripheral_SimpleProfileCBs = {
    simpleProfileChangeCB // Charactersitic value change callback
};

// Simple GATT Profile Callbacks
static OTAProfileCBs_t Peripheral_OTA_IAPProfileCBs = {
    OTA_IAPReadDataComplete, // Charactersitic value change callback
    OTA_IAPWriteData
};

// Callback when the connection parameteres are updated.
void PeripheralParamUpdate(uint16_t connInterval, uint16_t connSlaveLatency, uint16_t connTimeout);
gapRolesParamUpdateCB_t PeripheralParamUpdate_t = NULL;
/*********************************************************************
 * PUBLIC FUNCTIONS
 */

/*********************************************************************
 * @fn      peripheralRssiCB
 *
 * @brief   RSSI callback.
 *
 * @param   connHandle - connection handle
 * @param   rssi - RSSI
 *
 * @return  none
 */
static void peripheralRssiCB(uint16 connHandle, int8 rssi)
{
    Print_I3("RSSI=-%d dB Conn=%x",-rssi,connHandle);
}

/*********************************************************************
 * @fn      peripheralParamUpdateCB
 *
 * @brief   Parameter update complete callback
 *
 * @param   connHandle - connect handle
 *          connInterval - connect interval
 *          connSlaveLatency - connect slave latency
 *          connTimeout - connect timeout
 *
 * @return  none
 */
static void peripheralParamUpdateCB(uint16 connHandle, uint16 connInterval,
                                    uint16 connSlaveLatency, uint16 connTimeout)
{
	bStatus_t  state;
    if(  global_DEVICE_STATUS.fComplete_Bluetooth_data_transmission_flag == Is_Yes) 
    {
        state = GAPRole_TerminateLink(connHandle);
        if(state==SUCCESS)
        {
            Print_I3("Disconnect Ble Success");
            BLE_LOG_TEXT("BLE disconnect request ok\r\n");
        }
        else
        {
            Print_I3("Disconnect Ble Fail");
            FAULT_LOG_TEXT("FAULT ble disconnect request\r\n");
            FAULT_LOG_HEX8("FAULT ble status=", state);
        }
    }
    else
    {
        Print_I3("Update %x connInterval=%x %x %x", connHandle, connInterval,connSlaveLatency,connTimeout);
        BLE_LOG_TEXT("BLE param-update ok\r\n");
        BLE_LOG_HEX32("BLE interval=", connInterval);
        BLE_LOG_HEX32("BLE timeout=", connTimeout);
    }
}

/*********************************************************************
 * @fn      Peripheral_Init
 *
 * @brief   Initialization function for the Peripheral App Task.
 *          This is called during initialization and should contain
 *          any application specific initialization (ie. hardware
 *          initialization/setup, table initialization, power up
 *          notificaiton ... ).
 *
 * @param   task_id - the ID assigned by TMOS.  This ID should be
 *                    used to send messages and set timers.
 *
 * @return  none
 */
void Peripheral_Init()
{
    uint16_t advInt;
	int i = 0;
    
    Peripheral_TaskID = TMOS_ProcessEventRegister(Peripheral_ProcessEvent);

	intBoardCastData();
	Print_I3("222@@@@@@@@@@@@@@@@@@@@@@@@@@Peripheral_Init mac=%x %x %x %x %x %x \n",Mac[0],Mac[1],Mac[2],Mac[3],Mac[4],Mac[5]);

       // Set the GAP Characteristics
    GGS_SetParameter(GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN, attDeviceName);

    // Set advertising interval
    {
        advInt = DEFAULT_ADVERTISING_INTERVAL;
        GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN, advInt);
        advInt = DEFAULT_ADVERTISING_INTERVAL_Max;
        GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX, advInt);
		uint16_t scanInterval = 1600;//3000;//1600;  // 1000ms
		uint16_t scanWindow = 800;//1000;//480;     // 300ms
		GAP_SetParamValue(TGAP_DISC_SCAN_INT, scanInterval);
		GAP_SetParamValue(TGAP_DISC_SCAN_WIND, scanWindow);

		// �������Ӳ�������λ��1.25ms��
		/*uint16_t connMinInterval = 6;   // 7.5ms
		uint16_t connMaxInterval = 8;   // 10ms
		uint16_t connLatency = 0;       // ���ӳ�
		uint16_t supervisionTimeout = 4000;  // ��ʱʱ��4s
		GAPRole_SetParameter(GAPROLE_MIN_CONN_INTERVAL, sizeof(connMinInterval), &connMinInterval);
		GAPRole_SetParameter(GAPROLE_MAX_CONN_INTERVAL, sizeof(connMaxInterval), &connMaxInterval);
		//GAPRole_SetParameter(GAPROLE_SLAVE_LATENCY, sizeof(connLatency), &connLatency);
		//GAPRole_SetParameter(GAPROLE_CONN_TIMEOUT, sizeof(supervisionTimeout), &supervisionTimeout);

		// Enable scan req notify
        GAP_SetParamValue(TGAP_ADV_SCAN_REQ_NOTIFY, ENABLE);*/
    }

	//uint8_t afhEnable = TRUE;
	//GAPRole_SetParameter(TGAP_AFH_CHANNEL_MDOE, sizeof(afhEnable), &afhEnable);
	
    // Setup the GAP Bond Manager
    {
        uint32_t passkey = 0; // passkey "000000"
        uint8_t  pairMode = GAPBOND_PAIRING_MODE_WAIT_FOR_REQ;
        uint8_t  mitm = TRUE;
        uint8_t  ioCap = GAPBOND_IO_CAP_DISPLAY_ONLY;
        uint8_t  bonding = TRUE;
        GAPBondMgr_SetParameter(GAPBOND_PERI_DEFAULT_PASSCODE, sizeof(uint32_t), &passkey);
        GAPBondMgr_SetParameter(GAPBOND_PERI_PAIRING_MODE, sizeof(uint8_t), &pairMode);
        GAPBondMgr_SetParameter(GAPBOND_PERI_MITM_PROTECTION, sizeof(uint8_t), &mitm);
        GAPBondMgr_SetParameter(GAPBOND_PERI_IO_CAPABILITIES, sizeof(uint8_t), &ioCap);
        GAPBondMgr_SetParameter(GAPBOND_PERI_BONDING_ENABLED, sizeof(uint8_t), &bonding);
    }

    // Initialize GATT attributes
    GGS_AddService(GATT_ALL_SERVICES);           // GAP
    GATTServApp_AddService(GATT_ALL_SERVICES);   // GATT attributes
    
    CustomerInfo_AddService();                        // Boe Information Service
    SimpleProfile_AddService(GATT_ALL_SERVICES); // Simple GATT Profile
    OTAProfile_AddService(GATT_ALL_SERVICES);

    // Setup the SimpleProfile Characteristic Values
    {
        uint8_t charValue1 = 1;
        uint8_t charValue2 = 2;
        uint8_t charValue3 = 3;
        uint8_t charValue4[SIMPLEPROFILE_CHAR4_LEN] = "01234567";
        uint8_t charValue5[SIMPLEPROFILE_CHAR5_LEN] = {1, 2, 3, 4, 5};

        SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR1, sizeof(uint8_t), &charValue1);
        SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR2, sizeof(uint8_t), &charValue2);
        SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR3, sizeof(uint8_t), &charValue3);

        SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR4, SIMPLEPROFILE_CHAR4_LEN, charValue4);
        SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR5, SIMPLEPROFILE_CHAR5_LEN, charValue5);
    }

   

    // Register callback with SimpleGATTprofile
    SimpleProfile_RegisterAppCBs(&Peripheral_SimpleProfileCBs);

    //  Register callback with OTAGATTprofile
    OTAProfile_RegisterAppCBs(&Peripheral_OTA_IAPProfileCBs);

    // Setup a delayed profile startup
    tmos_set_event(Peripheral_TaskID, SBP_START_DEVICE_EVT);
}

void PeripheralParamUpdate(uint16_t connInterval, uint16_t connSlaveLatency, uint16_t connTimeout)
{
    DBPRINT("update %d %d %d \n", connInterval, connSlaveLatency, connTimeout);
}


/*********************************************************************
 * @fn      Peripheral_ProcessEvent
 *
 * @brief   Peripheral Application Task event processor.  This function
 *          is called to process all events for the task.  Events
 *          include timers, messages and any other user defined events.
 *
 * @param   task_id  - The TMOS assigned task ID.
 * @param   events - events to process.  This is a bit map and can
 *                   contain more than one event.
 *
 * @return  events not processed
 */
uint16_t Peripheral_ProcessEvent(uint8_t task_id, uint16_t events)
{
    //  VOID task_id; // TMOS required parameter that isn't used in this function
    if(events & SYS_EVENT_MSG)
    {
    	//Print_I3("SYS_EVENT_MSG 1");
        uint8_t *pMsg;

        if((pMsg = tmos_msg_receive(Peripheral_TaskID)) != NULL)
        {
            Peripheral_ProcessTMOSMsg((tmos_event_hdr_t *)pMsg);
            // Release the TMOS message
            tmos_msg_deallocate(pMsg);
        }
        // return unprocessed events
        return (events ^ SYS_EVENT_MSG);
    }

    if(events & SBP_START_DEVICE_EVT)
    {
        //Print_I3("SBP_PERIODIC_EVT 1");
        // Start the Device
        GAPRole_PeripheralStartDevice(Peripheral_TaskID, &Peripheral_BondMgrCBs, &Peripheral_PeripheralCBs);
        // Set timer for first periodic event

        return (events ^ SBP_START_DEVICE_EVT);
    }

    if(events & SBP_PERIODIC_EVT)
    {
    	//Print_I3("SBP_PERIODIC_EVT 2");
        return (events ^ SBP_PERIODIC_EVT);
    }

    //OTA_FLASH_ERASE_EVT
    if(events & OTA_FLASH_ERASE_EVT)
    {
        uint8_t status;

		// ????:????????????
		if (EraseBlockCnt >= EraseBlockNum+1)
		{
			Print_I3("EraseBlockCnt(%d) >= EraseBlockNum(%d), erase already complete\r\n", 
					 (int)EraseBlockCnt, (int)EraseBlockNum);
			global_DEVICE_STATUS.fOtaStatus = RTN_OTA_SUCCESS;
			OTA_LOG_TEXT("OTA erase complete\r\n");
			OTA_IAP_SendCMDDealSta(SUCCESS);
			return (events ^ OTA_FLASH_ERASE_EVT); // ??????
		}

        Print_I3("0000 ERASE:%x N:%d \r\n", (int)(EraseAdd + EraseBlockCnt * FLASH_BLOCK_SIZE), (int)EraseBlockCnt);
        status = FLASH_ROM_ERASE(EraseAdd + EraseBlockCnt * FLASH_BLOCK_SIZE, FLASH_BLOCK_SIZE);

        /* ����ʧ�� */
        if(status != SUCCESS)
        {
            OTA_IAP_SendCMDDealSta(status);
			global_DEVICE_STATUS.fOtaStatus = RTN_OTA_FAILURE;
			FAULT_LOG_TEXT("FAULT ota erase\r\n");
			FAULT_LOG_HEX8("FAULT ota status=", status);
			mDelaymS(10);
            SYS_ResetExecute();
            return (events ^ OTA_FLASH_ERASE_EVT);
        }

        EraseBlockCnt++;
		EraseTmpBlockCnt++;
		if(EraseTmpBlockCnt>=10){
			Print_I3("ERASE block send data EraseTmpBlockCnt: %d\r\n",EraseTmpBlockCnt);
			global_DEVICE_STATUS.fOtaStatus = RTN_OTA_ERASE_NEED;
			OTA_IAP_SendCMDDealSta(status);
			return (events ^ OTA_FLASH_ERASE_EVT);
		}
        /* �������� */
        if(EraseBlockCnt >= EraseBlockNum+1)
        {
            Print_I3("ERASE Complete %d\r\n",status);
			global_DEVICE_STATUS.fOtaStatus = RTN_OTA_SUCCESS;
			OTA_LOG_TEXT("OTA erase complete\r\n");
            OTA_IAP_SendCMDDealSta(status);
            return (events ^ OTA_FLASH_ERASE_EVT);
        }
        return (events);
    }

    // Discard unknown events
    return 0;
}

/*********************************************************************
 * @fn      Peripheral_ProcessTMOSMsg
 *
 * @brief   Process an incoming task message.
 *
 * @param   pMsg - message to process
 *
 * @return  none
 */
static void Peripheral_ProcessTMOSMsg(tmos_event_hdr_t *pMsg)
{
    switch(pMsg->event)
    {
        default:
            break;
    }
}

/*********************************************************************
 * @fn      peripheralStateNotificationCB
 *
 * @brief   Notification from the profile of a state change.
 *
 * @param   newState - new state
 *
 * @return  none
 */
static void peripheralStateNotificationCB(gapRole_States_t newState, gapRoleEvent_t *pEvent)
{
    switch(newState)
    {
        case GAPROLE_STARTED:
            Print_I3("Initialized..");
			BLE_LOG_TEXT("BLE stack started\r\n");
			global_DEVICE_STATUS.fisBleConnect = Is_No;
            break;
        case GAPROLE_ADVERTISING:
            //Print_I3("Advertising..");
			if(s_ble_log_connected == Is_No)
			{
				BLE_LOG_TEXT("BLE advertising\r\n");
			}
            global_DEVICE_STATUS.fisBleConnect = Is_No;
            break;
        case GAPROLE_CONNECTED:
        {
            gapEstLinkReqEvent_t *event = (gapEstLinkReqEvent_t *)pEvent;
            uint16_t              conn_interval = 0;

            conn_interval = event->connInterval;

//            uint8_t devAddr[B_ADDR_LEN]; //!< Device address of link
//            uint16_t connectionHandle;   //!< Connection Handle from controller used to ref the device
//            uint8_t connRole;            //!< Connection formed as Master or Slave
//            uint16_t connInterval;       //!< Connection Interval
//            uint16_t connLatency;        //!< Connection Latency
//            uint16_t connTimeout;        //!< Connection Timeout
//            uint8_t clockAccuracy;       //!< Clock Accuracy
            Print_I3("Connected..");
            printf("Addr=%02x %02x %02x %02x %02x %02x ",event->devAddr[0],event->devAddr[1],event->devAddr[2],event->devAddr[3],event->devAddr[4],event->devAddr[5]);
            printf("connInterval=%d \r\n",event->connInterval);
            printf("connLatency=%d \r\n",event->connLatency);
            printf("connTimeout=%d \r\n",event->connTimeout);
            printf("clockAccuracy=%d \r\n",event->clockAccuracy);
			BLE_LOG_TEXT("BLE connected\r\n");
			BLE_LOG_HEX32("BLE interval=", event->connInterval);
			BLE_LOG_HEX32("BLE timeout=", event->connTimeout);
			s_ble_log_connected = Is_Yes;

			//tmos_start_task(main_task_ID,EVENT_Check_TimeOut ,500);
#ifdef ENABLE_INK_SCREEN_UC8279_800X480_COLOR_3
			//Power_CS_A_on;
#endif
			global_DEVICE_STATUS.fisBleConnect = Is_Yes;
            if(conn_interval > DEFAULT_DESIRED_MAX_CONN_INTERVAL)
            {
                Print_I3("Send Update %d %d",DEFAULT_DESIRED_MIN_CONN_INTERVAL_v2,DEFAULT_DESIRED_MAX_CONN_INTERVAL_v2);
				BLE_LOG_TEXT("BLE param-update request\r\n");
				BLE_LOG_HEX32("BLE param-min=", DEFAULT_DESIRED_MIN_CONN_INTERVAL_v2);
				BLE_LOG_HEX32("BLE param-max=", DEFAULT_DESIRED_MAX_CONN_INTERVAL_v2);
                GAPRole_PeripheralConnParamUpdateReq(event->connectionHandle,
                                                     DEFAULT_DESIRED_MIN_CONN_INTERVAL_v2,
                                                     DEFAULT_DESIRED_MAX_CONN_INTERVAL_v2,
                                                     DEFAULT_DESIRED_SLAVE_LATENCY,
                                                     DEFAULT_DESIRED_CONN_TIMEOUT,
                                                     Peripheral_TaskID);
            }          
            break;
        }
        case GAPROLE_CONNECTED_ADV:
            Print_I3("Connected Advertising..");
			BLE_LOG_TEXT("BLE connected advertising\r\n");
            break;
        case GAPROLE_WAITING:
        {
			Print_I3("GAPROLE_WAITING..");
			if(s_ble_log_connected == Is_Yes)
			{
				BLE_LOG_TEXT("BLE disconnected\r\n");
				s_ble_log_connected = Is_No;
			}
			//global_DEVICE_STATUS.fisBleConnect = Is_No;
            //uint8_t initial_advertising_enable = TRUE;
            //global_DEVICE_STATUS.fBle_connect_or_no=Is_No;
            /*if(  global_DEVICE_STATUS.fComplete_Bluetooth_data_transmission_flag == Is_Yes)
            {
                initial_advertising_enable = FALSE;
                // Set the GAP Role Parameters
                GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &initial_advertising_enable);
                Print_I3("STop ..advertising");
            }
            else
            {
                // Set the GAP Role Parameters
                GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &initial_advertising_enable);
                Print_I3("Start .advertising.");
            }*/
            uint8_t initial_advertising_enable = TRUE;
            GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &initial_advertising_enable);
     		Print_I3("Start .advertising.");
			BLE_LOG_TEXT("BLE restart advertising\r\n");
			if(global_DEVICE_STATUS.fisOtaed == 1)
			{
				OTA_LOG_TEXT("OTA disconnect reset\r\n");
				delay_ms(100);	
				SYS_ResetExecute();
			}
			 //确保蓝牙异常断开后，能正常进入低功耗
			if(global_DEVICE_STATUS.fWorked == Is_No && global_DEVICE_STATUS.fDataSendSuccess == Is_No)
			{
				Print_I3("Start .advertising. Low_Power");
				if(global_DEVICE_STATUS.fDataSendSuccess == Is_No)
				{
					global_DEVICE_STATUS.fWorked = Is_No;
				}
				//tmos_start_task(main_task_ID,EVENT_Check_TimeOut,500);
				tmos_start_task(main_task_ID,EVENT_Low_Power ,3*1600);
			}
        }
        break;

        case GAPROLE_ERROR:
			global_DEVICE_STATUS.fisBleConnect = Is_No;
            Print_I3("Error..%x",newState);
			FAULT_LOG_TEXT("FAULT ble state\r\n");
			FAULT_LOG_HEX8("FAULT ble state-code=", newState);
            break;

        default:
            Print_I3("Other..%x",newState);            
			BLE_LOG_HEX8("BLE other-state=", newState);
            break;
    }
}

/*********************************************************************
 * @fn      performPeriodicTask
 *
 * @brief   Perform a periodic application task. This function gets
 *          called every five seconds as a result of the SBP_PERIODIC_EVT
 *          TMOS event. In this example, the value of the third
 *          characteristic in the SimpleGATTProfile service is retrieved
 *          from the profile, and then copied into the value of the
 *          the fourth characteristic.
 *
 * @param   none
 *
 * @return  none
 */
static void performPeriodicTask(void)
{
    uint8_t valueToCopy[SIMPLEPROFILE_CHAR4_LEN];
    uint8_t stat;

    // Call to retrieve the value of the third characteristic in the profile
    stat = SimpleProfile_GetParameter(SIMPLEPROFILE_CHAR4, valueToCopy);    
    if(stat == SUCCESS)
    {
        SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR4, SIMPLEPROFILE_CHAR4_LEN, valueToCopy);
    }
}

/*********************************************************************
 * @fn      simpleProfileChangeCB
 *
 * @brief   Callback from Profile indicating a value change
 *
 * @param   paramID - parameter ID of the value that was changed.
 *
 * @return  none
 */
static void simpleProfileChangeCB(uint8_t paramID)
{
    uint8_t newValue;

    switch(paramID)
    {
        case SIMPLEPROFILE_CHAR1:
            DBPRINT("profile ChangeCB CHAR1..\n");
            SimpleProfile_GetParameter(SIMPLEPROFILE_CHAR1, &newValue);
            break;

        case SIMPLEPROFILE_CHAR3:
            DBPRINT("profile ChangeCB CHAR3..\n");
            SimpleProfile_GetParameter(SIMPLEPROFILE_CHAR3, &newValue);
            break;

        default:
            // should not reach here!
            break;
    }
}

/*********************************************************************
 * @fn      OTA_IAP_SendData
 *
 * @brief   OTA IAP�������ݣ�ʹ��ʱ����20�ֽ�����
 *
 * @param   p_send_data - �������ݵ�ָ��
 * @param   send_len    - �������ݵĳ���
 *
 * @return  none
 */
void OTA_IAP_SendData(uint8_t *p_send_data, uint8_t send_len)
{
    OTAProfile_SendData(OTAPROFILE_CHAR, p_send_data, send_len);
}

/*********************************************************************
 * @fn      OTA_IAP_SendCMDDealSta
 *
 * @brief   OTA IAPִ�е�״̬����
 *
 * @param   deal_status - ���ص�״̬
 *
 * @return  none
 */
void OTA_IAP_SendCMDDealSta(uint8_t deal_status)
{
    uint8_t send_buf[2];

    send_buf[0] = deal_status;
    send_buf[1] = 0;
    OTA_IAP_SendData(send_buf, 2);
}

/*********************************************************************
 * @fn      OTA_IAP_CMDErrDeal
 *
 * @brief   OTA IAP�쳣�����봦��
 *
 * @return  none
 */
void OTA_IAP_CMDErrDeal(void)
{
    OTA_IAP_SendCMDDealSta(0xfe);
}

/*********************************************************************
 * @fn      SwitchImageFlag
 *
 * @brief   �л�dataflash���ImageFlag
 *
 * @param   new_flag    - �л���ImageFlag
 *
 * @return  none
 */

//  ���ݷ��䣺
//  1-  ��0���ֽ�  --����=1     ---- OTA��־
//  2-  ��1���ֽ�  --����=4     ---- ���յ��ģ�����ͼƬ�Ĺ㲥���ݴ��������Է�һֱ����
//  3-  ��7���ֽ�  --����=15    ---- ��10/����5��ͼƬ,һ���ֽڱ�ʾһ�������ͼƬ��Ч 0XAA �������Ч 0x00 ��15��
//  4-  ��0���ֽ�  --����=1     ---- OTA��־

void SwitchImageFlag(uint8_t new_flag)
{
    uint16_t i;
    uint32_t ver_flag;

	//Print_I3("SwitchImageFlag 000000000000000000000000000");
    /* ��ȡ��һ�� */
    EEPROM_READ(OTA_DATAFLASH_ADD, (uint32_t *)&block_buf[0], EEPROM_PAGE_SIZE);

    /* ������һ�� */
    EEPROM_ERASE(OTA_DATAFLASH_ADD, EEPROM_PAGE_SIZE);

    /* ����Image��Ϣ */
    block_buf[OTA_Position] = new_flag;

    /* ���DataFlash */
    EEPROM_WRITE(OTA_DATAFLASH_ADD, (uint32_t *)&block_buf[0], EEPROM_PAGE_SIZE);
}

/*********************************************************************
 * @fn      DisableAllIRQ
 *
 * @brief   �ر����е��ж�
 *
 * @return  none
 */
void DisableAllIRQ(void)
{
    SYS_DisableAllIrq(NULL);
}

UINT8  Ble_Len;

void Rec_OTA_Data(uint8 *pValue, uint16 len){
	tmos_memcpy((unsigned char *)&iap_rec_data, pValue, len);

	Ble_Len=len;
	Rec_OTA_IAP_DataDeal();
}

/*********************************************************************
 * @fn      
 *
 * @brief   ���յ�OTA���ݰ�����
 *
 * @return  none
 */
UINT16  Debug_info_OTA=Is_Zero;
void Rec_OTA_IAP_DataDeal(void)
{
    Debug_info_OTA++;
    switch(iap_rec_data.other.buf[0])
    {
        /* ��� */
        case CMD_IAP_PROM:
        {
            uint32_t i;
            uint8_t  status;

            //Print_I3("program 000000000000000000000000000");
            global_DEVICE_STATUS.fOtaStatus = RTN_OTA_COMMON;
            OpParaDataLen = iap_rec_data.program.len;
            OpAdd = (uint32_t)(iap_rec_data.program.addr[0]);
            OpAdd |= ((uint32_t)(iap_rec_data.program.addr[1]) << 8);
            OpAdd = OpAdd * 16;
            OpAdd += IMAGE_A_SIZE;

            if(Debug_info_OTA<=Is_Five)
            {
               printf("program:%x:%d:%d\r\n", (int)OpAdd, (int)OpParaDataLen,Ble_Len);
            }
			if(s_ota_program_logged == Is_No)
			{
				OTA_LOG_TEXT("OTA program begin\r\n");
				OTA_LOG_HEX32("OTA program-address=", OpAdd);
				OTA_LOG_HEX32("OTA program-length=", OpParaDataLen);
				s_ota_program_logged = Is_Yes;
			}
			//printf("program:%x:%d:%d\r\n", (int)OpAdd, (int)OpParaDataLen,Ble_Len);
			//hex_dump(iap_rec_data.program.buf,OpParaDataLen);

            /* ��ǰ��ImageA��ֱ�ӱ�� */
            status = FLASH_ROM_WRITE(OpAdd, iap_rec_data.program.buf, (uint16_t)OpParaDataLen);
            if(status)
            {
                Print_I3("IAP_PROM err");
				FAULT_LOG_TEXT("FAULT ota program\r\n");
				FAULT_LOG_HEX8("FAULT ota status=", status);
				ResultStatus = 1;
            }
            OTA_IAP_SendCMDDealSta(status);
            break;
        }
        /* ���� -- ������������������ */
        case CMD_IAP_ERASE:
        {
            uint32_t newEraseAdd;
            uint32_t newEraseBlockNum;
            
            Disable_GPIO_IRQ();        
            //Print_I3("erase 000000000000000000000000000");
            OpAdd = (uint32_t)(iap_rec_data.erase.addr[0]);
            OpAdd |= ((uint32_t)(iap_rec_data.erase.addr[1]) << 8);
            OpAdd = OpAdd * 16;
            OpAdd += IMAGE_A_SIZE;

			global_DEVICE_STATUS.fOtaStatus = RTN_OTA_COMMON;
            newEraseBlockNum = (uint32_t)(iap_rec_data.erase.block_num[0]);
            newEraseBlockNum |= ((uint32_t)(iap_rec_data.erase.block_num[1]) << 8);
            newEraseAdd = OpAdd;
            
            // ???????????:??????????????
            if(newEraseAdd != EraseAdd || newEraseBlockNum != EraseBlockNum)
            {
                // ???:???????
                EraseAdd = newEraseAdd;
                EraseBlockNum = newEraseBlockNum;
                EraseBlockCnt = 0;
                EraseTmpBlockCnt = 0;
				s_ota_program_logged = Is_No;
                Print_I3("New erase task: addr=%x, blocks=%d\r\n", (int)EraseAdd, (int)EraseBlockNum);
				OTA_LOG_TEXT("OTA erase begin\r\n");
				OTA_LOG_HEX32("OTA erase-address=", EraseAdd);
				OTA_LOG_HEX32("OTA erase-blocks=", EraseBlockNum);
            }
            else
            {
                // ????:???????(??????CMD_IAP_ERASE???)
                // ??? EraseBlockCnt,??????
                EraseTmpBlockCnt = 0;  // ?????????,?????10?
            }

            /* ����ͷ��ڲ�������0 */
            VerifyStatus = 0;
			ResultStatus = 0;
            if(Debug_info_OTA<=Is_Five)
            {
              	printf("erase %x:%d:%d:%d\r\n",(int)OpAdd, (int)EraseBlockNum,Ble_Len,EraseBlockCnt);
            }

            if(EraseAdd < IMAGE_B_START_ADD || (EraseAdd + (EraseBlockNum - 1) * FLASH_BLOCK_SIZE) > IMAGE_IAP_START_ADD)
            {                
                OTA_IAP_SendCMDDealSta(0xFF);
                Print_I3("erase -- er ");
				FAULT_LOG_TEXT("FAULT ota erase-range\r\n");
				FAULT_LOG_HEX32("FAULT ota erase-address=", EraseAdd);
                printf("EraseAdd=%x  %x\r\n",EraseAdd,IMAGE_B_START_ADD);
                printf("EraseAdd=%x  %x\r\n",(EraseAdd + (EraseBlockNum - 1) * FLASH_BLOCK_SIZE),IMAGE_IAP_START_ADD);
            }
            else
            {
                /* �������� */
                tmos_set_event(Peripheral_TaskID, OTA_FLASH_ERASE_EVT);
                //printf("��������\r\n");
            }
            break;
        }
        /* У�� */
        case CMD_IAP_VERIFY:
        {
            uint32_t i;
            uint8_t  status = 0;

            //Print_I3("verity 000000000000");
            global_DEVICE_STATUS.fOtaStatus = RTN_OTA_COMMON;
            OpParaDataLen = iap_rec_data.verify.len;

            OpAdd = (uint32_t)(iap_rec_data.verify.addr[0]);
            OpAdd |= ((uint32_t)(iap_rec_data.verify.addr[1]) << 8);
            OpAdd = OpAdd * 16;
            OpAdd += IMAGE_A_SIZE;
            
            if(Debug_info_OTA<=Is_Five)
            {
                printf("verity %x:%d:%d      \r\n", (int)OpAdd, (int)OpParaDataLen,Ble_Len);
            }

			//printf("verity %x:%d:%d      \r\n", (int)OpAdd, (int)OpParaDataLen,Ble_Len);
			//hex_dump(iap_rec_data.verify.buf,OpParaDataLen);
            /* ��ǰ��ImageA��ֱ�Ӷ�ȡImageBУ�� */
            status = FLASH_ROM_VERIFY(OpAdd, iap_rec_data.verify.buf, OpParaDataLen);
            if(status)
            {
                Print_I3("IAP_VERIFY err");
				FAULT_LOG_TEXT("FAULT ota verify\r\n");
				FAULT_LOG_HEX8("FAULT ota status=", status);
				ResultStatus = 1;
            }
            VerifyStatus |= status;
            OTA_IAP_SendCMDDealSta(VerifyStatus);
            break;
        }
        /* ��̽��� */
        case CMD_IAP_END:
        {
            Print_I3("iap end:%d\r\n",Ble_Len);
			OTA_LOG_TEXT("OTA switch-image\r\n");
            
            Debug_info_OTA=Is_Zero;

            /* ��ǰ����ImageA */
            /* �رյ�ǰ����ʹ���жϣ����߷���һ��ֱ��ȫ���ر� */
            DisableAllIRQ();

            /* �޸�DataFlash���л���ImageIAP */
            SwitchImageFlag(IMAGE_IAP_FLAG);
			initPicSave(SCREEN_CLEAN_ALL,0xff,0xff);

            /* �ȴ���ӡ��� ����λ*/
            mDelaymS(10);
			OTA_LOG_TEXT("OTA reboot pending\r\n");
            SYS_ResetExecute();
            break;
        }
        case CMD_IAP_INFO:
        {
            uint8_t send_buf[20];

            Print_I3("send iap info :%d\r\n      ",Ble_Len);
			OTA_LOG_TEXT("OTA info request\r\n");
			OTA_LOG_HEX32("OTA image-size=", IMAGE_SIZE);
			OTA_LOG_HEX32("OTA block-size=", FLASH_BLOCK_SIZE);
            
            /* IMAGE FLAG */
            send_buf[0] = IMAGE_B_FLAG;

            /* IMAGE_SIZE */
            send_buf[1] = (uint8_t)(IMAGE_SIZE & 0xff);
            send_buf[2] = (uint8_t)((IMAGE_SIZE >> 8) & 0xff);
            send_buf[3] = (uint8_t)((IMAGE_SIZE >> 16) & 0xff);
            send_buf[4] = (uint8_t)((IMAGE_SIZE >> 24) & 0xff);

            /* BLOCK SIZE */
            send_buf[5] = (uint8_t)(FLASH_BLOCK_SIZE & 0xff);
            send_buf[6] = (uint8_t)((FLASH_BLOCK_SIZE >> 8) & 0xff);

            send_buf[7] = CHIP_ID&0xFF;
            send_buf[8] = (CHIP_ID<<8)&0xFF;
            /* ����Ҫ������ */

            /* ������Ϣ */
            OTA_IAP_SendData(send_buf, 20);
            break;
        }
		case CMD_IAP_PROM_END:
		case CMD_IAP_VERIFY_END:	
        {
            Print_I3("CMD_IAP_PROM_END CMD_IAP_VERIFY_END 00000000000000 ResultStatus:%d",ResultStatus);
			OTA_LOG_TEXT("OTA stage complete\r\n");
			OTA_LOG_HEX8("OTA result=", ResultStatus);
            if(ResultStatus == 1){
				global_DEVICE_STATUS.fOtaStatus = RTN_OTA_FAILURE;
				FAULT_LOG_TEXT("FAULT ota result\r\n");
				/* �ȴ���ӡ��� ����λ*/
	            mDelaymS(10);
				OTA_LOG_TEXT("OTA reset pending\r\n");
	            SYS_ResetExecute();
            }else{
            	global_DEVICE_STATUS.fOtaStatus = RTN_OTA_SUCCESS;
            }
            break;
        }
        default:
        {
            Print_I3("OTA_IAP_CMDErrDeal:%d=[%s]",Ble_Len,iap_rec_data);
			FAULT_LOG_TEXT("FAULT ota command\r\n");
			FAULT_LOG_HEX8("FAULT ota command=", iap_rec_data.other.buf[0]);
            OTA_IAP_CMDErrDeal();
            break;
        }
    }
}    

/*********************************************************************
 * @fn      OTA_IAPReadDataComplete
 *
 * @brief   OTA ���ݶ�ȡ��ɴ���
 *
 * @param   index   - OTA ͨ�����
 *
 * @return  none
 */
void OTA_IAPReadDataComplete(unsigned char index)
{
	Print_I3("Ble Send=%x\r\n",index);
    //Ble_Send_over=Is_Ready;
}

/*********************************************************************
 * @fn      OTA_IAPWriteData
 *
 * @brief   OTA ͨ�����ݽ�����ɴ���
 *
 * @param   index   - OTA ͨ�����
 * @param   p_data  - д�������
 * @param   w_len   - д��ĳ���
 *
 * @return  none
 */

// Get info
//[OTA_IAPWriteData:920] index=0x0,w_len=20 Ble_Data_type=3
//0x84 0x12 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 
//[:845] ������Ϣ       ����Ҫ������        CMD_IAP_INFO
//IAP_INFO 
//OTA ���ݶ�ȡ��ɴ��� OTA Send Comp index=0

//#define CMD_IAP_PROM           0x80               // IAP�������

//#define CMD_IAP_ERASE          0x81               // IAP��������
//#define CMD_IAP_VERIFY         0x82               // IAPУ������
//#define CMD_IAP_END            0x83               // IAP������־
//#define CMD_IAP_INFO           0x84               // IAP��ȡ�豸��Ϣ

// 0x84,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
// 0x81,0x00,0x00,0x01,0x29,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
//[:801] ���� -- ������������������
// 0x83,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
//            [OTA_IAPWriteData:996] I=0x0,L=20 T=1
//0x84,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
//[:888] ������Ϣ       ����Ҫ������        CMD_IAP_INFO
//IAP_INFO 
//OTA ���ݶ�ȡ��ɴ��� OTA Send Comp index?[0m[OTA_IAPWriteData:996] I=0x0,L=20 T=1
//0x81,0x00,0x00,0x01,0x29,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
//[:821] ERASE start:00037000 num:41
//[:834] ��������
// 244
//static int ota1_data_len = 0;

void OTA_IAPWriteData(unsigned char index, unsigned char *p_data, unsigned char w_len)
{
    unsigned char  rec_len;
    unsigned char *rec_data;
    UINT16 ulI;

    Ble_Len=w_len;
    Print_I3("Get Ble Len=%d\r\n",w_len);
	global_DEVICE_STATUS.fisHaveData= Is_Yes;
	/*if(p_data[0] == 0x80){
		ota1_data_len++;
		PRINT("OTA_IAPWriteData ota_data_len:%d\r\n",ota1_data_len);
	}
	hex_dump(p_data, w_len);*/
	WWDG_SetCounter(0);
	Print_I3("Get 00 Ble Len=%d\r\n",w_len);
   	//Print_I3("I=0x%x,L=%d T=%x",index,w_len,Ble_Data_type);
   	rec_len = w_len;
   	rec_data = p_data;
   	tmos_memcpy((unsigned char *)&iap_rec_data, rec_data, rec_len);
   
	Rec_OTA_IAP_DataDeal();
}


