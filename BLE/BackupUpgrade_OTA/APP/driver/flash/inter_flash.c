#include <stdlib.h>

#include "base64.h"
#include "app_cfg.h"
#include "flash_api.h"
#include "flash_driver.h"
#include "commoninfo.h"

int flashaddr = 0;
unsigned char szSaveData[EXTERN_FLASH_BUFFER_SIZE_INTERNAL];
int packetIndex = 0;
int packetSize = 0;
unsigned int flash2Addr = 0;


UINT8 save256DataToInternalFlashTmp(UINT8 *data, UINT32 size, UINT8 index, UINT32 block) 
{
    bStatus_t status = SUCCESS;

    size_t padding = (4 - (size % 4)) % 4;
    unsigned char* paddedBuffer = NULL;
    if (padding > 0) {
        paddedBuffer = (unsigned char*)malloc((size + padding) * sizeof(unsigned char));
        if (paddedBuffer == NULL) {
            PRINT("malloc failure");
            return ( ATT_ERR_INSUFFICIENT_AUTHOR );
        }
        memcpy(paddedBuffer, data, size);
        memset(paddedBuffer + size, 0, padding);
        size += padding;
    } else {
        paddedBuffer = (unsigned char*)data;
    }

	//PRINT("saveDataToFlash zzzzzzzzzzzzzzzzz startAddr:%x\r\n",startAddr);
	status = FLASH_ROM_WRITE(block, paddedBuffer, size);
	if(status != SUCCESS)
	{
		printf("ERASE Err\r\n");
		if (padding > 0) {
        	free(paddedBuffer);
   		}
		return ( ATT_ERR_INSUFFICIENT_AUTHOR );
	}

    if (padding > 0) {
        free(paddedBuffer);
    }

	return status;
}

int save256DataToInternalFlash(UINT8 *data, UINT32 size, UINT8 index, UINT32 block) 
{
	bStatus_t status = 0;

	uint32_t SaveAdd =  PIC_SAVE_START_ADDR +  index*(EEPROM_BLOCK_SIZE*PIC_ERASE_BLOCK_NUM);
	if (packetIndex == 0) {
		memcpy(szSaveData, data, size);
		packetSize = size;
		if(global_DEVICE_STATUS.fPackageCnt+1 == global_DEVICE_STATUS.fPackageCount){
			//flash2Addr = flash2Addr + flashAddr;
			//PRINT("00000000000000000000000 write:%x \r\n", (int)(SaveAdd + flash2Addr));
			status = save256DataToInternalFlashTmp(szSaveData,packetSize,index,SaveAdd + flash2Addr);
			if(status != SUCCESS) {
				Print_I3("IAP_PROM err");
				return -1;
			}
			packetSize = 0;
		}
	} else {
		packetSize = packetSize + size;
		memcpy(szSaveData + PIC_COLOR_SEND_LENGTH, data, size);
		//memcpy(szSaveData + out_len2, out_decrypted, out_len2);
		//PRINT("11111111111111111111111111 write:%x \r\n", (int)(SaveAdd + flash2Addr));
		status = save256DataToInternalFlashTmp(szSaveData,packetSize,index,SaveAdd + flash2Addr);
		if(status != SUCCESS) {
			Print_I3("IAP_PROM err");
			return -1;
		}
		flash2Addr = flash2Addr + packetSize;
		packetSize = 0;
	}
	packetIndex = (packetIndex + 1) % 2;	

	return 0;
}


