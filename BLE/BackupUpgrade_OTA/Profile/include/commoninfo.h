#ifndef BOEINFO_H
#define BOEINFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CONFIG.h"
#include "app_cfg.h"
#include "epd_busy.h"

// internel flash
#ifdef ENABLE_BOARD_ENCRYPT
#define KEY_Position  				0
#define KEY_Len       				16

#define KEY_BURN_Position  			KEY_Position+KEY_Len
#define KEY_BURN_Len       			1

#define Max_KEY_RW_Flash			KEY_Len+KEY_BURN_Len
#endif
#define OTA_Position  				0
#define OTA_Len       				1

#define MAC_Position  				OTA_Position+OTA_Len
#define MAC_Len     				6

#define PHONEID_Position  			MAC_Position+MAC_Len
#define PHONEID_Len       			19

#define BONDED_Position  			PHONEID_Position+PHONEID_Len
#define BONDED_Len       			1

#define GROUPINFO_Position  		BONDED_Position+BONDED_Len
#define GROUPINFO_Len       		100 //index group room exitflag zip 20 pic 5X20

#define VERINFO_Position  			GROUPINFO_Position+GROUPINFO_Len
#define VERINFO_Len       			2 //index group room exitflag

#define WORKMODE_Position  			VERINFO_Position+VERINFO_Len
#define WORKMODE_Len       			1

#define ADC_Position  				WORKMODE_Position+WORKMODE_Len
#define ADC_Len       				1

#define BOARDCAST_Position  		ADC_Position+ADC_Len
#define BOARDCAST_Len       		13  // 13字节以支持新的广播格式（如G@!#11C%2^`e{）

#define BOARDLEN_Position  			BOARDCAST_Position+BOARDCAST_Len
#define BOARDLEN_Len       			1
// 上次刷屏信息（用于定时刷屏恢复）
#define LAST_REFRESH_Position  		BOARDLEN_Position+BOARDLEN_Len
#define LAST_REFRESH_Len       		3  // 1字节flags(压缩) + 1字节group + 1字节room（小时数写死在代码中）

// 首次开机初始化标志（1字节）
#define FIRST_BOOT_FLAG_POSITION  	LAST_REFRESH_Position+LAST_REFRESH_Len
#define FIRST_BOOT_FLAG_LEN       	1

// User ID（4字节）
#define USER_ID_POSITION  			FIRST_BOOT_FLAG_POSITION+FIRST_BOOT_FLAG_LEN
#define USER_ID_LEN       			4

// Store one zip flag for each common image slot (0/1), so timed AB-diff refresh can read them separately
#define COMMON_IMG_ZIP_INFO_POSITION	USER_ID_POSITION+USER_ID_LEN
#define COMMON_IMG_ZIP_INFO_LEN       	2

#define Max_RW_Flash  				(OTA_Len+MAC_Len+PHONEID_Len+BONDED_Len+GROUPINFO_Len+VERINFO_Len+WORKMODE_Len+ADC_Len+BOARDCAST_Len+BOARDLEN_Len+LAST_REFRESH_Len+FIRST_BOOT_FLAG_LEN+USER_ID_LEN+COMMON_IMG_ZIP_INFO_LEN)
//internel flash end

// 首次开机特征值（例如：0x66）
#define FIRST_BOOT_FLAG_VALUE		0x66

// 定时刷屏固定小时数（写死在代码中，不再通过蓝牙命令配置）
#if (defined(ENABLE_SCREEN_COLOR_2))
#define REFRESH_TIMER_FIXED_HOURS	24*5  // 两色固定为5天
#else
#define REFRESH_TIMER_FIXED_HOURS	24*15  // 其他固定为15天
#endif


#define DEVICE_BONDED  				1
#define DEVICE_NOBONDED       		0

#define DEVICE_MODE_HIGH  			1
#define DEVICE_MODE_LOW       		0

#define DEVICE_OP_COMMON  			0
#define DEVICE_OP_COMMON_PRESAVE  	1
#define DEVICE_OP_PRESAVE    		2

// 上次刷屏类型标志（用于flash存储）
#define LAST_REFRESH_TYPE_COMMON	0x00  // 普通刷屏
#define LAST_REFRESH_TYPE_PRESAVE	0x01  // 预存刷屏
#define LAST_REFRESH_TYPE_NONE		0xFF  // 未初始化

// 定时刷屏功能启用标志（用于flash存储）
#define REFRESH_TIMER_DISABLED		0x00  // 功能关闭
#define REFRESH_TIMER_ENABLED		0x01  // 功能开启

// flags字节的位定义（用于压缩存储）
#define FLAGS_BIT_TIMER_ENABLED		0     // bit 0: 定时器启用标志
#define FLAGS_BIT_TYPE_OFFSET		1     // bit 1-2: 刷屏类型(占2位)
#define FLAGS_BIT_SCREEN_MODE_OFFSET 3    // bit 3-4: 屏幕模式(占2位)
#define FLAGS_BIT_ZIP				5     // bit 5: 是否压缩
#define FLAGS_BIT_SCREEN_CLEARED	6     // bit 6: 是否已清屏标志（0=需要清屏/默认，1=已清屏）

#define FLAGS_MASK_TIMER_ENABLED	(1 << FLAGS_BIT_TIMER_ENABLED)           // 0x01
#define FLAGS_MASK_TYPE				(0x03 << FLAGS_BIT_TYPE_OFFSET)          // 0x06
#define FLAGS_MASK_SCREEN_MODE		(0x03 << FLAGS_BIT_SCREEN_MODE_OFFSET)   // 0x18
#define FLAGS_MASK_ZIP				(1 << FLAGS_BIT_ZIP)                     // 0x20
#define FLAGS_MASK_SCREEN_CLEARED	(1 << FLAGS_BIT_SCREEN_CLEARED)          // 0x40

// 清屏状态定义
#define SCREEN_NOT_CLEARED			0x00  // 未清屏（默认值，复位后）
#define SCREEN_ALREADY_CLEARED		0x01  // 已清屏

// 屏幕显示模式（2位，用于flash存储和恢复）
#define SCREEN_MODE_SINGLE_A		0  // 单刷A面
#define SCREEN_MODE_SINGLE_B		1  // 单刷B面
#define SCREEN_MODE_AB_SAME			2  // AB同显（相同内容）
#define SCREEN_MODE_AB_DIFF			3  // AB异显（不同内容）

// saveLastRefreshInfo 的"保持原值"标志
#define SAVE_KEEP_U8				0xFF
#define SAVE_KEEP_U16				0xFFFF  // 保留定义，但不再使用（小时数已写死）

#define DEVICE_REFRESH_AB_SAME  	2
#define DEVICE_PRE_SAVE  			1
#define DEVICE_CLEAN_SCREEN      	0

#ifdef ENABLE_SCREEN_COLOR_2
#define SCREEN_COLOR_WHITE			0xFF
#endif
#ifdef ENABLE_SCREEN_COLOR_3
#define SCREEN_COLOR_WHITE			0x00
#define SCREEN_COLOR_RED			0x11
#define SCREEN_COLOR_BLACK			0xFF
#if (defined(ENABLE_INK_SCREEN_UC8279_800X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79686BB_800X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_UC8179_800X480_COLOR_3))
#define SCREEN_BLACK_WHITE_COLOR_3_MAX		(800*480)/8		//800X480 SCREEN MAX DATA
#define SCREEN_RED_COLOR_3_MAX				(800*480)/8*2		//800X480 SCREEN MAX DATA
#endif
#ifdef ENABLE_INK_SCREEN_SSD1677_960X640_COLOR_3
#define SCREEN_BLACK_WHITE_COLOR_3_MAX		153600		
#define SCREEN_RED_COLOR_3_MAX				153600*2	
#endif
#if (defined(ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79686BB_1360X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3))
#define SCREEN_BLACK_WHITE_COLOR_3_MAX		40800		
#define SCREEN_RED_COLOR_3_MAX				40800*2	
#endif
#ifdef ENABLE_INK_SCREEN_SSD1683_272X792_COLOR_3
#define SCREEN_BLACK_WHITE_COLOR_3_MAX		13600		
#define SCREEN_RED_COLOR_3_MAX				13600*2		
#endif
#if (defined(ENABLE_INK_SCREEN_SSD1863_400X300_COLOR_3)) || (defined(ENABLE_INK_SCREEN_UC8276_400X300_COLOR_3))
#define SCREEN_BLACK_WHITE_COLOR_3_MAX		(400*300)/8		//400*300 SCREEN MAX DATA
#define SCREEN_RED_COLOR_3_MAX				(400*300)/8*2		//400*300 SCREEN MAX DATA
#endif
#endif
#ifdef ENABLE_SCREEN_COLOR_4
#define SCREEN_COLOR_WHITE			0x55
#endif
#ifdef ENABLE_SCREEN_COLOR_6
#define SCREEN_COLOR_BLACK			0x00
#define SCREEN_COLOR_WHITE			0x11
#define SCREEN_COLOR_YELLOW			0x22
#define SCREEN_COLOR_RED			0x33
#define SCREEN_COLOR_BLUE			0x55
#define SCREEN_COLOR_GREEN			0x66
#endif

#define SCREEN_TYPE_IMG_A			1		//IMAGE A PAGE	
#define SCREEN_TYPE_IMG_B			2		//IMAGE B PAGE
#define SCREEN_TYPE_IMG_AB			3		//IMAGE AB PAGE

#define SCREEN_TYPE_COLOR_3			0		//3 color
#define SCREEN_TYPE_COLOR_OTHER		1		//other color

#define SCREEN_DATA_START					0				//DATA SEND START
#define SCREEN_800X480_COLOR_3_MAX			(800*480)/8		//800X480 SCREEN MAX DATA
#define SCREEN_800X480_COLOR_6_MAX			(800*480)/2		//800X480 SCREEN MAX DATA
#define SCREEN_800X480_COLOR_4_MAX			96000
#define SCREEN_960X640_COLOR_4_MAX			153600
#define SCREEN_400X300_COLOR_4_MAX			30000
#define SCREEN_400X300_COLOR_3_MAX			30000
#define SCREEN_800X480_COLOR_2_MAX			48000
#define SCREEN_272X792_COLOR_2_HALF_MAX	13600
#define SCREEN_272X792_COLOR_2_MAX			27200
#define SCREEN_272X792_COLOR_3_MAX			13600*2
#define SCREEN_272X792_COLOR_4_MAX			13600*2
#define SCREEN_960X640_COLOR_3_MAX			153600*2
#define SCREEN_1360X480_COLOR_3_MAX			40800*2
#define SCREEN_1360X480_COLOR_4_MAX			163200/2   //4色主副屏需要分主副屏
#define SCREEN_1280X600_COLOR_4_MAX			192000/2
#define SCREEN_1024X600_COLOR_6_MAX			153600


#define PRESAVE_GROUP_SAVE_FLAG				0xAA	//group save flag
#define PRESAVE_POSITION_COUNT  			5

#define SCREEN_CLEAN_ALL						0		//CLEAN PRE ALL
#define SCREEN_CLEAN_ROOM						1		//CLEAN ROOM
#define SCREEN_CLEAN_ROOM_AND_GROUP				2		//CLEAN ROOM AND GROUP
#define SCREEN_CLEAN_SCRREN_ALL					3		//CLEAN PRE ALL
#define SCREEN_CLEAN_SCRREN_ROOM				4		//CLEAN ROOM
#define SCREEN_CLEAN_SCRREN_ROOM_AND_GROUP		5		//CLEAN ROOM AND GROUP
#define SCREEN_PRESAVE_OP						6		//presave

#define BOARD_CMD_LEN_4							4		//4
#define BOARD_CMD_LEN_5							5		//5
#define BOARD_CMD_LEN_6							6		//6

#define RTN_OTA_COMMON					80		//COMMON	
#define RTN_OTA_SUCCESS					84		//SUCCESS	
#define RTN_OTA_FAILURE					85		//FAILURE
#define RTN_OTA_ERASE_NEED				86		//NEED send data

#define	IS_NEED_DECMPRESS    			(1)
#define IS_NONEED_DECMPRESS    			(0)

#define	IS_HOST    						(0)
#define IS_SLAVE    					(1)

#define SCREEN_A_COMMON_INDEX			0
#define SCREEN_B_COMMON_INDEX			1

#define EXTERN_FLASH_SAVE_START_ADDR	2

struct _BOE_DEVICE_STATUS {
	unsigned char  fWorked;				//brush or not 1:yes 0:no 
	unsigned int   fPackageCnt;
	unsigned int   fPackageCount;
	unsigned char  fComplete_Bluetooth_data_transmission_flag;
	unsigned char  fIsBonded[1];
	unsigned char  fisOtaed;
	unsigned char  fScreenType;	
	unsigned char  fInitDriver;			//is need init driver 1:yes  0:no	
	unsigned char  fDataSendSuccess;
	unsigned char  fBoardCastType;	
	unsigned char  fBoardCastGroup;	
	unsigned char  fBoardCastRoom;	
	unsigned char  fAdcValue;	
	unsigned char  fIsCharg;
	unsigned char  fSystemTimeOut;
	unsigned char  fRefreshType;		//0:commom  1:pre save
	unsigned char  fIsNeedStandby;		//0:no need  1:need standby
	unsigned char  fisBleConnect;  		//0:disconnect  1:connect
	unsigned char  fisHaveData;			//0:no  1:yes
	unsigned char  fisHost;				//0:no  1:yes
	unsigned char  fisHardWareVer;		//0:old  1:new
	unsigned char  fisVaildDevice;		//0:invaild  1:vaild
	unsigned char  fisBurnId;			//0:no  1:yes
	unsigned char  fWillReboot;            //0:no  1:yes, prevent broadcast saving before reboot
	unsigned int   fFlashType;			//0:interal 1:extern
	unsigned int   fImageDataLen;			//0:interal 1:extern
	unsigned int   fImageType;			//0:interal 1:extern
	int	fOtaStatus;
};

struct _BOE_EXTERN_FLASH_INFO {
	unsigned char  fImageIndex;				// save image index 
	unsigned int   fBlockNum;				// Save Block num
	unsigned int   fZip;
};

__attribute__((weak))
void hex_dump(uint8_t* data,uint32_t length) {
    for(uint32_t i=0;i<length;i++) {
		if(i%20 == 0){
			PRINT("\r\n");
		}
        PRINT("0x%02x, ",*data);
        data++;
    }
    PRINT("\r\n");
}

extern struct _BOE_DEVICE_STATUS global_DEVICE_STATUS;
extern struct _BOE_EXTERN_FLASH_INFO global_EXTERN_FLASH_INFO;

// 定时刷屏相关变量
extern uint32_t global_refresh_timer_interval; // 定时刷屏的时间间隔(系统tick，1tick=625us)

/*
 * BoeInfo_AddService- Initializes the Device Information service by registering
 *          GATT attributes with the GATT server.
 *
 */
extern bStatus_t CustomerInfo_AddService(void);
extern void intBoardCastData();

extern void Save_EEPROM_Flag(uint8_t* new_flag,UINT16 pos,UINT16 Len);
extern void Get_EEPROM_Flag(uint8_t* new_flag,UINT16 pos,UINT16 Len);
extern void saveCommonImageZip(uint8_t index, uint8_t zip);
extern uint8_t getCommonImageZip(uint8_t index);
// 首次开机初始化标志的读写接口
extern void setFirstBootFlag(uint8_t value);
extern uint8_t getFirstBootFlag(void);
// User ID的读写接口
extern void setUserId(uint32_t user_id);
extern uint32_t getUserId(void);
extern void clearUserId(void);  // 清除user_id（写全0）
// 说明：
// - 普通刷图/预存刷图结束时：只更新 type/group/room/screen_mode/zip，enabled/screen_cleared 使用 SAVE_KEEP_U8 保持不变
// - TIME 命令：只更新 enabled，其余字段使用 SAVE_KEEP_U8 保持不变
// 注意：小时数已写死在代码中（REFRESH_TIMER_FIXED_HOURS），不再通过参数传递
extern void saveLastRefreshInfo(uint8_t type, uint8_t group, uint8_t room, uint8_t screen_mode, uint8_t zip, uint8_t enabled, uint8_t screen_cleared);
extern void getRefreshTimerStatus(uint8_t* enabled, uint8_t* screen_cleared);  // 只读取开关状态和清屏标志，不修改全局变量（用于开机检查）
extern void getLastRefreshInfo(uint8_t* type, uint8_t* group, uint8_t* room, uint8_t* enabled, uint8_t* screen_mode);
extern uint8_t global_screen_cleared_flag;  // 清屏标志全局变量（在peripheral_main.c中定义）
extern void clearBoardcastData();
extern void P_scanBondRspData(UINT8 bond_status);
extern void P_scanModeRspData(UINT8 work_status);
extern void P_scanAdcRspData(UINT8 work_status);
extern void P_scanPreNumRspData(UINT8 pre_num);
extern void P_scanIsChargeRspData();
extern int getPicSaveIndex(unsigned char *groupinfo, int group, int room);
extern int getPicCurIndex(unsigned char *groupinfo, int group, int room);
extern int getPicSaveNum();
extern void initPicSave(int type, int room, int group);
extern void setWorkMode(int mode);
extern int preSaveDisplayColor(uint8_t group, uint8_t room, int type);
extern int refreshScreenColor(unsigned char *data, unsigned int len, unsigned char isZip);
extern void Rec_OTA_Data(uint8 *pValue, uint16 len);
extern void cleanDisplayColor(int color, UINT8 isNeedStandy);
extern UINT32 getRefreshScreenTime();
extern void  Start_advertising(void);
//extern void  Ble_CRC_Error(UINT8 chR);
extern void  Stop_advertising(void);
extern void  Stop_Central_Scan(void);
extern void  Start_Central_Scan(void);
extern UINT8 ADC(void);
extern void  Disable_GPIO_IRQ(void);
extern void  Stop_Ble_Central(void);
extern void  delay_ms(UINT16 x);
extern void  delay_xms(unsigned int xms);
extern void  OTA_IAPWriteData(unsigned char index, unsigned char *p_data, unsigned char w_len);
extern void  OTA_IAP_SendData(uint8_t *p_send_data, uint8_t send_len);
extern void  Init_GPIO(void);
extern void  debug(char *str,const unsigned int value, unsigned char baseData);
extern void receiveGapBroadcastAdData(UINT8 *adData, UINT32 dataLen);
extern void processGapBroadcastAdData(void);
extern void cleanPresaveScreen(int type, int room, int group);
extern UINT8 check_Old_New_PCB_type(void);
extern void ControlEPDPower(UINT8 ison);
#ifdef ENABLE_BOARD_ENCRYPT
extern void Save_Key_EEPROM_Flag(uint8_t* new_flag,UINT16 pos,UINT16 Len);
extern void Get_Key_EEPROM_Flag(uint8_t* new_flag,UINT16 pos,UINT16 Len);
#endif
#endif
