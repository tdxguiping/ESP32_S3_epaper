#include "app_cfg.h"
#include "CONFIG.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "Display_EPD_W21_spi.h"

#ifdef ENABLE_INK_SCREEN_M009FT_1024X600_COLOR_6
#define R00_PSR         0x00
#define R01_PWR         0x01
#define R02_POF         0x02
#define R03_POFS        0x03
#define R04_PON         0x04
#define R05_BTST1       0x05
#define R06_BTST2       0x06
#define R07_DSLP        0x07
#define R08_BTST3       0x08
#define R10_DTM         0x10
#define R13_IPC         0x13
#define R30_PLL         0x30
#define R41_TSE         0x41
#define R50_CDI         0x50
#define R60_TCON        0x60
#define R61_TRES        0x61
#define R84_TVDCS       0x84
#define R86_AGID        0x86
#define RE3_PWS         0xE3
#define RE5_SPIM        0xE5

static void M009FT_SetCsHigh(void)
{
    SPI_CS_A_1;
    SPI_CS_B_1;
    SPI_CS_A_SLAVE_1;
    SPI_CS_B_SLAVE_1;
}

static void M009FT_SelectCs(UINT8 cs_num)
{
    M009FT_SetCsHigh();

    if ((global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_A) || (global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_AB))
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

    if ((global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_B) || (global_DEVICE_STATUS.fScreenType == SCREEN_TYPE_IMG_AB))
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
}

static void M009FT_WriteCommand(UINT8 cmd, UINT8 cs_num)
{
    M009FT_SelectCs(cs_num);
    SPI_DC_A_0;
    SPI_DC_B_0;
    mDelayuS(5);
    SPI_Write(cmd);
    mDelayuS(5);
    M009FT_SetCsHigh();
}

static void M009FT_WriteData(UINT8 data, UINT8 cs_num)
{
    M009FT_SelectCs(cs_num);
    SPI_DC_A_1;
    SPI_DC_B_1;
    mDelayuS(5);
    SPI_Write(data);
    mDelayuS(5);
    M009FT_SetCsHigh();
}

static UINT8 M009FT_GetActiveCs(void)
{
    return (global_DEVICE_STATUS.fisHost == IS_HOST) ? Is_HOST : Is_SLAVE;
}

static void M009FT_BeginDataWrite(UINT8 cs_num)
{
    M009FT_SelectCs(cs_num);
    SPI_DC_A_0;
    SPI_DC_B_0;
    mDelayuS(5);
    SPI_Write(R10_DTM);
    mDelayuS(5);
    SPI_DC_A_1;
    SPI_DC_B_1;
}


static UINT8 M009FT_BothBusyReleased(void)
{
    UINT32 key = GPIOA_ReadPort();
    UINT32 ready_mask = epaper_BUSY | GPIO_Pin_9;

    return ((key & ready_mask) == ready_mask) ? Is_Yes : Is_No;
}


UINT16 EPD_Check_Busy(void)
{
    unsigned int c;

    Print_I3("---M009FT Busy--");
    c = 0;
    do
    {
        if (M009FT_BothBusyReleased() == Is_Yes)
        {
            break;
        }
        delay_xms(2);
        c++;
    } while (c < 60);

    if (c >= 60)
    {
        printf("er=%d\r\n", c);
        return Is_Er;
    }

    printf("OK=%d\r\n", c);
    return Is_OK;
}

void Init_EPD_Driver(void)
{
    int i;


    EPD_W21_Reset();


    M009FT_WriteCommand(0xAA, Is_ALL);
    M009FT_WriteData(0x49, Is_ALL);
    M009FT_WriteData(0x55, Is_ALL);
    M009FT_WriteData(0x20, Is_ALL);
    M009FT_WriteData(0x08, Is_ALL);
    M009FT_WriteData(0x09, Is_ALL);
    M009FT_WriteData(0x18, Is_ALL);

    M009FT_WriteCommand(R00_PSR, Is_ALL);
    M009FT_WriteData(0x5F, Is_ALL);
    M009FT_WriteData(0x69, Is_ALL);
    EPD_Check_Busy();

    M009FT_WriteCommand(0xE0, Is_ALL);
    M009FT_WriteData(0x01, Is_ALL);
    EPD_Check_Busy();

    M009FT_WriteCommand(R01_PWR, Is_ALL);
    M009FT_WriteData(0x3F, Is_ALL);

    M009FT_WriteCommand(R03_POFS, Is_ALL);
    M009FT_WriteData(0x00, Is_ALL);
    M009FT_WriteData(0x54, Is_ALL);
    M009FT_WriteData(0x00, Is_ALL);
    M009FT_WriteData(0x44, Is_ALL);

    M009FT_WriteCommand(R05_BTST1, Is_ALL);
    M009FT_WriteData(0x40, Is_ALL);
    M009FT_WriteData(0x1F, Is_ALL);
    M009FT_WriteData(0x1F, Is_ALL);
    M009FT_WriteData(0x2C, Is_ALL);

    M009FT_WriteCommand(R06_BTST2, Is_ALL);
    M009FT_WriteData(0x6F, Is_ALL);
    M009FT_WriteData(0x1F, Is_ALL);
    M009FT_WriteData(0x16, Is_ALL);
    M009FT_WriteData(0x25, Is_ALL);

    M009FT_WriteCommand(R08_BTST3, Is_ALL);
    M009FT_WriteData(0x6F, Is_ALL);
    M009FT_WriteData(0x1F, Is_ALL);
    M009FT_WriteData(0x1F, Is_ALL);
    M009FT_WriteData(0x22, Is_ALL);

    M009FT_WriteCommand(R13_IPC, Is_ALL);
    M009FT_WriteData(0x00, Is_ALL);
    M009FT_WriteData(0x04, Is_ALL);

    M009FT_WriteCommand(R30_PLL, Is_ALL);
    M009FT_WriteData(0x08, Is_ALL);

    M009FT_WriteCommand(R41_TSE, Is_ALL);
    M009FT_WriteData(0x00, Is_ALL);

    M009FT_WriteCommand(R50_CDI, Is_ALL);
    M009FT_WriteData(0x3F, Is_ALL);

    M009FT_WriteCommand(R60_TCON, Is_ALL);
    M009FT_WriteData(0x02, Is_ALL);
    M009FT_WriteData(0x00, Is_ALL);

    M009FT_WriteCommand(R61_TRES, Is_ALL);
    M009FT_WriteData(0x02, Is_ALL);
    M009FT_WriteData(0x00, Is_ALL);
    M009FT_WriteData(0x02, Is_ALL);
    M009FT_WriteData(0x58, Is_ALL);
    EPD_Check_Busy();

    M009FT_WriteCommand(R84_TVDCS, Is_ALL);
    M009FT_WriteData(0x01, Is_ALL);

    M009FT_WriteCommand(R86_AGID, Is_ALL);
    M009FT_WriteData(0x00, Is_ALL);

    M009FT_WriteCommand(RE3_PWS, Is_ALL);
    M009FT_WriteData(0x2F, Is_ALL);

    M009FT_WriteCommand(RE5_SPIM, Is_ALL);
    M009FT_WriteData(0x00, Is_ALL);
}

void Display_EPD_Driver(void)
{
    Print_I3("Display_update_M009FT_6Color_1024_600 --");

    M009FT_WriteCommand(R04_PON, Is_ALL);
    delay_ms(100);
    EPD_Check_Busy();

    M009FT_WriteCommand(0x12, Is_ALL);
    M009FT_WriteData(0x00, Is_ALL);
    delay_ms(100);
    EPD_Check_Busy();

    M009FT_WriteCommand(R02_POF, Is_ALL);
    M009FT_WriteData(0x00, Is_ALL);
    delay_ms(100);
    EPD_Check_Busy();

    M009FT_SetCsHigh();
}

void Init_display_Bw(void)
{
}

void Init_display_Red(void)
{
    UINT8 active_cs = M009FT_GetActiveCs();

    if (active_cs == IS_HOST)
    {
        Print_I3("Init_display_Red Is_HOST M009FT");
    }
    else
    {
        Print_I3("Init_display_Red Is_SLAVE M009FT");
    }

    M009FT_BeginDataWrite(active_cs);
}

UINT32 EPD_Display_Time(void)
{
    return 40;
}

UINT32 EPD_GetDisplayMaxBuf(void)
{
    return SCREEN_1024X600_COLOR_6_MAX;
}

UINT8 EPD_GetScreenType(void)
{
#ifdef EPD_SCREEN_TYPE_A
	return 'B'; 
#else
	return 'A'; 
#endif
}

#endif
