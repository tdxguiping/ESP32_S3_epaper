#ifndef EPD_DRIVER_H
#define EPD_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CONFIG.h"

//_EL073TF1_400X600_COLOR_6
#define PSR         		0x00
#define PWR         		0x01
#define POF         		0x02
#define POFS        		0x03
#define PON         		0x04
#define BTST1       		0x05
#define BTST2       		0x06
#define DSLP        		0x07
#define BTST3       		0x08
#define DTM         		0x10
#define DRF         		0x12
#define PLL         		0x30
#define CDI        		 	0x50
#define TCON        		0x60
#define TRES        		0x61
#define REV         		0x70
#define VDCS        		0x82
#define T_VDCS      		0x84
#define PWS         		0xE3

//   JD79665_4Color_800_480 
#define R00_PSR         	0x00
#define R01_PWR         	0x01
#define R02_POF         	0x02
#define R03_POFS        	0x03
#define R04_PON         	0x04
#define R06_BTST        	0x06
#define R07_DSLP        	0x07
#define R10_DTM         	0x10
#define R11_DSP         	0x11
#define R12_DRF         	0x12
#define R17_AUTO        	0x17
#define R20_LUT0        	0x20
#define R30_PLL         	0x30
#define R40_TSC         	0x40
#define R41_TSE         	0x41
#define R42_TSW         	0x42
#define R43_TSR         	0x43
#define R44_GPIO        	0x44
#define R50_CDI         	0x50
#define R51_LPD         	0x51
#define R60_TCON        	0x60
#define R61_TRES        	0x61
#define R65_GSST        	0x65
#define R70_REV         	0x70
#define R71_FLG         	0x71
#define R80_AMV         	0x80
#define R81_VV          	0x81
#define R82_VDCS        	0x82
#define R83_PTLW        	0x83
#define R90_PGM         	0x90
#define R91_APG         	0x91
#define R92_RMTP        	0x92
#define RA2_PGM_CFG     	0xA2
#define RA3_PGM_STAT    	0xA3
#define RE0_CCSET       	0xE0
#define RE3_PWS         	0xE3
#define RE7_PST         	0xE7
#define RE8_CRC_STAT    	0xE8
#define LCD_XSIZE_4Color	800      /* Horizontal Active Period           */
#define LCD_YSIZE_4Color 	480        /* Vertical Active Period             */
#define IMAGE_SIZE_4Color 	(LCD_XSIZE_4Color*LCD_YSIZE_4Color/4) //   200 x 480 = 96000
#define LCD_XSIZE_400 		400      /* Horizontal Active Period           */
#define LCD_YSIZE_300  		300        /* Vertical Active Period             */
#define IMAGE_SIZE_4Color_400x300 (LCD_XSIZE_400*LCD_YSIZE_300/4)  // 100 x 300 =30000

#define EPD_BOARD_INFO_GROUP0_BASE 0x40
#define EPD_BOARD_INFO_GROUP1_BASE 0x50
#define EPD_BOARD_VENDOR_XT        0U
#define EPD_BOARD_VENDOR_DKE       1U
#define EPD_MAKE_BOARD_INFO(group, vendor_id) \
	((UINT8)((((group) & 0x01) ? EPD_BOARD_INFO_GROUP1_BASE : EPD_BOARD_INFO_GROUP0_BASE) | ((vendor_id) & 0x0F)))

// void define
extern void Init_EPD_Driver();
extern void Display_EPD_Driver();
extern void Init_display_Bw();
extern void Init_display_Red();
extern UINT32 EPD_Display_Time();
extern UINT32 EPD_GetDisplayMaxBuf();
extern UINT8 EPD_GetScreenType();
extern UINT8 EPD_GetBoardInfo(void);

#ifdef __cplusplus
}
#endif

#endif

