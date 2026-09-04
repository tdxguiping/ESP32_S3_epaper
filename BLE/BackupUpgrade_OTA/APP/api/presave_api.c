#include <stdlib.h>

#include "boenfoservice.h"
#include "rledecode.h"
#include "commoninfo.h"
#include "aes_util.h"
#include "base64.h"
#include "app_cfg.h"
#include "flash_api.h"
#include "epd_driver.h"

int checkRoomHaveData(unsigned char *groupinfo, int room)
{
	int found = 0;
	for(int i = 0; i < getFlashMaxGroup(); i++){
		if(groupinfo[PRESAVE_POSITION_COUNT*i+2] == room){
			found = 1;
		}else{
			groupinfo[PRESAVE_POSITION_COUNT*i] = 0xff;
			groupinfo[PRESAVE_POSITION_COUNT*i+1] = 0xff;
			groupinfo[PRESAVE_POSITION_COUNT*i+2] = 0xff;
			groupinfo[PRESAVE_POSITION_COUNT*i+3] = 0xff;
			groupinfo[PRESAVE_POSITION_COUNT*i+4] = 0xff;
		}
	}

	return found ? Is_OK: Is_No;
}

int getPicSaveIndex(unsigned char *groupinfo, int group, int room){
#ifdef ENABLE_SOFTWARE_TO_BOE
	for(int i=0; i<getFlashMaxGroup(); i++){
		if(groupinfo[PRESAVE_POSITION_COUNT*i+3] != PRESAVE_GROUP_SAVE_FLAG){
			PRINT("getPicSaveIndex i=%d\r\n",i);	
			return i;
		}else{
			if((groupinfo[PRESAVE_POSITION_COUNT*i+1] == group) && (groupinfo[PRESAVE_POSITION_COUNT*i+2] == room)){
				PRINT(" exit getPicSaveIndex i=%d\r\n",i);
				return i;
			}
		}
	}

	return -1;
#else
	if(checkRoomHaveData(groupinfo, room) == Is_OK){
		// 1) ????????(group, room)???
		for(int i=0; i<getFlashMaxGroup(); i++){
			if((groupinfo[PRESAVE_POSITION_COUNT*i+1] == group) && (groupinfo[PRESAVE_POSITION_COUNT*i+2] == room)){
				PRINT(" old room exit getPicSaveIndex i=%d\r\n",i);
				return i;
			}
		}
		// 2) ???????????????
		for(int i=0; i<getFlashMaxGroup(); i++){
			if(groupinfo[PRESAVE_POSITION_COUNT*i+3] != PRESAVE_GROUP_SAVE_FLAG){
				PRINT("old room getPicSaveIndex i=%d\r\n",i);
				return i;
			}
		}
		// 3) ???????,???
		return -1;
	}else{
		PRINT("new room getPicSaveIndex 000000000000000000000\r\n");
		return 0;
	}
#endif
}

int getPicCurIndex(unsigned char *groupinfo, int group, int room){
	for(int i=0; i<getFlashMaxGroup(); i++){
		if((groupinfo[PRESAVE_POSITION_COUNT*i+1] == group) && (groupinfo[PRESAVE_POSITION_COUNT*i+2] == room)){
			PRINT(" exit getPicCurIndex i=%d\r\n",i);
			global_EXTERN_FLASH_INFO.fZip = groupinfo[PRESAVE_POSITION_COUNT*i+4];
			return i;
		}
	}

	return -1;
}

int getPicSaveNum(){
	int num =0;
	unsigned char szGroupInfo[GROUPINFO_Len];

	memset(szGroupInfo, 0, GROUPINFO_Len);
	Get_EEPROM_Flag(szGroupInfo,GROUPINFO_Position,GROUPINFO_Len);
	for(int i=0; i<getFlashMaxGroup(); i++){
		if(szGroupInfo[PRESAVE_POSITION_COUNT*i+3] == PRESAVE_GROUP_SAVE_FLAG){
			//PRINT("getPicSaveNum num =%d\r\n",i);	
			num++;
		}
	}

	return getFlashMaxGroup() - num;
}

void clearBoardcastData(){
	uint8_t BoardcastData[BOARDCAST_Len];
	uint8_t BoardcastDataLen[BOARDLEN_Len];

	// 初始化整个广播数据区域为0xFF（清空状态）
	memset(BoardcastData, 0xFF, BOARDCAST_Len);
	// 前4个字节设置为"FFFF"
	memcpy(BoardcastData, "FFFF", 4);
	BoardcastDataLen[0] = 4;
	Save_EEPROM_Flag(BoardcastData,BOARDCAST_Position,BOARDCAST_Len);
	Save_EEPROM_Flag(BoardcastDataLen,BOARDLEN_Position,BOARDLEN_Len);
}

void initPicSave(int type, int room, int group){
	unsigned char szGroupInfo[GROUPINFO_Len];
	unsigned char isNeedSendPreNum = Is_No;

	if(type == SCREEN_CLEAN_ALL)
	{
		for(int i=0; i<GROUPINFO_Len; i++){
			szGroupInfo[i] = 0xff;
		}
		isNeedSendPreNum = Is_Yes;
	}else if(type == SCREEN_CLEAN_ROOM)
	{
		memset(szGroupInfo, 0, GROUPINFO_Len);
		Get_EEPROM_Flag(szGroupInfo,GROUPINFO_Position,GROUPINFO_Len);
		for(int i=0; i<getFlashMaxGroup(); i++){
			if(szGroupInfo[PRESAVE_POSITION_COUNT*i+2] == room){
				szGroupInfo[PRESAVE_POSITION_COUNT*i] = 0xff;
				szGroupInfo[PRESAVE_POSITION_COUNT*i+1] = 0xff;
				szGroupInfo[PRESAVE_POSITION_COUNT*i+2] = 0xff;
				szGroupInfo[PRESAVE_POSITION_COUNT*i+3] = 0xff;
				szGroupInfo[PRESAVE_POSITION_COUNT*i+4] = 0xff;
				PRINT("clean room i=%d\r\n",i);	
				isNeedSendPreNum = Is_Yes;
			}
		}
	}else if(type == SCREEN_CLEAN_ROOM_AND_GROUP)
	{
		memset(szGroupInfo, 0, GROUPINFO_Len);
		Get_EEPROM_Flag(szGroupInfo,GROUPINFO_Position,GROUPINFO_Len);
		for(int i=0; i<getFlashMaxGroup(); i++){
			if((szGroupInfo[PRESAVE_POSITION_COUNT*i+2] == room) && (szGroupInfo[PRESAVE_POSITION_COUNT*i+1] == group)){
				szGroupInfo[PRESAVE_POSITION_COUNT*i] = 0xff;
				szGroupInfo[PRESAVE_POSITION_COUNT*i+1] = 0xff;
				szGroupInfo[PRESAVE_POSITION_COUNT*i+2] = 0xff;
				szGroupInfo[PRESAVE_POSITION_COUNT*i+3] = 0xff;
				szGroupInfo[PRESAVE_POSITION_COUNT*i+4] = 0xff;
				PRINT("clean room and group i=%d\r\n",i);
				isNeedSendPreNum = Is_Yes;
			}
		}		
	}

	Save_EEPROM_Flag(szGroupInfo,GROUPINFO_Position,GROUPINFO_Len);

	if(isNeedSendPreNum == Is_Yes){
		int saveNum = getPicSaveNum();
		P_scanPreNumRspData(saveNum);
	}
	
	for(int i=0; i<getFlashMaxGroup(); i++){
		if(szGroupInfo[PRESAVE_POSITION_COUNT*i+3] == PRESAVE_GROUP_SAVE_FLAG){
			PRINT("have pre data i=%d\r\n",i);	
			return;
		}
	}

	PRINT("no have pre data @@@@@@@@@@@@@@@@@@@@\r\n");
#ifdef ENABLE_SOFTWARE_TO_BOE
	setWorkMode(DEVICE_MODE_LOW);
#endif
}

void cleanPresaveScreen(int type, int room, int group){
	unsigned char szGroupInfo[GROUPINFO_Len];
	unsigned char isCleaned = Is_No;

	if(type == SCREEN_CLEAN_SCRREN_ALL)
	{
		global_DEVICE_STATUS.fInitDriver = Is_Yes;
		cleanDisplayColor(SCREEN_COLOR_WHITE, Is_Yes);
		isCleaned = Is_Yes;
	}else if(type == SCREEN_CLEAN_SCRREN_ROOM || type == SCREEN_CLEAN_ROOM)
	{
		memset(szGroupInfo, 0, GROUPINFO_Len);
		Get_EEPROM_Flag(szGroupInfo,GROUPINFO_Position,GROUPINFO_Len);
		for(int i=0; i<getFlashMaxGroup(); i++){
			if(szGroupInfo[PRESAVE_POSITION_COUNT*i+2] == room){
				PRINT("clean room i=%d\r\n",i);	
				global_DEVICE_STATUS.fInitDriver = Is_Yes;
				cleanDisplayColor(SCREEN_COLOR_WHITE, Is_Yes);
				isCleaned = Is_Yes;
				break;
			}
		}
	}else if(type == SCREEN_CLEAN_ROOM_AND_GROUP)
	{
		memset(szGroupInfo, 0, GROUPINFO_Len);
		Get_EEPROM_Flag(szGroupInfo,GROUPINFO_Position,GROUPINFO_Len);
		PRINT("cleanPresaveScreen ROOM_AND_GROUP: target room=%d group=%d\r\n",room,group);
		for(int i=0; i<getFlashMaxGroup(); i++){
			if((szGroupInfo[PRESAVE_POSITION_COUNT*i+2] == room) && (szGroupInfo[PRESAVE_POSITION_COUNT*i+1] == group)){
				PRINT("clean room and group MATCHED i=%d\r\n",i);	
				global_DEVICE_STATUS.fInitDriver = Is_Yes;
				cleanDisplayColor(SCREEN_COLOR_WHITE, Is_Yes);
				isCleaned = Is_Yes;
				break;
			}
		}
	}
	// 关键：只有这里能设置全局清屏标志为已清屏状态
	if(isCleaned != Is_Yes){
		return;
	}
	global_screen_cleared_flag = SCREEN_ALREADY_CLEARED;
	saveLastRefreshInfo(//只保存是否清屏的标志位
						SAVE_KEEP_U8,  // type keep
						SAVE_KEEP_U8,  // group keep
						SAVE_KEEP_U8,  // room keep
						SAVE_KEEP_U8,  // screen_mode keep
						SAVE_KEEP_U8,  // zip keep
						SAVE_KEEP_U8,    // enabled set
						global_screen_cleared_flag   // screen_cleared keep
					);
				
}

void setWorkMode(int mode)
{
	unsigned char workmode[WORKMODE_Len] = {0x00};
	unsigned char workmode_tmp[WORKMODE_Len] = {0x01};

	Get_EEPROM_Flag(workmode,WORKMODE_Position,WORKMODE_Len);
	if(workmode[0] != mode){
		workmode_tmp[0] = mode;
		PRINT("@@@@@@@@@@@@@@@@@@setWorkMode workmode_tmp:%d\r\n",workmode_tmp[0]);
		Save_EEPROM_Flag(workmode_tmp,WORKMODE_Position,WORKMODE_Len);
		P_scanModeRspData(mode);
		if(mode == DEVICE_MODE_HIGH){
			tmos_start_task(main_task_ID, EVENT_Start_Central_Mode, 2*1600);
		}else{
			tmos_stop_task( main_task_ID, EVENT_Start_Central_Mode);
		}
	}
}

int preSaveDisplayColor(uint8_t group, uint8_t room, int type) {
    int i, index, data_len = 0;
    unsigned char *PicData = NULL; // ��Ϊָ�룬��̬�����ڴ�
 
    PicData = (unsigned char *)malloc(EXTERN_FLASH_BUFFER_SIZE_EXTERNAL); 

    if (PicData == NULL) {
        PRINT("PicData malloc failed\r\n");
        return -1;
    }

    if (type == DEVICE_PRE_SAVE) {
		if(global_DEVICE_STATUS.fRefreshType == DEVICE_OP_COMMON){
			// When reloading a common image from flash, read zip by the current index(0/1) to avoid sharing one flag across AB-diff refresh
			uint8_t common_zip = getCommonImageZip(global_EXTERN_FLASH_INFO.fImageIndex);
			if(common_zip != 0xFF){
				global_EXTERN_FLASH_INFO.fZip = common_zip;
			}
			//PRINT("preSaveDisplay6Color 88 global_EXTERN_FLASH_INFO.fZip:%d,global_EXTERN_FLASH_INFO.fImageIndex:%d\r\n", 
			//	global_EXTERN_FLASH_INFO.fZip,global_EXTERN_FLASH_INFO.fImageIndex);
		}else{
			//PRINT("preSaveDisplay6Color 99 global_EXTERN_FLASH_INFO.fZip:%d,global_EXTERN_FLASH_INFO.fImageIndex:%d\r\n", 
			//	global_EXTERN_FLASH_INFO.fZip,global_EXTERN_FLASH_INFO.fImageIndex);
	        unsigned char szGroupInfo[GROUPINFO_Len];
	        Get_EEPROM_Flag(szGroupInfo, GROUPINFO_Position, GROUPINFO_Len);
	        index = getPicCurIndex(szGroupInfo, group, room);
	        if ((index == -1) || (szGroupInfo[PRESAVE_POSITION_COUNT * index + 3] != PRESAVE_GROUP_SAVE_FLAG)) {
	            PRINT("@@@@@@@@@@@@@@@@@@@@ group not exist\r\n");
	            free(PicData); // �ͷ��ڴ�
	            return -1;
	        }
#ifdef ENABLE_SOFTWARE_TO_BOE
			global_EXTERN_FLASH_INFO.fImageIndex = index;
#else
			global_EXTERN_FLASH_INFO.fImageIndex = index+EXTERN_FLASH_SAVE_START_ADDR;
#endif
		}
        while (1) {
            // ����FLASH���ͻ�ȡʵ�ʻ���������
            uint32_t buffer_size = EXTERN_FLASH_BUFFER_SIZE_EXTERNAL;

            memset(PicData, 0, buffer_size);
            // ʹ��ͳһ�ӿڶ�ȡ���Զ���������/����
			Read256DataFromFlash(PicData,global_EXTERN_FLASH_INFO.fImageIndex,global_EXTERN_FLASH_INFO.fBlockNum);
            global_EXTERN_FLASH_INFO.fBlockNum++;

            data_len = refreshScreenColor(PicData, buffer_size, global_EXTERN_FLASH_INFO.fZip);
#if 1//#ifdef ENABLE_SCREEN_COLOR_3
#if (defined(ENABLE_INK_SCREEN_JD79686BB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3)) || \
    (defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4)) || (defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4)) || \
    (defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4)) || \
    (defined(ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2)) || (defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6)) || \
    (defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4))
            if ((data_len >= EPD_GetDisplayMaxBuf()) &&
                (global_DEVICE_STATUS.fisHost != IS_HOST))
#else
            if (data_len >= EPD_GetDisplayMaxBuf())
#endif
#else
            //if (data_len == 0) 
#endif
            {
                PRINT("preSaveDisplay6Color success\r\n");
                global_EXTERN_FLASH_INFO.fBlockNum = EXTERN_FLASH_BLOCK_FIRST_ADDR;
                break;
            }
        }
    } else if (type == DEVICE_CLEAN_SCREEN) {
        clearBoardcastData();
        cleanDisplayColor(SCREEN_COLOR_WHITE, Is_Yes);
    }

    free(PicData); // �ͷŶ�̬������ڴ�
    return 0;
}


