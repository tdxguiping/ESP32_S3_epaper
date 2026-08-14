#include "app_cfg.h"
#include "CONFIG.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"

#ifdef ENABLE_INK_SCREEN_UC8579_1360X480_COLOR_4

#define UC8579_WIDTH_H     0x02
#define UC8579_WIDTH_L     0xA8
#define UC8579_HEIGHT_H    0x01
#define UC8579_HEIGHT_L    0xE0
#define UC8579_BUSY_TIMEOUT_COUNT 35000

static void UC8579_SetCsHigh(void)
{
    SPI_CS_A_1;
    SPI_CS_B_1;
    SPI_CS_A_SLAVE_1;
    SPI_CS_B_SLAVE_1;
}

static void UC8579_SelectCs(UINT8 cs_num)
{
    UC8579_SetCsHigh();

    /*
     * The non-6-color board routes logical screen A to the B-side pins and
     * logical screen B to the A-side pins. Keep this aligned with reset/busy
     * handling in display_api.c.
     */
    if ((global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_A) ||
        (global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_AB))
    {
        if ((cs_num == Is_HOST) || (cs_num == Is_ALL))
        {
            SPI_CS_B_0;
        }
        if ((cs_num == Is_SLAVE) || (cs_num == Is_ALL))
        {
            SPI_CS_B_SLAVE_0;
        }
    }

    if ((global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_B) ||
        (global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_AB))
    {
        if ((cs_num == Is_HOST) || (cs_num == Is_ALL))
        {
            SPI_CS_A_0;
        }
        if ((cs_num == Is_SLAVE) || (cs_num == Is_ALL))
        {
            SPI_CS_A_SLAVE_0;
        }
    }
}

static void UC8579_WriteCommand(UINT8 cmd, UINT8 cs_num)
{
    UC8579_SelectCs(cs_num);
    SPI_DC_A_0;
    SPI_DC_B_0;
    mDelayuS(5);
    SPI_Write(cmd);
    mDelayuS(5);
    UC8579_SetCsHigh();
}

static void UC8579_WriteData(UINT8 data, UINT8 cs_num)
{
    UC8579_SelectCs(cs_num);
    SPI_DC_A_1;
    SPI_DC_B_1;
    mDelayuS(5);
    SPI_Write(data);
    mDelayuS(5);
    UC8579_SetCsHigh();
}

static UINT8 UC8579_GetActiveCs(void)
{
    return (global_DEVICE_STATUS.fisHost == IS_HOST) ? Is_HOST : Is_SLAVE;
}

static void UC8579_WriteResolution(UINT8 cs_num)
{
    UC8579_WriteCommand(R61_TRES, cs_num);
    UC8579_WriteData(UC8579_WIDTH_H, cs_num);
    UC8579_WriteData(UC8579_WIDTH_L, cs_num);
    UC8579_WriteData(UC8579_HEIGHT_H, cs_num);
    UC8579_WriteData(UC8579_HEIGHT_L, cs_num);

    UC8579_WriteCommand(R65_GSST, cs_num);
    UC8579_WriteData(0x00, cs_num);
    UC8579_WriteData(0x00, cs_num);
    UC8579_WriteData(0x00, cs_num);
    UC8579_WriteData(0x00, cs_num);
}

static void UC8579_BeginDataWrite(UINT8 cs_num)
{
    UC8579_SelectCs(cs_num);
    SPI_DC_A_0;
    SPI_DC_B_0;
    mDelayuS(5);
    SPI_Write(R10_DTM);
    mDelayuS(5);
    SPI_DC_A_1;
    SPI_DC_B_1;
}

UINT16 EPD_Check_Busy(void)
{
    unsigned int c;
    unsigned char busy;

    Print_I3("-----UC8579 Busy");
    c = 0;
    do
    {
        WWDG_SetCounter(0);
        busy = is_Busy();
        if (busy == 1)
        {
            break;
        }
        delay_xms(2);
        c++;
    } while (c < UC8579_BUSY_TIMEOUT_COUNT);

    if (c >= UC8579_BUSY_TIMEOUT_COUNT)
    {
        printf("er=%d\r\n", c);
        return Is_Er;
    }

    printf("OK=%d\r\n", c);
    return Is_OK;
}

void Init_EPD_Driver(void)
{
    Print_I3("UC8579 init");

    EPD_W21_Reset();
    delay_ms(100);
    EPD_Check_Busy();

    UC8579_WriteCommand(R00_PSR, Is_ALL);
    UC8579_WriteData(0x2F, Is_ALL);
    UC8579_WriteData(0x29, Is_ALL);

    UC8579_WriteCommand(R01_PWR, Is_ALL);
    UC8579_WriteData(0x07, Is_ALL);
    UC8579_WriteData(0x00, Is_ALL);

    UC8579_WriteCommand(R03_POFS, Is_ALL);
    UC8579_WriteData(0x00, Is_ALL);
    UC8579_WriteData(0x54, Is_ALL);
    UC8579_WriteData(0x44, Is_ALL);

    UC8579_WriteCommand(R06_BTST, Is_ALL);
    UC8579_WriteData(0x17, Is_ALL);
    UC8579_WriteData(0x24, Is_ALL);
    UC8579_WriteData(0x2C, Is_ALL);
    UC8579_WriteData(0x58, Is_ALL);

    UC8579_WriteCommand(R30_PLL, Is_ALL);
    UC8579_WriteData(0x08, Is_ALL);

    UC8579_WriteCommand(R50_CDI, Is_ALL);
    UC8579_WriteData(0x37, Is_ALL);

    UC8579_WriteCommand(R60_TCON, Is_ALL);
    UC8579_WriteData(0x02, Is_ALL);
    UC8579_WriteData(0x02, Is_ALL);

    UC8579_WriteCommand(RE3_PWS, Is_ALL);
    UC8579_WriteData(0x22, Is_ALL);

    UC8579_WriteCommand(0xE9, Is_ALL);
    UC8579_WriteData(0x01, Is_ALL);

    UC8579_WriteCommand(RE0_CCSET, Is_ALL);
    UC8579_WriteData(0x01, Is_ALL);

    UC8579_WriteResolution(Is_HOST);
    UC8579_WriteResolution(Is_SLAVE);
}

void Display_EPD_Driver(void)
{
    Print_I3("UC8579 refresh");

    UC8579_WriteCommand(R04_PON, Is_ALL);
    EPD_Check_Busy();
    delay_ms(200);

    UC8579_WriteCommand(R12_DRF, Is_ALL);
    UC8579_WriteData(0x00, Is_ALL);

    UC8579_SetCsHigh();
}

void Init_display_Bw(void)
{
}

void Init_display_Red(void)
{
    UC8579_BeginDataWrite(UC8579_GetActiveCs());
}

UINT32 EPD_Display_Time(void)
{
    return 65;
}

UINT32 EPD_GetDisplayMaxBuf(void)
{
    return SCREEN_1360X480_COLOR_4_MAX;
}

UINT8 EPD_GetScreenType(void)
{
#ifdef EPD_SCREEN_TYPE_A
		return '|';
#else
		return '~';
#endif
}

#endif
