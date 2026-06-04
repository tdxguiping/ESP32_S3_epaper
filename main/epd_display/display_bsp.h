#pragma once

#include <stdint.h>
#include <cstddef>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_rom_sys.h>
#include "tdx_cfg.h"

#define  NT61522_BUFFER_SIZE 300 //256

enum ColorSelection {
    ColorBlack = 0,
    ColorWhite,
    ColorYellow,
    ColorRed,
    ColorBlue = 5,
    ColorGreen
};

enum Four_Color_s {
    ColBlack = 0x00,
    ColWhite = 0x55,
    ColYellow = 0xAA,
    ColRed = 0xFF
};


typedef enum {
    TARGET_MASTER = 0,
    TARGET_SLAVE  = 1,
    TARGET_BOTH   = 2
} EP_Target_t;

#define GPIO_LOW	0
#define GPIO_HIGH	1

#define EPD_800_480 1 // 3色
#define EPD_1024_600 2  // 6色    307200 bytes
#define EPD_1600_1200_79 3  // 6色  960000 bytes
#define EPD_1600_1200_133 4 // 6色    960000 bytes

#define EPD_1360_480_1085 5 // 4 色  10.85   40800 bytes
#define EPD_800_480_4s_75  6 // 4 色  7.5  96000 bytes

// tag_tdx_a   need
#ifndef EPD_type_
#define EPD_type_   EPD_1600_1200_79
#endif


// Keep the old display driver's board switch overridable from tdx_cfg.h.
#define Hardware_Version_  1


#define EPD_CS_PIN_2    46

#define EPD_DC_PIN      8
#define EPD_CS_PIN      9
#define EPD_RST_PIN     12
#define EPD_BUSY_PIN    13


#define EPD2_DC_PIN      21
#define EPD2_CS_PIN      14
#define EPD2_RST_PIN     47
#define EPD2_BUSY_PIN    48


#define EPD_SCK_PIN     10
#define EPD_MOSI_PIN    11




#define R00_PSR         0x00
#define R01_PWR         0x01
#define R02_POF         0x02
#define R03_POFS        0x03
#define R04_PON         0x04
#define R05_BTST1       0x05
#define R06_BTST2       0x06
#define R06_BTST        0x06
#define R07_DSLP        0x07
#define R08_BTST3       0x08
#define R09_PSAVE       0x09
#define R10_DTM         0x10
#define R11_DSP         0x11
#define R12_DRF         0x12
#define R13_IPC         0x13
#define R17_AUTO        0x17
#define R20_LUT0        0x20
#define R21_LUT1        0x21
#define R30_PLL         0x30
#define R40_TSC         0x40
#define R41_TSE         0x41
#define R42_TSW         0x42
#define R43_TSR         0x43
#define R44_GPIO        0x44
#define R50_CDI         0x50
#define R51_LPD         0x51
#define R60_TCON        0x60
#define R61_TRES        0x61
#define R70_REV         0x70
#define R71_FLG         0x71
#define R80_AMV         0x80
#define R81_VV          0x81
#define R82_VDCS        0x82
#define R83_PTLW        0x83
#define R84_TVDCS       0x84
#define R86_AGID        0x86
#define R90_PGM         0x90
#define R91_APG         0x91
#define R92_ROTP        0x92
#define R93_RSRAM       0x93
#define RA0_LOTP        0xA0
#define RA2_PGM_CFG     0xA2
#define RA3_PGM_STAT    0xA3
#define RE0_CCSET       0xE0
#define RE3_PWS         0xE3
#define RE4_LVSEL       0xE4
#define RE5_SPIM        0xE5
#define RE6_TSSET       0xE6

#define RE5_TSSET           0xE5
#define RA5_DCDC	        0xA5
#define R05_BTST_N          0x05
#define R06_BTST_P          0x06


#define LCD_XSIZE       1024
#define LCD_YSIZE       600
#define IMAGE_YSIZE     (LCD_XSIZE * LCD_YSIZE / 2)
#define IMAGE_SIZE      (LCD_XSIZE * LCD_YSIZE / 4)
#define WF_DATA_LENGTH  535
#define EPD_DISENABLE   0
#define EPD_ENABLE      1
#define EPD_TRUE        1
#define EPD_FALSE       0
#define NO_OTP_Status   0
#define OTP_Status      1
#define W_B_R_Y_Status  3
#define W_B_R_Status    2
#define W_B_Status      1
#define EPD_All_refresh 0
#define EPD_Part_refresh 1
#define start_Is_play   1
#define EPD_White       0xFF
#define EPD_Black       0x00
#define EPD_Red         0xFF
#define COLOR_BLACK     1
#define COLOR_PIC       5
#define COLOR_A         6
#define COLOR_B         7
#define WRITE_OTP_OK                    0
#define WRITE_OTP_FAIL_NO_VCOM          1
#define WRITE_OTP_FAIL_HAS_Written      2
#define WRITE_OTP_FAIL_WRONG_OPERATION  3
#define WRITE_OTP_FAIL_CRC_FAIL         4
#define WRITE_OTP_OK_WITH_DATA          5
#define OTP_DATA_ADD                    12


//  4 coloe 800x480 7.5
#define  ALLSCREEN_BYTES  48000
#define	 X_Addr_Start_H    0x03
#define	 X_Addr_Start_L     0x20
#define  Y_Addr_Start_H     0x01  
#define  Y_Addr_Start_L     0xE0

#define Source_BITS    1360/2
#define Gate_BITS   480 


class ePaperPort {
    spi_device_handle_t spi = nullptr;
    spi_host_device_t    spi_host_ = SPI3_HOST;
    uint32_t             i2c_data_pdMS_TICKS = 0;
    uint32_t             i2c_done_pdMS_TICKS = 0;
    const char          *TAG = "Display";
    int                  mosi_;
    int                  scl_;
    int                  dc_;
    int                  cs_;
    int                  cs_2_;
    int                  rst_;
    int                  busy_;
    uint16_t             width_;
    uint16_t             height_;
    uint8_t             *DispBuffer = nullptr;
    uint8_t             *RotationBuffer = nullptr;
    size_t               DispBufferCapacity_ = 0;
    size_t               RotationBufferCapacity_ = 0;
    int                  DisplayLen = 0;
    uint8_t              Rotation = 0;
    uint8_t              mirrx = 0;
    uint8_t              mirry = 0;

    void    Set_ResetIOLevel(uint8_t level);
    void    Set_CSIOLevel(uint8_t level);
    void    Set_DCIOLevel(uint8_t level);
    void    Set_CS2IOLevel(uint8_t level);
    uint8_t Get_BusyIOLevel();
    void    EPD_Reset(void);
    void    EPD_LoopBusy(void);
    void    SPI_Write(uint8_t data);
    void    EPD_SendCommand(uint8_t Reg);
    void    EPD_SendData(uint8_t Data);
    void    EPD_Sendbuffera(uint8_t *Data, int len);
    void    EPD_TurnOnDisplay(void);
    bool    EnsureDispBuffer();
    bool    EnsureRotationBuffer();
    void    ReleaseDispBuffer();
    void    ReleaseRotationBuffer();
    uint8_t EPD_GetPixel4(const uint8_t* buf, int width, int x, int y);
    void    EPD_SetPixel4(uint8_t* buf, int width, int x, int y, uint8_t px);
    void    EPD_Rotate180_Fast(const uint8_t* src, uint8_t* dst, int width, int height);
    void    EPD_Rotate90CCW_Fast(const uint8_t* src, uint8_t* dst, int width, int height);
    void    EPD_Rotate90CW_Fast(const uint8_t* src, uint8_t* dst, int width, int height);
    void    EPD_PixelRotate();

    void delay_us(uint16_t us);
    void delay_ms(uint16_t ms);
    void EPD_Select_None();
    void EPD_Select_Master();
    void EPD_Select_Slave();
    void EPD_Select_Both();
    void EPD_interface_init();
    void E_SDI_IN();
    void E_SDI_OUT();
    uint8_t EPD_SPI_Read();
    void EPD_WriteCMD_PreSelected(uint8_t command);
    void EPD_WriteDATA_PreSelected(uint8_t data);


    static constexpr size_t NT61522_SPI_MAX_BUFFER_SIZE = 32768;

    uint8_t nt61522_chip_id_[3] = {0};
  
        
    esp_err_t spiTransmitCommand(uint8_t commandBuf);
    esp_err_t spiTransmitData(const uint8_t *dataBuffer, size_t dataLength);
    esp_err_t spiReceiveData(uint8_t *dataBuffer, size_t dataLength);
    esp_err_t spiTransmit(uint8_t commandBuf, const uint8_t *dataBuffer, size_t dataLength);
    void setGpioLevel(int pinNumber, uint8_t voltageLevel);
    uint8_t getGpioLevel(int pinNumber);        
    void setPinCs(EP_Target_t target, uint8_t setLevel);

    // 4 color 800x480 7.5
    void Epaper_Initial();


    static const uint8_t* NT61522_PSR_V();
    static const uint8_t* NT61522_PWR_V();
    static const uint8_t* NT61522_POF_V();
    static const uint8_t* NT61522_DRF_V();
    static const uint8_t* NT61522_CDI_V();
    static const uint8_t* NT61522_TRES_V();
    static const uint8_t* NT61522_AMV_V();
    static const uint8_t* NT61522_CCSET_V_CUR();
    static const uint8_t* NT61522_CCSET_V_LOCK();
    static const uint8_t* NT61522_PWS_V();
    static const uint8_t* NT61522_DCDC_V();
    static const uint8_t* NT61522_BTST_P_V();
    static const uint8_t* NT61522_BTST_N_V();
    static const uint8_t* NT61522_Sleep_V();

  public:
    void delayms(unsigned int delayTime);  
    void setPinCsAll(uint8_t setLevel);
    void NT61522_Sleep();
    void NT61522_ReadRevision();
    uint8_t NT61522_ReadTemperature();
    void NT61522_Init();
    void NT61522_Init_display();
    void NT61522_Display_net(const uint8_t *imageData, size_t imageSize);
    void NT61522_Display();
    void NT61522_DisplayImage(const uint8_t *imageData, size_t imageSize);
    ePaperPort(int mosi, int scl, int dc, int cs,int cs2, int rst, int busy,
               uint16_t width, uint16_t height, uint16_t scale_MaxWidth, uint16_t scale_MaxHeight,
               spi_host_device_t spihost = SPI3_HOST);
    ~ePaperPort();

    unsigned char Read_Temptr();
    void EPD_Init();
    void EPD_DispClear(uint8_t color);
    void EPD_Display();
    void EPD_SrcDisplayCopy(uint8_t *buffer,uint32_t len,uint32_t addlen);
    uint32_t EPD_GetBufferLength();
    void Set_Rotation(uint8_t rot);
    void Set_Mirror(uint8_t mirr_x,uint8_t mirr_y);
    uint8_t* EPD_GetIMGBuffer();
    void EPD_SetPixel(uint16_t x, uint16_t y, uint16_t color);

    void EPD_WriteCMD_ToMaster(uint8_t command);
    void EPD_WriteCMD_ToSlave(uint8_t command);
    void EPD_WriteDATA_ToMaster(uint8_t data);
    void EPD_WriteDATA_ToSlave(uint8_t data);
    void EPD_WriteMultiData_ToMaster(uint8_t *data, unsigned int len);
    void EPD_WriteMultiData_ToSlave(uint8_t *data, unsigned int len);
    void EPD_WriteCMD_ToBoth(uint8_t command);
    void EPD_WriteDATA_ToBoth(uint8_t data);
    void EPD_WriteMultiData_ToBoth(uint8_t *data, unsigned int len);
    void EPD_WriteCMD_Target(EP_Target_t target, uint8_t command);
    void EPD_WriteDATA_Target(EP_Target_t target, uint8_t data);
    void EPD_WriteMultiData_Target(EP_Target_t target, uint8_t *data, unsigned int len);
    void EPD_Read_reg_FromMaster(uint8_t reg, uint8_t *pbuf, unsigned int len);
    void EPD_Read_reg_FromSlave(uint8_t reg, uint8_t *pbuf, unsigned int len);
    uint8_t EPD_ReadByte_FromMaster(uint8_t reg);
    uint8_t EPD_ReadByte_FromSlave(uint8_t reg);
    void EPD_WriteCMD(uint8_t command);
    void EPD_WriteDATA(uint8_t data);
    void EPD_Read_reg(uint8_t reg, uint8_t *pbuf, unsigned int len);
    void EPD_sleep();
    void EPD_refresh();
    void EPD_refresh_17H();
    void EPD_initial();
    void EPD_Check_Busy();
    void Epaper_Update_and_Deepsleep();    
    void Epaper_Update();
    void Epaper_Init();
    void Set_EPD_which_one(uint8_t which_one);
};

void SetGlobalEPaperInstance(ePaperPort *instance);
ePaperPort *GetGlobalEPaperInstance();


const unsigned char PSR_V[2] = {
	0xDF, 0x69
};
const unsigned char PWR_V[6] = {
	0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38
};
const unsigned char POF_V[1] = {
	0x00
};
const unsigned char DRF_V[1] = {
	0x01
};
const unsigned char CDI_V[1] = {
	0x37
};

const unsigned char TRES_V[4] = {
	0x04, 0xB0, 0x03, 0x20
};
const unsigned char AMV_V[2] = {
	0x01, 0x00
};

//调用当前温度
const unsigned char CCSET_V_CUR[1] = {
	0x01
};
//锁定温度
const unsigned char CCSET_V_LOCK[1] = {
	0x03
};

const unsigned char PWS_V[1] = {
	0x22
};

const unsigned char DCDC_V[3] = {
	0x44, 0x54 ,0x00
};

const unsigned char BTST_P_V[2] = {
	0xE0, 0x20
};
const unsigned char BTST_N_V[2] = {
	0xE0, 0x20
};
const unsigned char Sleep_V[1] = {
	0xa5
};



#define PSR             0x00
#define PWR             0x01
#define POF             0x02
#define POFS            0x03
#define PON             0x04
#define BTST_N          0x05
#define BTST_P          0x06
#define DTM             0x10
#define DRF             0x12
#define PLL             0x30
#define TSC             0x40
#define CDI             0x50
#define TCON            0x60
#define TRES            0x61
#define PTLW            0x83
#define AN_TM           0x74
#define AGID            0x86
#define CMDA4           0xA4
#define DCDC            0xA5
#define BUCK_BOOST_VDDN 0xB0
#define TFT_VCOM_POWER  0xB1
#define EN_BUF          0xB6
#define BOOST_VDDP_EN   0xB7
#define CCSET           0xE0
#define PWS             0xE3
#define CMD66           0xF0
