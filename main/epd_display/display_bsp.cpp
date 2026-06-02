#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_rom_sys.h>
#include <freertos/event_groups.h>
#include "display_bsp.h"


uint8_t  EPD_which_one_=1;    // if 1 EPD1 , if 2 EPD2

//extern uint8_t  WHT20_Temp;
extern uint16_t sleep_time; 
extern EventGroupHandle_t sleep_group;
bool isEPDInit = false;

uint8_t  Display_had_flag = 0xAA;

#define BLOCK_SIZE 256
static ePaperPort *s_global_epaper_instance = nullptr;
void SetGlobalEPaperInstance(ePaperPort *instance) {
    s_global_epaper_instance = instance;
}
ePaperPort *GetGlobalEPaperInstance() {
    return s_global_epaper_instance;
}


void ePaperPort::Set_EPD_which_one(uint8_t which_one)
{
    EPD_which_one_ = which_one;
    if((EPD_which_one_ !=1) &&  (EPD_which_one_ !=2))
    {
        EPD_which_one_ = 1;
    }
}


ePaperPort::ePaperPort(int mosi, int scl, int dc, int cs,int cs2, int rst, int busy,
                       uint16_t width, uint16_t height, uint16_t scale_MaxWidth, uint16_t scale_MaxHeight,
                       spi_host_device_t spihost)
    : spi_host_(spihost),
      mosi_(mosi),
      scl_(scl),
      dc_(dc),
      cs_(cs),
      cs_2_(cs2),
      rst_(rst),
      busy_(busy),
      width_(width),
      height_(height) {
    (void)scale_MaxWidth;
    (void)scale_MaxHeight;
    esp_err_t ret;
    spi_bus_config_t buscfg = {};
    int transfer = width_ * height_;
    
    LOG_Blue("%s>%d", __func__, __LINE__);
    DisplayLen = transfer / 2;
    LOG_Blue("width=%d,height=%d,DisplayLen=%d",width,height,DisplayLen);

    buscfg.miso_io_num = -1;
    buscfg.mosi_io_num = mosi_;
    buscfg.sclk_io_num = scl_;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = NT61522_SPI_MAX_BUFFER_SIZE;

    spi_device_interface_config_t devcfg = {};
    memset(&devcfg, 0, sizeof(devcfg));
    devcfg.spics_io_num = -1;
    //devcfg.clock_speed_hz = 40 * 1000 * 1000;
    devcfg.clock_speed_hz = 5 * 1000 * 1000;

    devcfg.mode = 0;
    devcfg.queue_size = 7;
    devcfg.cs_ena_posttrans = 3;
    devcfg.flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_3WIRE | SPI_DEVICE_NO_DUMMY;
    // buscfg.miso_io_num                   = -1;
    // buscfg.mosi_io_num                   = EPD_MOSI_PIN;
    // buscfg.sclk_io_num                   = EPD_SCK_PIN;
    // buscfg.quadwp_io_num                 = -1;
    // buscfg.quadhd_io_num                 = -1;
    // buscfg.max_transfer_sz               = EXAMPLE_LCD_WIDTH * EXAMPLE_LCD_HEIGHT;
    // spi_device_interface_config_t devcfg = {};
    // devcfg.spics_io_num                  = -1;
    // devcfg.clock_speed_hz                = 10 * 1000 * 1000; //Clock out at 10 MHz
    // devcfg.mode                          = 0;                //SPI mode 0
    // devcfg.queue_size                    = 7;                //We want to be able to queue 7 transactions at a time
    // //devcfg.flags                         = SPI_DEVICE_HALFDUPLEX;

    ret = spi_bus_initialize(spi_host_, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);
    ret = spi_bus_add_device(spi_host_, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);

    #if(Hardware_Version_  == 1)
    gpio_config_t gpio_conf = {};
    gpio_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_conf.mode = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask = (0x1ULL << rst_) | (0x1ULL << dc_) | (0x1ULL << cs_) | (0x1ULL << cs_2_);
    gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

    gpio_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_conf.mode = GPIO_MODE_INPUT;
    gpio_conf.pin_bit_mask = (0x1ULL << busy_);
    gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));
    #else
    gpio_config_t gpio_conf = {};
    gpio_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_conf.mode = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask = (0x1ULL << rst_) | (0x1ULL << dc_) | (0x1ULL << cs_) | (0x1ULL << cs_2_)| (0x1ULL << EPD2_DC_PIN)| (0x1ULL << EPD2_CS_PIN)| (0x1ULL << EPD2_RST_PIN)| (0x1ULL << EPD2_BUSY_PIN);
    gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

    gpio_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_conf.mode = GPIO_MODE_INPUT;
    gpio_conf.pin_bit_mask = (0x1ULL << busy_)|(0x1ULL << EPD2_BUSY_PIN);
    gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));
    #endif
    
    EPD_interface_init();
    Set_ResetIOLevel(1);
    SetGlobalEPaperInstance(this);
}

bool ePaperPort::EnsureDispBuffer() {
    if (DispBuffer != nullptr && DispBufferCapacity_ >= (size_t)DisplayLen) {
        return true;
    }
    ReleaseDispBuffer();
    DispBuffer = static_cast<uint8_t *>(heap_caps_malloc(DisplayLen, MALLOC_CAP_SPIRAM));
    if (DispBuffer == nullptr) {
        ESP_LOGE(TAG, "DispBuffer alloc failed, len=%d", DisplayLen);
        return false;
    }
    DispBufferCapacity_ = (size_t)DisplayLen;
    ESP_LOGI(TAG, "DispBuffer alloc ok, ptr=%p, len=%d", DispBuffer, DisplayLen);
    return true;
}

bool ePaperPort::EnsureRotationBuffer() {
    if (RotationBuffer != nullptr && RotationBufferCapacity_ >= (size_t)DisplayLen) {
        return true;
    }
    ReleaseRotationBuffer();
    RotationBuffer = static_cast<uint8_t *>(heap_caps_malloc(DisplayLen, MALLOC_CAP_SPIRAM));
    if (RotationBuffer == nullptr) {
        ESP_LOGE(TAG, "RotationBuffer alloc failed, len=%d", DisplayLen);
        return false;
    }
    RotationBufferCapacity_ = (size_t)DisplayLen;
    ESP_LOGI(TAG, "RotationBuffer alloc ok, ptr=%p, len=%d", RotationBuffer, DisplayLen);
    return true;
}

void ePaperPort::ReleaseDispBuffer() {
    if (DispBuffer != nullptr) {
        ESP_LOGI(TAG, "Release DispBuffer ptr=%p, len=%u", DispBuffer, (unsigned int)DispBufferCapacity_);
        heap_caps_free(DispBuffer);
        DispBuffer = nullptr;
        DispBufferCapacity_ = 0;
    }
}

void ePaperPort::ReleaseRotationBuffer() {
    if (RotationBuffer != nullptr) {
        ESP_LOGI(TAG, "Release RotationBuffer ptr=%p, len=%u", RotationBuffer, (unsigned int)RotationBufferCapacity_);
        heap_caps_free(RotationBuffer);
        RotationBuffer = nullptr;
        RotationBufferCapacity_ = 0;
    }
}

ePaperPort::~ePaperPort() {
    if (s_global_epaper_instance == this) {
        s_global_epaper_instance = nullptr;
    }

    ReleaseDispBuffer();
    ReleaseRotationBuffer();
    if (spi) {
        spi_bus_remove_device(spi);
        spi = nullptr;
    }
    spi_bus_free(spi_host_);
}

void ePaperPort::Set_ResetIOLevel(uint8_t level) {
    if(EPD_which_one_ == 1){
    gpio_set_level((gpio_num_t)rst_, level ? 1 : 0);
    }
    else {
    gpio_set_level((gpio_num_t)EPD2_RST_PIN, level ? 1 : 0);
    }

    
}

void ePaperPort::Set_CSIOLevel(uint8_t level) {
    if(EPD_which_one_ == 1)
    {
        gpio_set_level((gpio_num_t)cs_, level ? 1 : 0);
    }
    else
    {
        gpio_set_level((gpio_num_t)EPD2_CS_PIN, level ? 1 : 0);
    }    
}

void ePaperPort::Set_CS2IOLevel(uint8_t level) {
    if(EPD_which_one_ == 1)
    {
        gpio_set_level((gpio_num_t)cs_2_, level ? 1 : 0);
    }   
    else
    {
        gpio_set_level((gpio_num_t)EPD2_CS_PIN, level ? 1 : 0);
    }        
}

void ePaperPort::Set_DCIOLevel(uint8_t level) {
    if(EPD_which_one_ == 1)
        {gpio_set_level((gpio_num_t)dc_, level ? 1 : 0);}
    else
        {gpio_set_level((gpio_num_t)EPD2_DC_PIN, level ? 1 : 0);}    
}

uint8_t ePaperPort::Get_BusyIOLevel() {
    if(EPD_which_one_ == 1)
      {return gpio_get_level((gpio_num_t)busy_);}
    else 
      {return gpio_get_level((gpio_num_t)EPD2_BUSY_PIN);}        
}

void ePaperPort::delay_us(uint16_t us) {
    esp_rom_delay_us(us);
}

void ePaperPort::delay_ms(uint16_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}



void ePaperPort::EPD_Reset(void) {
    
    LOG_Purple("%s>%d", __func__, __LINE__);
    Set_ResetIOLevel(0);
    vTaskDelay(pdMS_TO_TICKS(20)); //100
    Set_ResetIOLevel(1);
    vTaskDelay(pdMS_TO_TICKS(20));  //100      
}

void ePaperPort::EPD_LoopBusy(void) {
    uint32_t count;
    LOG_Purple("800x480 %s>%d",__func__,__LINE__);
    count=0;
    while (1) {
        if (Get_BusyIOLevel()) {
            printf("check busyOK\r\n");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(50)); // 50x20 = 1000ms
        count++;
        if (count > (8*20)) 
        {
            LOG_Purple("800x480 error %s>%d",__func__,__LINE__);
            return;
        }
    }
}

void ePaperPort::EPD_Check_Busy(void) { // If BUSYN=0 then waiting
    int32_t i = 0;
    int64_t start_us = esp_timer_get_time();
    int start_level = Get_BusyIOLevel();
    LOG_Purple("e-p@ %s>%d", __func__, __LINE__);
    ESP_LOGI(TAG, "EPD_Check_Busy start level=%d", start_level);
    while (1) {
        int level = Get_BusyIOLevel();
        if (level) {
            int elapsed_ms = (int)((esp_timer_get_time() - start_us) / 1000);
            LOG_WARN("read busy ok");
            ESP_LOGI(TAG, "EPD_Check_Busy ok level=%d loops=%ld elapsed_ms=%d",
                     level, (long)i, elapsed_ms);

            if(Display_had_flag == 0xA3)
            {
                Display_had_flag = 0xAA;  
            }
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(60)); // 50x20 = 1000ms    
        printf(".");
        i++;
        if (i > (30*20)) {
            int elapsed_ms = (int)((esp_timer_get_time() - start_us) / 1000);
            LOG_ERROR("read busy error %s %d %s", __func__, __LINE__, __FILE__);
            ESP_LOGE(TAG, "EPD_Check_Busy timeout level=%d loops=%ld elapsed_ms=%d",
                     Get_BusyIOLevel(), (long)i, elapsed_ms);
            if(Display_had_flag == 0xA3)
            {
                Display_had_flag = 0xAA;  
            }
            return;
        }
    }
}

void ePaperPort::SPI_Write(uint8_t data) {
    // 参考 comm.c 的 spiTransmitCommand/spiTransmitData 写法：每次事务先清零，再按 bit 长度发送。
    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));
    trans.length = 8;
    trans.tx_buffer = &data;
    trans.rx_buffer = nullptr;
    trans.rxlength = 0;

    esp_err_t ret = spi_device_transmit(spi, &trans);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI_Write failed data=0x%02X ret=%d", data, (int)ret);
    }
}

uint8_t ePaperPort::EPD_SPI_Read(void) {
    // 参考 comm.c 的 spiReceive 写法：同一个事务里配置 rxlength 和 rx_buffer。
    uint8_t data = 0;
    spi_transaction_t trans;
    memset(&trans, 0, sizeof(trans));
    trans.length = 8;
    trans.rxlength = 8;
    trans.rx_buffer = &data;
    trans.tx_buffer = nullptr;

    esp_err_t ret = spi_device_transmit(spi, &trans);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "EPD_SPI_Read failed ret=%d", (int)ret);
        return 0;
    }
    return data;
}

void ePaperPort::EPD_Select_None(void) {
    Set_CSIOLevel(1);
    Set_CS2IOLevel(1);
}

void ePaperPort::EPD_Select_Master(void) {
    Set_CSIOLevel(0);
    Set_CS2IOLevel(1);
}

void ePaperPort::EPD_Select_Slave(void) {
    Set_CSIOLevel(1);
    Set_CS2IOLevel(0);
}

void ePaperPort::EPD_Select_Both(void) {
    Set_CSIOLevel(0);
    Set_CS2IOLevel(0);
}

void ePaperPort::EPD_interface_init(void) {
    Set_ResetIOLevel(0);
    Set_DCIOLevel(0);
    Set_CSIOLevel(1);
    Set_CS2IOLevel(1);
    EPD_Select_None();
}

void ePaperPort::E_SDI_IN(void) {
    LOG_INFO("set sdi in %s>%d", __func__, __LINE__);
}

void ePaperPort::E_SDI_OUT(void) {
}

void ePaperPort::EPD_WriteCMD_PreSelected(uint8_t command) {
    Set_DCIOLevel(0);
    delay_us(1);
    SPI_Write(command);
}

void ePaperPort::EPD_WriteDATA_PreSelected(uint8_t data) {
    Set_DCIOLevel(1);
    delay_us(1);
    SPI_Write(data);
}

void ePaperPort::EPD_SendCommand(uint8_t Reg) {
    Set_DCIOLevel(0);
    Set_CSIOLevel(0);
    SPI_Write(Reg);
    Set_CSIOLevel(1);
}

void ePaperPort::EPD_SendData(uint8_t Data) {
    Set_DCIOLevel(1);
    Set_CSIOLevel(0);
    SPI_Write(Data);
    Set_CSIOLevel(1);
}

unsigned char Temptr[2] = {0};
unsigned char ePaperPort::Read_Temptr(void)
{
    NT61522_ReadRevision();

	setPinCsAll(GPIO_LOW);
	spiTransmit(RE0_CCSET, CCSET_V_CUR, sizeof(CCSET_V_CUR));
	setPinCsAll(GPIO_HIGH);
	delayms(10);
	
	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmitCommand(R40_TSC);
	delayms(10);
	EPD_Check_Busy();
	spiReceiveData(&Temptr[0], 2);
	setPinCs(TARGET_MASTER,GPIO_HIGH);
	EPD_Check_Busy();

	Temptr[0] = Temptr[0] > 50 ? 48 : Temptr[0];
    
    LOG_Purple("%s>%d temp=%d %d", __func__, __LINE__,Temptr[0],Temptr[1]);

    if(Temptr[0] <=1) Temptr[0]=0x2A;    
	return Temptr[0];
}

// (EPD_type_  ==  EPD_1600_1200_79 )
uint8_t ePaperPort::NT61522_ReadTemperature() { // (EPD_type_  ==  EPD_1600_1200_133 )      

    setPinCsAll(0);
    spiTransmit(RE0_CCSET, NT61522_CCSET_V_CUR(), 1);
    setPinCsAll(1);
    delayms(10);

    NT61522_ReadRevision();

    setPinCs(TARGET_MASTER, 0);
    spiTransmitCommand(R40_TSC);
    delayms(10);
    EPD_Check_Busy();
    spiReceiveData(&Temptr[0], 2);
    setPinCs(TARGET_MASTER, 1);
    EPD_Check_Busy();

    Temptr[0] = Temptr[0] > 50 ? 48 : Temptr[0];
    return Temptr[0];
}



void ePaperPort::EPD_Sendbuffera(uint8_t *Data, int len) {
    if (Data == nullptr || len <= 0) {
        return;
    }

    // 参考 spiTransmitLargeData：大数据按 NT61522_SPI_MAX_BUFFER_SIZE 分包发送，避免超过 SPI DMA 单次传输上限。
    Set_DCIOLevel(1);
    Set_CSIOLevel(0);

    uint8_t *ptr = Data;
    int remaining = len;
    while (remaining > 0) {
        int chunk = remaining > NT61522_SPI_MAX_BUFFER_SIZE ? NT61522_SPI_MAX_BUFFER_SIZE : remaining;
        spi_transaction_ext_t trans_ext;
        memset(&trans_ext, 0, sizeof(trans_ext));
        trans_ext.command_bits = 0;
        trans_ext.base.length = chunk * 8;
        trans_ext.base.tx_buffer = ptr;
        trans_ext.base.rx_buffer = nullptr;
        trans_ext.base.flags = SPI_TRANS_VARIABLE_CMD;

        esp_err_t ret = spi_device_transmit(spi, &trans_ext.base);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "EPD_Sendbuffera failed chunk=%d remaining=%d ret=%d", chunk, remaining, (int)ret);
            break;
        }
        ptr += chunk;
        remaining -= chunk;
    }

    Set_CSIOLevel(1);
}

void ePaperPort::EPD_WriteCMD_ToMaster(uint8_t command) {
    EPD_Select_Master();
    EPD_WriteCMD_PreSelected(command);
    EPD_Select_None();
}

void ePaperPort::EPD_WriteCMD_ToSlave(uint8_t command) {
    EPD_Select_Slave();
    EPD_WriteCMD_PreSelected(command);
    EPD_Select_None();
}

void ePaperPort::EPD_WriteDATA_ToMaster(uint8_t data) {
    EPD_Select_Master();
    EPD_WriteDATA_PreSelected(data);
    EPD_Select_None();
}

void ePaperPort::EPD_WriteDATA_ToSlave(uint8_t data) {
    EPD_Select_Slave();
    EPD_WriteDATA_PreSelected(data);
    EPD_Select_None();
}

void ePaperPort::EPD_WriteMultiData_ToMaster(uint8_t *data, unsigned int len) {
    if (len == 0 || data == nullptr) return;
    EPD_Select_Master();
    Set_DCIOLevel(1);
    delay_us(1);

    uint8_t *ptr = data;
    unsigned int remaining = len;
    while (remaining > 0) {
        unsigned int chunk = remaining > NT61522_SPI_MAX_BUFFER_SIZE ? NT61522_SPI_MAX_BUFFER_SIZE : remaining;
        spi_transaction_ext_t trans_ext;
        memset(&trans_ext, 0, sizeof(trans_ext));
        trans_ext.command_bits = 0;
        trans_ext.base.length = chunk * 8;
        trans_ext.base.tx_buffer = ptr;
        trans_ext.base.rx_buffer = nullptr;
        trans_ext.base.flags = SPI_TRANS_VARIABLE_CMD;
        esp_err_t ret = spi_device_transmit(spi, &trans_ext.base);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "EPD_WriteMultiData_ToMaster failed chunk=%u remaining=%u ret=%d", chunk, remaining, (int)ret);
            break;
        }
        ptr += chunk;
        remaining -= chunk;
    }
    EPD_Select_None();
}

void ePaperPort::EPD_WriteMultiData_ToSlave(uint8_t *data, unsigned int len) {
    if (len == 0 || data == nullptr) return;
    EPD_Select_Slave();
    Set_DCIOLevel(1);
    delay_us(1);

    uint8_t *ptr = data;
    unsigned int remaining = len;
    while (remaining > 0) {
        unsigned int chunk = remaining > NT61522_SPI_MAX_BUFFER_SIZE ? NT61522_SPI_MAX_BUFFER_SIZE : remaining;
        spi_transaction_ext_t trans_ext;
        memset(&trans_ext, 0, sizeof(trans_ext));
        trans_ext.command_bits = 0;
        trans_ext.base.length = chunk * 8;
        trans_ext.base.tx_buffer = ptr;
        trans_ext.base.rx_buffer = nullptr;
        trans_ext.base.flags = SPI_TRANS_VARIABLE_CMD;
        esp_err_t ret = spi_device_transmit(spi, &trans_ext.base);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "EPD_WriteMultiData_ToSlave failed chunk=%u remaining=%u ret=%d", chunk, remaining, (int)ret);
            break;
        }
        ptr += chunk;
        remaining -= chunk;
    }
    EPD_Select_None();
}

void ePaperPort::EPD_WriteCMD_ToBoth(uint8_t command) {
    EPD_Select_Both();
    EPD_WriteCMD_PreSelected(command);
    EPD_Select_None();
}

void ePaperPort::EPD_WriteDATA_ToBoth(uint8_t data) {
    EPD_Select_Both();
    EPD_WriteDATA_PreSelected(data);
    EPD_Select_None();
}

void ePaperPort::EPD_WriteMultiData_ToBoth(uint8_t *data, unsigned int len) {
    if (len == 0 || data == nullptr) return;
    EPD_Select_Both();
    Set_DCIOLevel(1);
    delay_us(1);

    uint8_t *ptr = data;
    unsigned int remaining = len;
    while (remaining > 0) {
        unsigned int chunk = remaining > NT61522_SPI_MAX_BUFFER_SIZE ? NT61522_SPI_MAX_BUFFER_SIZE : remaining;
        spi_transaction_ext_t trans_ext;
        memset(&trans_ext, 0, sizeof(trans_ext));
        trans_ext.command_bits = 0;
        trans_ext.base.length = chunk * 8;
        trans_ext.base.tx_buffer = ptr;
        trans_ext.base.rx_buffer = nullptr;
        trans_ext.base.flags = SPI_TRANS_VARIABLE_CMD;
        esp_err_t ret = spi_device_transmit(spi, &trans_ext.base);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "EPD_WriteMultiData_ToBoth failed chunk=%u remaining=%u ret=%d", chunk, remaining, (int)ret);
            break;
        }
        ptr += chunk;
        remaining -= chunk;
    }
    EPD_Select_None();
}

void ePaperPort::EPD_WriteCMD_Target(EP_Target_t target, uint8_t command) {
    switch (target) {
        case TARGET_MASTER: EPD_WriteCMD_ToMaster(command); break;
        case TARGET_SLAVE:  EPD_WriteCMD_ToSlave(command); break;
        case TARGET_BOTH:   EPD_WriteCMD_ToBoth(command); break;
        default: break;
    }
}

void ePaperPort::EPD_WriteDATA_Target(EP_Target_t target, uint8_t data) {
    switch (target) {
        case TARGET_MASTER: EPD_WriteDATA_ToMaster(data); break;
        case TARGET_SLAVE:  EPD_WriteDATA_ToSlave(data); break;
        case TARGET_BOTH:   EPD_WriteDATA_ToBoth(data); break;
        default: break;
    }
}

void ePaperPort::EPD_WriteMultiData_Target(EP_Target_t target, uint8_t *data, unsigned int len) {
    if (len == 0 || data == nullptr) return;
    switch (target) {
        case TARGET_MASTER: EPD_WriteMultiData_ToMaster(data, len); break;
        case TARGET_SLAVE:  EPD_WriteMultiData_ToSlave(data, len); break;
        case TARGET_BOTH:   EPD_WriteMultiData_ToBoth(data, len); break;
        default: break;
    }
}

void ePaperPort::EPD_Read_reg_FromMaster(uint8_t reg, uint8_t *pbuf, unsigned int len) {
    if (pbuf == nullptr || len == 0) return;
    EPD_Select_Master();
    EPD_WriteCMD_PreSelected(reg);
    E_SDI_IN();
    Set_DCIOLevel(1);
    delay_ms(10);
    for (unsigned int i = 0; i < len; i++) {
        pbuf[i] = EPD_SPI_Read();
    }
    EPD_Select_None();
    E_SDI_OUT();
}

void ePaperPort::EPD_Read_reg_FromSlave(uint8_t reg, uint8_t *pbuf, unsigned int len) {
    if (pbuf == nullptr || len == 0) return;
    EPD_Select_Slave();
    EPD_WriteCMD_PreSelected(reg);
    E_SDI_IN();
    Set_DCIOLevel(1);
    delay_ms(10);
    for (unsigned int i = 0; i < len; i++) {
        pbuf[i] = EPD_SPI_Read();
    }
    EPD_Select_None();
    E_SDI_OUT();
}

uint8_t ePaperPort::EPD_ReadByte_FromMaster(uint8_t reg) {
    uint8_t data = 0;
    EPD_Read_reg_FromMaster(reg, &data, 1);
    return data;
}

uint8_t ePaperPort::EPD_ReadByte_FromSlave(uint8_t reg) {
    uint8_t data = 0;
    EPD_Read_reg_FromSlave(reg, &data, 1);
    return data;
}

void ePaperPort::EPD_WriteCMD(uint8_t command) {
    Set_CSIOLevel(0);
    Set_DCIOLevel(0);
    delay_us(1);
    SPI_Write(command);
    Set_CSIOLevel(1);
}

void ePaperPort::EPD_WriteDATA(uint8_t data) {
    Set_CSIOLevel(0);
    Set_DCIOLevel(1);
    delay_us(1);
    SPI_Write(data);
    Set_CSIOLevel(1);
}

void ePaperPort::EPD_Read_reg(uint8_t reg, uint8_t *pbuf, unsigned int len) {
    if (pbuf == nullptr || len == 0) return;
    EPD_WriteCMD(reg);
    E_SDI_IN();
    Set_DCIOLevel(1);
    Set_CSIOLevel(0);
    delay_ms(40);
    for (unsigned int i = 0; i < len; i++) {
        pbuf[i] = EPD_SPI_Read();
    }
    Set_CSIOLevel(1);
    E_SDI_OUT();
}

void ePaperPort::EPD_TurnOnDisplay(void) {
    LOG_Purple("800x480 %s>%d",__func__,__LINE__);

    EPD_SendCommand(0x04);
    EPD_LoopBusy();
    EPD_SendCommand(0x06);
    EPD_SendData(0x6F);
    EPD_SendData(0x1F);
    EPD_SendData(0x17);
    EPD_SendData(0x49);
    EPD_SendCommand(0x12);
    EPD_SendData(0x00);
    EPD_LoopBusy();
    EPD_SendCommand(0x02);
    EPD_SendData(0x00);
    EPD_LoopBusy();
}

void ePaperPort::EPD_sleep(void) {
    //#define EPD_type_   EPD_1600_1200_133
//#define EPD_type_   EPD_1024_600

   isEPDInit = false;

#if (EPD_type_  ==  EPD_800_480 )
    LOG_Purple("%s>%d",__func__,__LINE__);
    LOG_Purple("EPD sleep power off start %s>%d",__func__,__LINE__);
    EPD_WriteCMD(0x02);
    delay_ms(30);
    EPD_Check_Busy();
    delay_ms(100);
    LOG_Purple("EPD sleep deep sleep cmd start %s>%d",__func__,__LINE__);
    EPD_WriteCMD(0x07);
    EPD_WriteDATA(0xA5);
    LOG_Purple("EPD sleep done %s>%d",__func__,__LINE__);
#elif (EPD_type_  ==  EPD_1600_1200_79 )
    NT61522_Sleep();
#elif (EPD_type_  ==  EPD_1600_1200_133 )
    NT61522_Sleep();
#elif (EPD_type_  ==  EPD_1024_600 )

#elif (EPD_type_  ==  EPD_1360_480_1085 )

#elif (EPD_type_  ==  EPD_800_480_4s_75 )

	EPD_SendCommand(0x04);   //Power on
  	EPD_Check_Busy();   
 
	EPD_SendCommand(0x12); //Update  
    EPD_SendData(0x00); 	
	EPD_Check_Busy();

	EPD_SendCommand(0x02); //Power off
	EPD_SendData(0x00);
	EPD_Check_Busy();
  
	EPD_SendCommand(0x07); //Power off
	EPD_SendData(0xA5);
   //delay_ms(200); 

    LOG_Purple("error %s>%d",__func__,__LINE__);
#else
    LOG_Purple("error %s>%d",__func__,__LINE__);
#endif
}

void ePaperPort::EPD_refresh(void) {
    EPD_WriteCMD(0x04);
    delay_ms(30);
    EPD_Check_Busy();
    EPD_WriteCMD(0x12);
    delay_ms(30);
    EPD_Check_Busy();
}

void ePaperPort::EPD_refresh_17H(void) {
    EPD_WriteCMD(0x17);
    EPD_WriteDATA(0xA5);
    delay_ms(30);
    EPD_Check_Busy();
}

void ePaperPort::Set_Rotation(uint8_t rot) {
    Rotation = rot;
}

void ePaperPort::Set_Mirror(uint8_t mirr_x, uint8_t mirr_y) {
    mirrx = mirr_x;
    mirry = mirr_y;
}

void ePaperPort::EPD_initial(void) {

    LOG_Purple("1024x600 %s>%d",__func__,__LINE__);

    EPD_Reset();
    EPD_WriteCMD_ToBoth(0xAA);
    EPD_WriteDATA_ToBoth(0x49);
    EPD_WriteDATA_ToBoth(0x55);
    EPD_WriteDATA_ToBoth(0x20);
    EPD_WriteDATA_ToBoth(0x08);
    EPD_WriteDATA_ToBoth(0x09);
    EPD_WriteDATA_ToBoth(0x18);

    EPD_WriteCMD_ToBoth(R00_PSR);
    EPD_WriteDATA_ToBoth(0x5F);
    EPD_WriteDATA_ToBoth(0x69);
    EPD_Check_Busy();

    EPD_WriteCMD_ToBoth(0xE0);
    EPD_WriteDATA_ToBoth(0x01);
    EPD_Check_Busy();

    EPD_WriteCMD_ToBoth(R01_PWR);
    EPD_WriteDATA_ToBoth(0x3F);

    EPD_WriteCMD_ToBoth(R03_POFS);
    EPD_WriteDATA_ToBoth(0x00);
    EPD_WriteDATA_ToBoth(0x54);
    EPD_WriteDATA_ToBoth(0x00);
    EPD_WriteDATA_ToBoth(0x44);

    EPD_WriteCMD_ToBoth(R05_BTST1);
    EPD_WriteDATA_ToBoth(0x40);
    EPD_WriteDATA_ToBoth(0x1F);
    EPD_WriteDATA_ToBoth(0x1F);
    EPD_WriteDATA_ToBoth(0x2C);

    EPD_WriteCMD_ToBoth(R06_BTST2);
    EPD_WriteDATA_ToBoth(0x6F);
    EPD_WriteDATA_ToBoth(0x1F);
    EPD_WriteDATA_ToBoth(0x16);
    EPD_WriteDATA_ToBoth(0x25);

    EPD_WriteCMD_ToBoth(R08_BTST3);
    EPD_WriteDATA_ToBoth(0x6F);
    EPD_WriteDATA_ToBoth(0x1F);
    EPD_WriteDATA_ToBoth(0x1F);
    EPD_WriteDATA_ToBoth(0x22);

    EPD_WriteCMD_ToBoth(R13_IPC);
    EPD_WriteDATA_ToBoth(0x00);
    EPD_WriteDATA_ToBoth(0x04);

    EPD_WriteCMD_ToBoth(0x30);
    EPD_WriteDATA_ToBoth(0x08);

    EPD_WriteCMD_ToBoth(R41_TSE);
    EPD_WriteDATA_ToBoth(0x00);

    EPD_WriteCMD_ToBoth(R50_CDI);
    EPD_WriteDATA_ToBoth(0x3F);

    EPD_WriteCMD_ToBoth(R60_TCON);
    EPD_WriteDATA_ToBoth(0x02);
    EPD_WriteDATA_ToBoth(0x00);

    EPD_WriteCMD_ToBoth(R61_TRES);
    EPD_WriteDATA_ToBoth(0x02);
    EPD_WriteDATA_ToBoth(0x00);
    EPD_WriteDATA_ToBoth(0x02);
    EPD_WriteDATA_ToBoth(0x58);
    EPD_Check_Busy();

    EPD_WriteCMD_ToBoth(0x84);
    EPD_WriteDATA_ToBoth(0x01);

    EPD_WriteCMD_ToBoth(R86_AGID);
    EPD_WriteDATA_ToBoth(0x00);

    EPD_WriteCMD_ToBoth(RE3_PWS);
    EPD_WriteDATA_ToBoth(0x2F);

    EPD_WriteCMD_ToBoth(0xE5);
    EPD_WriteDATA_ToBoth(0x00);
}


void ePaperPort::Epaper_Initial() {   
   LOG_Purple("4-color 800x480 7.5 %s>%d",__func__,__LINE__);
   EPD_Reset();
 
  EPD_WriteCMD(0x4D);
  EPD_WriteDATA(0x78);
 
  EPD_WriteCMD(R00_PSR);  //OTP mode
  EPD_WriteDATA(0x2F);
  EPD_WriteDATA(0x29);
  
  EPD_WriteCMD(0x01);
  EPD_WriteDATA(0x07);
  EPD_WriteDATA(0x00);
  EPD_WriteDATA(0x14);
  EPD_WriteDATA(0x00);
  EPD_WriteDATA(0x00);
  EPD_WriteDATA(0x14);

 
  EPD_WriteCMD(R50_CDI);
  EPD_WriteDATA(0x37);  //border white
  
  EPD_WriteCMD(R61_TRES); //resolution 
  EPD_WriteDATA(X_Addr_Start_H);
  EPD_WriteDATA(X_Addr_Start_L);
  EPD_WriteDATA(Y_Addr_Start_H);
  EPD_WriteDATA(Y_Addr_Start_L);  
	
  EPD_WriteCMD(0x65); //resolution 
  EPD_WriteDATA(0x00);
  EPD_WriteDATA(0x00);
  EPD_WriteDATA(0x00);
  EPD_WriteDATA(0x00);  

  EPD_WriteCMD(0xE3);
  EPD_WriteDATA(0x88);   
  
  EPD_WriteCMD(0xE9);
  EPD_WriteDATA(0x01);   
	
  EPD_WriteCMD(0x30);// frame go with waveform
  EPD_WriteDATA(0x08); 
}

void ePaperPort::Epaper_Update() {   
//  EPD_WriteCMD(0x04);
//    EPD_Check_Busy();  //while(1);
//	
//  EPD_WriteCMD(0xA2);	//********************
//EPD_WriteDATA(0x00);	

  EPD_WriteCMD(0x12);
  EPD_WriteDATA(0x00);
    EPD_Check_Busy();
	delay_ms(20);
 
  EPD_WriteCMD(0x02);
  EPD_WriteDATA(0x00);
    EPD_Check_Busy();	
//	
////	delay_ms(30);
//	
    EPD_WriteCMD(0x07);		// Deep_sleep
    EPD_WriteDATA(0xa5);   	
}

void ePaperPort::Epaper_Update_and_Deepsleep() {   

	EPD_WriteCMD(0x04);   //Power on
  	 EPD_Check_Busy();   
 
	EPD_WriteCMD(0x12); //Update  
    EPD_WriteDATA(0x00); 	
	EPD_Check_Busy();

	EPD_WriteCMD(0x02); //Power off
	EPD_WriteDATA(0x00);
	EPD_Check_Busy();
  
	EPD_WriteCMD(0x07); //Power off
	EPD_WriteDATA(0xA5);
    delay_ms(200); 
} 


void ePaperPort::Epaper_Init() {   
  
  EPD_WriteCMD(0x4D);		
  EPD_WriteDATA(0x78);

  EPD_WriteCMD(0xE5);		
  EPD_WriteDATA(0x08);

			EPD_WriteCMD(0xE0);	//0xE0
			EPD_WriteDATA(0x01);	

			EPD_WriteCMD(0xA2);	//MASTER enable
			EPD_WriteDATA(0x01);	

			EPD_WriteCMD(0x00);	//0x00 PSR
			EPD_WriteDATA(0x0F);	//UD=1,SHL=0
			EPD_WriteDATA(0x21);

			EPD_WriteCMD(0xA2);	//close
			EPD_WriteDATA(0x00);	
	

  EPD_WriteCMD(0x01);	// PWRR
  EPD_WriteDATA(0x07);
  EPD_WriteDATA(0x00);
  
  EPD_WriteCMD(0x06);	// BTST_P
	EPD_WriteDATA(0x0f);		// 2025-09-02 WQY
	EPD_WriteDATA(0x11);	
	EPD_WriteDATA(0x2d);   	
	EPD_WriteDATA(0x22);   	
	EPD_WriteDATA(0x19);      	
	EPD_WriteDATA(0x39);   	
	EPD_WriteDATA(0x10);  
 
	
  EPD_WriteCMD(0x30);	// PLL
  EPD_WriteDATA(0x08);	
	
  EPD_WriteCMD(0x50);	// CDI
  EPD_WriteDATA(0x37);
  
  EPD_WriteCMD(0x60);	// TCON
  EPD_WriteDATA(0x02);
  EPD_WriteDATA(0x02);
  
  EPD_WriteCMD(0x61); // TRES
  EPD_WriteDATA(Source_BITS/256);		// Source_BITS_H
  EPD_WriteDATA(Source_BITS%256);		// Source_BITS_L
  EPD_WriteDATA(Gate_BITS/256);			// Gate_BITS_H
  EPD_WriteDATA(Gate_BITS%256); 		// Gate_BITS_L		

	
  EPD_WriteCMD(0x65);	// GSST
  EPD_WriteDATA(0x00);	
  EPD_WriteDATA(0x00);	
  EPD_WriteDATA(0x00);	
  EPD_WriteDATA(0x00);

//  EPD_WriteCMD(0x82);	// VDCS
//  EPD_WriteDATA(0x80 | LUT_VCOM[0]);	

  EPD_WriteCMD(0xE3);	// PWS
  EPD_WriteDATA(0x08);
  EPD_WriteDATA(0x00);
	
  
  EPD_WriteCMD(0xE9);	// 
  EPD_WriteDATA(0x01); 
	
  EPD_WriteCMD(0xB8);	// 
  EPD_WriteDATA(0xB5); 
  delay_ms(20);	

//////////////////////////////////////////////////////////////////////////////	

  EPD_WriteCMD(0x4D);		
  EPD_WriteDATA(0x78);

  EPD_WriteCMD(0xE5);		
  EPD_WriteDATA(0x08);

			EPD_WriteCMD(0xE0);	//0xE0
			EPD_WriteDATA(0x01);
			
			EPD_WriteCMD(0xA2);	//SLAVE enable
			EPD_WriteDATA(0x02);	

			EPD_WriteCMD(0x00);	//0x00 PSR
			EPD_WriteDATA(0x0F);	//UD=1,SHL=1
			EPD_WriteDATA(0x21);

			EPD_WriteCMD(0xA2);	//close
			EPD_WriteDATA(0x00);	


  EPD_WriteCMD(0x01);	// PWRR
  EPD_WriteDATA(0x07);
  EPD_WriteDATA(0x00);
  
  EPD_WriteCMD(0x06);	// BTST_P
	EPD_WriteDATA(0x0f);		// 2025-09-02 WQY
	EPD_WriteDATA(0x11);	
	EPD_WriteDATA(0x2d);   	
	EPD_WriteDATA(0x22);   	
	EPD_WriteDATA(0x19);      	
	EPD_WriteDATA(0x39);   	
	EPD_WriteDATA(0x10);  
 
	
  EPD_WriteCMD(0x30);	// PLL
  EPD_WriteDATA(0x08);	
	
  EPD_WriteCMD(0x50);	// CDI
  EPD_WriteDATA(0x37);
  
  EPD_WriteCMD(0x60);	// TCON
  EPD_WriteDATA(0x02);
  EPD_WriteDATA(0x02);
  
  EPD_WriteCMD(0x61); // TRES
  EPD_WriteDATA(Source_BITS/256);		// Source_BITS_H
  EPD_WriteDATA(Source_BITS%256);		// Source_BITS_L
  EPD_WriteDATA(Gate_BITS/256);			// Gate_BITS_H
  EPD_WriteDATA(Gate_BITS%256); 		// Gate_BITS_L		

	
  EPD_WriteCMD(0x65);	// GSST
  EPD_WriteDATA(0x00);	
  EPD_WriteDATA(0x00);	
  EPD_WriteDATA(0x00);	
  EPD_WriteDATA(0x00);

//  EPD_WriteCMD(0x82);	// VDCS
//  EPD_WriteDATA(0x80 | LUT_VCOM[0]);	

  EPD_WriteCMD(0xE3);	// PWS
  EPD_WriteDATA(0x08);
  EPD_WriteDATA(0x00);
	
  
  EPD_WriteCMD(0xE9);	// 
  EPD_WriteDATA(0x01); 
	
  EPD_WriteCMD(0xB8);	// 
  EPD_WriteDATA(0xB5); 
  delay_ms(20);
	
  EPD_WriteCMD(0x04);
    EPD_Check_Busy();  //while(1);


}





void ePaperPort::EPD_Init() {   
    Display_had_flag = 0xAA;
    if (isEPDInit) {
        LOG_Purple("already init  %s>%d",__func__,__LINE__);

        // #if (EPD_type_  ==  EPD_1600_1200_133 )
        // Read_Temptr();
        // #elif (EPD_type_  ==  EPD_1600_1200_79 )
        // NT61522_ReadTemperature();
        // #endif

        return;
    }
    isEPDInit = true;    
    LOG_Purple("%s>%d",__func__,__LINE__);

#if (EPD_type_ == EPD_1024_600)
    LOG_Purple("1024x600 %s>%d",__func__,__LINE__);
    EPD_initial();
#elif (EPD_type_ == EPD_800_480_4s_75)   
    Epaper_Initial();
#elif (EPD_type_ == EPD_1600_1200_79) ||  (EPD_type_ == EPD_1600_1200_133)
    // LOG_Purple("1600x1200 %s>%d",__func__,__LINE__);
    NT61522_Init();
#else

    LOG_Purple("800x480 %s>%d",__func__,__LINE__);
    EPD_Reset();
    EPD_LoopBusy();
    vTaskDelay(pdMS_TO_TICKS(50));

    EPD_SendCommand(0xAA);
    EPD_SendData(0x49);
    EPD_SendData(0x55);
    EPD_SendData(0x20);
    EPD_SendData(0x08);
    EPD_SendData(0x09);
    EPD_SendData(0x18);

    EPD_SendCommand(0x01);
    EPD_SendData(0x3F);

    EPD_SendCommand(0x00);
    EPD_SendData(0x5F);
    EPD_SendData(0x69);

    EPD_SendCommand(0x03);
    EPD_SendData(0x00);
    EPD_SendData(0x54);
    EPD_SendData(0x00);
    EPD_SendData(0x44);

    EPD_SendCommand(0x05);
    EPD_SendData(0x40);
    EPD_SendData(0x1F);
    EPD_SendData(0x1F);
    EPD_SendData(0x2C);

    EPD_SendCommand(0x06);
    EPD_SendData(0x6F);
    EPD_SendData(0x1F);
    EPD_SendData(0x17);
    EPD_SendData(0x49);

    EPD_SendCommand(0x08);
    EPD_SendData(0x6F);
    EPD_SendData(0x1F);
    EPD_SendData(0x1F);
    EPD_SendData(0x22);

    EPD_SendCommand(0x30);
    EPD_SendData(0x03);

    EPD_SendCommand(0x50);
    EPD_SendData(0x3F);

    EPD_SendCommand(0x60);
    EPD_SendData(0x02);
    EPD_SendData(0x00);

    EPD_SendCommand(0x61);
    EPD_SendData(0x03);
    EPD_SendData(0x20);
    EPD_SendData(0x01);
    EPD_SendData(0xE0);

    EPD_SendCommand(0x84);
    EPD_SendData(0x01);

    EPD_SendCommand(0xE3);
    EPD_SendData(0x2F);

    EPD_SendCommand(0x04);
    EPD_LoopBusy();
    //EPD_DispClear(ColorWhite);
    LOG_Purple("800x480 over %s>%d",__func__,__LINE__);
#endif

}

void ePaperPort::EPD_DispClear(uint8_t color) {
    if (!EnsureDispBuffer()) {
        return;
    }
    uint8_t *buffer = DispBuffer;
    for (int j = 0; j < DisplayLen; j++) {
        buffer[j] = (color << 4) | color;
    }
}

// #define epd_black 0x00
// #define epd_white 0x11
// #define epd_yellow 0x22
// #define epd_red 0x33
// #define epd_blue 0x55
// #define epd_green 0x66

#define epd_black 0x00
#define epd_white 0x01
#define epd_yellow 0x02
#define epd_red 0x03
#define epd_blue 0x05
#define epd_green 0x06

void ePaperPort::EPD_Display() {
#if (EPD_type_ == EPD_1600_1200_79) || (EPD_type_ == EPD_1600_1200_133)
    LOG_Purple("1600x1200 %s>%d  DisplayLen=%d",__func__,__LINE__,DisplayLen);

    if (!EnsureDispBuffer()) {
        ESP_LOGE(TAG, "EPD_Display 1600x1200 aborted, DispBuffer not ready");
        return;
    }
    
    NT61522_DisplayImage(DispBuffer, DisplayLen);
    ReleaseRotationBuffer();
    ReleaseDispBuffer();

#elif (EPD_type_ ==  EPD_1360_480_1085)     
    uint32_t i = 0;
    uint8_t j = 0;
    spi_transaction_t t;

    EPD_WriteCMD(0xA2);	//********************
    EPD_WriteDATA(0x01);		
    EPD_WriteCMD(0x10);

    for (i = 0; i < (uint32_t)DisplayLen; i += 256) { // ALLSCREEN_BYTES =  DisplayLen
        if (j == 0) {
            EPD_Select_Master();  
            memset(&t, 0, sizeof(t));
            t.length    = 8 * 256;
            t.tx_buffer = DispBuffer + i;
            spi_device_polling_transmit(spi, &t); //Transmit!
            j = 1;
        } else {
            EPD_Select_Slave();
            memset(&t, 0, sizeof(t));
            t.length    = 8 * 256;
            t.tx_buffer = DispBuffer + i;
            spi_device_polling_transmit(spi, &t); //Transmit!
            j = 0;
        }
    }
#elif (EPD_type_ == EPD_800_480_4s_75)
    uint32_t i = 0;
    uint8_t j = 0;
    spi_transaction_t t;

    EPD_WriteCMD(0x10);
    for (i = 0; i < (uint32_t)DisplayLen; i += 256) { // ALLSCREEN_BYTES =  DisplayLen
        if (j == 0) {
            EPD_Select_Master();  
            memset(&t, 0, sizeof(t));
            t.length    = 8 * 256;
            t.tx_buffer = DispBuffer + i;
            spi_device_polling_transmit(spi, &t); //Transmit!
            j = 1;
        } else {
            EPD_Select_Slave();
            memset(&t, 0, sizeof(t));
            t.length    = 8 * 256;
            t.tx_buffer = DispBuffer + i;
            spi_device_polling_transmit(spi, &t); //Transmit!
            j = 0;
        }
    }
    Epaper_Update_and_Deepsleep();		

    // for(i=0;i<ALLSCREEN_BYTES*2;i++)
    // {	
    //     EPD_WriteDATA(*DispBuffer++);
    // }       
#elif (EPD_type_ == EPD_1024_600)
    LOG_Purple("1024x600 %s>%d  DisplayLen=%d",__func__,__LINE__,DisplayLen);
    if (!EnsureDispBuffer()) {
        ESP_LOGE(TAG, "EPD_Display 1024x600 aborted, DispBuffer not ready");
        return;
    }
    ESP_LOGI(TAG, "EPD_Display 1024x600 use DispBuffer=%p len=%d", DispBuffer, DisplayLen);
    if (DisplayLen >= 8) {
        ESP_LOGI(TAG, "Disp head: %02X %02X %02X %02X %02X %02X %02X %02X",
                 DispBuffer[0], DispBuffer[1], DispBuffer[2], DispBuffer[3],
                 DispBuffer[4], DispBuffer[5], DispBuffer[6], DispBuffer[7]);
    }

    uint32_t i = 0;
    uint8_t j = 0;
    spi_transaction_t t;

    EPD_WriteCMD_ToBoth(0x10);  	
    Set_DCIOLevel(1);
    for (i = 0; i < (uint32_t)DisplayLen; i += 256) {
        if (j == 0) {
            EPD_Select_Master();  
            memset(&t, 0, sizeof(t));
            t.length    = 8 * 256;
            t.tx_buffer = DispBuffer + i;
            spi_device_polling_transmit(spi, &t); //Transmit!
            j = 1;
        } else {
            EPD_Select_Slave();
            memset(&t, 0, sizeof(t));
            t.length    = 8 * 256;
            t.tx_buffer = DispBuffer + i;
            spi_device_polling_transmit(spi, &t); //Transmit!
            j = 0;
        }
    }
    EPD_Select_None();

    LOG_INFO("%s>%d", __func__, __LINE__);
    EPD_WriteCMD_ToBoth(0x04);
    delay_ms(100);
    EPD_Check_Busy();

    LOG_INFO("%s>%d", __func__, __LINE__);
    EPD_WriteCMD_ToBoth(0x12);
    EPD_WriteDATA_ToBoth(0x00);
    delay_ms(100);
    EPD_Check_Busy();

    LOG_INFO("%s>%d", __func__, __LINE__);
    EPD_WriteCMD_ToBoth(R02_POF);
    EPD_WriteDATA_ToBoth(0x00);
    delay_ms(100);
    EPD_Check_Busy();
    ReleaseRotationBuffer();
    ReleaseDispBuffer();
#else
    LOG_Purple("800x480 %s>%d",__func__,__LINE__);
    // EPD_PixelRotate();
    // EPD_SendCommand(0x10);
    // EPD_Sendbuffera(RotationBuffer, DisplayLen);
    // EPD_TurnOnDisplay();

    //memcpy(RotationBuffer, DispBuffer, DisplayLen);
    if (!EnsureDispBuffer()) {
        ESP_LOGE(TAG, "EPD_Display 800x480 aborted, DispBuffer not ready");
        return;
    }
    EPD_SendCommand(0x10);
    EPD_Sendbuffera(DispBuffer, DisplayLen);
    EPD_TurnOnDisplay();
    ReleaseRotationBuffer();
    ReleaseDispBuffer();
#endif
}


void ePaperPort::EPD_SrcDisplayCopy(uint8_t *buffer, uint32_t len, uint32_t addlen) {
    if (!EnsureDispBuffer()) {
        ESP_LOGE(TAG, "EPD_SrcDisplayCopy alloc DispBuffer failed");
        return;
    }
    if ((addlen + len) > (uint32_t)DisplayLen) {
        ESP_LOGE(TAG, "Data exceeds the buffer area.");
        return;
    }
    memcpy(DispBuffer + addlen, buffer, len);
    ESP_LOGI(TAG, "EPD_SrcDisplayCopy copied len=%u addlen=%u total=%u", (unsigned int)len, (unsigned int)addlen, (unsigned int)(addlen + len));
}

uint32_t ePaperPort::EPD_GetBufferLength() {
    return DisplayLen;
}

uint8_t* ePaperPort::EPD_GetIMGBuffer() {
    if (!EnsureDispBuffer()) {
        return nullptr;
    }
    return DispBuffer;
}

void ePaperPort::EPD_SetPixel(uint16_t x, uint16_t y, uint16_t color) {
    if (!EnsureDispBuffer()) {
        ESP_LOGE(TAG, "EPD_SetPixel alloc DispBuffer failed");
        return;
    }
    if (x >= width_ || y >= height_) {
        ESP_LOGE("Pixel", "Beyond the limit: (%d,%d)", x, y);
        return;
    }
    uint32_t index = y * (width_ >> 1) + (x >> 1);
    uint8_t px = DispBuffer[index];
    uint8_t xor_mask = (x & 1) ? 0xF0 : 0x0F;
    uint8_t shift = (x & 1) ? 0 : 4;
    DispBuffer[index] = (px & xor_mask) | (color << shift);
}

uint8_t ePaperPort::EPD_GetPixel4(const uint8_t *buf, int width, int x, int y) {
    int index = y * (width >> 1) + (x >> 1);
    uint8_t byte = buf[index];
    return (x & 1) ? (byte & 0x0F) : (byte >> 4);
}

void ePaperPort::EPD_SetPixel4(uint8_t *buf, int width, int x, int y, uint8_t px) {
    int index = y * (width >> 1) + (x >> 1);
    uint8_t old = buf[index];
    if (x & 1) buf[index] = (old & 0xF0) | (px & 0x0F);
    else buf[index] = (old & 0x0F) | (px << 4);
}

void ePaperPort::EPD_PixelRotate() {   
    if (!EnsureDispBuffer() || !EnsureRotationBuffer()) {
        ESP_LOGE(TAG, "EPD_PixelRotate alloc buffer failed");
        return;
    }
    if (Rotation == 3) {
        LOG_Cyan("3 %s>%d",__func__,__LINE__);            
        if (width_==1024)
        {
         LOG_Cyan("1024 width_=%d,height_=%d",width_, height_);            
         //EPD_Rotate90CCW_Fast(DispBuffer, RotationBuffer, 600, 1024);     
         EPD_Rotate180_Fast(DispBuffer, RotationBuffer, width_, height_);
        }
        else
        {
         LOG_Cyan("800 width_=%d,height_=%d",width_, height_);            
         //EPD_Rotate90CCW_Fast(DispBuffer,RotationBuffer,480,800);
         EPD_Rotate180_Fast(DispBuffer,RotationBuffer,800,480);
        }       
    } else if (Rotation == 1) {
        EPD_Rotate90CW_Fast(DispBuffer,RotationBuffer,480,800);
        LOG_Cyan("1   %s>%d",__func__,__LINE__);         
        LOG_Cyan("width_=%d,height_=%d",width_, height_);   
    } else if (Rotation == 2) {
        EPD_Rotate180_Fast(DispBuffer,RotationBuffer,800,480);
        LOG_Cyan("2  %s>%d",__func__,__LINE__);          
        LOG_Cyan("width_=%d,height_=%d",width_, height_);  
    } else {
        LOG_Cyan("er  %s>%d",__func__,__LINE__);         
        LOG_Cyan("width_=%d,height_=%d",width_, height_);   
        memcpy(RotationBuffer, DispBuffer, DisplayLen);
    }
}

void ePaperPort::EPD_Rotate180_Fast(const uint8_t *src, uint8_t *dst, int width, int height) {
    const int bytesPerRow = width >> 1;
    const int totalRows = height;
    for (int y = 0; y < totalRows; y++) {
        const uint8_t *srcRow = src + y * bytesPerRow;
        uint8_t *dstRow = dst + (totalRows - 1 - y) * bytesPerRow;
        for (int x = 0; x < bytesPerRow; x++) {
            uint8_t b = srcRow[x];
            b = (uint8_t)((b << 4) | (b >> 4));
            dstRow[bytesPerRow - 1 - x] = b;
        }
    }
}

void ePaperPort::EPD_Rotate90CCW_Fast(const uint8_t *src, uint8_t *dst, int width, int height) {
    const int srcBytesPerRow = width >> 1;
    memset(dst, 0, (size_t)(height * width / 2));
    for (int y = 0; y < height; y++) {
        const uint8_t *srcRow = src + y * srcBytesPerRow;
        for (int x = 0; x < width; x += 2) {
            uint8_t b = srcRow[x >> 1];
            uint8_t p0 = b >> 4;
            uint8_t p1 = b & 0x0F;
            int ny0 = width - 1 - x;
            int nx0 = y;
            int ny1 = width - 2 - x;
            int nx1 = y;
            EPD_SetPixel4(dst, height, nx0, ny0, p0);
            EPD_SetPixel4(dst, height, nx1, ny1, p1);
        }
    }
}

void ePaperPort::EPD_Rotate90CW_Fast(const uint8_t *src, uint8_t *dst, int width, int height) {
    const int srcBytesPerRow = width >> 1;
    memset(dst, 0, (size_t)(height * width / 2));
    for (int y = 0; y < height; y++) {
        const uint8_t *srcRow = src + y * srcBytesPerRow;
        for (int x = 0; x < width; x += 2) {
            uint8_t b = srcRow[x >> 1];
            uint8_t p0 = b >> 4;
            uint8_t p1 = b & 0x0F;
            int ny0 = x;
            int nx0 = height - 1 - y;
            int ny1 = x + 1;
            int nx1 = height - 1 - y;
            EPD_SetPixel4(dst, height, nx0, ny0, p0);
            EPD_SetPixel4(dst, height, nx1, ny1, p1);
        }
    }
}

const uint8_t* ePaperPort::NT61522_PSR_V() {
    static const uint8_t v[] = {0xDF, 0x6B};
    return v;
}

const uint8_t* ePaperPort::NT61522_PWR_V() {
    static const uint8_t v[] = {0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38};
    return v;
}

const uint8_t* ePaperPort::NT61522_POF_V() {
    static const uint8_t v[] = {0x01};
    return v;
}

const uint8_t* ePaperPort::NT61522_DRF_V() {
    static const uint8_t v[] = {0x00};
    return v;
}

const uint8_t* ePaperPort::NT61522_CDI_V() {
    static const uint8_t v[] = {0x37};
    return v;
}

const uint8_t* ePaperPort::NT61522_TRES_V() {
    static const uint8_t v[] = {0x04, 0xB0, 0x03, 0x20};
    return v;
}

const uint8_t* ePaperPort::NT61522_AMV_V() {
    static const uint8_t v[] = {0x01, 0x00};
    return v;
}

const uint8_t* ePaperPort::NT61522_CCSET_V_CUR() {
    static const uint8_t v[] = {0x01};
    return v;
}

const uint8_t* ePaperPort::NT61522_CCSET_V_LOCK() {
    static const uint8_t v[] = {0x03};
    return v;
}

const uint8_t* ePaperPort::NT61522_PWS_V() {
    static const uint8_t v[] = {0x22};
    return v;
}

const uint8_t* ePaperPort::NT61522_DCDC_V() {
    static const uint8_t v[] = {0x44, 0x54, 0x00};
    return v;
}

const uint8_t* ePaperPort::NT61522_BTST_P_V() {
    static const uint8_t v[] = {0xE0, 0x20};
    return v;
}

const uint8_t* ePaperPort::NT61522_BTST_N_V() {
    static const uint8_t v[] = {0xE0, 0x20};
    return v;
}

const uint8_t* ePaperPort::NT61522_Sleep_V() {
    static const uint8_t v[] = {0xA5};
    return v;
}

esp_err_t ePaperPort::spiTransmitCommand(uint8_t commandBuf) {
    esp_err_t ret;
    spi_transaction_t t;

    Set_DCIOLevel(0);delay_us(1);  // from command 

    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.tx_buffer = &commandBuf;
    t.rxlength=0;
    ret = spi_device_polling_transmit(spi, &t);
    assert(ret == ESP_OK);
    return ret;
    
}

esp_err_t ePaperPort::spiTransmitData(const uint8_t *dataBuffer, size_t dataLength) {
     esp_err_t ret;
     spi_transaction_t t;
    // uint32_t i;

    Set_DCIOLevel(1);delay_us(1);  // fro data

    if (dataBuffer == nullptr || dataLength == 0) return ESP_OK;


    //LOG_Purple("%s>%d L=%d",__func__,__LINE__,dataLength);
    //for(i=0;i<dataLength;i+=256)
    //{                
           memset(&t, 0, sizeof(t));
           t.length    = 8*dataLength;
           t.tx_buffer = dataBuffer;
           ret = spi_device_polling_transmit(spi, &t); //Transmit!
           if (ret != ESP_OK) {
               ESP_LOGE(TAG, "spiTransmitData failed, len=%u err=%s",
                        (unsigned int)dataLength, esp_err_to_name(ret));
               return ret;
           }
    //}            
    //return ret;
    return ret;

    // spi_transaction_ext_t trans_ext;
    // while (dataLength >= NT61522_SPI_MAX_BUFFER_SIZE) {
    //     memset(&trans_ext, 0, sizeof(trans_ext));
    //     trans_ext.command_bits = 0;
    //     trans_ext.base.length = NT61522_SPI_MAX_BUFFER_SIZE * 8;
    //     trans_ext.base.tx_buffer = dataBuffer;
    //     trans_ext.base.flags = SPI_TRANS_VARIABLE_CMD;
    //     esp_err_t status = spi_device_transmit(spi, &trans_ext.base);
    //     if (status != ESP_OK) return status;
    //     dataLength -= NT61522_SPI_MAX_BUFFER_SIZE;
    //     dataBuffer += NT61522_SPI_MAX_BUFFER_SIZE;
    // }

    // if (dataLength > 0) {
    //     memset(&trans_ext, 0, sizeof(trans_ext));
    //     trans_ext.command_bits = 0;
    //     trans_ext.base.length = dataLength * 8;
    //     trans_ext.base.tx_buffer = dataBuffer;
    //     trans_ext.base.flags = SPI_TRANS_VARIABLE_CMD;
    //     return spi_device_transmit(spi, &trans_ext.base);
    // }
    // return ESP_OK;
}

esp_err_t ePaperPort::spiReceiveData(uint8_t *dataBuffer, size_t dataLength) 
{
    if (dataBuffer == nullptr || dataLength == 0) {
        ESP_LOGW(TAG, "spiReceiveData: invalid param, buf=%p, len=%u",
                 dataBuffer, (unsigned int)dataLength);
        return ESP_OK;
    }

    //ESP_LOGI(TAG, "spiReceiveData: begin len=%u", (unsigned int)dataLength);

    Set_DCIOLevel(1);
    delay_us(1);  // data mode

    uint8_t *rx_ptr = dataBuffer;
    size_t remain = dataLength;

    while (remain > 0) {
        size_t chunk = remain;

        if (chunk > NT61522_SPI_MAX_BUFFER_SIZE) {
            chunk = NT61522_SPI_MAX_BUFFER_SIZE;
        }

        spi_transaction_t t;
        memset(&t, 0, sizeof(t));

        /*
         * 当前 SPI 初始化使用：
         * SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_3WIRE | SPI_DEVICE_NO_DUMMY
         *
         * 所以这里 length = 0，rxlength = chunk * 8 是正常读法。
         */
        t.length = 0;
        t.rxlength = chunk * 8;
        t.rx_buffer = rx_ptr;

        //ESP_LOGI(TAG, "spiReceiveData: read chunk=%u, remain=%u",(unsigned int)chunk, (unsigned int)remain);

        esp_err_t ret = spi_device_polling_transmit(spi, &t);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "spiReceiveData: failed ret=%s, chunk=%u",
                     esp_err_to_name(ret), (unsigned int)chunk);
            return ret;
        }

        rx_ptr += chunk;
        remain -= chunk;
    }

    // ESP_LOGI(TAG, "spiReceiveData: end len=%u, data[0]=0x%02X, data[1]=0x%02X",
    //          (unsigned int)dataLength,
    //          dataLength > 0 ? dataBuffer[0] : 0,
    //          dataLength > 1 ? dataBuffer[1] : 0);

    return ESP_OK;
}


esp_err_t ePaperPort::spiTransmit(uint8_t commandBuf, const uint8_t *dataBuffer, size_t dataLength) {
    esp_err_t status = spiTransmitCommand(commandBuf);
    if (status != ESP_OK) return status;
    return spiTransmitData(dataBuffer, dataLength);
}

void ePaperPort::delayms(unsigned int delayTime) {
    vTaskDelay(pdMS_TO_TICKS(delayTime));
}

void ePaperPort::setGpioLevel(int pinNumber, uint8_t voltageLevel) {
    gpio_set_level((gpio_num_t)pinNumber, voltageLevel);
}

uint8_t ePaperPort::getGpioLevel(int pinNumber) {
    return static_cast<uint8_t>(gpio_get_level((gpio_num_t)pinNumber));
}

void ePaperPort::setPinCsAll(uint8_t setLevel) {
    if (EPD_which_one_ == 1) {
    setGpioLevel(cs_, setLevel);
    setGpioLevel(cs_2_, setLevel);
    }
    else{
    setGpioLevel(EPD2_CS_PIN, setLevel);    
    }
}

void ePaperPort::setPinCs(EP_Target_t target, uint8_t setLevel) {
    switch (target) {
        case TARGET_MASTER:
            if (EPD_which_one_ == 1) {
                setGpioLevel(cs_, setLevel);
            }
            else{
                setGpioLevel(EPD2_CS_PIN, setLevel);
            }   
            break;
        case TARGET_SLAVE:
            if (EPD_which_one_ == 1) {
                setGpioLevel(cs_2_, setLevel);
            }
            else{
                setGpioLevel(EPD2_CS_PIN, setLevel);
            }
            break;
        case TARGET_BOTH:
            if (EPD_which_one_ == 1) {
                setGpioLevel(cs_, setLevel);
                setGpioLevel(cs_2_, setLevel);
            }
            else
            {
                setGpioLevel(EPD2_CS_PIN, setLevel);
            }
            break;
        default:
            break;
    }
}

void ePaperPort::NT61522_Sleep() {
    isEPDInit = false;
    LOG_Purple("%s>%d",__func__,__LINE__);
    
    setPinCsAll(0);
    spiTransmit(0x07, NT61522_Sleep_V(), 1);
    setPinCsAll(1);

    
}


// 0B 07  01/02/03 
void ePaperPort::NT61522_ReadRevision() {
    memset(nt61522_chip_id_, 0, sizeof(nt61522_chip_id_));
    setPinCs(TARGET_MASTER, 0);
    spiTransmitCommand(0x70);
    spiReceiveData(nt61522_chip_id_, sizeof(nt61522_chip_id_));
    setPinCs(TARGET_MASTER, 1);
    EPD_Check_Busy();
    ESP_LOGI(TAG, "Master Chip Revision 0x%02X 0x%02X 0x%02X", nt61522_chip_id_[0], nt61522_chip_id_[1], nt61522_chip_id_[2]);

    memset(nt61522_chip_id_, 0, sizeof(nt61522_chip_id_));
    setPinCs(TARGET_SLAVE, 0);
    spiTransmitCommand(0x70);
    spiReceiveData(nt61522_chip_id_, sizeof(nt61522_chip_id_));
    setPinCs(TARGET_SLAVE, 1);
    EPD_Check_Busy();
    ESP_LOGI(TAG, "Slave Chip Revision 0x%02X 0x%02X 0x%02X", nt61522_chip_id_[0], nt61522_chip_id_[1], nt61522_chip_id_[2]);
}

void ePaperPort::NT61522_Init() {
#if (EPD_type_ == EPD_1600_1200_133)
	unsigned char r74DataBuf[9]={0x00, 0x0C, 0x0C, 0xD9, 0xDD, 0xDD, 0x15, 0x15, 0x55};
	unsigned char rf0DataBuf[6]={0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};
	unsigned char r60DataBuf[2]={0x03, 0x03};
	unsigned char r86DataBuf[1]={0x10};
	unsigned char rb6DataBuf[1]={0x07};
	unsigned char rb7DataBuf[1]={0x01};
	unsigned char rb0DataBuf[1]={0x01};
	unsigned char rb1DataBuf[1]={0x02};
    #define Delay_time_133   9

    LOG_Purple("<1> 1600-13.3 %s>%d",__func__,__LINE__);


    EPD_Reset();
    setPinCsAll(GPIO_HIGH);
    Read_Temptr();       //添加锁定当前温度函数(掉电重启时解除)，为了避免屏幕多次运行IC升温导致调取波形温度与实际环境温度不符

	EPD_Reset();
	EPD_Check_Busy();

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(0x74, r74DataBuf, sizeof(r74DataBuf));
	setPinCs(TARGET_MASTER,GPIO_HIGH);delayms(Delay_time_133);

	setPinCsAll(GPIO_LOW);delayms(Delay_time_133);
	spiTransmit(0xF0, rf0DataBuf, sizeof(rf0DataBuf));
	setPinCsAll(GPIO_HIGH);delayms(Delay_time_133);

	setPinCsAll(GPIO_LOW);
	spiTransmit(R00_PSR, PSR_V, sizeof(PSR_V));
	setPinCsAll(GPIO_HIGH);delayms(Delay_time_133);
	
	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(RA5_DCDC, DCDC_V, sizeof(DCDC_V));
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	spiTransmit(R50_CDI, CDI_V, sizeof(CDI_V));
	setPinCsAll(GPIO_HIGH);delayms(Delay_time_133);

	setPinCsAll(GPIO_LOW);
	spiTransmit(0x60, r60DataBuf, sizeof(r60DataBuf));
	setPinCsAll(GPIO_HIGH);delayms(Delay_time_133);

	setPinCsAll(GPIO_LOW);
	spiTransmit(0x86, r86DataBuf, sizeof(r86DataBuf));
	setPinCsAll(GPIO_HIGH);delayms(Delay_time_133);

	setPinCsAll(GPIO_LOW);
	spiTransmit(RE3_PWS, PWS_V, sizeof(PWS_V));
	setPinCsAll(GPIO_HIGH);delayms(Delay_time_133);

	// setPinCsAll(GPIO_LOW);
	// spiTransmit(RE0_CCSET, CCSET_V_LOCK, sizeof(CCSET_V_LOCK));
	// setPinCsAll(GPIO_HIGH);delayms(Delay_time_133);

	setPinCsAll(GPIO_LOW);
	spiTransmit(R61_TRES, TRES_V, sizeof(TRES_V));
	setPinCsAll(GPIO_HIGH);delayms(Delay_time_133);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(R01_PWR, PWR_V, sizeof(PWR_V));
	setPinCs(TARGET_MASTER,GPIO_HIGH);delayms(Delay_time_133);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(0xB6, rb6DataBuf, sizeof(rb6DataBuf));
	setPinCs(TARGET_MASTER,GPIO_HIGH);delayms(Delay_time_133);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(R06_BTST_P, BTST_P_V, sizeof(BTST_P_V));
	setPinCs(TARGET_MASTER,GPIO_HIGH);delayms(Delay_time_133);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(0xB7, rb7DataBuf, sizeof(rb7DataBuf));
	setPinCs(TARGET_MASTER,GPIO_HIGH);delayms(Delay_time_133);
	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(R05_BTST_N, BTST_N_V, sizeof(BTST_N_V));
	setPinCs(TARGET_MASTER,GPIO_HIGH);

	delayms(Delay_time_133);
	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(0xB0, rb0DataBuf, sizeof(rb0DataBuf));
	setPinCs(TARGET_MASTER,GPIO_HIGH);
	delayms(Delay_time_133);
	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(0xB1, rb1DataBuf, sizeof(rb1DataBuf));
	setPinCs(TARGET_MASTER,GPIO_HIGH);
    delayms(Delay_time_133);
#else   //  Below is 7.09
    // static const uint8_t an_tm_v[] = {0x00, 0x0C, 0x0C, 0xD9, 0xDD, 0xDD, 0x15, 0x15, 0x55};
    // static const uint8_t cmd66_v[] = {0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};
    // static const uint8_t pll_v[] = {0x08};
    // static const uint8_t tcon_v[] = {0x03, 0x03};
    // static const uint8_t pofs_mv[] = {0x00, 0xC0, 0x03, 0xA8};
    // static const uint8_t pofs_sv[] = {0x00, 0xC0, 0x03, 0x9A};
    // static const uint8_t agid_v[] = {0x10};
    // static const uint8_t cmda4_v[] = {0x03, 0x00, 0x01, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00};
    // static const uint8_t en_buf_v[] = {0x07};
    // static const uint8_t boost_vddp_en_v[] = {0x01};
    // static const uint8_t buck_boost_vddn_v[] = {0x01};
    // static const uint8_t tft_vcom_power_v[] = {0x02};
    // #define Delay_time_709   10

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(0x74, an_tm_v, sizeof(an_tm_v));
    // setPinCs(TARGET_MASTER, 1);
    // delayms(Delay_time_709);   // 这些 10ms 很重要。否则会显示不清楚

    // setPinCsAll(0);
    // delayms(1);
    // spiTransmit(0xF0, cmd66_v, sizeof(cmd66_v));
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCsAll(0);
    // spiTransmit(R00_PSR, NT61522_PSR_V(), 2);
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(0xA5, NT61522_DCDC_V(), 3);
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCsAll(0);
    // spiTransmit(R30_PLL, pll_v, sizeof(pll_v));
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCsAll(0);
    // spiTransmit(R50_CDI, NT61522_CDI_V(), 1);
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCsAll(0);
    // spiTransmit(R60_TCON, tcon_v, sizeof(tcon_v));
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(R03_POFS, pofs_mv, sizeof(pofs_mv));
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_SLAVE, 0);
    // spiTransmit(R03_POFS, pofs_sv, sizeof(pofs_sv));
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCsAll(0);
    // spiTransmit(R86_AGID, agid_v, sizeof(agid_v));
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCsAll(0);
    // spiTransmit(RE3_PWS, NT61522_PWS_V(), 1);
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCsAll(0);
    // spiTransmit(RE0_CCSET, NT61522_CCSET_V_CUR(), 1);
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCsAll(0);
    // spiTransmit(R61_TRES, NT61522_TRES_V(), 4);
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(0xA4, cmda4_v, sizeof(cmda4_v));
    // setPinCs(TARGET_MASTER, 1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(R01_PWR, NT61522_PWR_V(), 6);
    // setPinCs(TARGET_MASTER, 1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(0xB6, en_buf_v, sizeof(en_buf_v));
    // setPinCs(TARGET_MASTER, 1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(0x06, NT61522_BTST_P_V(), 2);
    // setPinCs(TARGET_MASTER, 1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(0xB7, boost_vddp_en_v, sizeof(boost_vddp_en_v));
    // setPinCs(TARGET_MASTER, 1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(0x05, NT61522_BTST_N_V(), 2);
    // setPinCs(TARGET_MASTER, 1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(0xB0, buck_boost_vddn_v, sizeof(buck_boost_vddn_v));
    // setPinCs(TARGET_MASTER, 1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(0xB1, tft_vcom_power_v, sizeof(tft_vcom_power_v));
    // setPinCs(TARGET_MASTER, 1);
    // delayms(Delay_time_709);  
    // LOG_Purple("%s>%d  over",__func__,__LINE__);



const unsigned char PSR_V[2] = {	0xDF, 0x6B};
const unsigned char PWR_V[6] = {	0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38};
[[maybe_unused]] const unsigned char POF_V[1] = {	0x01};
const unsigned char POFS_MV[4] = {	0x00, 0xC0, 0x03, 0xA8};
const unsigned char POFS_SV[4] = {	0x00, 0xC0, 0x03, 0x9A};
[[maybe_unused]] const unsigned char DRF_V[1] = {	0x00};
const unsigned char PLL_V[1] = {	0x08 };
const unsigned char CDI_V[1] = {	0x37};
const unsigned char TCON_V[2] = {	0x03, 0x03};
const unsigned char TRES_V[4] = {	0x04, 0xB0, 0x03, 0x20};
const unsigned char CMD66_V[6] = {	0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};
const unsigned char EN_BUF_V[1] = {	0x07};
const unsigned char CCSET_V[1] = {	0x01};
const unsigned char PWS_V[1] = {	0x22};
const unsigned char AN_TM_V[9] = {	0x00, 0x0C, 0x0C, 0xD9, 0xDD, 0xDD, 0x15, 0x15, 0x55};
const unsigned char AGID_V[1] = {	0x10};
const unsigned char CMDA4_V[9] = {	0x03, 0x00, 0x01, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00};
const unsigned char DCDC_V[3] = {	0x44, 0x54 ,0x00};
const unsigned char BTST_P_V[2] = {	0xE0, 0x20};
const unsigned char BOOST_VDDP_EN_V[1] = {	0x01};
const unsigned char BTST_N_V[2] = {	0xE0, 0x20};
const unsigned char BUCK_BOOST_VDDN_V[1] = {	0x01};
const unsigned char TFT_VCOM_POWER_V[1] = {	0x02};


    LOG_Purple("%s>%d",__func__,__LINE__);
    EPD_Reset();  
    EPD_Check_Busy();

    NT61522_ReadTemperature();    
    EPD_Reset();  
    EPD_Check_Busy();

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(AN_TM, AN_TM_V, sizeof(AN_TM_V));
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	spiTransmit(CMD66, CMD66_V, sizeof(CMD66_V));
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	spiTransmit(PSR, PSR_V, sizeof(PSR_V));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(DCDC, DCDC_V, sizeof(DCDC_V));
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	spiTransmit(PLL, PLL_V, sizeof(PLL_V));
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	spiTransmit(CDI, CDI_V, sizeof(CDI_V));
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	spiTransmit(TCON, TCON_V, sizeof(TCON_V));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(POFS, POFS_MV, sizeof(POFS_MV));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_SLAVE,GPIO_LOW);
	spiTransmit(POFS, POFS_SV, sizeof(POFS_SV));
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	spiTransmit(AGID, AGID_V, sizeof(AGID_V));
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	spiTransmit(PWS, PWS_V, sizeof(PWS_V));
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	spiTransmit(CCSET, CCSET_V, sizeof(CCSET_V));
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	spiTransmit(TRES, TRES_V, sizeof(TRES_V));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(CMDA4, CMDA4_V, sizeof(CMDA4_V));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(PWR, PWR_V, sizeof(PWR_V));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(EN_BUF, EN_BUF_V, sizeof(EN_BUF_V));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(BTST_P, BTST_P_V, sizeof(BTST_P_V));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(BOOST_VDDP_EN, BOOST_VDDP_EN_V, sizeof(BOOST_VDDP_EN_V));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(BTST_N, BTST_N_V, sizeof(BTST_N_V));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(BUCK_BOOST_VDDN, BUCK_BOOST_VDDN_V, sizeof(BUCK_BOOST_VDDN_V));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(TFT_VCOM_POWER, TFT_VCOM_POWER_V, sizeof(TFT_VCOM_POWER_V));
	setPinCsAll(GPIO_HIGH);
#endif    
}


void ePaperPort::NT61522_Display() {
    // if(Display_had_flag == 0xA2)
    // {
    //   Display_had_flag = 0xA3;  
    // }
    // if(Display_had_flag != 0xA3)
    // {      
    //   LOG_Purple("Display_had error A4 %s>%d",__func__,__LINE__);
    //   return;
    // }
#if (EPD_type_ ==  EPD_800_480)
    LOG_Purple("800 %s>%d",__func__,__LINE__);
    EPD_TurnOnDisplay();
#elif (EPD_type_ ==  EPD_1024_600)
    LOG_Purple("1024-600 %s>%d",__func__,__LINE__);

    EPD_Select_None();
     //=====================================================
    LOG_INFO("%s>%d", __func__, __LINE__);
    EPD_WriteCMD_ToBoth(0x04);
    delay_ms(100);
    EPD_Check_Busy();

    LOG_INFO("%s>%d", __func__, __LINE__);
    EPD_WriteCMD_ToBoth(0x12);
    EPD_WriteDATA_ToBoth(0x00);
    delay_ms(100);
    EPD_Check_Busy();

    LOG_INFO("%s>%d", __func__, __LINE__);
    EPD_WriteCMD_ToBoth(R02_POF);
    EPD_WriteDATA_ToBoth(0x00);
    delay_ms(100);
    EPD_Check_Busy();
#elif (EPD_type_ ==  EPD_1600_1200_133)
    LOG_Purple("<3> 1024-600 13.3 %s>%d",__func__,__LINE__);
    int64_t display_start_us = esp_timer_get_time();
    int64_t stage_start_us = display_start_us;
    auto wait_busy_assert = [this](const char *stage) -> bool {
        int64_t start_us = esp_timer_get_time();
        int start_level = Get_BusyIOLevel();
        int loops = 0;
        while (Get_BusyIOLevel() && loops < 100) {
            delayms(10);
            loops++;
        }
        int now_level = Get_BusyIOLevel();
        int elapsed_ms = (int)((esp_timer_get_time() - start_us) / 1000);
        if (now_level == 0) {
            ESP_LOGI(TAG,
                     "NT61522 busy asserted stage=%s start=%d now=%d loops=%d elapsed_ms=%d",
                     stage, start_level, now_level, loops, elapsed_ms);
            return true;
        }
        ESP_LOGW(TAG,
                 "NT61522 busy assert timeout stage=%s start=%d now=%d loops=%d elapsed_ms=%d",
                 stage, start_level, now_level, loops, elapsed_ms);
        return false;
    };

    setPinCsAll(GPIO_LOW);
    ESP_LOGI(TAG, "NT61522_Display stage PON start busy=%d", Get_BusyIOLevel());
	spiTransmitCommand(R04_PON);
	setPinCsAll(GPIO_HIGH);
	delayms(30);
	wait_busy_assert("PON");
	EPD_Check_Busy();
    ESP_LOGI(TAG, "NT61522_Display stage PON done elapsed_ms=%d busy=%d",
             (int)((esp_timer_get_time() - stage_start_us) / 1000), Get_BusyIOLevel());
	delayms(30);

    setPinCsAll(GPIO_LOW);
    stage_start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "NT61522_Display stage DRF start busy=%d", Get_BusyIOLevel());
	spiTransmit(R12_DRF,DRF_V,sizeof(DRF_V));
	setPinCsAll(GPIO_HIGH);
	delayms(30);
	wait_busy_assert("DRF");
	EPD_Check_Busy();
    ESP_LOGI(TAG, "NT61522_Display stage DRF done elapsed_ms=%d busy=%d",
             (int)((esp_timer_get_time() - stage_start_us) / 1000), Get_BusyIOLevel());
	delayms(30);
	setPinCsAll(GPIO_LOW);
    stage_start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "NT61522_Display stage POF start busy=%d", Get_BusyIOLevel());
	spiTransmit(R02_POF,POF_V,sizeof(POF_V));
	setPinCsAll(GPIO_HIGH);
	delayms(30);
	wait_busy_assert("POF");
	EPD_Check_Busy();
    ESP_LOGI(TAG, "NT61522_Display stage POF done elapsed_ms=%d total_ms=%d busy=%d",
             (int)((esp_timer_get_time() - stage_start_us) / 1000),
             (int)((esp_timer_get_time() - display_start_us) / 1000),
             Get_BusyIOLevel());
	delayms(30);


#else
    LOG_Purple("1600-7.09  %s>%d",__func__,__LINE__);
    printf("Write PON \r\n");

	setPinCsAll(GPIO_LOW);
	spiTransmitCommand(PON);
	delayms(30);
	EPD_Check_Busy();
	setPinCsAll(GPIO_HIGH);

	printf("Write DRF \r\n");
	setPinCsAll(GPIO_LOW);
	delayms(30);
	spiTransmit(DRF, DRF_V, sizeof(DRF_V));
	delayms(30);
	EPD_Check_Busy();
	setPinCsAll(GPIO_HIGH);

	printf("Write POF \r\n");
	setPinCsAll(GPIO_LOW);
	spiTransmit(POF, POF_V, sizeof(POF_V));
	delayms(30);
	EPD_Check_Busy();
	setPinCsAll(GPIO_HIGH);
	printf("Display Done!! \r\n");


    // LOG_Purple("1600-7.09  %s>%d",__func__,__LINE__);
    // setPinCsAll(0);
    // spiTransmitCommand(R04_PON);
    // EPD_Check_Busy();
    // setPinCsAll(1);
    // delayms(1);

    // ESP_LOGI(TAG, "NT61522_Display stage: R12_DRF start");
    // setPinCsAll(0);
    // delayms(30);
    // spiTransmit(R12_DRF, NT61522_DRF_V(), 1);
    // EPD_Check_Busy();
    // setPinCsAll(1);
    // delayms(1);
    // ESP_LOGI(TAG, "NT61522_Display stage: R12_DRF done");

    // setPinCsAll(0);
    // spiTransmit(R02_POF, NT61522_POF_V(), 1);
    // EPD_Check_Busy();
    // setPinCsAll(1);
    // delayms(1);
    // LOG_Purple("%s>%d over",__func__,__LINE__);
#endif    

#if 0
    LOG_Purple("Sleep-1 %s>%d",__func__,__LINE__);
    // Request Network_sleep_Task to enter sleep.
    // 通知 Network_sleep_Task 进入休眠流程。
    LOG_Purple("request sleep_group bit0 %s>%d",__func__,__LINE__);
    ESP_LOGI(TAG, "NT61522_Display request sleep_group bit0");
    xEventGroupSetBits(sleep_group, (EventBits_t)(1U << 0));//会导至 SLeep  
#endif
    EPD_sleep();  // 加上这个后， 要重新 init  ,否则不会刷图
    LOG_Purple("--over--");
}

static  uint32_t image_countger;
static  uint8_t  u8flag;
void ePaperPort::NT61522_Init_display()
{
    // if(Display_had_flag == 0xAA)
    // {
    //   Display_had_flag = 0xA1;  
    // }
    // if(Display_had_flag != 0xA1)
    // {      
    //   LOG_Purple("Display_had error A1 %s>%d",__func__,__LINE__);
    //   return;
    // }

    sleep_time=0;// delay network sleep time

#if (EPD_type_ ==  EPD_1024_600)
    LOG_Purple("1024-600 %s>%d",__func__,__LINE__);
    //EPD_WriteCMD_ToBoth(0x10);  	
    image_countger =0;
    u8flag = 0xAA;
    EPD_Select_Master();
    spiTransmitCommand(R10_DTM);  // 0x10
    Set_DCIOLevel(1);    

#elif (EPD_type_ ==  EPD_800_480)    
    LOG_Purple("800-480 %s>%d",__func__,__LINE__);
    EPD_Init();  

    EPD_SendCommand(0x10);

#elif (EPD_type_ ==  EPD_1600_1200_133)
	unsigned char temptr_fill = 0;
    unsigned char dataBuff[10];

    LOG_Purple("<1> 1600-1200 13.3 %s>%d",__func__,__LINE__);
    image_countger =0;
    u8flag = 0xAA;


	setPinCsAll(GPIO_LOW);
	spiTransmit(RE0_CCSET, CCSET_V_LOCK, sizeof(CCSET_V_LOCK));
	setPinCsAll(GPIO_HIGH);
	delayms(10);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmitCommand(R40_TSC);
	delayms(10);
	EPD_Check_Busy();
	spiReceiveData(&dataBuff[0], 2);
	setPinCs(TARGET_MASTER,GPIO_HIGH);
	delayms(30);
	EPD_Check_Busy();
	

    //Temptr[0] =  WHT20_Temp+10;
	temptr_fill = Temptr[0]<<1;
    LOG_Purple("%s>%d Bsptemp=%d =%d", __func__, __LINE__,Temptr[0],temptr_fill);
	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(RE5_TSSET, &temptr_fill, 1);
	setPinCs(TARGET_MASTER,GPIO_HIGH);
	delayms(30);

    EPD_Select_Master();
    spiTransmitCommand(R10_DTM);

#else   //  || (EPD_type_ ==  EPD_1600_1200_79)   
	unsigned char temptr_fill = 0;
    unsigned char dataBuff[10];

    LOG_Purple("<1> 1600-1200 13.3 %s>%d",__func__,__LINE__);
    image_countger =0;
    u8flag = 0xAA;


	setPinCsAll(GPIO_LOW);
	spiTransmit(RE0_CCSET, CCSET_V_LOCK, sizeof(CCSET_V_LOCK));
	setPinCsAll(GPIO_HIGH);
	delayms(10);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmitCommand(R40_TSC);
	delayms(10);
	EPD_Check_Busy();
	spiReceiveData(&dataBuff[0], 2);
	setPinCs(TARGET_MASTER,GPIO_HIGH);
	delayms(30);
	EPD_Check_Busy();
	

    //Temptr[0] =  WHT20_Temp+10;
    //Temptr[0] =  WHT20_Temp;
	//temptr_fill = Temptr[0]<<1;

    temptr_fill = Temptr[0]+5;
    temptr_fill = Temptr[0];
    temptr_fill = temptr_fill<<1;

    LOG_Purple("%s>%d Bsptemp=%d =%d", __func__, __LINE__,Temptr[0],temptr_fill);
	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(RE5_TSSET, &temptr_fill, 1);
	setPinCs(TARGET_MASTER,GPIO_HIGH);
	delayms(30);

    EPD_Select_Master();
    spiTransmitCommand(R10_DTM);    
#endif    
}
/* 中文注释：
   只保留一个 256 字节缓存
   不够 256 时先存起来
   一旦达到 256 就立刻发出去
*/
[[maybe_unused]] static uint8_t s_net_buf[256] = {0};

/* 中文注释：
   当前缓存里已有的有效字节数
*/
[[maybe_unused]] static size_t s_net_buf_len = 0;

/* 中文注释：
   控制发送目标轮流切换
   true  -> 下一包发给 A
   false -> 下一包发给 B
*/
[[maybe_unused]] static bool s_send_to_a = true;

void ePaperPort::NT61522_Display_net(const uint8_t *imageData, size_t imageSize)
{   
    // if(Display_had_flag == 0xA1)
    // {
    //   Display_had_flag = 0xA2;  
    // }
    // if(Display_had_flag != 0xA2)
    // {      
    //   LOG_Purple("Display_had error A2 %s>%d",__func__,__LINE__);
    //   return;
    // }
#if (EPD_type_ ==  EPD_800_480) 
    LOG_Purple("800x480 %s>%d",__func__,__LINE__);
    //EPD_SendCommand(0x10);
    EPD_Sendbuffera(DispBuffer, DisplayLen);
    //EPD_TurnOnDisplay();

#elif (EPD_type_ ==  EPD_1360_480_1085)     
    uint32_t i = 0;
    uint8_t j = 0;
    spi_transaction_t t;

    EPD_WriteCMD(0xA2);	//********************
    EPD_WriteDATA(0x01);		
    EPD_WriteCMD(0x10);

    for (i = 0; i < (uint32_t)imageSize; i += 256) { // ALLSCREEN_BYTES =  DisplayLen
        if (j == 0) {
            EPD_Select_Master();  
            memset(&t, 0, sizeof(t));
            t.length    = 8 * 256;
            t.tx_buffer = imageData + i;
            spi_device_polling_transmit(spi, &t); //Transmit!
            j = 1;
        } else {
            EPD_Select_Slave();
            memset(&t, 0, sizeof(t));
            t.length    = 8 * 256;
            t.tx_buffer = imageData + i;
            spi_device_polling_transmit(spi, &t); //Transmit!
            j = 0;
        }
    }
#elif (EPD_type_ ==  EPD_800_480_4s_75)     
    uint32_t i = 0;
    uint8_t j = 0;
    spi_transaction_t t;

    EPD_WriteCMD(0x10);
    for (i = 0; i < (uint32_t)imageSize; i += 256) { // ALLSCREEN_BYTES =  DisplayLen
        if (j == 0) {
            EPD_Select_Master();  
            memset(&t, 0, sizeof(t));
            t.length    = 8 * 256;
            t.tx_buffer = imageData + i;
            spi_device_polling_transmit(spi, &t); //Transmit!
            j = 1;
        } else {
            EPD_Select_Slave();
            memset(&t, 0, sizeof(t));
            t.length    = 8 * 256;
            t.tx_buffer = imageData + i;
            spi_device_polling_transmit(spi, &t); //Transmit!
            j = 0;
        }
    }
#elif (EPD_type_ ==  EPD_1024_600) 
     #define  max_l_dat  300   
     uint32_t u32posi;

    /* 中文注释：       主屏区域总长度边界       0 ~ (1600*300 - 1) 属于 MASTER       >= 1600*300 属于 SLAVE    */
    const uint32_t master_limit = 1024U * 150U;//=307200;
    /* 中文注释：       参数保护       按你的描述 imageSize 范围为 1 ~ 900    */
    if (imageSize == 0) {
        LOG_Cyan("%s>%d error",__func__,__LINE__);
        return;
    }
    if(image_countger >=307200U){
        LOG_Cyan("%s>%d full %ld",__func__,__LINE__,image_countger);
        return;
    }

   //  LOG_Cyan("imageSize=%d",imageSize);

    size_t remain = imageSize;
    u32posi = 0;

    // memset(u8dat, epd_black,max_l_dat);
    while (remain > 0) {
        /* 中文注释：           单次最多处理 300        */
        uint32_t chunk = (remain > 300) ? 300U : (uint32_t)remain;
        /* 中文注释：           情况1：当前还在 MASTER 区域        */

        //LOG_Cyan("image_countger %ld, master_limit %ld",image_countger, master_limit);
        if (image_countger < master_limit) {
            /* 中文注释：               计算 MASTER 区域还剩多少空间            */
            uint32_t master_remain = master_limit - image_countger;

            if (chunk <= master_remain) {
                /* 中文注释：                   当前这一段完全落在 MASTER 区域                */
               // printf("M1:chunk=%ld,image_countger=%ld\r\n",chunk,image_countger);
                //setPinCs(TARGET_MASTER, chunk);
                EPD_Select_Master();
                //spiTransmitCommand(R10_DTM);
                spiTransmitData(imageData+u32posi, chunk);
                //spiTransmitData(u8dat, chunk);
                u32posi = u32posi + chunk;  
                //setPinCs(TARGET_MASTER, 1);
                //delayms(30);

                image_countger += chunk;
                remain -= chunk;
            } else {
                /* 中文注释：                   当前这一段跨越了 MASTER -> SLAVE 边界                   先发送 MASTER 剩余部分                */
                if (master_remain > 0U) {
                    // printf("M2:master_remain=%ld,image_countger=%ld\r\n",master_remain,image_countger);
                    //setPinCs(TARGET_MASTER, master_remain);
                    EPD_Select_Master();
                    //spiTransmitCommand(R10_DTM);
                    spiTransmitData(imageData+u32posi, master_remain);
                    //spiTransmitData(u8dat, master_remain);
                    //setPinCsAll(1);
                    u32posi = u32posi + master_remain;  
                    //setPinCs(TARGET_MASTER, 1);
                    //delayms(30);

                    image_countger += master_remain;
                    remain -= master_remain;
                }

                /* 中文注释：                   再发送本次 chunk 剩余的部分到 SLAVE
                   这里不能等下一轮 outer while，                   要在本轮把 chunk 的剩余部分处理掉
                */
                {
                    uint32_t slave_part = chunk - master_remain;
                    if (slave_part > 0U) {
                       // printf("S2:slave_part=%ld,image_countger=%ld\r\n",slave_part,image_countger);
                        //setPinCs(TARGET_SLAVE, slave_part);
                        EPD_Select_Slave();
                        if (u8flag == 0xAA)
                        spiTransmitCommand(R10_DTM);
                        {
                            u8flag = 0;
                            printf("----S:spiTransmitCommand(R10_DTM);\r\n");
                        }                        
                        spiTransmitData(imageData+u32posi, slave_part);
                        //spiTransmitData(u8dat, slave_part);
                        u32posi = u32posi + slave_part;  
                        //setPinCs(TARGET_SLAVE, 1);
                        //delayms(30);

                        image_countger += slave_part;
                        remain -= slave_part;
                    }
                }
            }
        } else {
            /* 中文注释：               情况2：当前已经在 SLAVE 区域            */
            //printf("S1:chunk=%ld,image_countger=%ld\r\n",chunk,image_countger);
            //setPinCs(TARGET_SLAVE, chunk);
            EPD_Select_Slave();
            if (u8flag == 0xAA)
            {
             printf("--xx--S:spiTransmitCommand(R10_DTM);\r\n");   
             spiTransmitCommand(R10_DTM);
             u8flag = 0;
            }                        
            spiTransmitData(imageData+u32posi, chunk);
            //spiTransmitData(u8dat, chunk);

            u32posi = u32posi + chunk;  
            //setPinCs(TARGET_SLAVE, 1);
            //delayms(30);

            image_countger += chunk;
            remain -= chunk;
        }
    }


  #if 0
    spi_transaction_t t;       
    // LOG_Purple("1024x600 %s>%d  DisplayLen=%d",__func__,__LINE__,DisplayLen);
    
    /* 中文注释：       参数保护    */
    if (imageData == nullptr || imageSize == 0)
    {
        return;
    }

    /* 中文注释：
       统计总输入字节数
    */
    if(image_countger >=(1024*300))
    {
        printf("Full-307200 er L=%ld =%d =%d\n",image_countger,imageSize,s_net_buf_len);
        return;
    }

   // printf("\r\n L=%ld =%d  =%d \r\n",image_countger,imageSize,s_net_buf_len+imageSize);
    //image_countger += (uint32_t)imageSize;

    /* 中文注释：
       src_index 表示当前处理到输入数据的哪个位置
    */
    size_t src_index = 0;
    
    while (src_index < imageSize)
    {
        /* 中文注释：
           先计算缓存区还剩多少空间
        */
        size_t free_space = 256 - s_net_buf_len;

        /* 中文注释：
           本轮最多拷贝的数据量
           要么填满缓存，要么把剩余输入拷完
        */
        size_t copy_len = imageSize - src_index;
        if (copy_len > free_space)
        {
            copy_len = free_space;
        }

        /* 中文注释：
           把输入数据拷贝到缓存尾部
        */
        memcpy(&s_net_buf[s_net_buf_len], &imageData[src_index], copy_len);
        s_net_buf_len += copy_len;
        src_index += copy_len;

        /* 中文注释：
           只要缓存满 256，就立刻发送
        */
        if (s_net_buf_len >= 256)
        {
            if (s_send_to_a)
            {                
               // printf("M> ");
               // memset(s_net_buf, epd_red,256);

                EPD_Select_Master();  
                memset(&t, 0, sizeof(t));
                t.length    = 8*256;
                t.tx_buffer = s_net_buf;
                spi_device_polling_transmit(spi, &t); //Transmit!
            }
            else
            {
                //printf("S> ");
                //memset(s_net_buf, epd_red,256);

                EPD_Select_Slave();
                memset(&t, 0, sizeof(t));
                t.length    = 8*256;
                t.tx_buffer = s_net_buf;            
                spi_device_polling_transmit(spi, &t); //Transmit!
            }

            image_countger += 256;
            if(image_countger >=(1024*300))
            {
                printf("Fullxxx-307200 er L=%ld =%d\n",image_countger,imageSize);
                return;
            }
            // else
            // {
            //   printf("L=%ld\r\n",image_countger);                
            // }

            /* 中文注释：
               一包发完后，缓存清空
               因为这里设计成“凑满 256 就立即发”
            */
            s_net_buf_len = 0;

            /* 中文注释：
               下一包切换到另一边
            */
            s_send_to_a = !s_send_to_a;
        }
    }

 //   printf("over %d\r\n",s_net_buf_len);                

    /* 中文注释：
       如果函数结束时 s_net_buf_len < 256
       说明还有残留数据，继续保留，等下次再凑满 256 后发送
    */
   #endif
#else    // 1600 1200
    // (void)imageData;
     //#define  max_l_dat  300   
     //uint8_t u8dat[max_l_dat];

     uint32_t u32posi;

#if 0
     uint32_t total;
     uint8_t Data_Buffer[NT61522_BUFFER_SIZE];
    LOG_Purple("%s>%d",__func__,__LINE__);   
    //=======================Master==================================
    memset(Data_Buffer, epd_yellow, NT61522_BUFFER_SIZE);
    EPD_Select_Master();
    spiTransmitCommand(R10_DTM);
    total = 480000;
    while (total >= NT61522_BUFFER_SIZE) {
        EPD_Select_Master();
        spiTransmitData(Data_Buffer, NT61522_BUFFER_SIZE);
        total -= NT61522_BUFFER_SIZE;
    }
    if (total > 0) {
        spiTransmitData(Data_Buffer, total);
    }
    //=========================================================
    LOG_Purple("%s>%d",__func__,__LINE__);
    //=================Slaver========================================
    memset(Data_Buffer, epd_red, NT61522_BUFFER_SIZE);
    EPD_Select_Slave();
    spiTransmitCommand(R10_DTM);
    total = 480000;
    while (total >= NT61522_BUFFER_SIZE) {
        EPD_Select_Slave();
        spiTransmitData(Data_Buffer, NT61522_BUFFER_SIZE);
        total -= NT61522_BUFFER_SIZE;
    }
    if (total > 0) {
        spiTransmitData(Data_Buffer, total);
    }
    //=========================================================
#endif

#if 1
    /* 中文注释：       主屏区域总长度边界       0 ~ (1600*300 - 1) 属于 MASTER       >= 1600*300 属于 SLAVE    */
    const uint32_t master_limit = 1600U * 300U;//=480000;
    /* 中文注释：       参数保护       按你的描述 imageSize 范围为 1 ~ 900    */
    if (imageSize == 0) {
        LOG_Cyan("%s>%d error",__func__,__LINE__);
        return;
    }
    if(image_countger >=(480000+480000)){
        LOG_Cyan("%s>%d full %ld",__func__,__LINE__,image_countger);
        return;
    }

     LOG_Cyan("%s<%d> imageSize=%d",__func__,__LINE__,imageSize);

    size_t remain = imageSize;
    u32posi = 0;
    // memset(u8dat, epd_black,max_l_dat);
    while (remain > 0) {
        /* 中文注释：           单次最多处理 300        */
        uint32_t chunk = (remain > 300) ? 300U : (uint32_t)remain;
        /* 中文注释：           情况1：当前还在 MASTER 区域        */

        //LOG_Cyan("image_countger %ld, master_limit %ld",image_countger, master_limit);
        if (image_countger < master_limit) {
            /* 中文注释：               计算 MASTER 区域还剩多少空间            */
            uint32_t master_remain = master_limit - image_countger;

            if (chunk <= master_remain) {
                /* 中文注释：                   当前这一段完全落在 MASTER 区域                */
               // printf("M1:chunk=%ld,image_countger=%ld\r\n",chunk,image_countger);
                //setPinCs(TARGET_MASTER, chunk);
                EPD_Select_Master();
                //spiTransmitCommand(R10_DTM);
                spiTransmitData(imageData+u32posi, chunk);
                //spiTransmitData(u8dat, chunk);
                u32posi = u32posi + chunk;  
                //setPinCs(TARGET_MASTER, 1);
                //delayms(30);

                image_countger += chunk;
                remain -= chunk;
            } else {
                /* 中文注释：                   当前这一段跨越了 MASTER -> SLAVE 边界                   先发送 MASTER 剩余部分                */
                if (master_remain > 0U) {
                    // printf("M2:master_remain=%ld,image_countger=%ld\r\n",master_remain,image_countger);
                    //setPinCs(TARGET_MASTER, master_remain);
                    EPD_Select_Master();
                    //spiTransmitCommand(R10_DTM);
                    spiTransmitData(imageData+u32posi, master_remain);
                    //spiTransmitData(u8dat, master_remain);
                    //setPinCsAll(1);
                    u32posi = u32posi + master_remain;  
                    //setPinCs(TARGET_MASTER, 1);
                    //delayms(30);

                    image_countger += master_remain;
                    remain -= master_remain;
                }

                /* 中文注释：                   再发送本次 chunk 剩余的部分到 SLAVE
                   这里不能等下一轮 outer while，                   要在本轮把 chunk 的剩余部分处理掉
                */
                {
                    uint32_t slave_part = chunk - master_remain;
                    if (slave_part > 0U) {
                       // printf("S2:slave_part=%ld,image_countger=%ld\r\n",slave_part,image_countger);
                        //setPinCs(TARGET_SLAVE, slave_part);
                        EPD_Select_Slave();
                        if (u8flag == 0xAA)
                        {
                            spiTransmitCommand(R10_DTM);
                            u8flag = 0;
                            printf("----S:spiTransmitCommand(R10_DTM);\r\n");
                        }                        
                        spiTransmitData(imageData+u32posi, slave_part);
                        //spiTransmitData(u8dat, slave_part);
                        u32posi = u32posi + slave_part;  
                        //setPinCs(TARGET_SLAVE, 1);
                        //delayms(30);

                        image_countger += slave_part;
                        remain -= slave_part;
                    }
                }
            }
        } else {
            /* 中文注释：               情况2：当前已经在 SLAVE 区域            */
            //printf("S1:chunk=%ld,image_countger=%ld\r\n",chunk,image_countger);
            //setPinCs(TARGET_SLAVE, chunk);
            EPD_Select_Slave();
            if (u8flag == 0xAA)
            {
             printf("--xx--S:spiTransmitCommand(R10_DTM);\r\n");   
             spiTransmitCommand(R10_DTM);
             u8flag = 0;
            }                        
            spiTransmitData(imageData+u32posi, chunk);
            //spiTransmitData(u8dat, chunk);

            u32posi = u32posi + chunk;  
            //setPinCs(TARGET_SLAVE, 1);
            //delayms(30);

            image_countger += chunk;
            remain -= chunk;
        }
    }
#endif    
#endif
}

void ePaperPort::NT61522_DisplayImage(const uint8_t *imageData, size_t imageSize) {
    uint32_t i;

    if (imageData == nullptr || imageSize == 0) {
        LOG_Purple("%s>%d--- er",__func__,__LINE__);
        return;
    }

    LOG_Purple("%s>%d",__func__,__LINE__);
    printf("imageData=%p imageSize=%u", imageData, (unsigned int)imageSize);
    if (imageSize != 960000) {
        ESP_LOGW(TAG, "NT61522_DisplayImage unexpected imageSize=%u, expect=960000", (unsigned int)imageSize);
    }

    ESP_LOGI(TAG, "NT61522_DisplayImage force EPD_Init before image write, old isEPDInit=%d", isEPDInit ? 1 : 0);
    isEPDInit = false;
    EPD_Init();
    NT61522_Init_display();
    //EPD_Select_Master();
    //spiTransmitCommand(R10_DTM);
    uint32_t offset;

#if 1
    ESP_LOGI(TAG, "NT61522 master transmit start");
    offset=0;
    for (i = 0; i < 1600; ++i) {
        spiTransmitData(imageData + offset, 300);
        //memset(Data_Buffer, 0x11,300);        
        //spiTransmitData(Data_Buffer, 300);
        offset += 300;
    }

    EPD_Select_Slave();
    ESP_LOGI(TAG, "NT61522 slave transmit start");    
    spiTransmitCommand(R10_DTM);
    for (i = 0; i < 1600; ++i) {
        spiTransmitData(imageData + offset, 300);
        //memset(Data_Buffer, 0x33,300);        
        //spiTransmitData(Data_Buffer, 300);
        offset += 300;
    }
#endif

    // memset(Data_Buffer, 0x11,75);        
    // memset(Data_Buffer+75, 0x22,75);                
    // memset(Data_Buffer+75+75, 0x33,75);                
    // memset(Data_Buffer+75+75+75, 0x55,75);                


    // setPinCs(TARGET_MASTER, 0);
    // spiTransmitCommand(R10_DTM);
    // ESP_LOGI(TAG, "NT61522 master transmit start");
    // offset=0;
    // j=0;
    // for (size_t i = 0; i < 1600; ++i) {
    //      spiTransmitData(imageData + offset, 300);
    //     offset += 300;
    // }
    // setPinCs(TARGET_MASTER, 1);
    // delayms(30);
    // ESP_LOGI(TAG, "NT61522 master transmit done, offset=%u", (unsigned int)offset);

    // setPinCs(TARGET_SLAVE, 0);
    // spiTransmitCommand(R10_DTM);
    // ESP_LOGI(TAG, "NT61522 slave transmit start");
    // for (size_t i = 0; i < 1600; ++i) {
    //     spiTransmitData(imageData + offset, 300);
    //     offset += 300;
    // }
    // setPinCs(TARGET_SLAVE, 1);
    // delayms(30);
    // ESP_LOGI(TAG, "NT61522 slave transmit done, offset=%u", (unsigned int)offset);

    setPinCsAll(1);   
    NT61522_Display();

    ESP_LOGI(TAG, "NT61522_DisplayImage end");
    LOG_Purple("------------------%s>%d",__func__,__LINE__);
}
// 、、、、、、、、、、、、、、、、、、、、
