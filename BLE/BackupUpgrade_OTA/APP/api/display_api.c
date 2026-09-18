#include "app_cfg.h"
#include "CONFIG.h"
//EPD

#include "commoninfo.h"
#include "epd_driver.h"
#include "Display_EPD_W21_spi.h"
#include "rledecode.h"
#include "release_trace.h"
#include "img_perf.h"
#include "tdx_image_stream.h"

#if TDX_STORE_ZLIB
static UINT8 s_refresh_deferred;
void EPD_SetRefreshDeferred(UINT8 deferred)
{
    s_refresh_deferred = deferred;
}
#endif

#if defined(ENABLE_INK_SCREEN_SPD1657_800X480_COLOR_6) && defined(ENABLE_SCREEN_COLOR_6)
#define EPD_BATCH_DATA 1
/* A 256-byte Flash block expands to at most 128 * 256 bytes. */
static unsigned char imageBatchBuffer[32768U];
#if TDX_STORE_ZLIB
static UINT8 imageBatchSending;
#if IMG_COLOR_LUT
static const UINT8 imageColorMap[16] = {
    0, 1, 2, 3, 5, 6, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};
static inline UINT8 ImageColorByte(UINT8 value)
{
    UINT8 high = imageColorMap[value >> 4], low = imageColorMap[value & 15U];
    return (high | low) == 0xff ? 0xff : (UINT8)((high << 4) | low);
}
#endif
#endif
typedef int (*ImgDataCallback_t)(unsigned char *pData, UINT32 Len);
#else
#define EPD_BATCH_DATA 0
typedef RleImgDataCallback_t ImgDataCallback_t;
#endif

ImgDataCallback_t dataimgCb = NULL;
int callbackValue = 0;
// ???????????????????????????????I?????
#define PENDING_NONE 0xBB
static UINT8 pendingCompressByte = PENDING_NONE;  // ?????????????????????
int ImgDataCallBack_flag=0;

static UINT32 epd_stage_elapsed(UINT32 start)
{
    UINT32 now = RTC_GetCycle32k();
    return now >= start ? now - start : RTC_MAX_COUNT - start + now;
}

static UINT32 epd_stage_ms(UINT32 ticks)
{
    return (ticks / CAB_LSIFQ) * 1000U + ((ticks % CAB_LSIFQ) * 1000U) / CAB_LSIFQ;
}

#ifdef ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4
#define UC8579_CLEAN_POWER_PREPARE_MS    25
#endif

#if (defined(ENABLE_INK_SCREEN_SPD1657_800X480_COLOR_6))
static void PreparePowerBeforeDeviceInit(void)
{
	UINT32 stage_start = RTC_GetCycle32k();
	ControlEPDPower(Is_On);
	BOOT_LOG_HEX32("T EPD_POWER=", epd_stage_ms(epd_stage_elapsed(stage_start)));
	#if !TDX_DISABLE_EPD_DELAY
	DelayMs(200);
	BOOT_LOG_TEXT("T EPD_WAIT=200ms\r\n");
	#else
	DelayMs(10);
	#endif
}
#endif

void EPD_W21_Reset_Spi2_AB(){
#ifdef ENABLE_SCREEN_COLOR_6
	SPI_NEW_RST_A_0;// Module reset
	SPI_NEW_RST_B_0;
		
#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
	DelayMs(100);
#else
	#if !TDX_DISABLE_EPD_DELAY
	DelayMs(5);//At least 10ms delay 
	#else
		DelayMs(2);//At least 10ms delay
	#endif
#endif
		
	SPI_NEW_RST_A_1;
	SPI_NEW_RST_B_1;
#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
	DelayMs(100);
#endif
#else
	SPI_RST_A_0;// Module reset
	SPI_RST_B_0;
	DelayMs(1+1);//At least 10ms delay 
	SPI_RST_A_1;
	SPI_RST_B_1;
#endif
}
void EPD_W21_Reset_Spi2_A_or_B(){
#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
	if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_A)
    	{	
		Print_I3("RST2 M A");
        	SPI_NEW_RST_A_0;// Module reset
			DelayMs(100);
			SPI_NEW_RST_A_1;
			DelayMs(100);
    	}
  	else if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_B)
    	{
		Print_I3("RST2 M B");
			SPI_NEW_RST_B_0;// Module reset
			DelayMs(100);
			SPI_NEW_RST_B_1;
			DelayMs(100);
    	}
	else if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_AB)
    	{
		Print_I3("RST2 M AB");
			SPI_NEW_RST_A_0;// Module reset
			SPI_NEW_RST_B_0;
	        DelayMs(100);
			SPI_NEW_RST_A_1;
			SPI_NEW_RST_B_1;
			DelayMs(100);
    	}
#elif !defined(ENABLE_SCREEN_COLOR_6)
	if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_A)
    	{	
		Print_I3("RST2 A");
        	SPI_RST_B_0;;// Module reset
		DelayMs(1+1);//At least 10ms delay 
		SPI_RST_B_1;
    	}
  	else if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_B)
    	{
		Print_I3("RST2 B");
		SPI_RST_A_0;// Module reset
		DelayMs(1+1);//At least 10ms delay 
		SPI_RST_A_1;
    	}
	else if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_AB)
    	{
		Print_I3("RST2 AB");
		SPI_RST_A_0;// Module reset
		SPI_RST_B_0;
        DelayMs(1+1);//At least 10ms delay 
		SPI_RST_A_1;
		SPI_RST_B_1;
    	}
#endif
}

void EPD_Control_AB(){
#if (defined(ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3))
	if(global_DEVICE_STATUS.fisHost == IS_HOST){
		Print_I3("CS AB H");
		SPI_CS_A_0;
		SPI_CS_A_SLAVE_1;
		SPI_CS_B_0;
		SPI_CS_B_SLAVE_1;
	}
	else{
		Print_I3("CS AB S");
		SPI_CS_A_1;
		SPI_CS_A_SLAVE_0;
		SPI_CS_B_1;
		SPI_CS_B_SLAVE_0;
	}
#else
	SPI_CS_B_0;
	SPI_CS_A_0;
#if (defined(ENABLE_INK_SCREEN_JD79686BB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4)) || (defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4))
	SPI_CS_A_SLAVE_1;
	SPI_CS_B_SLAVE_1;
#endif
#endif
}

void EPD_Control_A_Or_B(){

	if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_A)
   	{	
		Print_I3("PWR6 A");
       	SPI_CS_A_1;
       	SPI_CS_B_0;
#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
		SPI_CS_A_SLAVE_1;
    	SPI_CS_B_SLAVE_0;
#endif
   	}
  	else if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_B)
   	{
		Print_I3("PWR6 B");
       	SPI_CS_B_1;
       	SPI_CS_A_0;
#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
		SPI_CS_A_SLAVE_0;
		SPI_CS_B_SLAVE_1;
#endif
   	} 
	else if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_AB)
   	{
		Print_I3("PWR6 AB");
       	SPI_CS_B_0;
       	SPI_CS_A_0;
#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
		SPI_CS_A_SLAVE_0;
		SPI_CS_B_SLAVE_0;
#endif

		
   	}
}

/**
 * @brief i??????????
 */
void EPD_W21_Reset_Spi2(){
#ifdef ENABLE_SCREEN_COLOR_6
#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
	EPD_W21_Reset_Spi2_A_or_B();
#else
	EPD_W21_Reset_Spi2_AB();
#endif
#else
	EPD_W21_Reset_Spi2_A_or_B();
#endif
}
void EPD_W21_Reset(void)
{
	EPD_W21_Reset_Spi2();
}

/**
 * @brief i????????'??
 * @return 0: ???, ??0: ???
 */
int DeviceInit(){
	global_DEVICE_STATUS.fWorked=Is_Yes;
	Init_GPIO(); 
	Set_Spi0_output_init();
	
    // Force all CS lines high first so each chip starts from a known idle level.
    SPI_CS_A_1;
    SPI_CS_B_1;
#if defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6) || defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4)
    SPI_CS_A_SLAVE_1;
    SPI_CS_B_SLAVE_1;
#endif
	#if !TDX_DISABLE_EPD_DELAY
	DelayMs(5);
	#endif
}

void ControlEPDPower(UINT8 ison)
{
	UINT8 power_switch = Is_On;
#ifdef HARDWAR_DRY_CELL
	power_switch = Is_Off; //?????,????
#endif

	GPIOA_ModeCfg(LCD_Power_A, GPIO_ModeOut_PP_5mA);
#if defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4)
	GPIOB_ModeCfg(LCD_Power_B, GPIO_ModeOut_PP_5mA);
#endif
	if(ison == power_switch)
	{
		R32_PA_OUT |= LCD_Power_A;
#if defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4)
		R32_PB_OUT |= LCD_Power_B;
#endif
	}else
	{
		R32_PA_CLR |= LCD_Power_A;
#if defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4)
		R32_PB_CLR |= LCD_Power_B;
#endif
	}
}

/**
 * @brief i??????????
 */
int DevicePower(){
	ControlEPDPower(Is_On);
	
    // Force all CS lines high first, then switch to the target side.
    SPI_CS_A_1;
    SPI_CS_B_1;
#if defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6) || defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4)
    SPI_CS_A_SLAVE_1;
    SPI_CS_B_SLAVE_1;
#endif
    #if !TDX_DISABLE_EPD_DELAY
    DelayMs(5);  // Let CS settle at high level.
    #endif
    
#ifdef ENABLE_SCREEN_COLOR_6
	EPD_Control_A_Or_B();
#else
#if (defined(ENABLE_INK_SCREEN_JD79686BB_800X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_UC8279_800X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_UC8179_800X480_COLOR_2)) || (defined(ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2))
	EPD_Control_A_Or_B();
#else
	EPD_Control_AB();
#endif
#endif
}

/**
 * @brief ???EPD????
 */
void Display_EPD_AB()
{
#if TDX_STORE_ZLIB
    if(s_refresh_deferred) return;
#endif
	Print_I3("EPD_AB h=%d\r\n", global_DEVICE_STATUS.fisHost);
#if (defined(ENABLE_INK_SCREEN_JD79686BB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4)) || (defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2)) || (defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6)) || (defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4))
		// Split host/slave panels refresh only after the slave half is written.
		if(global_DEVICE_STATUS.fisHost == IS_SLAVE)
		{	
			Print_I3("AB S ref\r\n");
#if (!defined(ENABLE_INK_SCREEN_JD79686BB_1360X480_COLOR_3)) && (!defined(ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4)) && (!defined(ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2))
			SPI_CS_A_0;
			SPI_CS_A_SLAVE_0;
			SPI_CS_B_0;
			SPI_CS_B_SLAVE_0;
#endif
			Display_EPD_Driver();
		}
		else
		{
			Print_I3("AB H wait\r\n");
			SPI_CS_A_1;
			SPI_CS_B_1;
#if (defined(ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79686BB_1360X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4)) || (defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6)) || (defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4))
			SPI_CS_A_SLAVE_1;
			SPI_CS_B_SLAVE_1;
#endif
			return;
		}
#else
		Display_EPD_Driver();
#endif

	SPI_CS_A_1;
	SPI_CS_B_1;
#if (defined(ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79686BB_1360X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4)) || (defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6)) || (defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4))
	SPI_CS_A_SLAVE_1;
	SPI_CS_B_SLAVE_1;
#endif
	tmos_stop_task(main_task_ID,EVENT_Check_TimeOut);

	if(global_DEVICE_STATUS.fIsNeedStandby == 1){
		tmos_set_event(main_task_ID,EVENT_Low_Power);
	}
}

/**
 * @brief ??????????
 * @param colorData ?'???????
 * @return ??????????????
 */
UINT8 CovertColorData(UINT8 color_data){

	switch(color_data){
		case 0x00:		//black
		return 0x00;//0x00;
		case 0x01:		//white
		return 0x01;//0x11;
		case 0x02:		//yellow
		return 0x02;//0x22;
		case 0x03:		//red
		return 0x03;//0x33;
		case 0x04:		//blue
		return 0x05;//0x55;
		case 0x05:		//green
		return 0x06;//0x66;
	}
}

/**
 * @brief ??????????????
 * @param data ??????????
 * @return ???????????
 */
UINT8 isNeedDecompress(UINT8 by, UINT8 type)
{
#if (defined(ENABLE_SCREEN_COLOR_3)) || (defined(ENABLE_SCREEN_COLOR_2))
    return ((by == 0x00) || (by == 0xFF)) ? IS_NEED_DECMPRESS : IS_NONEED_DECMPRESS;
#endif
#ifdef ENABLE_SCREEN_COLOR_4
    return ((by == 0x00) || (by == 0xFF) || (by == 0x55) || (by == 0xAA)) ? IS_NEED_DECMPRESS : IS_NONEED_DECMPRESS;
#endif
}

/**
 * @brief ??????????????
 * @param pData ?????????
 * @param length ???????
 * @param zip ??????
 * @return 0: ???
 */
UINT8 PIC_Display_Compress_Data(const unsigned char* pBW, UINT16 Length, unsigned char zip)
{
#if EPD_BATCH_DATA
    UINT32 i, outputLen = 0;
    UINT32 perf_color = IP_Start(IP_COLOR);
    UINT16 count;
UINT8 value;
    unsigned char *out = imageBatchBuffer;

    if(Length == 0) return 0;
    if(pBW == NULL) goto batch_error;
    if(zip == IS_NEED_DECMPRESS) {
        if(Length & 1U) goto batch_error;
        for(i = 1; i < Length; i += 2) {
            outputLen += pBW[i];
#ifndef ENABLE_SOFTWARE_TO_BOE
            outputLen++;
#endif
        }
    } else {
        outputLen = Length;
    }
#if TDX_STORE_ZLIB
    /* Ignore only bytes beyond the complete image (legacy Flash page padding). */
    {
        UINT32 offset = global_DEVICE_STATUS.fInitDriver == Is_Yes ? 0 : global_DEVICE_STATUS.fImageDataLen;
        UINT32 maxLen = EPD_GetDisplayMaxBuf();
        if(offset >= maxLen) return 0;
        if(outputLen > maxLen - offset) outputLen = maxLen - offset;
    }
#endif
    /* Validate the complete batch before writing or calling the panel. */
    if(outputLen > sizeof(imageBatchBuffer)) goto batch_error;
#if TDX_STORE_ZLIB && IMG_COLOR_LUT
    if(zip != IS_NEED_DECMPRESS) {
#if TDX_LARGE_RAW_COLOR_DISABLE
        memcpy(out, pBW, outputLen);
#else
        for(i = 0; i < outputLen; i++) {
            value = ImageColorByte(pBW[i]);
            if(value == 0xff) goto batch_error;
            out[i] = value;
        }
#endif
    } else {
        for(i = 0; i < Length && out < imageBatchBuffer + outputLen; i += 2) {
            value = ImageColorByte(pBW[i]);
            if(value == 0xff) goto batch_error;
            count = (UINT16)pBW[i + 1] + 1U;
            if(count > imageBatchBuffer + outputLen - out)
                count = (UINT16)(imageBatchBuffer + outputLen - out);
            memset(out, value, count);
            out += count;
        }
    }
#else
    for(i = 0; i < Length; ) {
#if TDX_STORE_ZLIB
        if(out >= imageBatchBuffer + outputLen) break;
#endif
        value = pBW[i++];
        count = 1;
        if(zip == IS_NEED_DECMPRESS) {
            count = pBW[i++];
#ifndef ENABLE_SOFTWARE_TO_BOE
            count++;
#endif
        }
#ifdef ENABLE_SOFTWARE_TO_BOE
        if(zip != IS_NEED_DECMPRESS)
#endif
        {
            value = (CovertColorData((value >> 4) & 0x0f) << 4) |
                     CovertColorData(value & 0x0f);
        }
#if TDX_STORE_ZLIB
        if(count > imageBatchBuffer + outputLen - out)
            count = (UINT16)(imageBatchBuffer + outputLen - out);
#endif
        if(count == 1) *out = value;
        else memset(out, value, count);
        out += count;
    }
#endif
    IP_Toc(IP_COLOR, perf_color);
    if(outputLen && dataimgCb != NULL) {
        callbackValue = dataimgCb(imageBatchBuffer, outputLen);
    }
    return 0;

batch_error:
    IP_Toc(IP_COLOR, perf_color);
    BOOT_LOG_TEXT("IMG batch err\r\n");
    callbackValue = -1;
    return 1;
#elif (defined(ENABLE_SCREEN_COLOR_3)) || (defined(ENABLE_SCREEN_COLOR_4)) || (defined(ENABLE_SCREEN_COLOR_2))
    UINT8 currentByte;       // ??j?????????
    UINT16 i = 0;            // ????????
    UINT16 len;              // ??????????
    UINT16 j;                // ?????????

    /* ZLIB_COLOR_V1 has already expanded legacy RLE before writing Flash. */
    if(zip != IS_NEED_DECMPRESS) {
        for(i = 0; i < Length; i++) {
            if(dataimgCb != NULL) callbackValue = dataimgCb(pBW[i]);
        }
        return 0;
    }
    // 1. ??????????????????????????????????
    if (pendingCompressByte != PENDING_NONE) {
        currentByte = pendingCompressByte;
        pendingCompressByte = PENDING_NONE;  // ???????????

        // ????????j?????????????????????????
        len = pBW[i];
        i++;  // ???????????

        // ???? len+1 ??????????????len=0????????????????
        for (j = 0; j < len + 1; j++) {
            if (dataimgCb != NULL) {
                callbackValue = dataimgCb(currentByte);
            }
        }
    }

    // 2. ??????j???????????
    while (i < Length) {
        currentByte = pBW[i];

        // ????j?????????????0x00/0xFF??
        if (isNeedDecompress(currentByte, 0) == IS_NEED_DECMPRESS) {
            // ?????????????????????????
            if (i + 1 >= Length) {
                // ???????j????????????????????????????????
                pendingCompressByte = currentByte;
                break;  // ????????????????????
            }

            // ???????????????????????
            i++;  // ?z??????????
            len = pBW[i];

            // ???? len+1 ????????????????len=0??len>0??
            for (j = 0; j < len + 1; j++) {
                if (dataimgCb != NULL) {
                    callbackValue = dataimgCb(currentByte);
                }
            }
        }
        // ?????????????????j???
        else {
            if (dataimgCb != NULL) {
                callbackValue = dataimgCb(currentByte);
            }
        }

        i++;  // ????????????
    }
#else
	UINT8  By;
	UINT16 i,j,Len;
	UINT8  aData,aData_h,aData_l;

	if(zip == IS_NEED_DECMPRESS){
		for(i=0;i<Length/2;i++)
		{
			By=pBW[2*i];
#ifdef ENABLE_SOFTWARE_TO_BOE
			aData = By;
			Len= pBW[2*i+1];  
#else
			aData_h = CovertColorData((By>>4)&0x0f)<<4;
			aData_l = CovertColorData(By&0x0f);
			aData=aData_h|aData_l;
			Len= pBW[2*i+1] + 1; 
#endif
			for (j = 0; j < Len; j++) {
				if (dataimgCb != NULL) {
					callbackValue = dataimgCb(aData);
				}
			}
		}
	}else{
		for(i=0; i<Length; i++){
			By = pBW[i];
			aData_h = CovertColorData((By>>4)&0x0f)<<4;
			aData_l = CovertColorData(By&0x0f);
			aData = aData_h|aData_l;
			if (dataimgCb != NULL) {
	      		callbackValue = dataimgCb(aData);
	    	}
		}
	}
#endif
    return 0;
}

/**
 * @brief Write image data into the panel buffer.
 * @param pData/data Input batch (SPD1657) or byte (other panels).
 * @param dataLen Batch byte count (SPD1657 only).
 * @param length Current image offset.
 */
#if EPD_BATCH_DATA
void Display_Picture_To_Color(unsigned char *pData, UINT32 dataLen, UINT32 length){
    UINT32 endOffset, maxLen = EPD_GetDisplayMaxBuf();
    UINT32 perf_init = 0;
    UINT32 stage_start;
#if TDX_STORE_ZLIB
    UINT8 saved_div, saved_delay;
#endif
    UINT16 sendLen;
    if(pData == NULL || dataLen == 0 || length >= maxLen) return;
    if(dataLen > maxLen - length) {
        dataLen = maxLen - length;
    }
    endOffset = length + dataLen;
#else
void Display_Picture_To_Color(unsigned char data, int length){
	UINT8  By;
	int i,wLen;

	if(length >= EPD_GetDisplayMaxBuf()){
		return;
	}
#endif

	if(global_DEVICE_STATUS.fInitDriver == Is_Yes){
#if EPD_BATCH_DATA
        BOOT_LOG_HEX32("IMG batch=", dataLen);
        perf_init = IP_Start(IP_INIT);
#else
		Print_I3("PIC len:%d",length);
		IMAGE_LOG_TEXT("IMG panel-data begin\r\n");
#endif
		// Reset host/slave state at the start of every new image.
		// Otherwise the second half of the previous refresh can leak into the next frame.
#if (defined(ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4)) || (defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4)) || (defined(ENABLE_INK_SCREEN_SSD1683_272X792_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2)) || (defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6)) || (defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4))
		global_DEVICE_STATUS.fisHost = IS_HOST;
		ImgDataCallBack_flag = 0;
#endif
		global_DEVICE_STATUS.fImageDataLen = 0;
		pendingCompressByte = PENDING_NONE; // reset pending compress flag when starting a new image
#if (defined(ENABLE_INK_SCREEN_SPD1657_800X480_COLOR_6))
		PreparePowerBeforeDeviceInit(); // Keep the SPD1657 power-up sequence unchanged.
#endif
		stage_start = RTC_GetCycle32k();
		DeviceInit();
		BOOT_LOG_HEX32("T DEV_INIT=", epd_stage_ms(epd_stage_elapsed(stage_start)));
		global_DEVICE_STATUS.fInitDriver = Is_No;
	}

	if(length == SCREEN_DATA_START){ // Black and white color
#if !EPD_BATCH_DATA
		Print_I3("BW len:%d",length);
#endif
#if defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6)
		if(global_DEVICE_STATUS.fisHost == Is_HOST){
			DevicePower();
			DelayMs(20);
		}
		else{
			Print_I3("M S cont");
		}
#else
		stage_start = RTC_GetCycle32k();
		DevicePower();
		BOOT_LOG_HEX32("T DEV_PWR=", epd_stage_ms(epd_stage_elapsed(stage_start)));
		stage_start = RTC_GetCycle32k();
		#if !TDX_DISABLE_EPD_DELAY
		DelayMs(20);
		#endif
		BOOT_LOG_HEX32("T WAIT20=", epd_stage_ms(epd_stage_elapsed(stage_start)));
#endif
#if (defined(ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2)) || (defined(ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4))
		if(global_DEVICE_STATUS.fisHost == Is_HOST){
			Init_EPD_Driver();
		}
#elif (defined(ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4)) || (defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6)) || (defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4))
		if(global_DEVICE_STATUS.fisHost == Is_HOST){
			SPI_CS_A_0;
			SPI_CS_B_0;
#if !defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6)
			SPI_CS_A_SLAVE_0;
			SPI_CS_B_SLAVE_0;
#endif
			Init_EPD_Driver();
		}
#if (defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4)) || (defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6)) || (defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4))
		if(global_DEVICE_STATUS.fisHost == IS_SLAVE){
			SPI_CS_A_SLAVE_0;
			SPI_CS_B_SLAVE_0;
		}
#endif
#else
#if (defined(ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4))
		if(global_DEVICE_STATUS.fisHost == Is_HOST){
			Init_EPD_Driver();
		}
#else
		stage_start = RTC_GetCycle32k();
		Init_EPD_Driver();
		BOOT_LOG_HEX32("T EPD_INIT=", epd_stage_ms(epd_stage_elapsed(stage_start)));
#endif
#endif
#ifdef ENABLE_SCREEN_COLOR_3
		Init_display_Bw();
#endif
#if (defined(ENABLE_SCREEN_COLOR_6)) || (defined(ENABLE_SCREEN_COLOR_4)) || (defined(ENABLE_SCREEN_COLOR_2))
		Init_display_Red();
#endif
	}	

#ifdef ENABLE_SCREEN_COLOR_3
	if(length == SCREEN_BLACK_WHITE_COLOR_3_MAX){
		Print_I3("R len:%d",length);
		DevicePower();
		Init_display_Red();
	}
#endif

#if EPD_BATCH_DATA
#if TDX_STORE_ZLIB
    if(imageBatchSending && length == SCREEN_DATA_START) IP_Toc(IP_INIT, perf_init);
    saved_div = R8_SPI0_CLOCK_DIV;
    saved_delay = R8_SPI0_CTRL_CFG & RB_SPI_MST_DLY_EN;
    if(imageBatchSending) SPI0_CLKCfg(IMG_PANEL_SPI_DIV);
    IP_Pixels(imageBatchSending);
#endif
    while(dataLen) {
        UINT32 perf_spi = 0;
#if TDX_STORE_ZLIB
        if(imageBatchSending) perf_spi = IP_Start(IP_SPI);
#endif
        sendLen = dataLen > 4095U ? 4095U : (UINT16)dataLen;
        SPI0_MasterTrans(pData, sendLen);
        /* FIFO empty does not guarantee that the last byte is off the wire. */
        while(!(R8_SPI0_INT_FLAG & RB_SPI_FREE));
#if TDX_STORE_ZLIB
        if(imageBatchSending) IP_Toc(IP_SPI, perf_spi);
#endif
        pData += sendLen;
        dataLen -= sendLen;
    }
#if TDX_STORE_ZLIB
    IP_Pixels(0);
    /* Restore before commands, refresh or returning to the Flash reader. */
    R8_SPI0_CLOCK_DIV = saved_div;
    R8_SPI0_CTRL_CFG = (R8_SPI0_CTRL_CFG & ~RB_SPI_MST_DLY_EN) | saved_delay;
#endif
#elif (defined(ENABLE_INK_SCREEN_JD79686BB_800X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_SSD1863_400X300_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_UC8179_800X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_UC8279_800X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_SSD1683_272X792_COLOR_3))
	if(length < SCREEN_BLACK_WHITE_COLOR_3_MAX){
		SPI0_MasterSendByte(~data);
	}
	else{
		SPI0_MasterSendByte(data);
	}
#else
	SPI0_MasterSendByte(data);
#endif

#if defined(ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2)
	if (length == SCREEN_272X792_COLOR_2_HALF_MAX -1) {
		Print_I3("D2 len:%d",length);
		IP_FlowMark();
		Display_EPD_AB();
        // Clear per-image decode state after the last byte of the frame.
        pendingCompressByte = PENDING_NONE;
        callbackValue = 0;
	}
#else
#if EPD_BATCH_DATA
    if (endOffset == maxLen) {
        BOOT_LOG_TEXT("IMG done\r\n");
#else
	if (length == EPD_GetDisplayMaxBuf() -1) {
		Print_I3("D6 len:%d",length);
		IMAGE_LOG_TEXT("IMG panel-data complete\r\n");
#endif
		IP_FlowMark();
		Display_EPD_AB();
        // Clear per-image decode state after the last byte of the frame.
        pendingCompressByte = PENDING_NONE;
        callbackValue = 0;
	}
#endif
}
/**
 * @brief Decode compressed image data.
 * @param input Input buffer.
 * @param length Input length.
 * @param zip Compression flag.
 * @param rCb Output callback.
 * @return Decode result.
 */
int data_decrypt(unsigned char *input, int length, unsigned char zip, ImgDataCallback_t rCb) {
	dataimgCb = rCb;
    // Reset decode state when a new image starts.
    if (global_DEVICE_STATUS.fImageDataLen == 0) {
        pendingCompressByte = PENDING_NONE;
        callbackValue = 0;
    }
#if TDX_STORE_ZLIB && TDX_LARGE_RAW_COLOR_DISABLE && TDX_LARGE_RAW_DIRECT
    /* Large 6-color ZLIB output is already panel-format data. */
    if(zip != IS_NEED_DECMPRESS) {
        if(input == NULL || length < 0 || rCb == NULL) {
            callbackValue = -1;
            return -1;
        }
        if(length == 0) return callbackValue;
        callbackValue = rCb(input, (UINT32)length);
        return callbackValue;
    }
#endif
	PIC_Display_Compress_Data(input, length, zip);
	return callbackValue;
}
/**
 * @brief Image decode callback.
 * @param pData/data Image batch (SPD1657) or byte (other panels).
 * @param Len Batch byte count (SPD1657 only).
 * @return Current image offset after write.
 */
#if EPD_BATCH_DATA
int ImgDataCallBack(unsigned char *pData, UINT32 Len) {
    static UINT32 batchCount;
    UINT32 offset = global_DEVICE_STATUS.fImageDataLen;
    UINT32 maxLen = EPD_GetDisplayMaxBuf();
    if(global_DEVICE_STATUS.fInitDriver == Is_Yes) offset = SCREEN_DATA_START;
    if(offset >= maxLen || Len == 0) return offset;
    /* The final Flash block may contain padding beyond the image. */
    if(Len > maxLen - offset) Len = maxLen - offset;
    if(offset == SCREEN_DATA_START) batchCount = 0;
#if TDX_STORE_ZLIB
    imageBatchSending = 1;
#endif
    Display_Picture_To_Color(pData, Len, offset);
#if TDX_STORE_ZLIB
    imageBatchSending = 0;
#endif
    batchCount++;
    global_DEVICE_STATUS.fImageDataLen = offset + Len;
    /* One summary per image; no per-batch UART overhead. */
    if(global_DEVICE_STATUS.fImageDataLen == maxLen) {
        BOOT_LOG_TEXT("IMG SW=V24\r\n");
        BOOT_LOG_HEX32("IMG n=", batchCount);
        BOOT_LOG_HEX32("IMG bytes=", global_DEVICE_STATUS.fImageDataLen);
        BOOT_LOG_HEX32("IMG last=", Len);
    }
    return global_DEVICE_STATUS.fImageDataLen;
}
#else
int ImgDataCallBack(unsigned char data) {
	int screen_color_max = 0;
	
#if (defined(ENABLE_INK_SCREEN_SSD1683_272X792_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_JD79686BB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4)) || (defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4)) || (defined(ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2)) || \
	(defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6)) || (defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4))

#if (defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4))
	screen_color_max = SCREEN_1360X480_COLOR_4_MAX;
#elif (defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4))
	screen_color_max = SCREEN_1280X600_COLOR_4_MAX;
#elif (defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4))
	screen_color_max = SCREEN_1360X480_COLOR_4_MAX;
#elif (defined(ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2))
	screen_color_max = SCREEN_272X792_COLOR_2_HALF_MAX;
#elif (defined(ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4))
	screen_color_max = SCREEN_272X792_COLOR_4_MAX;
#elif (defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6))
	screen_color_max = SCREEN_1024X600_COLOR_6_MAX;
#else
	screen_color_max = SCREEN_RED_COLOR_3_MAX;
#endif
	if(global_DEVICE_STATUS.fImageDataLen == screen_color_max){
		if(ImgDataCallBack_flag < 1)
        {
            global_DEVICE_STATUS.fisHost = IS_SLAVE;
            global_DEVICE_STATUS.fImageDataLen = SCREEN_DATA_START;
            Print_I3("ImgCB h:%d",global_DEVICE_STATUS.fisHost);
            ImgDataCallBack_flag++;
        }
	}
#endif

	Display_Picture_To_Color(data, global_DEVICE_STATUS.fImageDataLen);
	global_DEVICE_STATUS.fImageDataLen++;

	return global_DEVICE_STATUS.fImageDataLen;
}
#endif
/**
 * @brief Clear the whole color panel with one color.
 * @param color Fill color.
 * @param isNeedStandy Enter standby after refresh or not.
 */
void cleanDisplayColor(int color, UINT8 isNeedStandy){
	int i;
	int screen_color_max = 0;
	unsigned char data[] = {0x11};

	data[0] = color;
	global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_AB;
	global_DEVICE_STATUS.fIsNeedStandby = isNeedStandy;
#ifdef ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4
	ControlEPDPower(Is_On);
	DelayMs(UC8579_CLEAN_POWER_PREPARE_MS);
#endif
#if (defined(ENABLE_INK_SCREEN_SSD1683_272X792_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_JD79686BB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4)) || (defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4)) || (defined(ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2)) || \
	(defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6)) || (defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4))
	
#if (defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4))	
	screen_color_max = SCREEN_1360X480_COLOR_4_MAX;
#elif (defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4))
	screen_color_max = SCREEN_1280X600_COLOR_4_MAX;
#elif (defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4))
	screen_color_max = SCREEN_1360X480_COLOR_4_MAX;
#elif (defined(ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2))
	screen_color_max = SCREEN_272X792_COLOR_2_HALF_MAX;
#elif (defined(ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4))
	screen_color_max = SCREEN_272X792_COLOR_4_MAX;
#elif (defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6))
	screen_color_max = SCREEN_1024X600_COLOR_6_MAX;
#else
	screen_color_max = SCREEN_RED_COLOR_3_MAX;
#endif

	int dLen = EPD_GetDisplayMaxBuf()*4;
#if defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4)
	dLen = EPD_GetDisplayMaxBuf()*2;
#elif defined(ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2)
	dLen = EPD_GetDisplayMaxBuf();
#endif
	for(i=0;i<dLen;i++)
	{
		if(i == screen_color_max){
			if(ImgDataCallBack_flag<1)
			{
				global_DEVICE_STATUS.fisHost = IS_SLAVE;
				Print_I3("ImgCB h:%d",global_DEVICE_STATUS.fisHost);
				ImgDataCallBack_flag++;
			}
		}
		if(i <screen_color_max){
			Display_Picture_To_Color(data[0],i);
		}else{
			Display_Picture_To_Color(data[0],i-screen_color_max);
		}
	}
#else
	int dLen = EPD_GetDisplayMaxBuf();
	for(i=0;i<dLen;i++)
	{
#if EPD_BATCH_DATA
        Display_Picture_To_Color(data, 1, i);
#else
		Display_Picture_To_Color(data[0],i);
#endif
	}
#endif
}
/**
 * @brief Get panel refresh time.
 * @return Refresh time in ticks.
 */
UINT32 getRefreshScreenTime()
{	
	return EPD_Display_Time();
}
int refreshScreenColor(unsigned char *data, unsigned int len, unsigned char isZip)
{
#if(defined(ENABLE_SOFTWARE_TO_BOE)) && (defined(ENABLE_SCREEN_COLOR_3))
	return rle_decrypt(data, len, ImgDataCallBack);
#else
	return data_decrypt(data, len, isZip, ImgDataCallBack);
#endif
}
