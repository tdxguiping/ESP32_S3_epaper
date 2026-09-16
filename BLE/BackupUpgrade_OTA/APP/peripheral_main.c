/******************************************************************************/
/* ͷ�ļ����� */
#include "app_cfg.h"
// CH58x_clk.h
#include "CONFIG.h"
#include "HAL.h"
#include "GATTprofile.h"
#include "Peripheral.h"
#include "OTA.h"
#include "OTAprofile.h"
#include "central.h"
#include "ch583_secure.h"
#include "release_uart0.h"
#include "release_trace.h"
#include "factory_selftest.h"

/* ��¼��ǰ��Image */
#include "commoninfo.h"
#include "flash_api.h"
#include "adc_api.h"
#ifdef ENABLE_SOFTWARE_TO_TDX
#include "tdxinfoservice.h"
#endif

#include "debug/Epd_Driver_Debug.h"

void   	Low_power(void);
void   	Low_power_IDLE(void);
/*********************************************************************
 * GLOBAL TYPEDEFS
 */
__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

#if(defined(BLE_MAC)) && (BLE_MAC == TRUE)
const uint8_t MacAddr[6] =    {0x84, 0xC2, 0xE4, 0x03, 0x02, 0x02};
#else
uint8_t Mac[6];
uint8_t Mac_ASCII[12];
#endif

/* ����APP�ж��ļ���Ч�� */
const uint32_t Address = 0xFFFFFFFF;
__attribute__((aligned(4))) uint32_t Image_Flag __attribute__((section(".ImageFlag"))) = (uint32_t)&Address;

struct _BOE_DEVICE_STATUS global_DEVICE_STATUS;
struct _BOE_EXTERN_FLASH_INFO global_EXTERN_FLASH_INFO;
uint32_t global_refresh_timer_interval = 0;
uint16_t global_refresh_timer_remaining_hours = 0; // 定时刷屏的时间间�?系统tick�?tick=625us)
uint8_t global_screen_cleared_flag = SCREEN_ALREADY_CLEARED;

#define ADC_CHECK_POLL_TICKS              ((uint32_t)2 * 1600)
#define ADC_CHECK_CHARGING_TICKS          ADC_CHECK_POLL_TICKS
#define ADC_CHECK_IDLE_TICKS              ((uint32_t)12 * 60 * 60 * 1600)
#define ADC_IDLE_CONFIRM_REQUIRED         2
#define ADC_CHECK_IDLE_CONFIRM_TICKS     ((uint32_t)2 * 1600)
#define REFRESH_TIMER_TICKS_PER_HOUR      ((uint32_t)3600 * 1600)
#define REFRESH_TIMER_MAX_SEGMENT_HOURS   23  // 清屏标志：默认已清屏（复位后�?
/* ע�⣺���ڳ���������flash�Ĳ���������ִ�У��������κ��жϣ���ֹ�����жϺ�ʧ�� */
/*********************************************************************
 * @fn      ReadImageFlag
 *
 * @brief   ��ȡ��ǰ�ĳ����Image��־��DataFlash���Ϊ�գ���Ĭ����ImageA
 *
 * @return  none
 */
void ReadImageFlag(void)
{
    unsigned char CurrImageFlag = 0xff;    
    
    OTADataFlashInfo_t p_image_flash;

    EEPROM_READ(OTA_DATAFLASH_ADD, &p_image_flash, 4);
    CurrImageFlag = p_image_flash.ImageFlag;

    Print_I3("ImageFlag=%x  Revd=%x %x %x\n", p_image_flash.ImageFlag, p_image_flash.Revd[0], p_image_flash.Revd[1], p_image_flash.Revd[2]);


    /* �����һ��ִ�У�����û�и��¹����Ժ���º��ڲ���DataFlash */
    if((CurrImageFlag != IMAGE_A_FLAG) && (CurrImageFlag != IMAGE_B_FLAG))
    {
        CurrImageFlag = IMAGE_A_FLAG;
    }
}

/*********************************************************************
 * @fn      Main_Circulation
 *
 * @brief   ��ѭ��
 *
 * @return  none
 */
__HIGH_CODE
void Main_Circulation()
{
    while(1)
    {
        WWDG_SetCounter(0);//ι�� , ����������� û��Ч��
        #if APP_FACTORY_UART0_ENABLE
        FactorySelftest_FastPoll();
        #endif
        TMOS_SystemProcess();
    }
}

//������������������������������������������������������������������������������������������������������
//PA00  #define  LED3                GPIO_Pin_0       //:PA  0=on , 1=off
//PA01  
//PA02  
//PA03  
//PA04  #define  epaper_RES   GPIO_Pin_4
//PA05  #define  epaper_DC    GPIO_Pin_5
//PA06  #define  LCD_Power_A  GPIO_Pin_6       //:PA6  0=on , 1=off
//PA07  
//������������������������������������������������������������������������������������������������������

//PA08  #define  ADC_Power              GPIO_Pin_8  // PA 8 ADC  is GPIO_ModeIN_Floating
//PA09  #define  epaper2_RES   GPIO_Pin_9   
//PA10  
//PA11  
//PA12  #define epaper_CS    GPIO_Pin_12
//PA13  #define epaper_SCK   GPIO_Pin_13
//PA14  #define epaper_BUSY  GPIO_Pin_14
//PA15  #define epaper_SDI   GPIO_Pin_15
//������������������������������������������������������������������������������������������������������
//������������������������������������������������������������������������������������������������������
// Sleep
//GPIOA_ModeCfg(GPIO_Pin_4|GPIO_Pin_5|GPIO_Pin_8|GPIO_Pin_9|GPIO_Pin_12|GPIO_Pin_13|GPIO_Pin_14|GPIO_Pin_15,GPIO_ModeIN_Floating);
//GPIOA_ModeCfg(GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_2|GPIO_Pin_3|GPIO_Pin_6|GPIO_Pin_7|GPIO_Pin_10|GPIO_Pin_11, GPIO_ModeIN_PU);

// Working
//    GPIOA_ModeCfg(GPIO_Pin_4|GPIO_Pin_5|GPIO_Pin_9|GPIO_Pin_12|GPIO_Pin_10|GPIO_Pin_11|GPIO_Pin_14,GPIO_ModeOut_PP_5mA);
//    GPIOA_ModeCfg(GPIO_Pin_8,GPIO_ModeIN_Floating);
//#ifdef ENABLE_SCREEN_COLOR_6
//    GPIOA_ModeCfg(GPIO_Pin_3|GPIO_Pin_6|GPIO_Pin_7|GPIO_Pin_10|GPIO_Pin_11|GPIO_Pin_14, GPIO_ModeIN_PU);
//#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
//    GPIOA_ModeCfg(GPIO_Pin_1|GPIO_Pin_3|GPIO_Pin_5|GPIO_Pin_11|GPIO_Pin_12, GPIO_ModeOut_PP_5mA); // M009FT CS/DC/RESET stay as outputs
//    GPIOA_ModeCfg(GPIO_Pin_4|GPIO_Pin_9, GPIO_ModeIN_PU); // Dual busy inputs
//#endif
//#else
//    GPIOA_ModeCfg(GPIO_Pin_3|GPIO_Pin_7|GPIO_Pin_14, GPIO_ModeIN_PU);
//#endif

    
// Working
//GPIOB_ModeCfg(GPIO_Pin_14 |GPIO_Pin_15,GPIO_ModeOut_PP_5mA);
//GPIOB_ModeCfg(GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_2|GPIO_Pin_3|GPIO_Pin_4|GPIO_Pin_5|GPIO_Pin_6|GPIO_Pin_7|GPIO_Pin_8|GPIO_Pin_9|GPIO_Pin_10|GPIO_Pin_11|GPIO_Pin_12|GPIO_Pin_13, GPIO_ModeIN_PU);
//   GPIO_ModeIN_Floating, //��������
//   GPIO_ModeIN_PU,       //��������
//...GPIO_ModeIN_PD,       //��������
//  .GPIO_ModeOut_PP_5mA,  //����������5mA
//...GPIO_ModeOut_PP_20mA, //����������20mA

//������������������������������������������������������������������������������������������������������
//PA00  NULL   GPIO_ModeIN_Floating, //��������
//PA01  NULL   GPIO_ModeIN_Floating, //��������
//PA02  NULL   GPIO_ModeIN_Floating, //��������
//PA03  NULL   GPIO_ModeIN_Floating, //��������
//PA04  #define  epaper_RES   GPIO_Pin_4     GPIO_ModeIN_PU,       //��������
//PA05  #define  epaper_DC    GPIO_Pin_5     GPIO_ModeIN_PU,       //��������
//PA06  #define  LCD_Power_A  GPIO_Pin_6      
        //:PA6  0=on , 1=off   
        //GPIO_ModeOut_PP_5mA,  //����������5mA
//PA07  NULL   GPIO_ModeIN_Floating, //��������
//������������������������������������������������������������������������������������������������������

//PA08  NULL   GPIO_ModeIN_Floating, //��������
//PA09  #define  epaper2_RES   GPIO_Pin_9       GPIO_ModeIN_PU,       //�������� 
//PA10  NULL   GPIO_ModeIN_Floating, //��������
//PA11  NULL   GPIO_ModeIN_Floating, //��������
//PA12  #define epaper_CS    GPIO_Pin_12     GPIO_ModeIN_PU,       //��������
//PA13  #define epaper_SCK   GPIO_Pin_13     GPIO_ModeIN_PU,       //��������
//PA14  #define epaper_BUSY  GPIO_Pin_14     GPIO_ModeIN_PU,       //��������
//PA15  #define epaper_SDI   GPIO_Pin_15     GPIO_ModeIN_PU,       //��������
//������������������������������������������������������������������������������������������������������

//GPIOA_ModeCfg(GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_2|GPIO_Pin_3|GPIO_Pin_7|GPIO_Pin_8|GPIO_Pin_10|GPIO_Pin_11,GPIO_ModeIN_Floating);
//GPIOA_ModeCfg(GPIO_Pin_4|GPIO_Pin_5|GPIO_Pin_9|GPIO_Pin_12|GPIO_Pin_13|GPIO_Pin_14|GPIO_Pin_15|, GPIO_ModeIN_PU);
//GPIOA_ModeCfg(GPIO_Pin_6, GPIO_ModeOut_PP_5mA);
//������������������������������������������������������������������������������������������������������
//PB00  NULL  GPIO_ModeIN_Floating, //��������
//PB01  NULL  GPIO_ModeIN_Floating, //��������
//PB02  NULL  GPIO_ModeIN_Floating, //��������
//PB03  NULL  GPIO_ModeIN_Floating, //��������
//PB04  NULL  GPIO_ModeIN_Floating, //��������
//PB05  NULL  GPIO_ModeIN_Floating, //��������
//PB06  NULL  GPIO_ModeIN_Floating, //��������
//PB07  NULL  GPIO_ModeIN_Floating, //��������
//������������������������������������������������������������������������������������������������������

//PB08  NULL  GPIO_ModeIN_Floating, //��������
//PB09  #define  LCD_Power_B       GPIO_Pin_9    
       //:PB9  0=on , 1=off //:PA6  0=on , 1=off
       //GPIO_ModeOut_PP_5mA,  //����������5mA
//PB10  NULL  GPIO_ModeIN_Floating, //��������  
//PB11  NULL  GPIO_ModeIN_Floating, //��������  
//PB12  #define epaper2_BUSY  GPIO_Pin_12   GPIO_ModeIN_PU,       //�������� 
//PB13  NULL  GPIO_ModeIN_Floating, //��������
//PB14  #define epaper2_CS    GPIO_Pin_14  GPIO_ModeIN_PU,       //�������� 
//PB15  #define epaper2_DC    GPIO_Pin_15  GPIO_ModeIN_PU,       //�������� 
//������������������������������������������������������������������������������������������������������

//#define  Key1_def  GPIO_Pin_8    // PB
//#define  Key2_def  GPIO_Pin_3    // PB
//#define  Key3_def  GPIO_Pin_0    // PB
//#define  Key5_def  GPIO_Pin_4    // PB
//#define  CHARGE_LED  GPIO_Pin_13   // PB  �����?//#define  LED3                GPIO_Pin_0       //:PA  0=on , 1=off
//#define  LED1                GPIO_Pin_1       //:PB  0=on , 1=off
//#define  LED2                GPIO_Pin_2       //:PB  0=on , 1=off
//#define  LED5_Power          GPIO_Pin_5       //:PB  0=on , 1=off
//#define  LED6_Power          GPIO_Pin_6       //:PB  0=on , 1=off
//#define  LED5                GPIO_Pin_21      //:PB  0=on , 1=off

void  low_power_IIO_New(void)
{
	ControlEPDPower(Is_Off);
	mDelaymS(500);

	GPIOA_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_Floating);
	GPIOB_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_Floating);

#ifdef ENABLE_SCREEN_COLOR_6
    /*
     * 6-color hardware:
     * CLK-1/CLK-2: PA13, DATA-1/DATA-2: PA15, CS-1: PA12, CD-1: PA5,
     * BUSY-1: PA4, RST-1: PA3, CS-2: PB14, CD-2: PB15, BUSY-2: PA9, RST-2: PA11,
     * LED1/LED2: PB5/PB6, POWER-1/POWER-2: PA6, ADC: PA7, CHARGE: PB13,
     * FLASH-DATA/FLASH-CLK/FLASH-CS: PA2/PA0/PB12.
     */

    // LED1/LED2 off. PB7 keeps the original 6-color low-power state.
    GPIOB_SetBits(GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7);
    GPIOB_ModeCfg(GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7, GPIO_ModeOut_PP_5mA);

    // CS-1, CS-2 and FLASH-CS low.
    GPIOA_ResetBits(GPIO_Pin_12);
    GPIOA_ModeCfg(GPIO_Pin_12, GPIO_ModeOut_PP_5mA);
    GPIOB_ResetBits(GPIO_Pin_12 | GPIO_Pin_14);
    GPIOB_ModeCfg(GPIO_Pin_12 | GPIO_Pin_14, GPIO_ModeOut_PP_5mA);

    // BUSY-1/BUSY-2 pull-down inputs.
    GPIOA_ModeCfg(GPIO_Pin_4 | GPIO_Pin_9, GPIO_ModeIN_PD);

    // CHARGE detect pull-down input.
    GPIOB_ModeCfg(GPIO_Pin_13, GPIO_ModeIN_PD);

    // FLASH-CLK, RST-1, CD-1, RST-2 and CLK low.
    GPIOA_ResetBits(GPIO_Pin_0 | GPIO_Pin_3 | GPIO_Pin_5 | GPIO_Pin_11 | GPIO_Pin_13);
    GPIOA_ModeCfg(GPIO_Pin_0 | GPIO_Pin_3 | GPIO_Pin_5 | GPIO_Pin_11 | GPIO_Pin_13, GPIO_ModeOut_PP_5mA);

    // FLASH-DATA, DATA and CD-2 stay floating.
    GPIOA_ModeCfg(GPIO_Pin_2 | GPIO_Pin_15, GPIO_ModeIN_Floating);
    GPIOB_ModeCfg(GPIO_Pin_15, GPIO_ModeIN_Floating);

    // ADC pull-up input.
    GPIOA_ModeCfg(GPIO_Pin_7, GPIO_ModeIN_PU);

#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
    // M009FT extra slave CS pins.
    GPIOA_ResetBits(GPIO_Pin_1);
    GPIOA_ModeCfg(GPIO_Pin_1, GPIO_ModeOut_PP_5mA);
    GPIOB_ResetBits(GPIO_Pin_9);
    GPIOB_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);
#endif
#else
    /*
     * Non-6-color hardware:
     * CLK-1/CLK-2: PA13, DATA-1/DATA-2: PA15, CS-1: PA12, CD-1: PA5,
     * BUSY-1: PA14, RST-1: PA4, CS-2: PB14, CD-2: PB15, BUSY-2: PB16, RST-2: PA9,
     * LED1/LED2: PB5/PB6, POWER-1/POWER-2: PA6, ADC: PA7, CHARGE: PB13,
     * FLASH-DATA/FLASH-CLK/FLASH-CS: PA2/PA0/PB12.
     */

    // LED1/LED2 off. PB7 keeps the original low-power state.
    GPIOB_SetBits(GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7);
    GPIOB_ModeCfg(GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7, GPIO_ModeOut_PP_5mA);

    // CS-1, CS-2 and FLASH-CS low.
    GPIOA_ResetBits(GPIO_Pin_12);
    GPIOA_ModeCfg(GPIO_Pin_12, GPIO_ModeOut_PP_5mA);
    GPIOB_ResetBits(GPIO_Pin_12 | GPIO_Pin_14);
    GPIOB_ModeCfg(GPIO_Pin_12 | GPIO_Pin_14, GPIO_ModeOut_PP_5mA);

    // BUSY-1/BUSY-2 pull-down inputs.
    GPIOA_ModeCfg(GPIO_Pin_14, GPIO_ModeIN_PD);
    GPIOB_ModeCfg(GPIO_Pin_16, GPIO_ModeIN_PD);

    // CHARGE detect pull-down input.
    GPIOB_ModeCfg(GPIO_Pin_13, GPIO_ModeIN_PD);

    // FLASH-CLK, RST-1, CD-1, RST-2 and CLK low.
    GPIOA_ResetBits(GPIO_Pin_0 | GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_9 | GPIO_Pin_13);
    GPIOA_ModeCfg(GPIO_Pin_0 | GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_9 | GPIO_Pin_13, GPIO_ModeOut_PP_5mA);

    // FLASH-DATA, DATA and CD-2 stay floating.
    GPIOA_ModeCfg(GPIO_Pin_2 | GPIO_Pin_15, GPIO_ModeIN_Floating);
    GPIOB_ModeCfg(GPIO_Pin_15, GPIO_ModeIN_Floating);

    // ADC pull-up input.
    GPIOA_ModeCfg(GPIO_Pin_7, GPIO_ModeIN_PU);
#endif

}

tmosTaskID main_task_ID;

void  Disable_GPIO_IRQ(void)
{
    PFIC_DisableIRQ(GPIO_B_IRQn);
}

void Init_GPIO(void)
{
	Print_I3("===================");
	// Working
    GPIOA_ModeCfg(GPIO_Pin_4|GPIO_Pin_5|GPIO_Pin_9|GPIO_Pin_12|GPIO_Pin_10|GPIO_Pin_11|GPIO_Pin_14,GPIO_ModeOut_PP_5mA);
    GPIOA_ModeCfg(GPIO_Pin_8,GPIO_ModeIN_Floating);
#ifdef ENABLE_SCREEN_COLOR_6
    GPIOA_ModeCfg(GPIO_Pin_3|GPIO_Pin_6|GPIO_Pin_7|GPIO_Pin_10|GPIO_Pin_11|GPIO_Pin_14, GPIO_ModeIN_PU);
#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
    GPIOA_ModeCfg(GPIO_Pin_1|GPIO_Pin_3|GPIO_Pin_5|GPIO_Pin_11|GPIO_Pin_12, GPIO_ModeOut_PP_5mA); // M009FT CS/DC/RESET stay as outputs
    GPIOA_ModeCfg(GPIO_Pin_4|GPIO_Pin_9, GPIO_ModeIN_PU); // Dual busy inputs
#endif
#else
    GPIOA_ModeCfg(GPIO_Pin_3|GPIO_Pin_7|GPIO_Pin_14, GPIO_ModeIN_PU);
#endif

	// Working
	GPIOB_ModeCfg(GPIO_Pin_14 |GPIO_Pin_15|GPIO_Pin_16,GPIO_ModeOut_PP_5mA);
#ifdef ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4
	GPIOB_ModeCfg(GPIO_Pin_16, GPIO_ModeIN_PU); // UC8579 B-side BUSY
#endif
#ifdef  Debug_mode_on
	GPIOB_ModeCfg(GPIO_Pin_7, GPIO_ModeOut_PP_5mA);
#ifdef ENABLE_SCREEN_COLOR_6
	GPIOB_ModeCfg(GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_21|GPIO_Pin_2|GPIO_Pin_3|GPIO_Pin_4|GPIO_Pin_5|GPIO_Pin_6|GPIO_Pin_8|GPIO_Pin_9|GPIO_Pin_10|GPIO_Pin_11|GPIO_Pin_12|GPIO_Pin_22, GPIO_ModeIN_PU);
#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
	GPIOB_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA); // B side slave CS
#endif
#else
	GPIOB_ModeCfg(GPIO_Pin_21|GPIO_Pin_2|GPIO_Pin_3|GPIO_Pin_4|GPIO_Pin_5|GPIO_Pin_6|GPIO_Pin_8|GPIO_Pin_10|GPIO_Pin_11|GPIO_Pin_12|GPIO_Pin_22, GPIO_ModeIN_PU);
#endif
#else
#ifdef ENABLE_SCREEN_COLOR_6
	GPIOB_ModeCfg(GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_21|GPIO_Pin_2|GPIO_Pin_3|GPIO_Pin_4|GPIO_Pin_5|GPIO_Pin_6|GPIO_Pin_7|GPIO_Pin_8|GPIO_Pin_9|GPIO_Pin_10|GPIO_Pin_11|GPIO_Pin_12|GPIO_Pin_22, GPIO_ModeIN_PU);
#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
	GPIOB_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA); // B side slave CS
#endif
#else
	GPIOB_ModeCfg(GPIO_Pin_21|GPIO_Pin_2|GPIO_Pin_3|GPIO_Pin_4|GPIO_Pin_5|GPIO_Pin_6|GPIO_Pin_7|GPIO_Pin_8|GPIO_Pin_10|GPIO_Pin_11|GPIO_Pin_12|GPIO_Pin_22, GPIO_ModeIN_PU);
#endif
#endif
	GPIOB_ModeCfg(GPIO_Pin_13, GPIO_ModeIN_PD);

#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
	R32_PA_OUT |= (epaper_NEW_RES | epaper2_NEW_RES | epaper_CS | epaper_CS_SLAVE);
	R32_PB_OUT |= (epaper2_CS | epaper2_CS_SLAVE);
#elif defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4)
	R32_PA_OUT |= (epaper_CS | epaper_CS_SLAVE | epaper2_CS_SLAVE);
	R32_PB_OUT |= epaper2_CS;
#else
	R32_PA_OUT |= (epaper_CS);//R32_PA_OUT |= (epaper_CS | epaper_CS_SLAVE);
	R32_PB_OUT |= (epaper2_CS);//R32_PB_OUT |= (epaper2_CS | epaper2_CS_SLAVE);
#endif

#if APP_UART0_ENABLE
	/* DeviceInit() reuses Init_GPIO(); keep PB4/PB7 as UART0 afterwards. */
	release_uart0_restore_pins();
#endif
}

/******************************** endfile @ main ******************************/
// Rec_OTA_IAP_DataDeal
// OTA_IAPWriteData
void  Get_time(void)
{
    uint8 Len;
    uint8 Buf[200];    
    uint16_t  py, pmon, pd, ph, pm, ps;
    
    RTC_GetTime(&py,&pmon,&pd,&ph,&pm,&ps);
    Print_I3("��=%d  ��=%d  ��=%d  Сʱ=%d   ����=%d   ��=%d",py, pmon, pd, ph, pm, ps);
    Len = strlen(Buf);
    if(Len >20)
        Len=20;
}


UINT8  Ble_Scan=Is_Off;
UINT8  Ble_Central_Scan= Is_Off;
static uint8_t adc_idle_confirm_count = 0;
static uint16_t adc_idle_monitor_count = 0;
#if APP_FACTORY_UART0_ENABLE && APP_FACTORY_POWER_HOLD_ENABLE
/* Last direct PB13 state. It lets an unplug restart the normal idle timer. */
static uint8_t factory_power_was_present = Is_No;
#endif


static void StartChargeMonitor(void)
{
    adc_idle_confirm_count = 0;
    adc_idle_monitor_count = 0;
    tmos_start_task(main_task_ID, EVENY_Is_Charge, ADC_CHECK_POLL_TICKS);
}

static void ScheduleNextRefreshTimerSegment(void)
{
	uint16_t segment_hours;

	if(global_refresh_timer_remaining_hours == 0){
		return;
	}

	segment_hours = global_refresh_timer_remaining_hours;
	if(segment_hours > REFRESH_TIMER_MAX_SEGMENT_HOURS){
		segment_hours = REFRESH_TIMER_MAX_SEGMENT_HOURS;
	}

	tmos_start_task(main_task_ID,
					EVENT_Refresh_Timer,
					(uint32_t)segment_hours * REFRESH_TIMER_TICKS_PER_HOUR);
}

static void StartRefreshTimerCycle(void)
{
	global_refresh_timer_interval = (uint32_t)REFRESH_TIMER_FIXED_HOURS * REFRESH_TIMER_TICKS_PER_HOUR;
	global_refresh_timer_remaining_hours = REFRESH_TIMER_FIXED_HOURS;
	ScheduleNextRefreshTimerSegment();
}

static uint8_t RefreshTimerSegmentElapsed(void)
{
	uint16_t segment_hours;

	if(global_refresh_timer_remaining_hours == 0){
		return Is_Yes;
	}

	segment_hours = global_refresh_timer_remaining_hours;
	if(segment_hours > REFRESH_TIMER_MAX_SEGMENT_HOURS){
		segment_hours = REFRESH_TIMER_MAX_SEGMENT_HOURS;
	}

	global_refresh_timer_remaining_hours -= segment_hours;
	if(global_refresh_timer_remaining_hours > 0){
		ScheduleNextRefreshTimerSegment();
		return Is_No;
	}

	return Is_Yes;
}

//UINT8  Frist_time=0;
void Save_LastRefresh_Info_To_Flash(void)
{
	// ============ 统一在这里保存刷屏信息到flash（用于定时刷屏恢复） ============
	// 在进入低功耗前，判断刚才是什么类型的刷屏，并保存索引、group、room等信�?	// 注意：只保存一次，使用静态变量避免重复保�?	// fDataSendSuccess保持Is_Yes状态，确保Low_power_IDLE()循环期间不会被peripheral.c:713-716打断
	static uint8_t last_saved_success_flag = Is_No;
	if(global_DEVICE_STATUS.fDataSendSuccess == Is_Yes && last_saved_success_flag == Is_No){
		// 刷屏成功且未保存过，保存信息
		// 判断屏幕模式（根据fScreenType和fImageIndex�?		// 注意：普通刷图时，fImageIndex就是0�?（无偏移），预存刷图不依赖此判断
		uint8_t base_index = global_EXTERN_FLASH_INFO.fImageIndex;
		
		uint8_t screen_mode;
		if(base_index == SCREEN_A_COMMON_INDEX){
			// 基础index=0 表示异显模式
			screen_mode = SCREEN_MODE_AB_DIFF;
		} else {
			// 基础index=1，根�?fScreenType 判断
			if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_A){
				screen_mode = SCREEN_MODE_SINGLE_A;
			} else if(global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_B){
				screen_mode = SCREEN_MODE_SINGLE_B;
			} else {
				screen_mode = SCREEN_MODE_AB_SAME;
			}
		}
		
		if(global_DEVICE_STATUS.fRefreshType == DEVICE_OP_COMMON){
			// 纯实时刷屏（不带预存�?			// 刷图不允许改 enabled（只由TIME命令改），这里保持原�?			// 正常刷图后，设置清屏标志为未清屏，下次开机能正常进入定时刷图
			global_screen_cleared_flag = SCREEN_NOT_CLEARED;
			saveLastRefreshInfo(LAST_REFRESH_TYPE_COMMON, 0, 0, screen_mode, global_EXTERN_FLASH_INFO.fZip, 
							   SAVE_KEEP_U8, global_screen_cleared_flag);
		}
		else if(global_DEVICE_STATUS.fRefreshType == DEVICE_OP_COMMON_PRESAVE || 
		        global_DEVICE_STATUS.fRefreshType == DEVICE_OP_PRESAVE){
			// 预存刷屏（包括实时传�?预存、或从flash读取预存�?			// 预存刷屏的screen_mode无意义，因为通过group/room定位，固定为AB_SAME
			Print_I3("Save refresh info: PRESAVE mode, group=%d, room=%d, index=%d, zip=%d (keep timer cfg)",
					 global_DEVICE_STATUS.fBoardCastGroup, global_DEVICE_STATUS.fBoardCastRoom,
					 global_EXTERN_FLASH_INFO.fImageIndex, global_EXTERN_FLASH_INFO.fZip);
			// 刷图不允许改 enabled（只由TIME命令改），这里保持原�?			// 预存刷图后，设置清屏标志为未清屏，下次开机能正常进入定时刷图
			global_screen_cleared_flag = SCREEN_NOT_CLEARED;
			saveLastRefreshInfo(LAST_REFRESH_TYPE_PRESAVE,
							   global_DEVICE_STATUS.fBoardCastGroup,
							   global_DEVICE_STATUS.fBoardCastRoom,
							   screen_mode,
							   global_EXTERN_FLASH_INFO.fZip,
							   SAVE_KEEP_U8,
							   global_screen_cleared_flag);
		}
		
		// 标记已保存，避免重复保存，但不重置fDataSendSuccess
        // keep success flag until a new refresh cycle starts
        last_saved_success_flag = Is_Yes;
	}
	// 如果是新的刷屏周期开始（fDataSendSuccess从Is_Yes变为Is_No），重置保存标志
	
	if(global_DEVICE_STATUS.fDataSendSuccess == Is_No){
		last_saved_success_flag = Is_No;
	}
	// ======================================================================

}

tmosEvents Main_Event(tmosTaskID task_id, tmosEvents events)
{
    uint8_t *msgPtr;
    if(events & SYS_EVENT_MSG)
    { // SYS_EVENT_MSG
        msgPtr = tmos_msg_receive(task_id);
        if(msgPtr)
        {
            /* De-allocate */
            tmos_msg_deallocate(msgPtr);
        }
        return events ^ SYS_EVENT_MSG;
    }

    if(events & EVENT_Get_Battle_Charge)
    {	
	if(global_DEVICE_STATUS.fImageType == 1){
		// 异显模式：需要刷两次（A�?B面）
		// 普通刷图的异显模式：不管内外置flash，都直接�?/1（不加偏移）
        // First refresh: A side.
        global_DEVICE_STATUS.fInitDriver = Is_Yes;
		global_DEVICE_STATUS.fImageDataLen = 0;
		global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_A;
		global_EXTERN_FLASH_INFO.fImageIndex = SCREEN_B_COMMON_INDEX; // 1
		
		if(preSaveDisplayColor(0, 0, DEVICE_PRE_SAVE) == -1){
			PRINT("send pre save  error \r\n");	
			global_DEVICE_STATUS.fWorked =Is_No;
		}
		mDelaymS(2000);
		
        // Second refresh: B side.
        global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_B;
		global_DEVICE_STATUS.fIsNeedStandby = 1;
		global_DEVICE_STATUS.fInitDriver = Is_Yes;
		global_EXTERN_FLASH_INFO.fBlockNum = EXTERN_FLASH_BLOCK_FIRST_ADDR;
		global_DEVICE_STATUS.fPackageCnt = 0;
		global_DEVICE_STATUS.fImageDataLen = 0;
		global_EXTERN_FLASH_INFO.fImageIndex = SCREEN_A_COMMON_INDEX; // 0
		
		if(preSaveDisplayColor(0, 0, DEVICE_PRE_SAVE) == -1){
			PRINT("send pre save  error \r\n");	
			global_DEVICE_STATUS.fWorked =Is_No;
		}
		else{
			// AB双屏刷新成功，标记成功（在EVENT_Low_Power统一保存）Save_LastRefresh_Info_To_Flash用到
			global_DEVICE_STATUS.fDataSendSuccess = Is_Yes;
		}
	}
	else{
		global_DEVICE_STATUS.fImageType = 0;
		global_DEVICE_STATUS.fIsNeedStandby = 1;
		if(preSaveDisplayColor(global_DEVICE_STATUS.fBoardCastGroup, global_DEVICE_STATUS.fBoardCastRoom, DEVICE_PRE_SAVE) == -1){
			PRINT("send pre save  error \r\n"); 
			global_DEVICE_STATUS.fWorked =Is_No;
		}
		else{
			// 预存刷屏成功，标记成功（在EVENT_Low_Power统一保存）Save_LastRefresh_Info_To_Flash用到
			global_DEVICE_STATUS.fDataSendSuccess = Is_Yes;
		}

	}
      	return events ^ EVENT_Get_Battle_Charge;
    }
	
    if(events & EVENY_Is_Charge)
    {
        uint8_t was_charging = global_DEVICE_STATUS.fIsCharg;
        uint8_t is_charging_now = GetCurrentChargeStatus();
        uint8_t need_adc_check = Is_No;

#if APP_FACTORY_UART0_ENABLE && APP_FACTORY_POWER_HOLD_ENABLE
        uint8_t factory_power_now = FactorySelftest_ShouldBlockDeepSleep();
        if(factory_power_now != factory_power_was_present)
        {
            BOOT_LOG_HEX8("BOOT power=", factory_power_now);
        }
        if((factory_power_was_present == Is_Yes) && (factory_power_now == Is_No))
        {
            /* Do not use the stale >30 second timeout accumulated on fixture power. */
            global_DEVICE_STATUS.fSystemTimeOut = 0;
            /* The earlier low-power event was held while fixture power was present. */
            tmos_start_task(main_task_ID, EVENT_Low_Power, 500);
        }
        factory_power_was_present = factory_power_now;
#endif

        if((is_charging_now == Is_Yes) || (was_charging == Is_Yes)){
            adc_idle_confirm_count = 0;
            adc_idle_monitor_count = 0;
            need_adc_check = Is_Yes;
        }else{
            if(adc_idle_confirm_count < ADC_IDLE_CONFIRM_REQUIRED){
                adc_idle_confirm_count++;
                need_adc_check = Is_Yes;
            }else{
                adc_idle_monitor_count++;
                if(adc_idle_monitor_count >= (ADC_CHECK_IDLE_TICKS / ADC_CHECK_POLL_TICKS)){
                    adc_idle_monitor_count = 0;
                    need_adc_check = Is_Yes;
                }
            }
        }

        if(need_adc_check == Is_Yes){
            AdcTask();
        }

        tmos_start_task(main_task_ID, EVENY_Is_Charge, ADC_CHECK_POLL_TICKS);
        return events ^ EVENY_Is_Charge;
    }

    if(events & EVENT_Check_TimeOut)
    {
		if((global_DEVICE_STATUS.fWorked == Is_No) && (global_DEVICE_STATUS.fDataSendSuccess == Is_No)
			&& ((global_DEVICE_STATUS.fRefreshType == DEVICE_OP_COMMON) || (global_DEVICE_STATUS.fRefreshType == DEVICE_OP_COMMON_PRESAVE)))
		{
			if(global_DEVICE_STATUS.fisHaveData == Is_Yes){
				global_DEVICE_STATUS.fisHaveData = Is_No;
				global_DEVICE_STATUS.fSystemTimeOut = 0;
			}else{
				global_DEVICE_STATUS.fSystemTimeOut++;
				Print_I3("ble timeout = %d",global_DEVICE_STATUS.fSystemTimeOut);
				if(global_DEVICE_STATUS.fSystemTimeOut > 30){
					Print_I3("bleconnect timeout");
					global_DEVICE_STATUS.fSystemTimeOut = 0;
					//global_DEVICE_STATUS.fWorked =Is_No;
					tmos_start_task(main_task_ID,EVENT_Low_Power ,500);
				}
			}
    	}else{
			global_DEVICE_STATUS.fSystemTimeOut = 0;
    	}
		tmos_start_task(main_task_ID,EVENT_Check_TimeOut,1600);
	
		return events ^ EVENT_Check_TimeOut;
    }
	
	if(events & EVENT_Low_Power)
	{ 
		Print_I3("..............EVENT_Low_Power 00000000000000000");
		BOOT_LOG_TEXT("BOOT lowpower event\r\n");
		BOOT_LOG_HEX8("BOOT fWorked=", global_DEVICE_STATUS.fWorked);

#if APP_FACTORY_UART0_ENABLE && APP_FACTORY_POWER_HOLD_ENABLE
		if((global_DEVICE_STATUS.fWorked != Is_Yes) && (global_DEVICE_STATUS.fWillReboot != Is_Yes) && (FactorySelftest_ShouldBlockDeepSleep() == Is_Yes))
		{
			/* Persist completed refresh metadata but retain UART0 and all working IO. */
			BOOT_LOG_TEXT("BOOT power-hold\r\n");
			factory_power_was_present = Is_Yes;
			Save_LastRefresh_Info_To_Flash();
			return events ^ EVENT_Low_Power;
		}

		if(factory_power_was_present == Is_Yes)
		{
			/* PB13 fell since the last check: restart the existing BLE idle timeout. */
			BOOT_LOG_TEXT("BOOT power-release\r\n");
			factory_power_was_present = Is_No;
			global_DEVICE_STATUS.fSystemTimeOut = 0;
			/* Re-run the event now that PB13 no longer holds the fixture awake. */
			tmos_start_task(main_task_ID, EVENT_Low_Power, 500);
			return events ^ EVENT_Low_Power;
		}
#endif
		
		Save_LastRefresh_Info_To_Flash();	
			
		if(global_DEVICE_STATUS.fisOtaed != 1)
		{
			BOOT_LOG_TEXT("BOOT deep-sleep pending\r\n");
			Low_power();
		}
		
		return events ^ EVENT_Low_Power;
	}

	if(events & EVENT_Refresh_Timer)
	{
		if(global_refresh_timer_interval > 0){
			if(RefreshTimerSegmentElapsed() != Is_Yes){
				return events ^ EVENT_Refresh_Timer;
			}

			Print_I3("EVENT_Refresh_Timer triggered, refreshing screen from flash");
			// 从flash读取上次刷屏的类型和参数
			// index/zip/fScreenType 会由 getLastRefreshInfo() 回写到全局变量
			// 小时数使用固定�?REFRESH_TIMER_FIXED_HOURS
			uint8_t last_type, last_group, last_room, last_enabled, last_screen_mode;
			getLastRefreshInfo(&last_type, &last_group, &last_room, &last_enabled, &last_screen_mode);

			uint8_t last_index = global_EXTERN_FLASH_INFO.fImageIndex;
			uint8_t last_zip = global_EXTERN_FLASH_INFO.fZip;

            // Set refresh parameters to match normal image transfer.
            global_DEVICE_STATUS.fPackageCnt = 0;
            global_DEVICE_STATUS.fPackageCount = 0xFFFF;
            global_DEVICE_STATUS.fDataSendSuccess = Is_No;
            global_DEVICE_STATUS.fOtaStatus = RTN_OTA_COMMON;
            global_DEVICE_STATUS.fIsNeedStandby = 1;
            global_DEVICE_STATUS.fInitDriver = Is_Yes;
            global_DEVICE_STATUS.fImageDataLen = 0;
            global_EXTERN_FLASH_INFO.fBlockNum = EXTERN_FLASH_BLOCK_FIRST_ADDR;
            global_EXTERN_FLASH_INFO.fImageIndex = last_index;
            global_EXTERN_FLASH_INFO.fZip = last_zip;

			// 根据flash中保存的类型进行刷屏
			if(last_type == LAST_REFRESH_TYPE_COMMON || last_type == LAST_REFRESH_TYPE_NONE){
                // Common refresh mode.
                global_DEVICE_STATUS.fRefreshType = DEVICE_OP_COMMON;
				
                // Check whether this is AB-diff mode.
                if(last_screen_mode == SCREEN_MODE_AB_DIFF){
					// 异显模式：触�?EVENT_Get_Battle_Charge，让它自动刷两次（A�?B面）
                    // fImageType is already set by getLastRefreshInfo().
                    InitFlashDriver();
					mDelayuS(10);
					
					tmos_start_task(main_task_ID, EVENT_Get_Battle_Charge, 100);
					mDelayuS(10);
					DeInitFlashDriver();
				} else {
                    // Single side or same-display mode: refresh once.
                    InitFlashDriver();
					mDelayuS(10);
					
					if(preSaveDisplayColor(0, 0, DEVICE_PRE_SAVE) == -1){
						PRINT("Refresh timer: pre save display error\r\n");
						global_DEVICE_STATUS.fWorked = Is_No;
					} else {
						global_DEVICE_STATUS.fDataSendSuccess = Is_Yes;
					}
					mDelayuS(10);
					DeInitFlashDriver();
				}
			}else if(last_type == LAST_REFRESH_TYPE_PRESAVE){
				// 预存模式：需要根�?group/room 重新计算 index
				global_DEVICE_STATUS.fRefreshType = DEVICE_OP_PRESAVE;
                // Save group/room for low-power refresh info persistence.
                global_DEVICE_STATUS.fBoardCastGroup = last_group;
				global_DEVICE_STATUS.fBoardCastRoom = last_room;
				InitFlashDriver();
				mDelayuS(10);
				
				if(preSaveDisplayColor(global_DEVICE_STATUS.fBoardCastGroup, global_DEVICE_STATUS.fBoardCastRoom, DEVICE_PRE_SAVE) == -1){
					PRINT("Refresh timer: pre save display error\r\n");
					global_DEVICE_STATUS.fWorked = Is_No;
				} else {
                    // Mark success so EVENT_Low_Power can persist refresh info.
                    global_DEVICE_STATUS.fDataSendSuccess = Is_Yes;
				}
				mDelayuS(10);
				DeInitFlashDriver();
			}
			else{
				PRINT("Unknown refresh type: 0x%02X, skip refresh\r\n", last_type);
			}
			
			// 重新启动定时器，实现循环刷屏
			StartRefreshTimerCycle();
		}
		
		return events ^ EVENT_Refresh_Timer;
	}
	
    if(events & EVENT_IDLE)
    {    
   		Low_power_IDLE();
      	return events ^ EVENT_IDLE;
    }  

    if(events & EVENT_Test_Msg)
    { 
		//Print_I3("..............EVENT_Test_Msg 000000000000000000000");   
#ifdef EPD_BWSOLID_IMG_CYCLE_TEST_ENABLE
		EPD_BWSolidImg_CycleTest();
#endif
		tmos_start_task(main_task_ID,EVENT_Test_Msg,1600);
		return events ^ EVENT_Test_Msg;
    }

	if(events & EVENT_Start_Peripheral_Mode)
	{		
		//Print_I3("EVENT_Start_Peripheral_Mode 00 Ble_connect_or_no:%d UC7279_worked:%d Ble_Scan:%d",Ble_connect_or_no,UC7279_worked,Ble_Scan);
		if(Ble_Scan==Is_Off)
		{
			Ble_Scan=Is_On;
			Start_advertising();
			//Print_I3("EVENT_Start_Peripheral_Mode start ......");
			tmos_start_task(main_task_ID,EVENT_Start_Peripheral_Mode ,100);	//	(units of 625us, 80=50ms)
		}
		else
		{
			Ble_Scan=Is_Off;
			//Print_I3("EVENT_Start_Peripheral_Mode stop stop stop stop stop stop");
			Stop_advertising();
			tmos_start_task(main_task_ID,EVENT_Start_Peripheral_Mode ,1000);	// 
		}
		
		return events ^ EVENT_Start_Peripheral_Mode;
	}

    if(events & EVENT_Start_Central_Mode)
    {   
		/*if(global_DEVICE_STATUS.fBle_connect_or_no==Is_OK)
		{
			Ble_Scan=Is_On;
		}

		if(  global_DEVICE_STATUS.fWorked == Is_Yes)
		{
			Ble_Scan=Is_On;
		}*/
      	if(Ble_Central_Scan==Is_Off)
       	{
			Ble_Central_Scan=Is_On;
			Start_Central_Scan();
			//Start_advertising();
			//Print_I3("high Start_Central_Scan start start start start");
			tmos_start_task(main_task_ID,EVENT_Start_Central_Mode ,100);  //  (units of 625us, 80=50ms)
       	}
       	else
       	{
			Ble_Central_Scan=Is_Off;
			Stop_Central_Scan();
			//Stop_advertising();
			//Print_I3("high Start_Central_Scan stop stop stop stop");
			tmos_start_task(main_task_ID,EVENT_Start_Central_Mode ,2*1600);   // 
       	}
      	return events ^ EVENT_Start_Central_Mode;
    }

    if(events & EVENT_Boardcast_Op)
    { 
		processGapBroadcastAdData();
		return events ^ EVENT_Boardcast_Op;
    }

	return 0;
}    

//  RXD0/TXD0 0=RXD0/TXD0 on PB[4]/PB[7], 1=RXD0_/TXD0_ on PA[15]/PA[14]
//  RXD1/TXD1 0=RXD1/TXD1 on PA[8]/PA[9], 1=RXD1_/TXD1_ on PB[12]/PB[13]
//  RXD2/TXD2 0=RXD2/TXD2 on PA[6]/PA[7], 1=RXD2_/TXD2_ on PB[22]/PB[23]
//  RXD3/TXD3 0=RXD3/TXD3 on PA[4]/PA[5], 1=RXD3_/TXD3_ on PB[20]/PB[21]
//R8_GLOB_RESET_KEEP
//RB_ROM_CODE_OFS
//SYS_GetLastResetSta()
//#define  RB_ROM_CODE_OFS    0x10                      // RWA, code offset address selection in Flash ROM: 0=start address 0x000000, 1=start address 0x008000
// RB_RESET_FLAG: recent reset flag
//   000 - SR, software reset, by RB_SOFTWARE_RESET=1 @RB_WDOG_RST_EN=0
//   001 - RPOR, real power on reset
//   010 - WTR, watch-dog timer-out reset
//   011 - MR, external manual reset by RST pin input low
//   101 - GRWSM, global reset by waking under shutdown mode
//   1?? - LRW, power on reset occurred during sleep

UINT8  GetLastResetSta(void)
{
   UINT32  u32chr; 
   u32chr = SYS_GetLastResetSta();
   //Print_I3(" %x",u32chr);
   return u32chr;
    //   000 - SR, software reset, by RB_SOFTWARE_RESET=1 @RB_WDOG_RST_EN=0
    //   001 - RPOR, real power on reset
    //   010 - WTR, watch-dog timer-out reset
    //   011 - MR, external manual reset by RST pin input low
    //   101 - GRWSM, global reset by waking under shutdown mode
    //   1?? - LRW, power on reset occurred during sleep  
}

//  Sleep_Time==7  then 8 second
//  Sleep_Time==8  then 16 second
//  Sleep_Time==9  then 32 second

//RTC_TMRFunCfg(Period_2_S);
//RTC_TRIGFunCfg(32768*2);    //32768Ϊ1s
UINT8   Low_Power_RTC(UINT8  Sleep_Time)
{
	//      LClk32K_Select(Clk32K_LSE);//�����ⲿ32K
	//      R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG1;
	//      R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG2;
	//      R8_CK32K_CONFIG |= RB_CLK_XT32K_PON;
	//      R8_SAFE_ACCESS_SIG = 0;
	uint32_t irq_status;      
	SYS_DisableAllIrq(&irq_status);
	// WWDG_ResetCfg(DISABLE);
	PFIC_DisableIRQ(RTC_IRQn);
	// LClk32K_Select(0);
	// HAL_SleepInit();
	RTC_InitTime(2022,3,14,0,0,0);

	if(Sleep_Time==Period_0_125_S)
	{
		Print_I3("Period_0_125_S\r\n");
		RTC_TMRFunCfg(Period_0_125_S);
	}
	else  if(Sleep_Time==Period_0_25_S)
	{
		Print_I3("Period_0_25_S\r\n");
		RTC_TMRFunCfg(Period_0_25_S);  //  Period_8_S    Period_16_S
	}      
	else  if(Sleep_Time==Period_0_5_S)
	{
		Print_I3("Period_0_5_S\r\n");
		RTC_TMRFunCfg(Period_0_5_S);  //  Period_8_S    Period_16_S
	}            
	else  if(Sleep_Time==Period_1_S)
	{
		Print_I3("Period_1_S\r\n");
		RTC_TMRFunCfg(Period_1_S);  //  Period_8_S    Period_16_S         
	}
	else  if(Sleep_Time==Period_4_S)
	{
		Print_I3("Period_4_S\r\n");
		RTC_TMRFunCfg(Period_4_S);  //  Period_8_S    Period_16_S         
	}
	else  if(Sleep_Time==Period_8_S)
	{
		Print_I3("Period_8_S\r\n");
		RTC_TMRFunCfg(Period_8_S);  //  Period_8_S    Period_16_S         
	}
	else  if(Sleep_Time==Period_16_S)
	{
		Print_I3("Period_16_S\r\n");
		RTC_TMRFunCfg(Period_16_S);  //  Period_8_S    Period_16_S         
	}
	else  if(Sleep_Time==199)
	{
		Print_I3(".0xFFFFF..RTC_TRIGFunCfg \r\n");
		RTC_TRIGFunCfg(0xFFFFF);  // 0x4FFFF = 10s  0xFFFFF=30s
	}
	else  if(Sleep_Time==210)
	{
		//RTC_TMRFunCfg(Period_2_S);
		//RTC_TRIGFunCfg(32768*2);    //32768Ϊ1s

		Print_I3(".0x4FFFF..RTC_TRIGFunCfg \r\n");
		RTC_TRIGFunCfg(0x4FFFF);  // 0x4FFFF = 10s  0xFFFFF=30s
	}      
	else
	{
		Print_I3("Period_16_S \r\n");
		RTC_TMRFunCfg(Period_16_S);  //  Period_8_S    Period_16_S        
	}

	return Is_OK;
}

// PB-8  16 17
//void GPIO_makeup(void)
//{
//    /* ���û���ԴΪ GPIO - PA5 */
//   
//   PWR_PeriphWakeUpCfg(ENABLE, RB_SLP_GPIO_WAKE, Long_Delay);
//}
UINT8  CPU_had_IDLE_counter=0;
UINT8  CPU_had_IDLE=Is_No;
#define EPD_BUSY_IDLE_STABLE_COUNT    2
static UINT8 CPU_busy_had_active=Is_No;
static UINT8 CPU_busy_idle_stable=0;
void   Low_power_IDLE(void)
{
    UINT8 refresh_over;
    UINT8 timeout_over;
    UINT8 busy_supported;
    UINT8 busy_timeout;
    EPD_BUSY_STATUS busy_status;

    if(CPU_had_IDLE_counter == 0)
    {
        CPU_busy_had_active = Is_No;
        CPU_busy_idle_stable = 0;
        EPD_Busy_PrepareObserve();
        BUSY_LOG_TEXT("BUSY monitor begin\r\n");
    }

    EPD_Busy_GetStatus(&busy_status);
    busy_supported = ((busy_status.supported == Is_Yes) &&
                      (busy_status.target_sides != EPD_BUSY_SIDE_NONE)) ? Is_Yes : Is_No;

    if(CPU_had_IDLE==Is_No)
    {
        timeout_over = (CPU_had_IDLE_counter > getRefreshScreenTime()) ? Is_Yes : Is_No;
        busy_timeout = Is_No;
        refresh_over = Is_No;

        if(busy_supported == Is_Yes)
        {
            if(busy_status.any_busy == Is_Yes)
            {
                if(CPU_busy_had_active == Is_No)
                {
                    BUSY_LOG_TEXT("BUSY active-seen\r\n");
                }
                CPU_busy_had_active = Is_Yes;
                CPU_busy_idle_stable = 0;
            }
            else if((CPU_busy_had_active == Is_Yes) && (busy_status.all_idle == Is_Yes))
            {
                CPU_busy_idle_stable++;
                if(CPU_busy_idle_stable >= EPD_BUSY_IDLE_STABLE_COUNT)
                {
                    refresh_over = Is_Yes;
                }
            }

            if((refresh_over == Is_No) && (timeout_over == Is_Yes))
            {
                busy_timeout = Is_Yes;
                refresh_over = Is_Yes;
            }
        }
        else
        {
            refresh_over = timeout_over;
        }

        if(refresh_over == Is_Yes)
        {
            if(busy_supported == Is_Yes)
            {
                if(busy_timeout == Is_Yes)
                {
                    BUSY_LOG_TEXT("BUSY refresh-timeout\r\n");
                    FAULT_LOG_TEXT("FAULT epd busy-timeout\r\n");
                }
                else
                {
                    BUSY_LOG_TEXT("BUSY refresh-complete\r\n");
                }
                BUSY_LOG_HEX8("BUSY rawA=", busy_status.raw_a);
                BUSY_LOG_HEX8("BUSY rawB=", busy_status.raw_b);
            }
            if(busy_supported == Is_Yes)
            {
                Print_I3("CPU_had_IDLE over=%d target=%x rawA=%d rawB=%d busyA=%d busyB=%d stable=%d timeout=%d",
                         CPU_had_IDLE_counter, busy_status.target_sides, busy_status.raw_a, busy_status.raw_b,
                         busy_status.busy_a, busy_status.busy_b, CPU_busy_idle_stable, busy_timeout);
            }
            else
            {
                Print_I3("CPU_had_IDLE over =%d",CPU_had_IDLE_counter);
            }
#ifdef ENABLE_SOFTWARE_TO_TDX
            TdxInfo_ClearDisplayBusyProtect();
#endif
            CPU_had_IDLE=Is_Yes;
        }
        else
        {
            if(busy_supported == Is_Yes)
            {
                Print_I3("CPU_had_IDLE continue=%d target=%x rawA=%d rawB=%d busyA=%d busyB=%d seen=%d stable=%d",
                         CPU_had_IDLE_counter, busy_status.target_sides, busy_status.raw_a, busy_status.raw_b,
                         busy_status.busy_a, busy_status.busy_b, CPU_busy_had_active, CPU_busy_idle_stable);
            }
            else
            {
                Print_I3("CPU_had_IDLE continue=%d",CPU_had_IDLE_counter);
            }
            CPU_had_IDLE_counter++;
        }
        tmos_start_task(main_task_ID,EVENT_IDLE,1600);
    }
    else
    {
        global_DEVICE_STATUS.fWorked =Is_No;
        global_DEVICE_STATUS.fWillReboot =Is_Yes;
        Print_I3("EVENT_Low_Power=%d",CPU_had_IDLE_counter);
        IMAGE_LOG_TEXT("IMG refresh lifecycle complete\r\n");
        BOOT_LOG_TEXT("BOOT lowpower queued\r\n");
        CPU_had_IDLE_counter = 0;
        CPU_had_IDLE = Is_No;
        CPU_busy_had_active = Is_No;
        CPU_busy_idle_stable = 0;
        tmos_start_task(main_task_ID,EVENT_Low_Power ,500);
    }
}
void  Low_Power_IDLE_Times(UINT8 u8t)
{    
    Low_Power_RTC(u8t);
    //global_DEVICE_STATUS.fWorked = Is_No;       
    //LowPower_Sleep(RB_PWR_RAM30K | RB_PWR_RAM2K); // ERR ֻ����30+2K SRAM ����
    //LowPower_Sleep(0);  // ERR
    //LowPower_Halt();   // ERR
    LowPower_Idle();  // OK
    
    Print_I3("over");
}

void   Low_power(void)
{
	#if !APP_LOW_POWER_ENABLE
	return;
	#endif

	uint32_t tmp, irq_status;
	//Print_I3("-----");   

	if(global_DEVICE_STATUS.fWorked == Is_Yes)
	{
		//Stop_advertising();
		Print_I3("LowPower_IDLE");
		Low_power_IDLE();
		return;
	}
#if 1
	if(global_DEVICE_STATUS.fWorked == Is_Yes)
	{
		// û�� ʹ��
		Print_I3("��Ե�ǰʱ��Ĵ������ʱ�䣬����LSE/LSIʱ��������");
		global_DEVICE_STATUS.fWorked = Is_No;
		global_DEVICE_STATUS.fWillReboot =Is_Yes;
		Low_Power_IDLE_Times(8);
		Low_Power_IDLE_Times(8);
		Low_Power_IDLE_Times(8);
	}
	else
	{
		/* PB7 is still UART0 here; finish its final diagnostics before pin remap. */
		release_uart0_wait_tx_idle(60000U);
		low_power_IIO_New();
		Print_I3("LowPower_Shutdown Period_4_S");       
		Low_Power_RTC(Period_0_125_S);
	}

	LowPower_Shutdown(0); //ȫ���ϵ磬���Ѻ�λ
	RTC_ModeFunDisable(RTC_TMR_MODE);
	R8_RTC_FLAG_CTRL = (RB_RTC_TMR_CLR | RB_RTC_TRIG_CLR);
	RTCTigFlag = 0;
	SYS_ResetExecute();
	/*
	��ģʽ���Ѻ��ִ�и�λ������������벻�����У�
	ע��Ҫȷ��ϵͳ˯��ȥ�ٻ��Ѳ��ǻ��Ѹ�λ�������п��ܱ��IDLE�ȼ�����
	*/
#endif
}

//������������������������������������������������������������������������������������������������������������������ 
unsigned char CharToHex(unsigned char bHex)
{
	if((bHex >= 0x0) && (bHex <= 9))
	{
		bHex += 0x30;
	}
	else if((bHex>=10)&&(bHex<=15))//Capital
	{
		bHex += 0x37;
	}  
	else
	{ 
		bHex = 0xff;
	}
	//   Print_I("=%x %c \r\n",bHex,bHex);
	return bHex;
}

void Mac_To_Ascii(void)
{
    uint8_t i,k;

    k=0;
    for(i=0;i<6;i++)
    {
        Mac_ASCII[k++]=CharToHex((Mac[i] & 0xF0)>>4);
        Mac_ASCII[k++]=CharToHex(Mac[i] & 0x0F);
    }

    /*for(i=0;i<12;i++)
    {
        Print_I3("%d=%c ",i,Mac_ASCII[i]);
    }
    Print_I3("\r\n");*/
}

void Init_FirstBootLastRefresh(void)
{
	// 开机时检查“首次开机初始化标志�?	// 约定：FIRST_BOOT_FLAG_VALUE(默认0x66) 为已初始化标志值，其他�?�?xFF)视为首次开�?	// 开机初始化自动定时刷屏功能

	uint8_t first_boot_flag = getFirstBootFlag();
	if(first_boot_flag != FIRST_BOOT_FLAG_VALUE){
		// 第一次开机：写入特征�?FIRST_BOOT_FLAG_VALUE
		setFirstBootFlag(FIRST_BOOT_FLAG_VALUE);
		saveLastRefreshInfo(LAST_REFRESH_TYPE_COMMON, 0, 0, SCREEN_MODE_AB_SAME, 1, REFRESH_TIMER_ENABLED, SCREEN_ALREADY_CLEARED);
		//Print_I3("Boot: first boot detected, set FIRST_BOOT_FLAG to 0x%02X (old=0x%02X)", FIRST_BOOT_FLAG_VALUE, first_boot_flag);
	}else{
		//Print_I3("Boot: not first boot, FIRST_BOOT_FLAG=0x%02X", first_boot_flag);
	}
}
void Start_LastRefresh(void)
{
	// 开机时只读取定时刷屏开关状态和清屏标志（不修改全局变量，避免影响预存刷屏功能）
	uint8_t saved_enabled, saved_screen_cleared;
	getRefreshTimerStatus(&saved_enabled, &saved_screen_cleared);
	
    // Restore the persisted screen-cleared flag.
    global_screen_cleared_flag = saved_screen_cleared;
	
	if(saved_enabled == REFRESH_TIMER_ENABLED){
		if(saved_screen_cleared == SCREEN_ALREADY_CLEARED){
            // Screen already cleared, skip refresh timer.
		} else {
			// 未清屏状态，启动定时器（使用固定小时数）
			// 将小时转换为系统tick单位�?25us�?			// 1小时 = 3600�?= 3600000毫秒 = 3600000000微秒
			// 系统tick单位�?25us，所以：1小时 = 3600000000 / 625 = 5760000 tick
			StartRefreshTimerCycle();
            // Refresh timer restored from flash.
		}
	}
	else{
        // Refresh timer disabled or not configured.
	}

}


int main(void)
{
 	int length;
	UINT8  GetResetState=0;

#if(defined(DCDC_ENABLE)) && (DCDC_ENABLE == TRUE)
	PWR_DCDCCfg(ENABLE);
#endif
	SetSysClock(SYSCLK_FREQ);   
#if APP_UART0_ENABLE
	extern void release_uart0_init(void);
#endif

	GetResetState = GetLastResetSta();

#if APP_LOW_POWER_ENABLE
	/* Original boot low-leakage profile; UART0 is restored immediately below. */
	low_power_IIO_New();
#else
	Init_GPIO();
#endif
#if APP_UART0_ENABLE
	release_uart0_init();
	BOOT_LOG_TEXT("BOOT start\r\n" );
	BOOT_LOG_TEXT("BOOT uart0-ready\r\n");
	BOOT_LOG_HEX8("BOOT reset=", GetResetState);
#endif
	CH58x_BLEInit();
	HAL_Init();
	Mac_To_Ascii();
	BOOT_LOG_TEXT("BOOT ble-ready\r\n");

#ifdef ENABLE_BOARD_ENCRYPT
	if(is_illegal_device(Mac, 6) == Is_No){
		//Main_Circulation();
		//return -1;
		Print_I3("check key failure 11\n");
	}
#endif

	GAPRole_PeripheralInit();
	if(GetResetState  == 3)
	{
		Print_I3("reset low pic ");
		setWorkMode(DEVICE_MODE_LOW);
		setFirstBootFlag(0xFF);//恢复默认值，如果不是0x66，就会初始化定时刷屏功能
		initPicSave(SCREEN_CLEAN_ALL,0xff,0xff);
		clearUserId();
	}
	global_DEVICE_STATUS.fFlashType = FLASH_TYPE_EXTERNAL;
    // Init automatic timed refresh state.
    Init_FirstBootLastRefresh();

	/*FlashType g_flashType = detectFlashType();
	if (g_flashType == FLASH_TYPE_EXTERNAL) {
		global_DEVICE_STATUS.fFlashType = FLASH_TYPE_EXTERNAL;
    } else {
		global_DEVICE_STATUS.fFlashType = FLASH_TYPE_INTERNAL;
    }*/

	Peripheral_Init();    
	GAPRole_CentralInit();
	Central_Init();
	main_task_ID = TMOS_ProcessEventRegister(Main_Event);
#if APP_FACTORY_UART0_ENABLE
	FactorySelftest_Init();
	FactorySelftest_SendBootReport();
	BOOT_LOG_TEXT("BOOT factory-ready\r\n");
#endif
	global_DEVICE_STATUS.fisOtaed = 0;
	global_DEVICE_STATUS.fWillReboot = Is_No;

	ControlEPDPower(Is_Off);
	AdcInit();

#if APP_FACTORY_UART0_ENABLE && APP_FACTORY_POWER_HOLD_ENABLE
	factory_power_was_present = FactorySelftest_ShouldBlockDeepSleep();
	BOOT_LOG_HEX8("BOOT power=", factory_power_was_present);
#endif
	
#ifndef ENABLE_SOFTWARE_TO_XT
	if(GetResetState  == 3)
	{
		//Print_I3("reset low pic 0000");
		global_DEVICE_STATUS.fInitDriver = Is_Yes;
		cleanDisplayColor(SCREEN_COLOR_WHITE, Is_Yes);
		//global_DEVICE_STATUS.fWorked = Is_No;
		tmos_set_event(main_task_ID,EVENT_Low_Power);
	}
#endif

	//tmos_start_task(main_task_ID,EVENT_Test_Msg ,2*1600);    //  2��
    StartChargeMonitor();
	WWDG_ClearFlag();
	WWDG_ResetCfg(DISABLE); // ENABLE

	Ble_Scan=Is_Off;
	Stop_Central_Scan();
	//tmos_start_task(main_task_ID,EVENT_Start_Peripheral_Mode ,2*1600);
	
	unsigned char mode1[2];
	Get_EEPROM_Flag(mode1,WORKMODE_Position,WORKMODE_Len);
	if(mode1[0] == DEVICE_MODE_HIGH){
		tmos_start_task(main_task_ID,EVENT_Start_Central_Mode ,2*1600);
	}

#ifdef EPD_BWSOLID_IMG_CYCLE_TEST_ENABLE
	tmos_start_task(main_task_ID,EVENT_Test_Msg ,500);
#endif
	
#ifdef EPD_DISPLAY_TEST_ENABLE
	//EPD_Driver_Display_WithOut_Flash_Debug();
	EPD_Driver_Display_From_Flash_Debug();
#endif
    // Start automatic refresh timer.
    Start_LastRefresh();

	uint32_t stored_user_id = getUserId();
    PRINT("--- User id = 0x%08X ---\n", stored_user_id);

	// ADC 漏电保持上拉输入
	GPIOA_ModeCfg(GPIO_Pin_7, GPIO_ModeIN_PU);

//    Print_I3("CMD PICDIS..AB Screen");
//    Xin_Tai(1);
//    AB_only();   
//    global_BOE_DEVICE_STATUS.fIsNeedStandby = 1;
//    tmos_set_event(main_task_ID,EVENT_Low_Power);

	Main_Circulation();
}
// F:\project\AK80X\e_papper\CH583_Ble_E_paper\BLE\BackupUpgrade_OTA\obj\a.bin
// ��һ���������˻�����������жϣ��ڶ����ڵ͹���ǰʹ�ܿ��Ź����������ڵ͹��ĺ��һ������λ
