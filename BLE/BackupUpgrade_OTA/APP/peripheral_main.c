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

/* ��¼��ǰ��Image */
#include "commoninfo.h"
#include "flash_api.h"
#include "adc_api.h"
#include "uart1_wifi_passthrough.h"
#include "app_nfc.h"
#ifdef ENABLE_SOFTWARE_TO_TDX
#include "tdxinfoservice.h"
#endif

#include "debug/Epd_Driver_Debug.h"

void   	Low_power(void);
void   	Low_power_IDLE(void);
__INTERRUPT
__HIGH_CODE
void HardFault_Handler(void)
{
    uint32_t mcause = __get_MCAUSE();
    uint32_t mepc = __get_MEPC();
    uint32_t mtval = __get_MTVAL();

    printf("\r\n[CH585] HardFault mcause=0x%08lX mepc=0x%08lX mtval=0x%08lX\r\n",
           (unsigned long)mcause,
           (unsigned long)mepc,
           (unsigned long)mtval);
    while(1);
}
#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
#define WIFI_WAKE_KEY_DEBOUNCE_MS 20U
#define WIFI_WAKE_KEY_RELEASE_STABLE_COUNT 5U
#define WIFI_WAKE_KEY_PB1 GPIO_Pin_1
#define WIFI_WAKE_KEY_PB2 WIFI_WAKE_KEY
#define WIFI_WAKE_KEY_PB1_LONG_PRESS_TICKS (5U * 1600U)
static uint8_t s_wifi_wake_key_pb2_down = Is_No;
static uint8_t s_wifi_wake_key_armed = Is_No;
static uint8_t s_wifi_wake_key_release_count = 0;
static uint8_t s_wifi_wake_key_pb1_down = Is_No;
static uint8_t s_wifi_wake_key_pb1_triggered = Is_No;
static uint32_t s_wifi_wake_key_pb1_press_tick = 0;
static void WifiWakeKey_Config(void);
static void WifiWakeKey_Poll(void);
#endif
static void NfcReservedPins_ProtectLowPower(void);
static void LowPower_DisableUnusedDigitalInputs(void);

#ifdef ENABLE_UART1_OFFICIAL_EXAMPLE_TEST
static uint8_t s_uart1_exam_rx[100];
static uint8_t s_uart1_exam_trig = 7;
static volatile uint32_t s_uart1_exam_irq_count = 0;
static volatile uint32_t s_uart1_exam_rx_bytes = 0;
static volatile uint32_t s_uart1_exam_line_count = 0;
static volatile uint32_t s_uart1_exam_ready_count = 0;
static volatile uint32_t s_uart1_exam_tout_count = 0;
static volatile uint8_t s_uart1_exam_changed = 0;
volatile uint8_t g_uart1_exam_disable_hal_sleep = 0;

static void UART1_OfficialExampleInit(void)
{
	uint8_t TxBuff[] = "This is a tx exam\r\n";

	GPIOA_SetBits(GPIO_Pin_9);
	GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
	GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);
	UART1_DefInit();
	UART1_BaudRateCfg(115200);
	UART1_ByteTrigCfg(UART_7BYTE_TRIG);
	UART1_INTCfg(ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
	PFIC_SetPriority(UART1_IRQn, 0x00);
	PFIC_EnableIRQ(UART1_IRQn);
	PFIC_EnableAllIRQ();

	Print_I3("[UART1_EXAM] UART1 irq start");
	UART1_SendString(TxBuff, sizeof(TxBuff));
}

static void UART1_OfficialExamplePollPrint(void)
{
	static uint32_t last_irq = 0;

	if(s_uart1_exam_changed || last_irq != s_uart1_exam_irq_count)
	{
		s_uart1_exam_changed = 0;
		last_irq = s_uart1_exam_irq_count;
		Print_I3("[UART1_EXAM] irq=%lu bytes=%lu rfc=%d lsr=%02X",
				 s_uart1_exam_irq_count,
				 s_uart1_exam_rx_bytes,
				 R8_UART1_RFC,
				 UART1_GetLinSTA());
	}
}

__INTERRUPT
__HIGH_CODE
void UART1_IRQHandler(void)
{
	volatile uint8_t i;
	uint16_t len;

	s_uart1_exam_irq_count++;
	switch(UART1_GetITFlag())
	{
		case UART_II_LINE_STAT:
			UART1_GetLinSTA();
			s_uart1_exam_line_count++;
			s_uart1_exam_changed = 1;
			break;

		case UART_II_RECV_RDY:
			s_uart1_exam_ready_count++;
			for(i = 0; i != s_uart1_exam_trig; i++)
			{
				s_uart1_exam_rx[i] = UART1_RecvByte();
				UART1_SendByte(s_uart1_exam_rx[i]);
			}
			s_uart1_exam_rx_bytes += s_uart1_exam_trig;
			s_uart1_exam_changed = 1;
			break;

		case UART_II_RECV_TOUT:
			s_uart1_exam_tout_count++;
			len = UART1_RecvString(s_uart1_exam_rx);
			UART1_SendString(s_uart1_exam_rx, len);
			s_uart1_exam_rx_bytes += len;
			s_uart1_exam_changed = 1;
			break;

		default:
			break;
	}
}
#endif

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
uint32_t global_refresh_timer_interval = 0; // 定时刷屏的时间间�?系统tick�?tick=625us)
uint8_t global_screen_cleared_flag = SCREEN_ALREADY_CLEARED;  // 清屏标志：默认已清屏（复位后�?
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
#ifdef ENABLE_UART1_OFFICIAL_EXAMPLE_TEST
		UART1_OfficialExamplePollPrint();
#endif
#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
        WifiPassthrough_FastPoll();
        WifiWakeKey_Poll();
#endif
#ifdef ENABLE_APP_NFC
        NfcApp_FastPoll();
#endif
        TMOS_SystemProcess();
#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
        WifiPassthrough_FastPoll();
        WifiWakeKey_Poll();
#endif
#ifdef ENABLE_APP_NFC
        NfcApp_FastPoll();
#endif
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
//GPIOA_ModeCfg(GPIO_Pin_4|GPIO_Pin_5|GPIO_Pin_9|GPIO_Pin_12|GPIO_Pin_14,GPIO_ModeOut_PP_5mA);
//GPIOA_ModeCfg(GPIO_Pin_8,GPIO_ModeIN_Floating);
//GPIOA_ModeCfg(GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_2|GPIO_Pin_3|GPIO_Pin_6|GPIO_Pin_7|GPIO_Pin_10|GPIO_Pin_11|GPIO_Pin_14, GPIO_ModeIN_PU);


//������������������������������������������������������������������������������������������������������
//PB00  #define  Key3_def  GPIO_Pin_0    // PB
//PB01  #define  LED1                GPIO_Pin_1       //:PB  0=on , 1=off
//PB02  #define  LED2                GPIO_Pin_2       //:PB  0=on , 1=off
//PB03  #define  Key2_def  GPIO_Pin_3    // PB
//PB04  Key5
//PB05  #define  LED5_Power              GPIO_Pin_5       //:PB  0=on , 1=off
//PB06  #define  LED6_Power              GPIO_Pin_6       //:PB  0=on , 1=off
//PB07
//������������������������������������������������������������������������������������������������������

//PB08  #define  Key1_def  GPIO_Pin_8    // PB
//PB09  #define  LCD_Power_B       GPIO_Pin_9       //:PB9  0=on , 1=off
//PB10
//PB11
//PB12  #define epaper2_BUSY  GPIO_Pin_12
//PB13  #define  CHARGE_LED  GPIO_Pin_13   // PB  �����?//PB14  #define epaper2_CS    GPIO_Pin_14
//PB15  #define epaper2_DC    GPIO_Pin_15
//������������������������������������������������������������������������������������������������������
//������������������������������������������������������������������������������������������������������
// PB21 LED5
// Sleep
//GPIOB_ModeCfg(GPIO_Pin_12 | GPIO_Pin_14 |GPIO_Pin_15|GPIO_Pin_7,GPIO_ModeIN_Floating);
//GPIOB_ModeCfg(GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_2|GPIO_Pin_3|GPIO_Pin_4|GPIO_Pin_5|GPIO_Pin_6|GPIO_Pin_8|GPIO_Pin_9|GPIO_Pin_10|GPIO_Pin_11|GPIO_Pin_13, GPIO_ModeIN_PU);


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

static void NfcReservedPins_ProtectLowPower(void)
{
#ifdef ENABLE_APP_NFC
    NfcApp_EnterLowPowerIo();
#else
    GPIOB_ModeCfg(GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_14 | GPIO_Pin_15 | GPIO_Pin_16 | GPIO_Pin_17, GPIO_ModeIN_Floating);
#endif
}

static void LowPower_DisableUnusedDigitalInputs(void)
{
    uint32_t pa_disable = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 |
                          GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_7 |
                          GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 |
                          GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_13 |
                          GPIO_Pin_14 | GPIO_Pin_15;
    uint32_t pb_disable = GPIO_Pin_0 | GPIO_Pin_8 | GPIO_Pin_9 |
                          GPIO_Pin_10 | GPIO_Pin_11 | GPIO_Pin_14 |
                          GPIO_Pin_15;

    R32_PIN_IN_DIS |= pa_disable;
    R32_PIN_IN_DIS |= (pb_disable << 16);
    R16_PIN_CONFIG |= ((GPIO_Pin_16 | GPIO_Pin_17) >> 8);
}

void  low_power_IIO_New(void)
{
    mDelaymS(500);

    GPIOA_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_Floating);
    GPIOB_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_Floating);

    ControlEPDPower(Is_Off);

    GPIOB_ModeCfg(GPIO_Pin_3, GPIO_ModeOut_PP_5mA);
    GPIOB_ResetBits(GPIO_Pin_3);

    GPIOB_ModeCfg(GPIO_Pin_12, GPIO_ModeOut_PP_5mA);
    GPIOB_SetBits(GPIO_Pin_12);

    GPIOA_ModeCfg(GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9, GPIO_ModeIN_Floating);
    GPIOB_ModeCfg(CHARGE_LED, GPIO_ModeIN_Floating);
#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
    WifiWakeKey_Config();
    WifiPassthrough_EnterLowPowerIo();
#endif
#if defined(Debug_mode_on)
    GPIOPinRemap(DISABLE, RB_PIN_UART0);
    GPIOB_ModeCfg(GPIO_Pin_4, GPIO_ModeIN_PU);
    GPIOB_ModeCfg(GPIO_Pin_7, GPIO_ModeOut_PP_5mA);
#endif
    RED_Power_off;
    GREEN_Power_off;
    NfcReservedPins_ProtectLowPower();
    LowPower_DisableUnusedDigitalInputs();
}

void  Disable_GPIO_IRQ(void)
{
    PFIC_DisableIRQ(GPIO_B_IRQn);
}


#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
static void WifiWakeKey_Config(void)
{
    R32_PIN_IN_DIS &= ~((uint32_t)(WIFI_WAKE_KEY_PB1 | WIFI_WAKE_KEY_PB2) << 16);
    GPIOB_ModeCfg(WIFI_WAKE_KEY_PB1 | WIFI_WAKE_KEY_PB2, GPIO_ModeIN_PU);
    s_wifi_wake_key_pb2_down = Is_No;
    s_wifi_wake_key_armed = Is_No;
    s_wifi_wake_key_release_count = 0;
    s_wifi_wake_key_pb1_down = Is_No;
    s_wifi_wake_key_pb1_triggered = Is_No;
    s_wifi_wake_key_pb1_press_tick = 0;
    PWR_PeriphWakeUpCfg(ENABLE, RB_SLP_GPIO_WAKE, Long_Delay);
}

static void WifiWakeKey_PollPb1(void)
{
    uint32_t now;
    uint32_t elapsed;

    if(GPIOB_ReadPortPin(WIFI_WAKE_KEY_PB1) == 0)
    {
        now = TMOS_GetSystemClock();
        if(s_wifi_wake_key_pb1_down != Is_Yes)
        {
            mDelaymS(WIFI_WAKE_KEY_DEBOUNCE_MS);
            if(GPIOB_ReadPortPin(WIFI_WAKE_KEY_PB1) != 0)
            {
                return;
            }
            s_wifi_wake_key_pb1_down = Is_Yes;
            s_wifi_wake_key_pb1_triggered = Is_No;
            s_wifi_wake_key_pb1_press_tick = TMOS_GetSystemClock();
            return;
        }

        if(s_wifi_wake_key_pb1_triggered == Is_Yes)
        {
            return;
        }

        elapsed = now - s_wifi_wake_key_pb1_press_tick;
        if(elapsed >= WIFI_WAKE_KEY_PB1_LONG_PRESS_TICKS)
        {
            s_wifi_wake_key_pb1_triggered = Is_Yes;
            WifiPassthrough_OnWakeKeyPb1Pressed();
        }
        return;
    }

    s_wifi_wake_key_pb1_down = Is_No;
    s_wifi_wake_key_pb1_triggered = Is_No;
    s_wifi_wake_key_pb1_press_tick = 0;
}

static void WifiWakeKey_PollPb2(void)
{
    if(GPIOB_ReadPortPin(WIFI_WAKE_KEY_PB2) == 0)
    {
        if(s_wifi_wake_key_pb2_down != Is_Yes)
        {
            mDelaymS(WIFI_WAKE_KEY_DEBOUNCE_MS);
            if(GPIOB_ReadPortPin(WIFI_WAKE_KEY_PB2) == 0)
            {
                s_wifi_wake_key_pb2_down = Is_Yes;
                WifiPassthrough_OnWakeKeyPb2Pressed();
            }
        }
    }
    else
    {
        s_wifi_wake_key_pb2_down = Is_No;
    }
}

static void WifiWakeKey_Poll(void)
{
    if(s_wifi_wake_key_armed != Is_Yes)
    {
        if(GPIOB_ReadPortPin(WIFI_WAKE_KEY_PB1 | WIFI_WAKE_KEY_PB2) != (WIFI_WAKE_KEY_PB1 | WIFI_WAKE_KEY_PB2))
        {
            s_wifi_wake_key_pb2_down = Is_Yes;
            s_wifi_wake_key_release_count = 0;
            return;
        }

        mDelaymS(WIFI_WAKE_KEY_DEBOUNCE_MS);
        if(GPIOB_ReadPortPin(WIFI_WAKE_KEY_PB1 | WIFI_WAKE_KEY_PB2) == (WIFI_WAKE_KEY_PB1 | WIFI_WAKE_KEY_PB2))
        {
            if(s_wifi_wake_key_release_count < WIFI_WAKE_KEY_RELEASE_STABLE_COUNT)
            {
                s_wifi_wake_key_release_count++;
            }
            if(s_wifi_wake_key_release_count >= WIFI_WAKE_KEY_RELEASE_STABLE_COUNT)
            {
                s_wifi_wake_key_armed = Is_Yes;
                s_wifi_wake_key_pb2_down = Is_No;
                s_wifi_wake_key_pb1_down = Is_No;
                s_wifi_wake_key_pb1_triggered = Is_No;
                s_wifi_wake_key_pb1_press_tick = 0;
                Print_I3("[WIFI_PT] PB1/PB2 wake key armed");
            }
        }
        else
        {
            s_wifi_wake_key_pb2_down = Is_Yes;
            s_wifi_wake_key_release_count = 0;
        }
        return;
    }

    WifiWakeKey_PollPb1();
    WifiWakeKey_PollPb2();
}
#endif
void Init_GPIO(void)
{

    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_Floating); // ADC / UART1 RX when WiFi is off
    GPIOB_ModeCfg(CHARGE_LED, GPIO_ModeIN_PD);
    RED_Power_off;
    GREEN_Power_off;

#ifdef  Debug_mode_on
    GPIOPinRemap(DISABLE, RB_PIN_UART0);
    GPIOB_ModeCfg(GPIO_Pin_4, GPIO_ModeIN_PU);
    GPIOB_ModeCfg(GPIO_Pin_7, GPIO_ModeOut_PP_5mA);
#endif
#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
    GPIOA_ModeCfg(GPIO_Pin_6, GPIO_ModeOut_PP_5mA);
    WifiWakeKey_Config();
#if WIFI_FORCE_ALWAYS_ON
    R32_PA_OUT |= GPIO_Pin_6;
#else
    R32_PA_CLR |= GPIO_Pin_6;
#endif
#endif
    NfcReservedPins_ProtectLowPower();
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

tmosTaskID main_task_ID;

UINT8  Ble_Scan=Is_Off;
UINT8  Ble_Central_Scan= Is_Off;

//UINT8  Frist_time=0;
void Save_LastRefresh_Info_To_Flash(void)
{
#ifdef DISABLE_EPD_IMAGE_FEATURES
	return;
#else
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
		// 这样可以保持刷屏成功状态，让Low_power_IDLE()循环正常进行30�?		last_saved_success_flag = Is_Yes;
	}
	// 如果是新的刷屏周期开始（fDataSendSuccess从Is_Yes变为Is_No），重置保存标志

	if(global_DEVICE_STATUS.fDataSendSuccess == Is_No){
		last_saved_success_flag = Is_No;
	}
	// ======================================================================

#endif
}

tmosEvents Main_Event(tmosTaskID task_id, tmosEvents events)
{
    uint8_t *msgPtr;
    if(events & SYS_EVENT_MSG)
    { // ����HAL����Ϣ������tmos_msg_receive��ȡ��Ϣ��������ɺ�ɾ����Ϣ��?        msgPtr = tmos_msg_receive(task_id);
        if(msgPtr)
        {
            /* De-allocate */
            tmos_msg_deallocate(msgPtr);
        }
        return events ^ SYS_EVENT_MSG;
    }

    if(events & EVENT_Get_Battle_Charge)
    {
#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
		return events ^ EVENT_Get_Battle_Charge;
#endif
	if(global_DEVICE_STATUS.fImageType == 1){
		// 异显模式：需要刷两次（A�?B面）
		// 普通刷图的异显模式：不管内外置flash，都直接�?/1（不加偏移）
		// �?次：刷A面（从index=1�?		global_DEVICE_STATUS.fInitDriver = Is_Yes;
		global_DEVICE_STATUS.fImageDataLen = 0;
		global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_A;
		global_EXTERN_FLASH_INFO.fImageIndex = SCREEN_B_COMMON_INDEX; // 1

		if(preSaveDisplayColor(0, 0, DEVICE_PRE_SAVE) == -1){
			PRINT("send pre save  error \r\n");
			global_DEVICE_STATUS.fWorked =Is_No;
		}
		mDelaymS(2000);

		// �?次：刷B面（从index=0�?		global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_B;
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
    	AdcTask();
		tmos_start_task(main_task_ID, EVENY_Is_Charge, ADC_TASK_PERIOD_TMOS_TICKS);
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
#ifdef ENABLE_APP_NFC
		if(NfcApp_IsWindowBusy() == Is_Yes)
		{
			Print_I3("EVENT_Low_Power ignored, NFC window busy");
			return events ^ EVENT_Low_Power;
		}
#endif
#ifdef ENABLE_WIFI_UART1_PASSTHROUGH

		if(WifiPassthrough_IsWifiAwake() == Is_Yes)
		{
			Print_I3("EVENT_Low_Power ignored, WiFi awake");
			return events ^ EVENT_Low_Power;
		}

		if(WifiPassthrough_IsWakePending() == Is_Yes)
		{
			Print_I3("EVENT_Low_Power ignored, WiFi wake pending");
			return events ^ EVENT_Low_Power;
		}

		if(WifiPassthrough_IsUsbPowerPresent() == Is_Yes)
		{
			Print_I3("EVENT_Low_Power ignored, USB present");
			return events ^ EVENT_Low_Power;
		}

		if(WifiPassthrough_IsTimedWakeArmed() == Is_Yes)
		{
			Print_I3("EVENT_Low_Power ignored, timed wake armed");
			return events ^ EVENT_Low_Power;
		}
#endif

		if(global_DEVICE_STATUS.fisBleConnect == Is_Yes)
		{
			Print_I3("EVENT_Low_Power ignored, BLE connected");
			return events ^ EVENT_Low_Power;
		}

		if(global_DEVICE_STATUS.fWorked == Is_Yes || global_DEVICE_STATUS.fisHaveData == Is_Yes)
		{
			Print_I3("EVENT_Low_Power ignored, busy");
			return events ^ EVENT_Low_Power;
		}

#ifndef ENABLE_WIFI_UART1_PASSTHROUGH
		Save_LastRefresh_Info_To_Flash();
#endif

		if(global_DEVICE_STATUS.fisOtaed != 1)
			Low_power();

		return events ^ EVENT_Low_Power;
	}

	if(events & EVENT_Refresh_Timer)
	{
#ifdef DISABLE_EPD_IMAGE_FEATURES
		return events ^ EVENT_Refresh_Timer;
#else
		// 定时刷屏功能：从flash读取上次刷屏的数据并重新刷屏
		Print_I3("EVENT_Refresh_Timer triggered, refreshing screen from flash");

		if(global_refresh_timer_interval > 0){
			// 从flash读取上次刷屏的类型和参数
			// index/zip/fScreenType 会由 getLastRefreshInfo() 回写到全局变量
			// 小时数使用固定�?REFRESH_TIMER_FIXED_HOURS
			uint8_t last_type, last_group, last_room, last_enabled, last_screen_mode;
			getLastRefreshInfo(&last_type, &last_group, &last_room, &last_enabled, &last_screen_mode);

			uint8_t last_index = global_EXTERN_FLASH_INFO.fImageIndex;
			uint8_t last_zip = global_EXTERN_FLASH_INFO.fZip;

			// 设置刷屏参数（与正常投图保持一致，参考InitFirstPackage�?			global_DEVICE_STATUS.fPackageCnt = 0;  // 初始化包计数�?			global_DEVICE_STATUS.fPackageCount = 0xFFFF;  // 定时刷屏不需要包计数，设置为最大�?			global_DEVICE_STATUS.fDataSendSuccess = Is_No;  // 初始化发送状�?			global_DEVICE_STATUS.fOtaStatus = RTN_OTA_COMMON;  // 设置OTA状态为普通模�?			global_DEVICE_STATUS.fIsNeedStandby = 1;  // 需要待�?			global_DEVICE_STATUS.fInitDriver = Is_Yes;  // 需要初始化驱动
			global_DEVICE_STATUS.fImageDataLen = 0;  // 重置图像数据长度
			global_EXTERN_FLASH_INFO.fBlockNum = EXTERN_FLASH_BLOCK_FIRST_ADDR;  // 从第一个块开�?			global_EXTERN_FLASH_INFO.fImageIndex = last_index;  // 使用保存的索引（支持外置flash�?			global_EXTERN_FLASH_INFO.fZip = last_zip;  // 使用保存的zip标志

			// 根据flash中保存的类型进行刷屏
			if(last_type == LAST_REFRESH_TYPE_COMMON || last_type == LAST_REFRESH_TYPE_NONE){
				// 普通刷屏模�?				global_DEVICE_STATUS.fRefreshType = DEVICE_OP_COMMON;

				// 检查是否为异显模式（需要刷两次�?				if(last_screen_mode == SCREEN_MODE_AB_DIFF){
					// 异显模式：触�?EVENT_Get_Battle_Charge，让它自动刷两次（A�?B面）
					// fImageType=1 已在 getLastRefreshInfo() 中设�?					InitFlashDriver();
					mDelayuS(10);

					tmos_start_task(main_task_ID, EVENT_Get_Battle_Charge, 100);
					mDelayuS(10);
					DeInitFlashDriver();
				} else {
					// 单面或同显模式：直接刷一�?					InitFlashDriver();
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
				// 关键修复：设�?fBoardCastGroup �?fBoardCastRoom，供 EVENT_Low_Power 保存时使�?				global_DEVICE_STATUS.fBoardCastGroup = last_group;
				global_DEVICE_STATUS.fBoardCastRoom = last_room;
				InitFlashDriver();
				mDelayuS(10);

				if(preSaveDisplayColor(global_DEVICE_STATUS.fBoardCastGroup, global_DEVICE_STATUS.fBoardCastRoom, DEVICE_PRE_SAVE) == -1){
					PRINT("Refresh timer: pre save display error\r\n");
					global_DEVICE_STATUS.fWorked = Is_No;
				} else {
					// 刷屏成功，标记成功（触发EVENT_Low_Power保存逻辑�?					global_DEVICE_STATUS.fDataSendSuccess = Is_Yes;
				}
				mDelayuS(10);
				DeInitFlashDriver();
			}
			else{
				PRINT("Unknown refresh type: 0x%02X, skip refresh\r\n", last_type);
			}

			// 重新启动定时器，实现循环刷屏
			tmos_start_task(main_task_ID, EVENT_Refresh_Timer, global_refresh_timer_interval);
		}
#endif
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
#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
		return events ^ EVENT_Start_Central_Mode;
#endif
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
#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
		return events ^ EVENT_Boardcast_Op;
#endif
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

#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
static UINT8 ShouldBootWakeWifi(UINT8 reset_state)
{
    switch(reset_state)
    {
        case 1: // RPOR, real power on reset
        case 3: // MR, external manual reset
        case 5: // GRWSM, wake from shutdown mode
            return Is_Yes;

        case 0: // SR, software reset
        case 2: // WTR, watchdog reset
        default:
            return Is_No;
    }
}
#endif

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
void   Low_power_IDLE(void)
{
    char busy;
    /*if((Product_Type == INK_SCREEN_JD79668A_400X300_COLOR_4) || (Product_Type == INK_SCREEN_JD79665_800X480_COLOR_4))
    {
		if(is_Busy()==0)
        {
          	busy = Is_Over;
        }
        else
        {
          	busy = Is_Busying;
        }
    }
    else*/
    {
      	busy = Is_Busying;
    }
    // ���µĴ��룬 ֻ�� LowPower_Idle ʱ��Ч����
    // ������ ֱ�� �����ˣ������ߵ�
    if(CPU_had_IDLE==Is_No)
    {
        if((CPU_had_IDLE_counter > getRefreshScreenTime())||(busy == Is_Over))     // 5
        {
            /* �޸�DataFlash���л���ImageIAP */
            Print_I3("CPU_had_IDLE over =%d",CPU_had_IDLE_counter);
#ifdef ENABLE_SOFTWARE_TO_TDX
			TdxInfo_ClearDisplayBusyProtect();
#endif
            CPU_had_IDLE=Is_Yes;
        }
        else
        {
            Print_I3("CPU_had_IDLE continue=%d",CPU_had_IDLE_counter);
            CPU_had_IDLE_counter++;
        }
       	//tmos_set_event(main_task_ID,EVENT_IDLE);
       	tmos_start_task(main_task_ID,EVENT_IDLE,1600);  // 8000
    }
    else
    {
        global_DEVICE_STATUS.fWorked =Is_No;
		global_DEVICE_STATUS.fWillReboot =Is_Yes;
        Print_I3("EVENT_Low_Power=%d",CPU_had_IDLE_counter);
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
		low_power_IIO_New();
#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
		Print_I3("WiFi low power IO ready, skip shutdown");
		return;
#else
		Print_I3("LowPower_Shutdown Period_4_S");
		Low_Power_RTC(Period_0_125_S);
#endif
	}

	LowPower_Shutdown(0); //ȫ���ϵ磬���Ѻ�λ
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
#ifdef DISABLE_EPD_IMAGE_FEATURES
	return;
#else
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
#endif
}
void Start_LastRefresh(void)
{
#ifdef DISABLE_EPD_IMAGE_FEATURES
	return;
#else
	// 开机时只读取定时刷屏开关状态和清屏标志（不修改全局变量，避免影响预存刷屏功能）
	uint8_t saved_enabled, saved_screen_cleared;
	getRefreshTimerStatus(&saved_enabled, &saved_screen_cleared);

	// 更新全局清屏标志（从flash读取�?	global_screen_cleared_flag = saved_screen_cleared;

	if(saved_enabled == REFRESH_TIMER_ENABLED){
		if(saved_screen_cleared == SCREEN_ALREADY_CLEARED){
			// 已清屏状态，不启动定时刷�?			/*Print_I3("Boot: Screen already cleared, skip refresh timer: enabled=%d, cleared=%d, fixed_hours=%d",
					 saved_enabled, saved_screen_cleared, REFRESH_TIMER_FIXED_HOURS);*/
		} else {
			// 未清屏状态，启动定时器（使用固定小时数）
			// 将小时转换为系统tick单位�?25us�?			// 1小时 = 3600�?= 3600000毫秒 = 3600000000微秒
			// 系统tick单位�?25us，所以：1小时 = 3600000000 / 625 = 5760000 tick
			global_refresh_timer_interval = (uint32_t)REFRESH_TIMER_FIXED_HOURS * 5760000;//96000;
			tmos_start_task(main_task_ID, EVENT_Refresh_Timer, global_refresh_timer_interval);
			/*Print_I3("Boot: Restore refresh timer from flash: enabled=%d, cleared=%d, fixed_hours=%d, interval=%u ticks",
					 saved_enabled, saved_screen_cleared, REFRESH_TIMER_FIXED_HOURS, global_refresh_timer_interval);*/
		}
	}
	else{
		/*Print_I3("Boot: Refresh timer disabled or not configured: enabled=%d, cleared=%d, fixed_hours=%d",
				 saved_enabled, saved_screen_cleared, REFRESH_TIMER_FIXED_HOURS);*/
	}

#endif
}


int main(void)
{
 	int length;
	UINT8  GetResetState=0;

#if(defined(DCDC_ENABLE)) && (DCDC_ENABLE == TRUE)
	PWR_DCDCCfg(ENABLE);
#endif
	HSECFG_Capacitance(HSECap_18p);
	SetSysClock(SYSCLK_FREQ);
	GetResetState = GetLastResetSta();
#ifdef  Debug_mode_on

	GPIOPinRemap(DISABLE,RB_PIN_UART0);  // UART0: PB4 RX, PB7 TX
	GPIOB_ModeCfg(GPIO_Pin_4, GPIO_ModeIN_PU);
	GPIOB_SetBits(GPIO_Pin_7);
	GPIOB_ModeCfg(GPIO_Pin_7, GPIO_ModeOut_PP_5mA);
	UART0_DefInit();

	if(GetResetState  != 5)
	{
		//Print_I3("low ");
		// Low_power();
	}
#endif
	Print_I3("Boot reset state=%d", GetResetState);

#ifdef  Low_power_mode_on
#if(defined(HAL_SLEEP)) && (HAL_SLEEP == TRUE)
	low_power_IIO_New();
#endif
#else
#endif

#ifdef ENABLE_UART1_OFFICIAL_EXAMPLE_TEST
	Print_I3("[UART1_EXAM] BLE adv + UART1 irq test");
#endif

#ifdef ENABLE_APP_NFC
    if(NfcApp_IsSessionBootRequest() == Is_Yes)
    {
        NfcApp_RunOnlySession();
    }
#endif

	ReadImageFlag();
	CH58x_BLEInit();
	HAL_Init();
	Mac_To_Ascii();
	GAPRole_PeripheralInit();
#ifndef Low_power_mode_on
	Init_GPIO();
	init_cs();
	Set_Spi0_Input_all_input();
#endif
	if(GetResetState  == 3)
	{
		Print_I3("reset low pic ");
		setWorkMode(DEVICE_MODE_LOW);
		#ifndef DISABLE_EPD_IMAGE_FEATURES

		setFirstBootFlag(0xFF);//恢复默认值，如果不是0x66，就会初始化定时刷屏功能
		initPicSave(SCREEN_CLEAN_ALL,0xff,0xff);

		#endif
		#ifndef DISABLE_USER_LOCK_FEATURE

		clearUserId();

		#endif
	}
	global_DEVICE_STATUS.fFlashType = FLASH_TYPE_EXTERNAL;
	//初始化自动定时刷屏功�?	Init_FirstBootLastRefresh();

	/*FlashType g_flashType = detectFlashType();
	if (g_flashType == FLASH_TYPE_EXTERNAL) {
		global_DEVICE_STATUS.fFlashType = FLASH_TYPE_EXTERNAL;
    } else {
		global_DEVICE_STATUS.fFlashType = FLASH_TYPE_INTERNAL;
    }*/
	Peripheral_Init();
#if !defined(ENABLE_WIFI_UART1_PASSTHROUGH) && !defined(ENABLE_UART1_OFFICIAL_EXAMPLE_TEST)
	GAPRole_CentralInit();
	Central_Init();
#endif
	main_task_ID = TMOS_ProcessEventRegister(Main_Event);
	global_DEVICE_STATUS.fisOtaed = 0;
	global_DEVICE_STATUS.fWillReboot = Is_No;

	ControlEPDPower(Is_Off);
#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
    WifiPassthrough_Init();
    if(ShouldBootWakeWifi(GetResetState) == Is_Yes)
    {
        Print_I3("Boot WiFi wake allowed");
        WifiPassthrough_RequestBootWake();
    }
    else
    {
        Print_I3("Boot WiFi wake skipped, reset state=%d", GetResetState);
    }
#endif
#ifdef ENABLE_APP_NFC
    NfcApp_Init();
    NfcApp_HandleBleBootPendingActions();
#endif
    AdcInit();
#ifdef ENABLE_UART1_OFFICIAL_EXAMPLE_TEST
	UART1_OfficialExampleInit();
#endif

#if !defined(ENABLE_SOFTWARE_TO_XT) && !defined(ENABLE_WIFI_UART1_PASSTHROUGH)
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
	tmos_start_task(main_task_ID, EVENY_Is_Charge, ADC_TASK_PERIOD_TMOS_TICKS);
	WWDG_ClearFlag();
	WWDG_ResetCfg(DISABLE); // ENABLE

	Ble_Scan=Is_Off;
	Stop_Central_Scan();
	//tmos_start_task(main_task_ID,EVENT_Start_Peripheral_Mode ,2*1600);

	unsigned char mode1[2];
	Get_EEPROM_Flag(mode1,WORKMODE_Position,WORKMODE_Len);
#if !defined(ENABLE_WIFI_UART1_PASSTHROUGH) && !defined(ENABLE_UART1_OFFICIAL_EXAMPLE_TEST)
	if(mode1[0] == DEVICE_MODE_HIGH){
		tmos_start_task(main_task_ID,EVENT_Start_Central_Mode ,2*1600);
	}
#endif

#ifdef EPD_BWSOLID_IMG_CYCLE_TEST_ENABLE
	tmos_start_task(main_task_ID,EVENT_Test_Msg ,500);
#endif

#ifdef EPD_DISPLAY_TEST_ENABLE
	//EPD_Driver_Display_WithOut_Flash_Debug();
	EPD_Driver_Display_From_Flash_Debug();
#endif
    //开启自动刷屏功�?
#if !defined(ENABLE_WIFI_UART1_PASSTHROUGH) && !defined(ENABLE_UART1_OFFICIAL_EXAMPLE_TEST)
	Start_LastRefresh();
#endif
#ifndef DISABLE_USER_LOCK_FEATURE
    uint32_t stored_user_id = getUserId();
    PRINT("--- User id = 0x%08X ---\n", stored_user_id);
#else
    PRINT("--- User lock disabled ---\n");
#endif


//    Print_I3("CMD PICDIS..AB Screen");
//    Xin_Tai(1);
//    AB_only();
//    global_BOE_DEVICE_STATUS.fIsNeedStandby = 1;
//    tmos_set_event(main_task_ID,EVENT_Low_Power);
	Main_Circulation();
}
// F:\project\AK80X\e_papper\CH583_Ble_E_paper\BLE\BackupUpgrade_OTA\obj\a.bin
// ��һ���������˻�����������жϣ��ڶ����ڵ͹���ǰʹ�ܿ��Ź����������ڵ͹��ĺ��һ������λ
