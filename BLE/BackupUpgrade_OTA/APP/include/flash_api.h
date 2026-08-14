#ifndef FLASH_API_H
#define FLASH_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CONFIG.h"

// ÔÚflash_api.hÖÐÉùÃ÷
typedef enum {
    FLASH_TYPE_INTERNAL,
    FLASH_TYPE_EXTERNAL,
    FLASH_TYPE_NONE
} FlashType;

#define EXTERN_FLASH_ID  					0x852016

#define PIC_COLOR_SEND_LENGTH  				238

#define EXTERN_FLASH_BLOCK_FIRST_ADDR  		0		//192000/1024=187K

//extern flash
#define EXTERN_FLASH_PIC_BUFFER_SIZE 		256  	// extern flash block size
#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
#define EXTERN_FLASH_PIC_SIZE  				300		//307200/1024=300K
#else
#define EXTERN_FLASH_PIC_SIZE  				188		//192000/1024=187K
#endif
#define EXTERN_FLASH_BUFFER_SIZE_EXTERNAL	EXTERN_FLASH_PIC_BUFFER_SIZE
#define EXTERN_FLASH_MAX_GROUP 				(4096 / EXTERN_FLASH_PIC_SIZE)	// BOE uses slots from index 0


//internal flash
#define PIC_SAVE_START_ADDR					0x37000 //save img addr
#define PIC_ERASE_BLOCK_NUM					10 		//save img addr
#define PIC_ERASE_BLOCK_MAX_NUM				46 		//save img addr
#define EXTERN_FLASH_BUFFER_SIZE_INTERNAL	2*PIC_COLOR_SEND_LENGTH

void InitFlashDriver();
void DeInitFlashDriver();
void Read256DataFromFlash(UINT8* buffer, UINT8 index, UINT32 block);
UINT8 Save256DataToFlash(UINT8 *data, int size, UINT8 index, UINT32 block) ;
void ReadDataFromFlash(UINT8* buffer, UINT8 index, UINT32 blocknum, UINT16 blocksize);
int SaveDataFromFlash(UINT8* buffer, UINT8 index, UINT32 blocknum, UINT16 blocksize);
bStatus_t EraseFLashData(unsigned char index);
int EraseSaveBlock(uint16 connHandle, unsigned char groupNum, unsigned char roomNum, unsigned char zip);
void Start_Flash_power(void);
void Set_Spi1_output_init(void);
void SPI_FLASH_Init(void);
UINT32 SPI_FLASH_ReadJedecID(void);
UINT16 SPI_FLASH_ReadManuID_DeviceID(UINT32 ReadManu_DeviceID_Addr);
unsigned int getFlashMaxGroup();

#ifdef __cplusplus
}
#endif

#endif

