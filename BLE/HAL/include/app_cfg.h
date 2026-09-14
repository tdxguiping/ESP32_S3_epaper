/*
 * Release / factory UART0 controls.
 *
 * Keep product selection (VER, panel and customer) in the existing build
 * scripts. These switches are deliberately here so a production build never
 * needs a second, protocol-specific script.
 */
#ifndef APP_RELEASE_BUILD
#define APP_RELEASE_BUILD            1
#endif

#ifndef APP_UART0_ENABLE
#define APP_UART0_ENABLE             1
#endif

#ifndef APP_FACTORY_UART0_ENABLE
#define APP_FACTORY_UART0_ENABLE     1
#endif

#ifndef UART0_BOOT_LOG_ENABLE
#define UART0_BOOT_LOG_ENABLE        1
#endif

#ifndef UART0_TRANSFER_LOG_ENABLE
#define UART0_TRANSFER_LOG_ENABLE    1
#endif

/*
 * Enable release low power. Fixture power on PB13 keeps deep low power from
 * reconfiguring UART0; use 0 only while diagnosing low-power behavior.
 */
#ifndef APP_LOW_POWER_ENABLE
#define APP_LOW_POWER_ENABLE          1
#endif

/*
 * Keep the production-test UART alive while the fixture powers the board.
 * The fixture-present signal is CHARGE_LED/PB13 and is read directly, so it
 * is independent of the slower ADC charge-status update.
 */
#ifndef APP_FACTORY_POWER_HOLD_ENABLE
#define APP_FACTORY_POWER_HOLD_ENABLE 1
#endif

#if APP_FACTORY_UART0_ENABLE && !APP_UART0_ENABLE
#error "APP_FACTORY_UART0_ENABLE requires APP_UART0_ENABLE"
#endif

#if APP_RELEASE_BUILD && !defined(RELEASE_BUILD)
#define RELEASE_BUILD
#endif

#if APP_UART0_ENABLE && !defined(RELEASE_UART)
#define RELEASE_UART
#endif

#if APP_FACTORY_UART0_ENABLE && !defined(ENABLE_FACTORY_UART0_SELFTEST)
#define ENABLE_FACTORY_UART0_SELFTEST
#endif

#include "CH58x_common.h"
#include "CONFIG.h"

#define   Is_Ready    (1)
#define   Is_Yes      (1)
#define   Is_OK       (1)
#define   Is_On       (1)
#define   Is_Charge   (1)
#define   Is_Starting (1)
#define   Is_BlackWite (1)
#define   Is_Display   (1)
#define   Is_A_Image    (1)
#define   Is_Busying    (1)

#define   Is_Zero    (0)
#define   Is_Er      (0)
#define   Is_No      (0)
#define   Is_Over    (0)
#define   Is_Busy    (0)
#define   Is_Off     (0)
#define   Is_Battery (0)
#define   No_Start   (0)
#define   Is_Red     (0)
#define   No_Display (0)
#define   Is_B_Image (0)

#define   Is_AB_Image (2)

#define   Is_Five    (5)
#define   Is_Nine    (9)

#define   Is_HOST    	(0)
#define   Is_SLAVE    	(1)
#define   Is_ALL    	(2)

#define   Is_OLD    	(0)
#define   Is_NEW    	(1)

//is refresh pic or not
#define   INK_SCREEN_REFRESH_PIC   							1	// refresh pic
#define   INK_SCREEN_REFRESH_OTHER   						0	// refresh other

//customer
#define   INK_SCREEN_CUSTOMER_TY   							2	// CUSTOMER:TDX
#define   INK_SCREEN_CUSTOMER_TDX   						1	// CUSTOMER:TDX
#define   INK_SCREEN_CUSTOMER_MX   							0	// CUSTOMER:MX

//chip
#define   INK_CHIP_CH582   									0	// CH582
#define   INK_CHIP_CH583   									1	// CH583
#define   INK_CHIP_CH585                                   2   // CH585

//screen chip
#define   INK_SCREEN_CHIP_UC   								0	// UC
#define   INK_SCREEN_CHIP_SSD   							1	// SSD
#define   INK_SCREEN_CHIP_JD   								2	// JD
#define   INK_SCREEN_CHIP_EL   								3	// EL

/*************************************************/
//   need customer modify
//
/*************************************************/
//software version
//#define   SOFTWARE_VERSION									34//33
//#define   SOFTWARE_BOE_S_VER								'3'//'1'//33
//#define   SOFTWARE_BOE_M_VER								'2'//'f'//33
#ifndef VER
#define VER                           						100 
#endif

#ifndef INK_SCREEN_CUSTOMER
#define INK_SCREEN_CUSTOMER                           		INK_SCREEN_CUSTOMER_TDX
#endif

#ifndef INK_SCREEN_CHIP
#define INK_SCREEN_CHIP                           			INK_SCREEN_CHIP_UC
#endif

#ifndef INK_DEVICE_CHIP
#define INK_DEVICE_CHIP                                   INK_CHIP_CH585
#endif

//#define   ENABLE_SOFTWARE_TO_BOE							// boe or tdx
//#define   ENABLE_SOFTWARE_TO_TDX	
//#define   ENABLE_SOFTWARE_TO_XT	

//enable ink screen
//#define   ENABLE_INK_SCREEN_TDX_JDF   					1	// jindongfang
//#define   ENABLE_INK_SCREEN_XZ075BG_800X480_COLOR_3 		1	//3 color 800x480   'T', '%',
//#define   ENABLE_INK_SCREEN_JD79686BB_800X480_COLOR_3 	1	//3 color 800x480   'T', '%',
//#define   ENABLE_INK_SCREEN_UC8279_800X480_COLOR_3   		1	//3 color 800x480   'T', '%',
//#define   ENABLE_INK_SCREEN_SSD1863_400X300_COLOR_3   		1	//3 color 400x300 'T', '&',
//#define   ENABLE_INK_SCREEN_JD79665_800X480_COLOR_4   		1	//4 color 800x480 'T', '=',
//#define   ENABLE_INK_SCREEN_JD79665CA_800X480_COLOR_4   	1	//DKE 4 color 800x480 'T', '=',
//#define   ENABLE_INK_SCREEN_JD79668A_400X300_COLOR_4 		1	//4 color 400x300 'T', '$',
//#define   ENABLE_INK_SCREEN_UC8276_400X300_COLOR_3  		1
//#define   ENABLE_INK_SCREEN_UC8251D_250X122_COLOR_3   	1
//#define   ENABLE_INK_SCREEN_SSD1677_960X640_COLOR_3 		1	//3 color 960x640 'T', '*',
//#define   ENABLE_INK_SCREEN_SSD1683_272X792_COLOR_3 		1	//3 color 272x792 'T', '#',
//#define   ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2		1	//5.79 inch 2 color 272x792 SSD1683A 'T', 'C',
//#define   ENABLE_INK_SCREEN_UC8179_800X480_COLOR_2 			1	//2 color 800x480 'T', '?',
//#define   ENABLE_INK_SCREEN_UC8179_800X480_COLOR_3        
//#define   ENABLE_INK_SCREEN_SPD1657_800X480_COLOR_6 		1	//6 color 800X480 'T', '@',
//#define   ENABLE_INK_SCREEN_EL040EF1_400X300_COLOR_6 		1	//6 color 400x300 'T', '+',
//#define   ENABLE_INK_SCREEN_SSD2683_400X300_COLOR_4 		1	//4 color 400x300 'T', '$',  //XT
//#define   ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3		1	//3 color 1360X480 'T', '/',
//#define   ENABLE_INK_SCREEN_JD79686BB_1360X480_COLOR_3	//BOE
//#define   ENABLE_INK_SCREEN_JD79665_960x640_COLOR_4    1 //4 color 960x640 'T', '!',
//#define   ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4	//YSGD 4 color 1360X480
//#define   ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4	//XT 4 color 1280X600
//#define   ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4		//DKE 10.85 inch 4 color 1360X480 UC8579
//#define   ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3	//YSGD 3 color 1360X480
//#define   ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4	//DKE 4 color 272X792
//#define   ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6 		1	//6 color 1024X600 M009FT


//#define HARDWAR_DRY_CELL 

/*************************************************/
// 
//
/*************************************************/
#if defined(ENABLE_INK_SCREEN_UC8179_800X480_COLOR_2) || defined(ENABLE_INK_SCREEN_SSD1683A_272X792_COLOR_2)
#define ENABLE_SCREEN_COLOR_2
#endif

#if (defined(ENABLE_INK_SCREEN_UC8279_800X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79686BB_800X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_SSD1863_400X300_COLOR_3)) || (defined(ENABLE_INK_SCREEN_UC8276_400X300_COLOR_3))  || \
	(defined(ENABLE_INK_SCREEN_SSD1683_272X792_COLOR_3)) || (defined(ENABLE_INK_SCREEN_SSD1677_960X640_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79686BB_1360X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_UC8179_800X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3))
#define ENABLE_SCREEN_COLOR_3
#endif

#if (defined(ENABLE_INK_SCREEN_JD79665_800X480_COLOR_4)) || (defined(ENABLE_INK_SCREEN_JD79668A_400X300_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_JD79665CA_800X480_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_SSD2683_400X300_COLOR_4)) || (defined(ENABLE_INK_SCREEN_JD79665_960x640_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4)) || (defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_SSD2683ZA_272X792_COLOR_4)) || (defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4))
#define ENABLE_SCREEN_COLOR_4
#endif

#if (defined(ENABLE_INK_SCREEN_SPD1657_800X480_COLOR_6)) || (defined(ENABLE_INK_SCREEN_EL040EF1_400X300_COLOR_6)) || \
	(defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6))
#define ENABLE_SCREEN_COLOR_6
#endif

//MAC aes encrypt and decrypt
#define ENABLE_BOARD_ENCRYPT

// ий?????????????и▓?????????????????????
//#define EPD_DISPLAY_TEST_ENABLE

// ????????????????????????????????????EPD_3C_BWSolidImg_CycleTest?????
//#define EPD_BWSOLID_IMG_CYCLE_TEST_ENABLE

/***************************************/
// test demo
/***************************************/
//#define ENABLE_TEST_CLEAR_SCREEN
/***************************************/
//
/***************************************/
#define   Low_power_mode_on 		(1)
#if !APP_RELEASE_BUILD
#define   Debug_mode_on     		(1)
#endif

extern UINT8   File_Qutity;

extern  tmosTaskID main_task_ID;
extern  uint8_t Mac[6];
extern 	uint8_t Mac_ASCII[12];

// busy  -- PA14
// Res   -- PA4
// D/C   -- PA5
// CS    -- PA12
// SCK   -- PA13
// SDI   -- PA15
// Gan   -- 
// 3.3V  -- 
// All In Port-A
#ifdef ENABLE_SCREEN_COLOR_6
#define epaper_BUSY  						GPIO_Pin_4//PA
#define epaper_NEW_RES   					GPIO_Pin_3//PA
#define epaper_DC    						GPIO_Pin_5//PA
#define epaper_CS    						GPIO_Pin_12//PA
#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
#define epaper_CS_SLAVE    					GPIO_Pin_1 //PA
#endif

#define epaper2_NEW_RES   					GPIO_Pin_11 	//PA
#define epaper2_OLD_RES   					GPIO_Pin_12     //PB  11 -> 13  9
//#define epaper2_BUSY  					GPIO_Pin_9   // PB//  12->10   7(demo) GPIO_Pin_13 can not use , it will error, 
#define epaper2_DC    						GPIO_Pin_15   //  PB ?
#define epaper2_CS    						GPIO_Pin_14   // PB
#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
#define epaper2_CS_SLAVE    				GPIO_Pin_9    // PB
#endif
#else
#define epaper_BUSY  						GPIO_Pin_14  //PA   5ma
#define epaper_RES   						GPIO_Pin_4	 //PA	5ma
#define epaper_DC    						GPIO_Pin_5	 //PA   5ma
#define epaper_CS    						GPIO_Pin_12  //PA   5ma
#define epaper_CS_SLAVE    					GPIO_Pin_11  //PA   5ma

#define epaper2_RES   						GPIO_Pin_9   // :PA  11 -> 13  9  5ma
//#define epaper2_BUSY  						GPIO_Pin_12   // PB//  12->10   7(demo) GPIO_Pin_13 can not use , it will error, 
#define epaper2_DC    						GPIO_Pin_15   //  PB ?  5ma
#define epaper2_CS    						GPIO_Pin_14   // PB     5ma
#define epaper2_CS_SLAVE    				GPIO_Pin_10   // PA     5ma
#endif

// ?????-----PB13---PB17(???????????)
// ???ADC-------  PA
// ????????----PB5----PB6
#define  LED5_Power 						GPIO_Pin_5       //:PB  0=on , 1=off
#define  LED6_Power      					GPIO_Pin_6       //:PB  0=on , 1=off

#define RED_Power_on   						GPIOB_ModeCfg(LED5_Power, GPIO_ModeOut_PP_5mA);    \
                  								R32_PB_CLR |= LED5_Power                        
#define RED_Power_off  						GPIOB_ModeCfg(LED5_Power, GPIO_ModeOut_PP_5mA);    \
                 								R32_PB_OUT |= LED5_Power
#define GREEN_Power_on   					GPIOB_ModeCfg(LED6_Power, GPIO_ModeOut_PP_5mA);    \
                  								R32_PB_CLR |= LED6_Power                      
#define GREEN_Power_off  					GPIOB_ModeCfg(LED6_Power, GPIO_ModeOut_PP_5mA);    \
                 								R32_PB_OUT |= LED6_Power

#define  LCD_Power_A       					GPIO_Pin_6       //:PA6  0=on , 1=off
#ifndef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
#define  LCD_Power_B       					GPIO_Pin_9       //:PB9  0=on , 1=off
#endif

#define  CHARGE_LED  						GPIO_Pin_13   // PB  For charging

//======================================================================
#define SPI_CS_A_0							R32_PA_CLR |= epaper_CS // fEPD_W21_CS_0()
#define SPI_CS_A_1							R32_PA_OUT |= epaper_CS //  fEPD_W21_CS_1()
#if (defined(ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79686BB_1360X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4)) || (defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6)) || \
	(defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4))
#define SPI_CS_A_SLAVE_0					R32_PA_CLR |= epaper_CS_SLAVE// fEPD_W21_CS_0()
#define SPI_CS_A_SLAVE_1					R32_PA_OUT |= epaper_CS_SLAVE //  fEPD_W21_CS_1()
#endif 

#define SPI_DC_A_0							R32_PA_CLR |= epaper_DC   // fEPD_W21_DC_0()
#define SPI_DC_A_1							R32_PA_OUT |= epaper_DC   // fEPD_W21_DC_1()
//======================================================================
#define SPI_CS_B_0							R32_PB_CLR |= epaper2_CS // fEPD_W21_CS_0()
#define SPI_CS_B_1							R32_PB_OUT |= epaper2_CS //  fEPD_W21_CS_1()
#if defined(ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6)
#define SPI_CS_B_SLAVE_0					R32_PB_CLR |= epaper2_CS_SLAVE // fEPD_W21_CS_0()
#define SPI_CS_B_SLAVE_1					R32_PB_OUT |= epaper2_CS_SLAVE //  fEPD_W21_CS_1()
#elif (defined(ENABLE_INK_SCREEN_JD79686AB_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_JD79686BB_1360X480_COLOR_3)) || \
	(defined(ENABLE_INK_SCREEN_JD79665AA_1360X480_COLOR_4)) || (defined(ENABLE_INK_SCREEN_JD79665AA_1280X600_COLOR_4)) || \
	(defined(ENABLE_INK_SCREEN_JD79686AC_1360X480_COLOR_3)) || (defined(ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4))
#define SPI_CS_B_SLAVE_0					R32_PA_CLR |= epaper2_CS_SLAVE // fEPD_W21_CS_0()
#define SPI_CS_B_SLAVE_1					R32_PA_OUT |= epaper2_CS_SLAVE //  fEPD_W21_CS_1()
#endif               						

#define SPI_DC_B_0							R32_PB_CLR |= epaper2_DC   // fEPD_W21_DC_0()
#define SPI_DC_B_1							R32_PB_OUT |= epaper2_DC   // fEPD_W21_DC_1()

#ifdef ENABLE_SCREEN_COLOR_6
#define SPI_NEW_RST_A_0						R32_PA_CLR |= epaper_NEW_RES // fEPD_W21_RST_0() 
#define SPI_NEW_RST_A_1						R32_PA_OUT |= epaper_NEW_RES // fEPD_W21_RST_1()
#define SPI_NEW_RST_B_0  					R32_PA_CLR |= epaper2_NEW_RES // fEPD_W21_RST_0() 
#define SPI_NEW_RST_B_1  					R32_PA_OUT |= epaper2_NEW_RES // fEPD_W21_RST_1()
#define SPI_OLD_RST_B_0  					R32_PB_CLR |= epaper2_OLD_RES // fEPD_W21_RST_0() 
#define SPI_OLD_RST_B_1  					R32_PB_OUT |= epaper2_OLD_RES // fEPD_W21_RST_1()
#else
#define SPI_RST_A_0							R32_PA_CLR |= epaper_RES // fEPD_W21_RST_0() 
#define SPI_RST_A_1							R32_PA_OUT |= epaper_RES // fEPD_W21_RST_1()
#define SPI_RST_B_0  						R32_PA_CLR |= epaper2_RES // fEPD_W21_RST_0() 
#define SPI_RST_B_1  						R32_PA_OUT |= epaper2_RES // fEPD_W21_RST_1()
#endif
//======================================================================

#define   EVENY_Is_Charge               	(0x0001)
#define   EVENT_Refresh_Timer               (0x0002)
#define   EVENT_Ble_Picture_TimeOut     	(0x0004)
#define   EVENT_Start_Peripheral_Mode     	(0x0008)

#define   EVENT_Low_Power               	(0x0010)
#define   EVENT_Boardcast_Op             	(0x0020)
#define   EVENT_IDLE                    	(0x0040)
#define   EVENT_Key                     	(0x0080)

#define   EVENT_Check_TimeOut        		(0x0100)
#define   EVENT_Key_IRQ                 	(0x0200)
#define   EVENT_GET_ADC_Value           	(0x0400)
#define   EVENT_Get_Battle_Charge        	(0x0800)

#define   EVENT_Test_Msg          			(0x1000)
#define   EVENT_Start_Central_Mode    		(0x2000)
#define   EVENT_Stop_Ads                	(0x4000)

#define   _Ble_Picture_TimeOut           	(61999)

// small inter 20ms
//#define DEFAULT_ADVERTISING_INTERVAL   		1400//1400//1200//(1.0*1600)//(1.0*1600)//260  //200ms
///#define DEFAULT_ADVERTISING_INTERVAL_Max    1600//1400//1200//(1.0*1600)//(1.0*1600)//1600     //1.5s

#define DEFAULT_ADVERTISING_INTERVAL   		800
#define DEFAULT_ADVERTISING_INTERVAL_Max    800


#ifdef Debug_mode_on
#define Print_I(pszFormat...)                                                          \
    printf("\033[0m[%s\033[0;31m:%d] \033[1;34m",__FUNCTION__, __LINE__); \
    printf(pszFormat);                                                                  \
    printf("\033[0m");
#if 0
#define Print_I3(pszFormat...)													 \
	printf("\033[0m[%s\033[0;31m:%d] \033[0m",__FUNCTION__, __LINE__); \
	printf(pszFormat);																\
	printf("\r\n\033[0m");
#else
#define Print_I3(pszFormat...)                                                     \
	printf("[LOG] "); \
	printf(pszFormat);																\
	printf("\r\n");
#endif

#define Print_Ix(pszFormat...)                                                     \
   	printf("\033[0m[%s\033[0;31m:%d] \033[0m",__FUNCTION__, __LINE__); \
   	printf(pszFormat);                                                              \
   	printf("\r\033[0m");

#define Print_I2(pszFormat...)                                                     \
   	printf("\r\n \033[1;32m[%s\r\n:%s\033[0;31m:%d]\033[1;35m", __FILE__, __FUNCTION__, __LINE__); \
   	printf(pszFormat);                                                              \
   	printf("\033[0m");

#define Print_L2(pszFormat...)                                                        \
   	printf("\033[0m[%s\033[0;31m:%d]\033[1;34m", __FUNCTION__, __LINE__); \
   	printf(pszFormat);                                                                 \
   	printf("\033[0m");

#define Print_L(pszFormat...)                                                        \
   	printf("\033[0m[%s\033[0;31m:%d]\033[1;34m",__FUNCTION__, __LINE__); \
   	printf(pszFormat);                                                                 \
   	printf("\033[0m");    

#define PRINT_xx(pszFormat...)                                                     \
                          	printf("\033[0m[%s: %s\033[0;31m:%d] \033[0m",__FILE__,__FUNCTION__, __LINE__); \
                          	printf(pszFormat);                                                              \
                          	printf("\r\033[0m");
#define DBPRINT(X...)     	printf(X);
#else
#define DBPRINT(X...)
#define Print_I(pszFormat...)
#define Print_I3(pszFormat...)
#define Print_Ix(pszFormat...)
#define PRINT_xx(pszFormat...)
#endif



/* Direct printf calls in legacy application code are diagnostics. */
#if APP_RELEASE_BUILD
#define printf(...) (0)
#endif