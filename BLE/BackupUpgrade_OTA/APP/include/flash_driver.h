#ifndef FLASH_DRIVER_H
#define FLASH_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CONFIG.h"

//Internal flash
int saveDataToInternalFlash(UINT8* buffer, UINT8 index, UINT32 blocknum, UINT8 blocksize);
UINT8 save256DataToInternalFlashTmp(UINT8 *data, UINT32 size, UINT8 index, UINT32 block) ;
int save256DataToInternalFlash(UINT8 *data, UINT32 size, UINT8 index, UINT32 block);

//extern flash
void initExternFlashDriver();
void deInitExternFlashDriver();
void readDataFromExternFlash(UINT8* buffer, UINT8 index, UINT32 blocknum, UINT16 blocksize);
int saveDataToExternFlash(UINT8* buffer, UINT8 index, UINT32 blocknum, UINT16 blocksize);
void read256DataFromExternFlash(UINT8* buffer, UINT8 index, UINT32 block);
UINT8 save256DataToExternFlashTmp(UINT8 *data, UINT32 size, UINT8 index, UINT32 block) ;
bStatus_t eraseExternFLashData(unsigned char index);
int saveDataFromExternFlash(UINT8* buffer, UINT8 index, UINT32 blocknum, UINT16 blocksize);
int save256DataToExternFlash(UINT8 *data, UINT32 size, UINT8 index, UINT32 block);

#ifdef __cplusplus
}
#endif

#endif

