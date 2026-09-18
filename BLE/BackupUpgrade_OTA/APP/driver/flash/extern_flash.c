#include "img_perf.h"
#include <stdlib.h>

#include "base64.h"
#include "app_cfg.h"
#include "flash_api.h"
#include "flash_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"
#include "release_trace.h"

static UINT8 flash_power_ready = 0;
static UINT8 flash_wait_ok = 1;
static UINT8 flash_ready_failed = 0;

void Set_Spi1_output_init(void)
{
    GPIOA_ModeCfg(GPIO_Pin_0 | GPIO_Pin_2| GPIO_Pin_1, GPIO_ModeOut_PP_5mA);
    //GPIOA_ModeCfg(GPIO_Pin_1, GPIO_ModeIN_PU);    
   
    SPI1_MasterDefInit();
    SPI1_DataMode(Mode0_HighBitINFront);

    // add for spi flash
    GPIOB_ModeCfg(GPIO_Pin_12, GPIO_ModeOut_PP_5mA);    
}

void Set_Spi0_output_for_SPIflash_init(void)
{
    // Print_I3("");
    GPIOPinRemap(0,RB_PIN_SPI0);  // DISABLE  ENABLE
    
    GPIOA_ModeCfg(GPIO_Pin_13 | GPIO_Pin_15, GPIO_ModeOut_PP_5mA);
    GPIOB_ModeCfg(GPIO_Pin_12, GPIO_ModeOut_PP_5mA);    
    GPIOA_ModeCfg(GPIO_Pin_14, GPIO_ModeIN_PU);    
    
    //SPI0_MasterDefInit_For_SPIFlash_output();       
    SPI0_MasterDefInit_output();
    
    SPI0_DataMode(Mode0_HighBitINFront); // Mode0_LowBitINFront   Mode0_HighBitINFront
}

//================================================================================================
//================================================================================================
//存储器部分被分割成32个块（Block）
//单个块区包含16个扇区（Sector）
//每个扇区又由16个页组成，单页存储256字节数据，
//共计32 x 16 x 16 x 256 ≈ 2M 字节。存储器通过页地址和字节地址来定位特定的数据位置。

//24位地址线通过上述的层次结构来映射整个存储器的地址空间，具体见下面表格。

//分区	地址位
//块（Block）-----    地址	23~16位（0x1F0000到0x1FFFFF） ---(8 位)）
//扇区（Sector）-----地址	15~12位（0x00F000到0x00FFFF） ---(4 位)
//页（Page）--------地址	11~8位---(4 位)
//字节（Byte）------地址	7~0位   ---(8 位)

//  一块存一张图片 ，一块有 64K bytes  32 x 16 x 16 x 256 ≈ 2M(32张图片)   64 x 16 x 16 x 256 ≈ 4M （64张图片）
void SPI_FLASH_Init(void);
void SPI_FLASH_WriteEnable(void);
void SPI_FLASH_WriteDisable(void);
UINT8 SPI_Flash_ReadStatusRegister(void);
void SPI_Flash_WriteStatusRegister(UINT8 Byte);
UINT8 SPI0_MasterRecvByte(void);
UINT8 SPI_FLASH_ByteRead(UINT32 ReadAddr);
UINT8 SPI_FLASH_FasttRead(UINT32 ReadAddr);
void SPI_FLASH_BufferRead(UINT8* pBuffer, UINT32 ReadAddr, UINT16 NumByteToRead);
UINT8 SPI_FLASH_SectorErase(UINT32 SectorAddr);
void SPI_FLASH_BulkErase(UINT32 BlockAddr);
void SPI_FLASH_ChipErase(void);
void SPI_FLASH_PowerDown();
void SPI_FLASH_ReleasePowerDown();
UINT8 SPI_FLASH_ReadDeviceID(void);
UINT16 SPI_FLASH_ReadManuID_DeviceID(UINT32 ReadManu_DeviceID_Addr);
UINT32 SPI_FLASH_ReadJedecID(void);
void SPI_FLASH_ByteWrite(UINT8 Byte, UINT32 WriteAddr);
void SPI_FLASH_PageWrite(UINT8* pBuffer, UINT32 WriteAddr, UINT16 NumByteToWrite);
void SPI_FLASH_WaitForWriteEnd(void);
static UINT8 SPI_FLASH_WaitForWriteEndTimeout(UINT32 timeout_ms);

#define SPI_FLASH_CS_LOW()          GPIOB_ResetBits(GPIO_Pin_12)
#define SPI_FLASH_CS_HIGH()         GPIOB_SetBits(GPIO_Pin_12)
#define SPI_Flash_WP_LOW()       
#define SPI_Flash_WP_HIGH()      
#define SPI_Flash_HOLD_LOW()     
#define SPI_Flash_HOLD_HIGH()    

#define SPI_FLASH_PageSize 256

#define WriteEnable               0x06       //写使能，设置状态寄存器
#define WriteDisable              0x04       //写禁止
#define ReadStatusRegister        0x05       //读状态寄存器
#define WriteStatusRegister       0x01       //写状态寄存器
#define Read_Data                 0x03       //读取存储器数据
#define FastReadData              0x0B       //快速读取存储器数据
#define FastReadDualOutput        0x3B       //快速双端口输出方式读取存储器数据
#define Page_Program              0x02       //页面编程--写数据
#define BlockErace                0xD8       //块擦除
#define SectorErace               0x20       //扇区擦除
#define ChipErace                 0xC7       //片擦除
#define Power_Down                0xB9       //掉电模式
#define ReleacePowerDown          0xAB       //退出掉电模式
#define ReadDeviceID              0xAB       //获取设备ID信息
#define ReadDeviceID              0xAB       //退出掉电模式、设备ID信息
#define ReadManuIDDeviceID        0x90       //读取制造厂商ID信息和设备ID信息
#define ReadJedec_ID              0x9F       //JEDEC的ID信息
///
// 6色屏 ，一张最大 187K   4M=4x1024 =4096 / 187 =21
//W25x系列Flash芯片驱动程序(SPI调试通过)

void SPI_FLASH_Init(void)
{
	SPI_FLASH_CS_HIGH(); 
	SPI_Flash_WP_HIGH();
	SPI_Flash_HOLD_HIGH();
}

void SPI_FLASH_WriteEnable(void)
{
	SPI_FLASH_CS_LOW();
	SPI1_MasterSendByte(WriteEnable);  
	SPI_FLASH_CS_HIGH();
}

void SPI_FLASH_WriteDisable(void)
{
	SPI_FLASH_CS_LOW();
	SPI1_MasterSendByte(WriteDisable);
	SPI_FLASH_CS_HIGH();
}

UINT8 SPI_Flash_ReadStatusRegister(void)
{
	UINT8 StatusRegister = 0;
	
	SPI_FLASH_CS_LOW();
	SPI1_MasterSendByte(ReadStatusRegister);
	StatusRegister = SPI1_MasterRecvByte();
	SPI_FLASH_CS_HIGH();
	
	return StatusRegister;
}

void SPI_Flash_WriteStatusRegister(UINT8 Byte)
{
	SPI_FLASH_CS_LOW();
	SPI1_MasterSendByte(WriteStatusRegister);
	SPI1_MasterSendByte(Byte);
	SPI_FLASH_CS_HIGH();
}

UINT8 SPI_FLASH_ByteRead(UINT32 ReadAddr)
{
	UINT32 Temp = 0;
	
	SPI_FLASH_CS_LOW();
	SPI1_MasterSendByte(Read_Data);
	SPI1_MasterSendByte((ReadAddr & 0xFF0000) >> 16);
	SPI1_MasterSendByte((ReadAddr& 0xFF00) >> 8);
	SPI1_MasterSendByte(ReadAddr & 0xFF);

	Temp = SPI1_MasterRecvByte();
	//Temp = SPI0_MasterSendByte(Dummy_Byte);
	SPI_FLASH_CS_HIGH();
	
	return Temp;
}

UINT8 SPI_FLASH_FasttRead(UINT32 ReadAddr)
{
	UINT32 Temp = 0;
	
	SPI_FLASH_CS_LOW();
	SPI1_MasterSendByte(FastReadData);
	SPI1_MasterSendByte((ReadAddr & 0xFF0000) >> 16);
	SPI1_MasterSendByte((ReadAddr& 0xFF00) >> 8);
	SPI1_MasterSendByte(ReadAddr & 0xFF);

	//SPI1_MasterSendByte(Dummy_Byte);  
	Temp = SPI1_MasterRecvByte();
	//Temp = SPI1_MasterSendByte(Dummy_Byte);
	SPI_FLASH_CS_HIGH();
	
	return Temp;
}

void SPI_FLASH_BufferRead(UINT8* pBuffer, UINT32 ReadAddr, UINT16 NumByteToRead)
{
	SPI_FLASH_CS_LOW();
	SPI1_MasterSendByte(Read_Data);
	SPI1_MasterSendByte((ReadAddr & 0xFF0000) >> 16);
	SPI1_MasterSendByte((ReadAddr& 0xFF00) >> 8);
	SPI1_MasterSendByte(ReadAddr & 0xFF);

	while(NumByteToRead--)
	{
		*pBuffer = SPI1_MasterRecvByte();
		pBuffer++;
	}
	SPI_FLASH_CS_HIGH();
}

UINT8 SPI_FLASH_SectorErase(UINT32 SectorAddr)
{
    UINT32 perf_start = IP_Start(IP_ERASE);
    UINT8 status;
    UINT8 result;
	SPI_FLASH_WriteEnable();
    status = SPI_Flash_ReadStatusRegister();
    if((status & 0x02U) == 0) {
        BOOT_LOG_TEXT("FLASH WEL fail\r\n");
        BOOT_LOG_HEX8("FLASH SR=", status);
        IP_Toc(IP_ERASE, perf_start);
        return 1;
    }
	SPI_FLASH_CS_LOW();
	SPI1_MasterSendByte(SectorErace);
	SPI1_MasterSendByte((SectorAddr & 0xFF0000) >> 16);
	SPI1_MasterSendByte((SectorAddr & 0xFF00) >> 8);
	SPI1_MasterSendByte(SectorAddr & 0xFF);
	SPI_FLASH_CS_HIGH();
	result = SPI_FLASH_WaitForWriteEndTimeout(1000U);
    IP_Toc(IP_ERASE, perf_start);
    return result;
}

void SPI_FLASH_BulkErase(UINT32 BlockAddr)
{
	SPI_FLASH_WriteEnable();
	SPI_FLASH_CS_LOW();
	SPI1_MasterSendByte(BlockErace);

	SPI1_MasterSendByte((BlockAddr & 0xFF0000) >> 16);
	SPI1_MasterSendByte((BlockAddr & 0xFF00) >> 8);
	SPI1_MasterSendByte(BlockAddr & 0xFF);
	SPI_FLASH_CS_HIGH();
	SPI_FLASH_WaitForWriteEndTimeout(5000U);
}

void SPI_FLASH_ChipErase(void)
{
	SPI_FLASH_WriteEnable();
	SPI_FLASH_CS_LOW();
	SPI1_MasterSendByte(ChipErace);
	SPI_FLASH_CS_HIGH();
	SPI_FLASH_WaitForWriteEndTimeout(120000U);
}

void SPI_FLASH_PowerDown()
{
	SPI_FLASH_WriteEnable();
	SPI_FLASH_CS_LOW();
	SPI1_MasterSendByte(Power_Down);
	SPI_FLASH_CS_HIGH();
	SPI_FLASH_WaitForWriteEnd();
}

void SPI_FLASH_ReleasePowerDown()
{
	SPI_FLASH_WriteEnable();
	SPI_FLASH_CS_LOW();
	SPI1_MasterSendByte(ReleacePowerDown);
	SPI_FLASH_CS_HIGH();
	SPI_FLASH_WaitForWriteEnd();
}

UINT8 SPI_FLASH_ReadDeviceID(void)
{
	UINT8 DeviceID = 0;
	SPI_FLASH_CS_LOW();
	SPI1_MasterSendByte(ReadDeviceID);
	//SPI1_MasterSendByte(Dummy_Byte);
	DeviceID = SPI1_MasterRecvByte();
	SPI_FLASH_CS_HIGH();      
	return DeviceID;
}

UINT16 SPI_FLASH_ReadManuID_DeviceID(UINT32 ReadManu_DeviceID_Addr)
{
	UINT16 ManuID_DeviceID = 0;
	UINT8 ManufacturerID = 0,  DeviceID = 0;
	
	SPI_FLASH_CS_LOW();
	SPI1_MasterSendByte(ReadManuIDDeviceID);

	SPI1_MasterSendByte((ReadManu_DeviceID_Addr & 0xFF0000) >> 16);
	SPI1_MasterSendByte((ReadManu_DeviceID_Addr & 0xFF00) >> 8);
	SPI1_MasterSendByte(ReadManu_DeviceID_Addr & 0xFF);

	if(ReadManu_DeviceID_Addr==1)
	{
		DeviceID = SPI1_MasterRecvByte();
		ManufacturerID = SPI1_MasterRecvByte();
	}
	else 
	{
		ManufacturerID = SPI1_MasterRecvByte();
		DeviceID = SPI1_MasterRecvByte();
	}
	ManuID_DeviceID = ((ManufacturerID<<8) | DeviceID);
	SPI_FLASH_CS_HIGH();
	printf("ManufacturerID =%x,DeviceID=%x\r\n",ManufacturerID,DeviceID);

	return ManuID_DeviceID;
}

UINT32 SPI_FLASH_ReadJedecID(void)
{
	UINT32 JEDECID = 0, Temp0 = 0, Temp1 = 0, Temp2 = 0;
	
	SPI_FLASH_CS_LOW();
	SPI1_MasterSendByte(ReadJedec_ID);
	Temp0 = SPI1_MasterRecvByte();
	Temp1 = SPI1_MasterRecvByte();
	Temp2 = SPI1_MasterRecvByte();
	SPI_FLASH_CS_HIGH();  
	JEDECID = (Temp0 << 16) | (Temp1 << 8) | Temp2;
	return JEDECID;
}

void SPI_FLASH_ByteWrite(UINT8 Byte, UINT32 WriteAddr)
{
	SPI_FLASH_WriteEnable(); 
	SPI_FLASH_CS_LOW();
	SPI1_MasterSendByte(Page_Program);
	SPI1_MasterSendByte((WriteAddr & 0xFF0000) >> 16);
	SPI1_MasterSendByte((WriteAddr & 0xFF00) >> 8);  
	SPI1_MasterSendByte(WriteAddr & 0xFF);

	SPI1_MasterSendByte(Byte); 
	SPI_FLASH_CS_HIGH();
	SPI_FLASH_WaitForWriteEnd();
}

void SPI_FLASH_PageWrite(UINT8* pBuffer, UINT32 WriteAddr, UINT16 NumByteToWrite)
{
	SPI_FLASH_WriteEnable(); 
	SPI_FLASH_CS_LOW();
	SPI1_MasterSendByte(Page_Program);
	SPI1_MasterSendByte((WriteAddr & 0xFF0000) >> 16);
	SPI1_MasterSendByte((WriteAddr & 0xFF00) >> 8);  
	SPI1_MasterSendByte(WriteAddr & 0xFF);

	while(NumByteToWrite--)
	{
		SPI1_MasterSendByte(*pBuffer);
		pBuffer++; 
	}
	SPI_FLASH_CS_HIGH();
	SPI_FLASH_WaitForWriteEnd();
}

void SPI_FLASH_WaitForWriteEnd(void)
{
    SPI_FLASH_WaitForWriteEndTimeout(20U);
}

static UINT32 flash_wait_elapsed(UINT32 start)
{
    UINT32 now = RTC_GetCycle32k();
    return now >= start ? now - start : RTC_MAX_COUNT - start + now;
}

static UINT8 SPI_FLASH_WaitForWriteEndTimeout(UINT32 timeout_ms)
{
    UINT8 flash_status;
    UINT32 start = RTC_GetCycle32k();
    UINT32 timeout_ticks = (CAB_LSIFQ * timeout_ms + 999U) / 1000U;
    UINT32 count = 0;

    do
    {
        WWDG_SetCounter(0);
        flash_status = SPI_Flash_ReadStatusRegister();
        count++;
        if((flash_status & WriteStatusRegister) != SET) {
            flash_wait_ok = 1;
            SPI_FLASH_CS_HIGH();
            return 0;
        }
#if TDX_FLASH_WIP_LEGACY_DELAY
        DelayMs(1);
#endif
    } while(flash_wait_elapsed(start) < timeout_ticks);

    SPI_FLASH_CS_HIGH();
    flash_wait_ok = 0;
    BOOT_LOG_TEXT("FLASH wait-timeout\r\n");
    BOOT_LOG_HEX8("FLASH status=", flash_status);
    BOOT_LOG_HEX32("FLASH count=", count);
    BOOT_LOG_HEX32("FLASH limit=", timeout_ms);
    return 1;
}

UINT8 TDX_SPI_FLASH_E_4096Bytes(UINT16 PIC_Number,UINT16 Block_Number,UINT16 F_type)
{
	UINT32 Addr;
	if(flash_ready_failed) return 1;

	Addr=(1024*F_type)*PIC_Number+Block_Number*256;  
	printf("TDX_SPI_FLASH_E_4096Bytes E-Addr=%X\r\n",Addr);
	return SPI_FLASH_SectorErase(Addr);
}

// 以 256bytes 为单位，一次最少写 256 bytes
// Addr = 表示 第几个 256 块
void TDX_SPI_FLASH_W_256Bytes_xt(UINT8* pBuffer,UINT16 PIC_Number,UINT16 Block_Number,UINT16 F_type)
{
	UINT32 Addr;
	UINT16 NumByteToWrite;  

	Addr=(1024*F_type)*PIC_Number+Block_Number*256;

	printf("W-addr=%X\r\n",Addr);
	SPI_FLASH_WriteEnable(); 
	SPI_FLASH_CS_LOW();
	SPI1_MasterSendByte(Page_Program);

	SPI1_MasterSendByte((Addr & 0xFF0000) >> 16);
	SPI1_MasterSendByte((Addr & 0xFF00) >> 8);  
	SPI1_MasterSendByte(Addr & 0xFF);

	NumByteToWrite=256;
	while(NumByteToWrite--)
	{    
		//if(NumByteToWrite>240)
		//  printf("%02X ",*pBuffer);
		SPI1_MasterSendByte(*pBuffer);
		pBuffer++;     
	}
	SPI_FLASH_CS_HIGH();
	//printf("\r\n");
	SPI_FLASH_WaitForWriteEnd();
}

// 以 256bytes 为单位，一次最少写 256 bytes
// Addr = 表示 第几个 256 块
UINT8 TDX_SPI_FLASH_W_256Bytes(UINT8* pBuffer,UINT16 PIC_Number,UINT16 Block_Number,UINT16 F_type)
{
    UINT32 perf_start = IP_Start(IP_WRITE);
    UINT8 result;
	UINT32 Addr;
	UINT16 NumByteToWrite;
	if(flash_ready_failed) return 1;

	Addr=(1024*F_type)*PIC_Number+Block_Number*256;
	//printf("W-Addr=%X\r\n",Addr);

	SPI_FLASH_WriteEnable(); 
	SPI_FLASH_CS_LOW();
	SPI1_MasterSendByte(Page_Program);

	SPI1_MasterSendByte((Addr & 0xFF0000) >> 16);
	SPI1_MasterSendByte((Addr & 0xFF00) >> 8);  
	SPI1_MasterSendByte(Addr & 0xFF);

	NumByteToWrite=256;
	while(NumByteToWrite--)
	{
		SPI1_MasterSendByte(*pBuffer);
		pBuffer++; 
	}
	SPI_FLASH_CS_HIGH();
	result = SPI_FLASH_WaitForWriteEndTimeout(20U);
    IP_Toc(IP_WRITE, perf_start);
    return result;
}

// 以 256bytes 为单位，一次最少写 256 bytes
// Addr = 表示 第几个 256 块
void TDX_SPI_FLASH_Read_256Bytes(UINT8* pBuffer,UINT16 PIC_Number,UINT16 Block_Number,UINT16 F_type)
{
	UINT32 Addr;
	UINT16 NumByteToRead;  
	UINT8  u8dat;

	Addr=(1024*F_type)*PIC_Number+Block_Number*256;
	//printf("R-Addr=%X\r\n",Addr);  

	SPI_FLASH_CS_LOW();
	SPI1_MasterSendByte(Read_Data);  //FastReadData

	SPI1_MasterSendByte((Addr & 0xFF0000) >> 16);
	SPI1_MasterSendByte((Addr & 0xFF00) >> 8);  
	SPI1_MasterSendByte(Addr & 0xFF);

	for(NumByteToRead=0;NumByteToRead<256;NumByteToRead++)
	{
		//u8dat=SPI1_MasterRecvByte();
		pBuffer[NumByteToRead]=SPI1_MasterRecvByte();

		//printf(" %d ",u8dat);
		//printf(" %d ",Buffer[i]);
	}        

	SPI_FLASH_CS_HIGH();
}

void Stop_Flash_power(void)
{
	GPIOA_ModeCfg(GPIO_Pin_6, GPIO_ModeOut_PP_20mA);     
#ifndef HARDWAR_DRY_CELL
    R32_PA_CLR |= GPIO_Pin_6;   
#else
    R32_PA_OUT |= GPIO_Pin_6; //骞茬數姹犳柟妗堬紝鐢垫簮鍙嶇疆
#endif
    SPI_FLASH_Init(); 
    flash_power_ready = 0;
    flash_ready_failed = 0;
	Print_I3("Stop_Flash_power @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@");
}

void Start_Flash_power(void)
{
    GPIOA_ModeCfg(GPIO_Pin_6, GPIO_ModeOut_PP_20mA);     
#ifndef HARDWAR_DRY_CELL
	R32_PA_OUT |= GPIO_Pin_6;   
#else
    R32_PA_CLR |= GPIO_Pin_6; //骞茬數姹犳柟妗堬紝鐢垫簮鍙嶇疆
#endif
    SPI_FLASH_Init(); 
}

#ifdef EXTERN_FLASH_TEST
void test_spi_flash(void)
{
    UINT8   k;
    UINT16  i;
    UINT8   Buffer[256];
	UINT8   Buffer1[256] = {
		0x7f, 0xff, 0xd7, 0x6f, 0xff, 0xff, 0xfa, 0x32, 0x7f, 0xff, 0xff, 0x4c, 0x2b, 0xff, 0x02, 0x02, 0x1c, 0x00, 0x00, 0x00, 
		0x18, 0x00, 0x00, 0x00, 0x30, 0x56, 0x00, 0x20, 0xaa, 0xa5, 0x59, 0x11, 0x88, 0xaf, 0x61, 0x95, 0xa6, 0xb3, 0xb6, 0xfb, 
		0xbd, 0x7a, 0xae, 0x30, 0x24, 0x00, 0x00, 0x00, 0x76, 0x58, 0x71, 0x75, 0x69, 0x71, 0x71, 0x6c, 0x57, 0x52, 0x47, 0x49, 
		0x72, 0x32, 0x47, 0x56, 0x70, 0x72, 0x4f, 0x32, 0x2b, 0x77, 0x3d, 0x3d, 0x00, 0x59, 0x67, 0x4c, 0x58, 0x16, 0x76, 0xac, 
		0x6c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0x58, 0x71, 0x75, 0x69, 0x71, 0x71, 0x6c, 0x57, 0x52, 0x47, 0x49, 
		0x72, 0x32, 0x47, 0x56, 0x70, 0x72, 0x4f, 0x32, 0x2b, 0x77, 0x3d, 0x3d, 0x4c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
		0x76, 0x58, 0x71, 0x75, 0x69, 0x71, 0x71, 0x6c, 0x57, 0x52, 0x47, 0x49, 0x72, 0x32, 0x47, 0x56, 0x70, 0x72, 0x4f, 0x32, 
		0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x69, 0x71, 0x71, 0x6c, 0x57, 0x52, 0x47, 0x49, 0x72, 0x32, 0x47, 0x56, 
		0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0x58, 0x71, 0x75, 0x69, 0x71, 0x71, 0x6c, 0x0c, 0x00, 0x00, 0x00, 
		0x00, 0x00, 0x00, 0x00, 0x76, 0x58, 0x71, 0x75, 0xb3, 0x06, 0x24, 0x41, 0x11, 0x09, 0x83, 0x27, 0xc9, 0xff, 0x23, 0xa0, 
		0xf4, 0x80, 0x83, 0xc7, 0x64, 0x80, 0x93, 0xe7, 0x07, 0x01, 0x03, 0xc7, 0x64, 0x80, 0x62, 0x07, 0x61, 0x87, 0xe3, 0x4c, 
		0x07, 0xfe, 0x23, 0x83, 0xf4, 0x80, 0x03, 0xc7, 0x64, 0x80, 0x62, 0x07, 0x61, 0x87, 0xe3, 0x4c, 0x07, 0xfe, 0x00, 0x00, 
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    UINT16 PIC_Number;
    UINT16 Block_Number;
	
    Start_Flash_power();
    Set_Spi1_output_init();

	SPI_FLASH_ReadManuID_DeviceID(0x000000);
	WWDG_SetCounter(0);//喂狗 , 不可以在这里， 没有效果

    // Init Data
#if 1  
	k=0;
	for(i=0;i<256;i++)
	{
		Buffer[i]=k;
		k++;
	}        

	printf("\r\n--spi flash test-b--\r\n");   

	// 6色屏 ，一张最大 187K  ==>  4M=4x1024 =4096 / 187 =21
	// PIC_Number    0 -- 20  (存 21 张图片)
	// Block_Number  0 -- 747  （187*1024/256=748） 最多 748 块
	// Buffer  一次 256 （不可修改。如果不够，补成 0xff）    
	PIC_Number=0;
	TDX_SPI_FLASH_E_4096Bytes(PIC_Number,0,EXTERN_FLASH_PIC_SIZE);
	Block_Number=0;   
	TDX_SPI_FLASH_W_256Bytes(Buffer1,PIC_Number,Block_Number,EXTERN_FLASH_PIC_SIZE);
#endif

	for(i=0;i<256;i++)
	{
		Buffer[i]=0;
	}      

	// 6色屏 ，一张最大 187K  ==>  4M=4x1024 =4096 / 187 =21
	// PIC_Number    0 -- 20  (存 21 张图片)
	// Block_Number  0 -- 747  （187*1024/256=748）最多 748 块
	// Buffer  一次 256 （不可修改。如果不够，补成 0xff）    
	PIC_Number=0;
	Block_Number=0;      
	TDX_SPI_FLASH_Read_256Bytes(Buffer,PIC_Number,Block_Number,EXTERN_FLASH_PIC_SIZE);

	WWDG_SetCounter(0);//喂狗 , 不可以在这里， 没有效果
	for(i=0;i<256;i++)
	{
	    printf(" 0x%x ",Buffer[i]);
	}        
	SPI_FLASH_CS_HIGH();
	SPI_FLASH_ReadManuID_DeviceID(0x000000);

	printf("\r\n--spi flash test-over--\r\n");   
}
#endif

unsigned char slave_buffer[EXTERN_FLASH_BUFFER_SIZE_EXTERNAL];
int buffer_offset = 0;

// Pad the unwritten tail of the last 256-byte flash block with a literal value.
// This keeps the debug/presave readback path from feeding stale bytes into the
// decompressor when the final payload chunk is shorter than one flash page.
#define EXTERN_FLASH_PAD_BYTE 0x11

void initExternFlashDriver()
{
    UINT32 jedec, ready_start;
	Start_Flash_power();
	Set_Spi1_output_init();  
    if(!flash_power_ready) {
#if ((defined(ENABLE_INK_SCREEN_SPD1657_800X480_COLOR_6))) //6鑹查瀛樺垏鎹㈤棶棰橈紝鍗曠嫭鍋氬欢鏃跺鐞?
	#if TDX_FLASH_POWERUP_DELAY_MS
	delay_ms(TDX_FLASH_POWERUP_DELAY_MS);
        BOOT_LOG_HEX32("FLASH powerMs=", TDX_FLASH_POWERUP_DELAY_MS);
	#endif
#endif	
        ready_start = RTC_GetCycle32k();
        do {
            jedec = SPI_FLASH_ReadJedecID();
            if(jedec == EXTERN_FLASH_ID) {
                flash_power_ready = 1;
                flash_ready_failed = 0;
                BOOT_LOG_HEX32("FLASH jedec=", jedec);
                BOOT_LOG_HEX32("FLASH readyMs=", (flash_wait_elapsed(ready_start) * 1000U) / CAB_LSIFQ);
                break;
            }
            WWDG_SetCounter(0);
        } while(flash_wait_elapsed(ready_start) < (CAB_LSIFQ / 5U));
        if(!flash_power_ready) {
            flash_ready_failed = 1;
            BOOT_LOG_TEXT("FLASH ready-timeout\r\n");
            BOOT_LOG_HEX32("FLASH jedec=", jedec);
            BOOT_LOG_HEX32("FLASH readyMs=", (flash_wait_elapsed(ready_start) * 1000U) / CAB_LSIFQ);
        }
    }
	SPI_FLASH_ReadManuID_DeviceID(0x000000);
	WWDG_SetCounter(0);//喂狗 , 不可以在这里， 没有效果
}

void deInitExternFlashDriver()
{
	WWDG_SetCounter(0);
	SPI_FLASH_CS_HIGH();
	SPI_FLASH_ReadManuID_DeviceID(0x000000);
	//Stop_Flash_power();
}

void readDataFromExternFlash(UINT8* buffer, UINT8 index, UINT32 blocknum, UINT16 blocksize)
{
	TDX_SPI_FLASH_Read_256Bytes(buffer,index,blocknum,blocksize);
}

int saveDataFromExternFlash(UINT8* buffer, UINT8 index, UINT32 blocknum, UINT16 blocksize)
{
	TDX_SPI_FLASH_W_256Bytes(buffer,index,blocknum,blocksize);
	return 0;
}

//#define FALSH_TEST_DEBUG
void read256DataFromExternFlash(UINT8* buffer, UINT8 index, UINT32 block)
{
	readDataFromExternFlash(buffer, index, block, EXTERN_FLASH_PIC_SIZE);
#ifdef FALSH_TEST_DEBUG
	Print_I3("read flash ....... start block:%d index:%d fPackageCntx:%d",block,index,
				global_DEVICE_STATUS.fPackageCnt);
	hex_dump(buffer, EXTERN_FLASH_BUFFER_SIZE_EXTERNAL);
	Print_I3("read flash ....... end ");
#endif
}

UINT8 save256DataToExternFlashTmp(UINT8 *data, UINT32 size, UINT8 index, UINT32 block) 
{    
	UINT8 ret = 0;
    for (int i = 0; i < size; i++) {
        slave_buffer[buffer_offset++] = data[i];
        
        if (buffer_offset == EXTERN_FLASH_BUFFER_SIZE_EXTERNAL) {
			if(block%16 == 0)
				TDX_SPI_FLASH_E_4096Bytes(index,block,EXTERN_FLASH_PIC_SIZE);
#ifdef FALSH_TEST_DEBUG
			Print_I3("host_send_packet data @@@@@@ global_DEVICE_STATUS.fPackageCnt:%d fPackageNum:%d",global_DEVICE_STATUS.fPackageCnt,
				global_DEVICE_STATUS.fPackageCount);
            Print_I3("write flash ....... block:%d index:%d fPackageCntx:%d",block,index,
				global_DEVICE_STATUS.fPackageCnt);
			hex_dump(slave_buffer, EXTERN_FLASH_BUFFER_SIZE_EXTERNAL);
			Print_I3("write flash ....... end");
#endif
            TDX_SPI_FLASH_W_256Bytes(slave_buffer,index,block,EXTERN_FLASH_PIC_SIZE);
#ifdef FALSH_TEST_DEBUG
			unsigned char PicData[EXTERN_FLASH_BUFFER_SIZE_EXTERNAL];
		    memset(PicData,0,EXTERN_FLASH_BUFFER_SIZE_EXTERNAL);
		    read256DataFromExternFlash(PicData,index,block);
#endif
			global_EXTERN_FLASH_INFO.fBlockNum++;
            buffer_offset = 0;
			ret = 1;
        }
    }

	return ret;
}

int save256DataToExternFlash(UINT8 *data, UINT32 size, UINT8 index, UINT32 block) {
	UINT8 ret = 0;
	
	ret = save256DataToExternFlashTmp(data, size, index, block);

	block = global_EXTERN_FLASH_INFO.fBlockNum;
    //Print_I3("host_split_and_senddata #### global_DEVICE_STATUS.fPackageCnt:%d fPackageNum:%d",global_DEVICE_STATUS.fPackageCnt,
	//			global_DEVICE_STATUS.fPackageCount);
    if(global_DEVICE_STATUS.fPackageCnt + 1 == global_DEVICE_STATUS.fPackageCount){
	    if (buffer_offset > 0) {
			if(block%16 == 0)
				TDX_SPI_FLASH_E_4096Bytes(index,block,EXTERN_FLASH_PIC_SIZE);
	        memset(&slave_buffer[buffer_offset], EXTERN_FLASH_PAD_BYTE, EXTERN_FLASH_BUFFER_SIZE_EXTERNAL - buffer_offset);
#ifdef FALSH_TEST_DEBUG
			Print_I3("end data save=======================================");
			Print_I3("write flash ....... block:%d index:%d fPackageCntx:%d",block,index,global_DEVICE_STATUS.fPackageCnt);
			hex_dump(slave_buffer, EXTERN_FLASH_BUFFER_SIZE_EXTERNAL);
			Print_I3("write flash ....... end");
#endif
	        TDX_SPI_FLASH_W_256Bytes(slave_buffer,index,block,EXTERN_FLASH_PIC_SIZE);
#ifdef FALSH_TEST_DEBUG
			unsigned char PicData[EXTERN_FLASH_BUFFER_SIZE_EXTERNAL];
		    memset(PicData,0,EXTERN_FLASH_BUFFER_SIZE_EXTERNAL);
		    read256DataFromExternFlash(PicData,index,block);
#endif
			global_EXTERN_FLASH_INFO.fBlockNum++;
	        buffer_offset = 0;
	    }
	}

	return ret;
}

bStatus_t eraseExternFLashData(unsigned char index) 
{
	return SUCCESS;
}

// 以 256bytes 为单位，一次最少写 244 bytes
// Addr = 表示 第几个 244 块
void TDX_SPI_FLASH_Read_244Bytes(UINT8* pBuffer,UINT16 PIC_Number,UINT16 Block_Number,UINT16 F_type)
{
  UINT32 Addr;
  UINT16 NumByteToRead;  
  //UINT8  u8dat;

  Addr=(1024*F_type)*PIC_Number+Block_Number*244;
  printf("R-Addr=%X\r\n",Addr);  
  
  SPI_FLASH_CS_LOW();
  SPI1_MasterSendByte(Read_Data);  //FastReadData
  
  SPI1_MasterSendByte((Addr & 0xFF0000) >> 16);
  SPI1_MasterSendByte((Addr & 0xFF00) >> 8);  
  SPI1_MasterSendByte(Addr & 0xFF);

  for(NumByteToRead=0;NumByteToRead<244;NumByteToRead++)
  //for(NumByteToRead=0;NumByteToRead<1024;NumByteToRead++)
  {
       //u8dat=SPI1_MasterRecvByte();
       pBuffer[NumByteToRead]=SPI1_MasterRecvByte();
  }        

  SPI_FLASH_CS_HIGH();
}

// 以 256bytes 为单位，一次最少写 244 bytes
// Addr = 表示 第几个 244 块
void TDX_SPI_FLASH_Read_244Bytes_By_addr(UINT8* pBuffer,UINT16 PIC_Number,UINT16 Block_Number,UINT16 F_type,UINT32 off_Addr)
{
  UINT32 Addr;
  UINT16 NumByteToRead;  
  //UINT8  u8dat;
  
  Addr=(1024*F_type)*PIC_Number+Block_Number*244;
  Addr += off_Addr;
  printf("R-Addr=%X %X\r\n",Addr,off_Addr);  
  
  SPI_FLASH_CS_LOW();
  SPI1_MasterSendByte(Read_Data);  //FastReadData
  
  SPI1_MasterSendByte((Addr & 0xFF0000) >> 16);
  SPI1_MasterSendByte((Addr & 0xFF00) >> 8);  
  SPI1_MasterSendByte(Addr & 0xFF);

  for(NumByteToRead=0;NumByteToRead<244;NumByteToRead++)
  //for(NumByteToRead=0;NumByteToRead<1024;NumByteToRead++)
  {
       //u8dat=SPI1_MasterRecvByte();
       pBuffer[NumByteToRead]=SPI1_MasterRecvByte();
  }

  SPI_FLASH_CS_HIGH();
}

