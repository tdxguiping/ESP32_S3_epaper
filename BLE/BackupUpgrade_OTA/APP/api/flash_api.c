#include <stdlib.h>

#include "base64.h"
#include "app_cfg.h"
#include "flash_api.h"
#include "flash_driver.h"
#include "commoninfo.h"
#include "util.h"

void InitFlashDriver()
{
	initExternFlashDriver();
}

void DeInitFlashDriver()
{
	deInitExternFlashDriver();
}

void ReadDataFromFlash(UINT8* buffer, UINT8 index, UINT32 blocknum, UINT16 blocksize)
{
	readDataFromExternFlash(buffer,index,blocknum,blocksize);
}

int SaveDataFromFlash(UINT8* buffer, UINT8 index, UINT32 blocknum, UINT16 blocksize)
{
	return saveDataFromExternFlash(buffer,index,blocknum,blocksize);
}

UINT8 Save256DataToFlash(UINT8 *data, int size, UINT8 index, UINT32 block) 
{
	return save256DataToExternFlash(data, size, index, block);
}

void Read256DataFromFlash(UINT8* buffer, UINT8 index, UINT32 block)
{
	read256DataFromExternFlash(buffer, index, block);
}

unsigned int getFlashMaxGroup()
{
	unsigned int maxGroup;
	
	maxGroup = EXTERN_FLASH_MAX_GROUP;

	return maxGroup;
}

int EraseSaveBlock(uint16 connHandle, unsigned char groupNum, unsigned char roomNum, unsigned char zip){
	bStatus_t status = SUCCESS;
	int savIndex = 0;
	
	unsigned char szGroupInfo[GROUPINFO_Len];
	Get_EEPROM_Flag(szGroupInfo,GROUPINFO_Position,GROUPINFO_Len);
	
	savIndex = getPicSaveIndex(szGroupInfo,groupNum,roomNum);
	if(savIndex == -1){
		PRINT("@@@@@@@@@@@@@@@ save pic error\r\n");
		return -1;
	}
	for(int i=0; i<getFlashMaxGroup(); i++){
		if(i == savIndex){
			szGroupInfo[PRESAVE_POSITION_COUNT*i] = savIndex;
			szGroupInfo[PRESAVE_POSITION_COUNT*i+1] = groupNum;
			szGroupInfo[PRESAVE_POSITION_COUNT*i+2] = roomNum;
			szGroupInfo[PRESAVE_POSITION_COUNT*i+3] = PRESAVE_GROUP_SAVE_FLAG;
			szGroupInfo[PRESAVE_POSITION_COUNT*i+4] = zip;
		}
	}
	Save_EEPROM_Flag(szGroupInfo,GROUPINFO_Position,GROUPINFO_Len);
	
#ifdef ENABLE_SOFTWARE_TO_BOE
	int saveNum = getPicSaveNum();
	P_scanPreNumRspData(saveNum);
#endif

	status = eraseExternFLashData(savIndex);
	
	if(status != SUCCESS)
	{
		printf("E er\r\n");
		return -1;
	}
#ifdef ENABLE_SOFTWARE_TO_BOE
	return savIndex;
#else
	return savIndex+EXTERN_FLASH_SAVE_START_ADDR;
#endif
}


