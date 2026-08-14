#include "app_cfg.h"
#include "CONFIG.h"
//EPD

#include "commoninfo.h"
#include "epd_driver.h"
#include "Display_EPD_W21_spi.h"
#include "rledecode.h"

RleImgDataCallback_t dataimgCb = NULL;
int callbackValue = 0;
// �������壺��ȷ���壨�޴�����ѹ����־ʱ��Ĭ��ֵ��
#define PENDING_NONE 0xBB
static UINT8 pendingCompressByte = PENDING_NONE;  // ��̬�����洢�����ѹ�����?
int ImgDataCallBack_flag=0;

#if (defined(ENABLE_INK_SCREEN_SPD1657_800X480_COLOR_6))
static void PreparePowerBeforeDeviceInit(void)
{
	ControlEPDPower(Is_On);
	DelayMs(200);
}
#endif

UINT32 getBusyGpio(){
#ifndef ENABLE_SCREEN_COLOR_6	
	return GPIO_Pin_16;
#else
	return GPIO_Pin_9;
#endif
}

/**
 * @brief ���īˮ��æ״�?
 * @return 0: æ, 1: ����
 */
char  is_Busy(void)
{
    UINT32 key;
    if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_B)
    {
  		key = GPIOA_ReadPort();   
#if (defined(ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4))
        if ((key & epaper_BUSY) == 0)//5.79寸busy脚反�?busy=0�?
#else
		if ((key & epaper_BUSY) == epaper_BUSY)
#endif
        {
            return 0;
        }            
    }
    else
    {
#ifndef ENABLE_SCREEN_COLOR_6	    
		key = GPIOB_ReadPort();
#else
		key = GPIOA_ReadPort();
#endif
#if (defined(ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4))
        if ((key & getBusyGpio()) == 0)//5.79寸busy脚反�?busy=0�?
#else
		if ((key & getBusyGpio()) == getBusyGpio())
#endif
        {
            return 0;
        }            
    }
    return 1;
}   

void EPD_W21_Reset_Spi2_AB(){
#ifdef ENABLE_SCREEN_COLOR_6
	SPI_NEW_RST_A_0;// Module reset
	SPI_NEW_RST_B_0;
		
#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
	DelayMs(100);
#else
	DelayMs(5);//At least 10ms delay 
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
    		Print_I3("Display_Picture_To_Color EPD_W21_Reset_Spi2 M009FT SCREEN_TYPE_IMG_A");
        	SPI_NEW_RST_A_0;// Module reset
			DelayMs(100);
			SPI_NEW_RST_A_1;
			DelayMs(100);
    	}
  	else if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_B)
    	{
    		Print_I3("Display_Picture_To_Color EPD_W21_Reset_Spi2 M009FT SCREEN_TYPE_IMG_B");
			SPI_NEW_RST_B_0;// Module reset
			DelayMs(100);
			SPI_NEW_RST_B_1;
			DelayMs(100);
    	}
	else if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_AB)
    	{
    		Print_I3("Display_Picture_To_Color EPD_W21_Reset_Spi2 M009FT SCREEN_TYPE_IMG_AB");
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
    		Print_I3("Display_Picture_To_Color EPD_W21_Reset_Spi2 SCREEN_TYPE_IMG_A");
        	SPI_RST_B_0;;// Module reset
		DelayMs(1+1);//At least 10ms delay 
		SPI_RST_B_1;
    	}
  	else if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_B)
    	{
    		Print_I3("Display_Picture_To_Color EPD_W21_Reset_Spi2 SCREEN_TYPE_IMG_B");
		SPI_RST_A_0;// Module reset
		DelayMs(1+1);//At least 10ms delay 
		SPI_RST_A_1;
    	}
	else if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_AB)
    	{
    		Print_I3("Display_Picture_To_Color EPD_W21_Reset_Spi2 SCREEN_TYPE_IMG_AB");
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
		Print_I3("==============EPD_Control_AB HOST ===================");
		SPI_CS_A_0;
		SPI_CS_A_SLAVE_1;
		SPI_CS_B_0;
		SPI_CS_B_SLAVE_1;
	}
	else{
		Print_I3("==============EPD_Control_AB SLAVE ===================");
		SPI_CS_A_1;
		SPI_CS_A_SLAVE_0;
		SPI_CS_B_1;
		SPI_CS_B_SLAVE_0;
	}
#else
	SPI_CS_B_0;
	SPI_CS_A_0;
#if (defined(ENABLE_INK_SCREEN_JD79686BB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4))
	SPI_CS_A_SLAVE_1;
	SPI_CS_B_SLAVE_1;
#endif
#endif
}

void EPD_Control_A_Or_B(){

	if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_A)
   	{	
   		Print_I3("Display_Picture_To_6Color DevicePower SCREEN_TYPE_IMG_A");
       	SPI_CS_A_1;
       	SPI_CS_B_0;
#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
		SPI_CS_A_SLAVE_1;
    	SPI_CS_B_SLAVE_0;
#endif
   	}
  	else if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_B)
   	{
   		Print_I3("Display_Picture_To_6Color DevicePower SCREEN_TYPE_IMG_B");
       	SPI_CS_B_1;
       	SPI_CS_A_0;
#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
		SPI_CS_A_SLAVE_0;
		SPI_CS_B_SLAVE_1;
#endif
   	} 
	else if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_AB)
   	{
   		Print_I3("Display_Picture_To_6Color DevicePower SCREEN_TYPE_IMG_AB");
       	SPI_CS_B_0;
       	SPI_CS_A_0;
#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
		SPI_CS_A_SLAVE_0;
		SPI_CS_B_SLAVE_0;
#endif

		
   	}
}

/**
 * @brief īˮ����λ����
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
 * @brief īˮ��Ӳ����ʼ��
 * @return 0: �ɹ�, ��0: ʧ��
 */
int DeviceInit(){
	global_DEVICE_STATUS.fWorked=Is_Yes;
	Init_GPIO(); 
	Set_Spi0_output_init();
	
    // Force all CS lines high first so each chip starts from a known idle level.
    SPI_CS_A_1;
    SPI_CS_B_1;
#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
    SPI_CS_A_SLAVE_1;
    SPI_CS_B_SLAVE_1;
#endif
    DelayMs(5);
}

void ControlEPDPower(UINT8 ison)
{
	UINT8 power_switch = Is_On;
#ifdef HARDWAR_DRY_CELL
	power_switch = Is_Off; //干电池方案，电源反置
#endif

	GPIOA_ModeCfg(LCD_Power_A, GPIO_ModeOut_PP_5mA);
	if(ison == power_switch)
	{
		R32_PA_OUT |= LCD_Power_A;
	}else
	{
		R32_PA_CLR |= LCD_Power_A;
	}
}

/**
 * @brief īˮ����Դ����
 */
int DevicePower(){
	ControlEPDPower(Is_On);
	
    // Force all CS lines high first, then switch to the target side.
    SPI_CS_A_1;
    SPI_CS_B_1;
#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
    SPI_CS_A_SLAVE_1;
    SPI_CS_B_SLAVE_1;
#endif
    DelayMs(5);  // Let CS settle at high level.
    
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
 * @brief ��ʾEPD����
 */
void Display_EPD_AB()
{    
	Print_I3("Display_EPD_AB...... fisHost=%d\r\n", global_DEVICE_STATUS.fisHost);
#if (defined(ENABLE_INK_SCREEN_JD79686BB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4)) || (defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2)) || (defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6))
		// Split host/slave panels refresh only after the slave half is written.
		if(global_DEVICE_STATUS.fisHost == IS_SLAVE)
		{	
			Print_I3("-----------Display_EPD_AB...... SLAVE ready, now refresh!\r\n");
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
			Print_I3("-----------Display_EPD_AB...... HOST done, wait for SLAVE data\r\n");
			SPI_CS_A_1;
			SPI_CS_B_1;
#if (defined(ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79686BB_1360X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4)) || (defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6))
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
	(defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6))
	SPI_CS_A_SLAVE_1;
	SPI_CS_B_SLAVE_1;
#endif
	tmos_stop_task(main_task_ID,EVENT_Check_TimeOut);

	if(global_DEVICE_STATUS.fIsNeedStandby == 1){
		tmos_set_event(main_task_ID,EVENT_Low_Power);
	}
}

/**
 * @brief ��ɫ����ת��
 * @param colorData ԭʼ��ɫ����
 * @return ת�������ɫ����?
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
 * @brief �ж��Ƿ���Ҫ��ѹ��
 * @param data ���жϵ�����
 * @return �Ƿ���Ҫ��ѹ��
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
 * @brief ����ѹ��ͼ������
 * @param pData ���ݻ�����
 * @param length ���ݳ���
 * @param zip �Ƿ�ѹ��
 * @return 0: �ɹ�
 */
UINT8 PIC_Display_Compress_Data(const unsigned char* pBW, UINT16 Length, unsigned char zip)
{
#if (defined(ENABLE_SCREEN_COLOR_3)) || (defined(ENABLE_SCREEN_COLOR_4)) || (defined(ENABLE_SCREEN_COLOR_2))
    UINT8 currentByte;       // ��ǰ�������ֽ�
    UINT16 i = 0;            // ��������
    UINT16 len;              // ѹ�����ݳ���
    UINT16 j;                // ѭ��������

    // 1. ���ȴ�����һ��������ѹ����־����������
    if (pendingCompressByte != PENDING_NONE) {
        currentByte = pendingCompressByte;
        pendingCompressByte = PENDING_NONE;  // ����������־

        // �������ݵĵ�һ���ֽڼ�Ϊ��һ��ѹ����־�ĳ���
        len = pBW[i];
        i++;  // ���������ֽ�

        // ���� len+1 ��ѹ����־������len=0�������ͳһ������?
        for (j = 0; j < len + 1; j++) {
            if (dataimgCb != NULL) {
                callbackValue = dataimgCb(currentByte);
            }
        }
    }

    // 2. ������ǰ����ʣ������
    while (i < Length) {
        currentByte = pBW[i];

        // ����ǰ�ֽ���ѹ����־��0x00/0xFF��
        if (isNeedDecompress(currentByte, 0) == IS_NEED_DECMPRESS) {
            // �����һ���ֽ��Ƿ���ڣ�����Խ�磩
            if (i + 1 >= Length) {
                // �������ǰ�����һ���ֽڣ�����ѹ����־����һ�δ���
                pendingCompressByte = currentByte;
                break;  // �˳�ѭ�����ȴ���һ������
            }

            // �����������һ���ֽ�Ϊ����?
            i++;  // �ƶ��������ֽ�
            len = pBW[i];

            // ���� len+1 ��ѹ����־��ͳһ����len=0��len>0��
            for (j = 0; j < len + 1; j++) {
                if (dataimgCb != NULL) {
                    callbackValue = dataimgCb(currentByte);
                }
            }
        }
        // ��ѹ����־��ֱ�ӷ��͵�ǰ�ֽ�
        else {
            if (dataimgCb != NULL) {
                callbackValue = dataimgCb(currentByte);
            }
        }

        i++;  // ������һ���ֽ�
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
 * @param data Input byte.
 * @param length Current image offset.
 */
void Display_Picture_To_Color(unsigned char data, int length){
	UINT8  By;
	int i,wLen;

	if(length >= EPD_GetDisplayMaxBuf()){
		return;
	}

	if(global_DEVICE_STATUS.fInitDriver == Is_Yes){
		Print_I3("Display_Picture_To_Color start length:%d",length);
		// Reset host/slave state at the start of every new image.
		// Otherwise the second half of the previous refresh can leak into the next frame.
#if (defined(ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4)) || (defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4)) || (defined(ENABLE_INK_SCREEN_SSD1683_272X792_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2)) || (defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6))
		global_DEVICE_STATUS.fisHost = IS_HOST;
		ImgDataCallBack_flag = 0;
#endif
		global_DEVICE_STATUS.fImageDataLen = 0;
		pendingCompressByte = PENDING_NONE; // reset pending compress flag when starting a new image
#if (defined(ENABLE_INK_SCREEN_SPD1657_800X480_COLOR_6))
		PreparePowerBeforeDeviceInit(); // Keep the SPD1657 power-up sequence unchanged.
#endif
		DeviceInit();
		global_DEVICE_STATUS.fInitDriver = Is_No;
	}

	if(length == SCREEN_DATA_START){ // Black and white color
		Print_I3("Display_Picture_To_Color black data length:%d",length);
#if defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6)
		if(global_DEVICE_STATUS.fisHost == Is_HOST){
			DevicePower();
			DelayMs(20);
		}
		else{
			Print_I3("Display_Picture_To_Color M009FT slave continue without repower");
		}
#else
		DevicePower();
		DelayMs(20);
#endif
#if (defined(ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2)) || (defined(ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4))
		if(global_DEVICE_STATUS.fisHost == Is_HOST){
			Init_EPD_Driver();
		}
#elif (defined(ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4)) || (defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6))
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
	(defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6))
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
		Init_EPD_Driver();
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
		Print_I3("Display_Picture_To_3Color red data length:%d",length);
		DevicePower();
		Init_display_Red();
	}
#endif

#if (defined(ENABLE_INK_SCREEN_JD79686BB_800X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_SSD1863_400X300_COLOR_3)) || \
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
		Print_I3("Display_Picture_To_2Color @@@@ display global_DEVICE_STATUS.fImageDataLen:%d",length);
		Display_EPD_AB();
        // Clear per-image decode state after the last byte of the frame.
        pendingCompressByte = PENDING_NONE;
        callbackValue = 0;
	}
#else
	if (length == EPD_GetDisplayMaxBuf() -1) {
		Print_I3("Display_Picture_To_6Color @@@@ display global_DEVICE_STATUS.fImageDataLen:%d",length);
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
int data_decrypt(unsigned char *input, int length, unsigned char zip, RleImgDataCallback_t rCb) {	
	dataimgCb = rCb;
    // Reset decode state when a new image starts.
    if (global_DEVICE_STATUS.fImageDataLen == 0) {
        pendingCompressByte = PENDING_NONE;
        callbackValue = 0;
    }
	PIC_Display_Compress_Data(input, length, zip);
	return callbackValue;
}
/**
 * @brief Image decode callback.
 * @param data Image byte.
 * @return Current image offset after write.
 */
int ImgDataCallBack(unsigned char data) {
	int screen_color_max = 0;
	
#if (defined(ENABLE_INK_SCREEN_SSD1683_272X792_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_JD79686BB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4)) || (defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4)) || (defined(ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2)) || \
	(defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6))

#if (defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4))
	screen_color_max = SCREEN_1360X480_COLOR_4_MAX;
#elif (defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4))
	screen_color_max = SCREEN_1280X600_COLOR_4_MAX;
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
            Print_I3("ImgDataCallBack global_DEVICE_STATUS.fisHost:%d",global_DEVICE_STATUS.fisHost);
            ImgDataCallBack_flag++;
        }
	}
#endif

	Display_Picture_To_Color(data, global_DEVICE_STATUS.fImageDataLen);
	global_DEVICE_STATUS.fImageDataLen++;

	return global_DEVICE_STATUS.fImageDataLen;
}
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
#if (defined(ENABLE_INK_SCREEN_SSD1683_272X792_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_JD79686BB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4)) || (defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4)) || (defined(ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2)) || \
	(defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6))
	
#if (defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4))	
	screen_color_max = SCREEN_1360X480_COLOR_4_MAX;
#elif (defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4))
	screen_color_max = SCREEN_1280X600_COLOR_4_MAX;
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
#if defined(ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2)
	dLen = EPD_GetDisplayMaxBuf();
#endif
	for(i=0;i<dLen;i++)
	{
		if(i == screen_color_max){
			if(ImgDataCallBack_flag<1)
			{
				global_DEVICE_STATUS.fisHost = IS_SLAVE;
				Print_I3("ImgDataCallBack global_DEVICE_STATUS.fisHost:%d",global_DEVICE_STATUS.fisHost);
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
		Display_Picture_To_Color(data[0],i);
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













