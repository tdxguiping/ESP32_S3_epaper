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
uint8_t  Hardware_Version_ = 1;

//extern uint8_t  WHT20_Temp;
extern uint16_t sleep_time; 
extern EventGroupHandle_t sleep_group;
bool isEPDInit = false;

#define BLOCK_SIZE 256
static ePaperPort *s_global_epaper_instance = nullptr;
void SetGlobalEPaperInstance(ePaperPort *instance) {
    s_global_epaper_instance = instance;
}
ePaperPort *GetGlobalEPaperInstance() {
    return s_global_epaper_instance;
}

void ePaperPort::Set_EPD_type(uint8_t type)
{
    const epd_type_config_t *config = EpdType_GetConfig(type);
    if (config == nullptr) {
        ESP_LOGE(TAG, "invalid EPD type=%u", (unsigned int)type);
        return;
    }
    if (epd_type_ == type) {
        return;
    }

    ReleaseDispBuffer();
    ReleaseRotationBuffer();
    epd_type_ = type;
    width_ = config->width;
    height_ = config->height;
    DisplayLen = (int)config->display_size;
    isEPDInit = false;
    ESP_LOGI(TAG, "EPD config type=%u name=%s width=%u height=%u size=%u",
             (unsigned int)type,
             config->name,
             (unsigned int)width_,
             (unsigned int)height_,
             (unsigned int)DisplayLen);
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
    
    DisplayLen = transfer / 2;

    buscfg.miso_io_num = USER_EPD_MISO_PIN;
    buscfg.mosi_io_num = mosi_;
    buscfg.sclk_io_num = scl_;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = NT61522_SPI_MAX_BUFFER_SIZE;

    spi_device_interface_config_t devcfg = {};
    memset(&devcfg, 0, sizeof(devcfg));
    devcfg.spics_io_num = -1;    
    // devcfg.clock_speed_hz = 40 * 1000 * 1000;   // 40MHz is ok
    //devcfg.clock_speed_hz = 5 * 1000 * 1000;
    //devcfg.clock_speed_hz = 10 * 1000 * 1000;
    devcfg.clock_speed_hz = 20 * 1000 * 1000;


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

    // Initialize the shared C5 SPI bus with SD MISO present so SDSPI can reuse the same bus later.
    // 初始化 C5 共用 SPI 总线时带上 SD MISO，便于后续 SDSPI 复用同一总线。
    ESP_LOGI(TAG, "EPD SPI bus init host=%d mosi=%d miso=%d sck=%d cs=%d cs2=%d",
             (int)spi_host_,
             mosi_,
             USER_EPD_MISO_PIN,
             scl_,
             cs_,
             cs_2_);
    ret = spi_bus_initialize(spi_host_, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);
    ret = spi_bus_add_device(spi_host_, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);

    Hardware_Version_ = (EPD_type == EPD_TYPE_800_480_4S_75 ||
                         EPD_type == EPD_TYPE_800_480_4S_75_2 ||
                         EPD_type == EPD_TYPE_800_480_4S_75_3) ? 2U : 1U;
    gpio_config_t gpio_conf = {};
    if (Hardware_Version_ == 2) {
        gpio_conf.intr_type = GPIO_INTR_DISABLE;
        gpio_conf.mode = GPIO_MODE_OUTPUT;
        gpio_conf.pin_bit_mask = (0x1ULL << rst_) | (0x1ULL << dc_) | (0x1ULL << cs_) | (0x1ULL << cs_2_) | (0x1ULL << EPD2_DC_PIN) | (0x1ULL << EPD2_CS_PIN) | (0x1ULL << EPD2_RST_PIN) | (0x1ULL << EPD_Power_PIN);
        gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

        gpio_conf.intr_type = GPIO_INTR_DISABLE;
        gpio_conf.mode = GPIO_MODE_INPUT;
        gpio_conf.pin_bit_mask = (0x1ULL << busy_) | (0x1ULL << EPD2_BUSY_PIN);
        gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

        gpio_set_level((gpio_num_t)EPD2_RST_PIN, 1);
        gpio_set_level((gpio_num_t)EPD2_DC_PIN, 0);
        gpio_set_level((gpio_num_t)EPD2_CS_PIN, 1);
    } else {
        gpio_conf.intr_type = GPIO_INTR_DISABLE;
        gpio_conf.mode = GPIO_MODE_OUTPUT;
        gpio_conf.pin_bit_mask = (0x1ULL << rst_) | (0x1ULL << dc_) | (0x1ULL << cs_) | (0x1ULL << cs_2_) | (0x1ULL << EPD_Power_PIN);
        gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

        gpio_conf.intr_type = GPIO_INTR_DISABLE;
        gpio_conf.mode = GPIO_MODE_INPUT;
        gpio_conf.pin_bit_mask = (0x1ULL << busy_);
        gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));
    }
    ESP_LOGI(TAG, "EPD hardware version=%u", (unsigned int)Hardware_Version_);

    Set_Power(0);
    
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
    return true;
}

void ePaperPort::ReleaseDispBuffer() {
    if (DispBuffer != nullptr) {
        heap_caps_free(DispBuffer);
        DispBuffer = nullptr;
        DispBufferCapacity_ = 0;
    }
}

void ePaperPort::ReleaseRotationBuffer() {
    if (RotationBuffer != nullptr) {
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

void ePaperPort::Set_Power(uint8_t Power_switch) {
    gpio_config_t io_conf = {};
    // gpio_set_level((gpio_num_t)EPD_Power_PIN, Power_switch ? 1 : 0);
    //ESP_LOGI(TAG, "EPD power=%u", (unsigned int)(Power_switch ? 1 : 0));

    // io_conf.pin_bit_mask = (1ULL << EPD_CS_PIN_2) |
    //                        (1ULL << EPD_DC_PIN) |
    //                        (1ULL << EPD_CS_PIN) |
    //                        (1ULL << EPD_RST_PIN);

    io_conf.pin_bit_mask = (1ULL << EPD_RST_PIN);
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    if(Power_switch == 1)
    {
        ESP_LOGI(TAG, "----------EPD power on");
        gpio_set_level((gpio_num_t)EPD_Power_PIN, 1);
        io_conf.mode = GPIO_MODE_OUTPUT;
    }
    else
    {
        ESP_LOGI(TAG, "---------EPD power off");
        gpio_set_level((gpio_num_t)EPD_Power_PIN, 1);
        io_conf.mode = GPIO_MODE_DISABLE;
    }    

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed ret=%s", esp_err_to_name(ret));
        return;
    }

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
        gpio_set_level((gpio_num_t)EPD2_CS_PIN,1); //  确保另一个屏幕不被选中
    }
    else
    {
        gpio_set_level((gpio_num_t)EPD2_CS_PIN, level ? 1 : 0);
        gpio_set_level((gpio_num_t)cs_,1); //  确保另一个屏幕不被选中
    }    
}

void ePaperPort::Set_CS2IOLevel(uint8_t level) {
    if(EPD_which_one_ == 1)
    {
        gpio_set_level((gpio_num_t)cs_2_, level ? 1 : 0);
    }   
    else
    {
        (void)level;
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

    Set_Power(1);
    // gpio_set_level((gpio_num_t)cs_,1);
    // gpio_set_level((gpio_num_t)cs_2_,1);
    // if (EPD_which_one_ == 2) {
    //     gpio_set_level((gpio_num_t)EPD2_CS_PIN,1);
    // }   

    Set_ResetIOLevel(0);
    vTaskDelay(pdMS_TO_TICKS(100)); //100
    Set_ResetIOLevel(1);
    vTaskDelay(pdMS_TO_TICKS(100));  //100      
}

void ePaperPort::EPD_LoopBusy(uint16_t loop_counter) {
    int16_t count;
    count=0;
    while (1) {
        if (Get_BusyIOLevel()) {
            printf("Check Busy over\r\n");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); //  1000ms = 1s
        count++;
        printf(".%d.",count);
        if (count > loop_counter)
        {
            ESP_LOGE(TAG, "EPD busy timeout");
            EpdType_ReportDisplayFailure(ESP_ERR_TIMEOUT);
            return;
        }
    }
}

void ePaperPort::EPD_Check_Busy(uint16_t loop_counter) { // If BUSYN=0 then waiting
    int16_t i;

    int64_t start_us = esp_timer_get_time();
    i=1;    
    while (1) {
        int level = Get_BusyIOLevel();
        if (level) {
            printf("Check Busy over\r\n");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1000ms    
        i++;
        printf(".%d.",i);

        if (i > loop_counter) {
            int elapsed_ms = (int)((esp_timer_get_time() - start_us) / 1000);
            ESP_LOGE(TAG, "EPD-com busy timeout level=%d loops=%ld elapsed_ms=%d",
                     Get_BusyIOLevel(), (long)i, elapsed_ms);
            EpdType_ReportDisplayFailure(ESP_ERR_TIMEOUT);
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

     gpio_set_level((gpio_num_t)cs_,1);
     gpio_set_level((gpio_num_t)cs_2_,1);
     if (EPD_which_one_ == 2) {
     gpio_set_level((gpio_num_t)EPD2_CS_PIN,1);
     }    
}

void ePaperPort::EPD_Select_Master(void) {
    Set_CSIOLevel(0);
    Set_CS2IOLevel(1);

    //gpio_set_level((gpio_num_t)cs_,0);  // for master
    //gpio_set_level((gpio_num_t)cs_2_,1);  // for salve
}

void ePaperPort::EPD_Select_Slave(void) {
    Set_CSIOLevel(1);
    Set_CS2IOLevel(0);
    //gpio_set_level((gpio_num_t)cs_,1);  // for master
    //gpio_set_level((gpio_num_t)cs_2_,0);  // for salve    
}

void ePaperPort::EPD_Select_Both(void) {
    Set_CSIOLevel(0);
    Set_CS2IOLevel(0);
    //gpio_set_level((gpio_num_t)cs_,0);  // for master
    //gpio_set_level((gpio_num_t)cs_2_,0);  // for salve    
}

void ePaperPort::EPD_interface_init(void) {
    Set_ResetIOLevel(0);
    Set_DCIOLevel(0);

    //Set_CSIOLevel(1);
    //Set_CS2IOLevel(1);
    gpio_set_level((gpio_num_t)cs_,1);  // for master
    gpio_set_level((gpio_num_t)cs_2_,1);  // for salve    
    if (EPD_which_one_ == 2) {
        gpio_set_level((gpio_num_t)EPD2_CS_PIN,1);
    }
    EPD_Select_None();
}

void ePaperPort::E_SDI_IN(void) {
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






void ePaperPort::EPD_Sendbuffera(uint8_t *Data, uint16_t len) {
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
    EPD_SendCommand(0x04);
    EPD_LoopBusy(23);
    EPD_SendCommand(0x06);
    EPD_SendData(0x6F);
    EPD_SendData(0x1F);
    EPD_SendData(0x17);
    EPD_SendData(0x49);
    EPD_SendCommand(0x12);
    EPD_SendData(0x00);
    EPD_LoopBusy(23);
    EPD_SendCommand(0x02);
    EPD_SendData(0x00);
    EPD_LoopBusy(23);
}

void ePaperPort::EPD_sleep(void) {
   isEPDInit = false;
   EpdType_DispatchSleep(*this);
}

void ePaperPort::EPD_refresh(void) {
    LOG_Purple("%s>%d",__func__,__LINE__);

    EPD_WriteCMD(0x04);
    delay_ms(30);
    EPD_Check_Busy(24);
    EPD_WriteCMD(0x12);
    delay_ms(30);
    EPD_Check_Busy(24);
}

void ePaperPort::EPD_refresh_17H(void) {
    LOG_Purple("%s>%d",__func__,__LINE__);

    EPD_WriteCMD(0x17);
    EPD_WriteDATA(0xA5);
    delay_ms(30);
    EPD_Check_Busy(24);
}

void ePaperPort::Set_Rotation(uint8_t rot) {
    Rotation = rot;
}

void ePaperPort::Set_Mirror(uint8_t mirr_x, uint8_t mirr_y) {
    mirrx = mirr_x;
    mirry = mirr_y;
}

void ePaperPort::EPD_Init() {   
    int64_t start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "EPD step EPD_Init start");

    // if (isEPDInit) {
    //     ESP_LOGI(TAG, "EPD step EPD_Init reused elapsed_ms=%lld",
    //              (long long)((esp_timer_get_time() - start_us) / 1000));
    //     // return;  // force re-init for each EPD_Init call, to ensure the display is always in a known state.
    // }
    //isEPDInit = true;    

    EpdType_DispatchInit(*this);

    ESP_LOGI(TAG, "EPD step EPD_Init done elapsed_ms=%lld ,target=%d",
             (long long)((esp_timer_get_time() - start_us) / 1000),EPD_which_one_);
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
EpdType_DispatchDisplay(*this);
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
        if (width_==1024)
        {
         //EPD_Rotate90CCW_Fast(DispBuffer, RotationBuffer, 600, 1024);     
         EPD_Rotate180_Fast(DispBuffer, RotationBuffer, width_, height_);
        }
        else
        {
         //EPD_Rotate90CCW_Fast(DispBuffer,RotationBuffer,480,800);
         EPD_Rotate180_Fast(DispBuffer,RotationBuffer,800,480);
        }       
    } else if (Rotation == 1) {
        EPD_Rotate90CW_Fast(DispBuffer,RotationBuffer,480,800);
    } else if (Rotation == 2) {
        EPD_Rotate180_Fast(DispBuffer,RotationBuffer,800,480);
    } else {
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
    Set_DCIOLevel(1);delay_us(1);  // fro data

    if (dataBuffer == nullptr || dataLength == 0) return ESP_OK;


    //LOG_Purple("%s>%d L=%d",__func__,__LINE__,dataLength);
    constexpr size_t kSafeDmaTxChunk = 4092U;
    const uint8_t *ptr = dataBuffer;
    size_t remaining = dataLength;
    while (remaining > 0) {
           size_t chunk = remaining > kSafeDmaTxChunk ? kSafeDmaTxChunk : remaining;
           spi_transaction_t t;
           memset(&t, 0, sizeof(t));
           t.length    = 8 * chunk;
           t.tx_buffer = ptr;
           esp_err_t ret = spi_device_polling_transmit(spi, &t); //Transmit!
           if (ret != ESP_OK) {
               ESP_LOGE(TAG, "spiTransmitData failed, chunk=%u remaining=%u total=%u err=%s dma_free=%u dma_largest=%u internal_free=%u",
                        (unsigned int)chunk,
                        (unsigned int)remaining,
                        (unsigned int)dataLength,
                        esp_err_to_name(ret),
                        (unsigned int)heap_caps_get_free_size(MALLOC_CAP_DMA),
                        (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
                        (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
               return ret;
           }
           ptr += chunk;
           remaining -= chunk;
    }
    return ESP_OK;

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

    ESP_LOGI(TAG, "spiReceiveData: end len=%u, data[0]=0x%02X, data[1]=0x%02X",
        dataLength > 0 ? dataBuffer[0] : 0,
        (unsigned int)dataLength,
             dataLength > 1 ? dataBuffer[1] : 0);

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




// 0B 07  01/02/03 
void ePaperPort::NT61522_ReadRevision() {
    //LOG_Purple("%s>%d",__func__,__LINE__);

    memset(nt61522_chip_id_, 0, sizeof(nt61522_chip_id_));
    setPinCs(TARGET_MASTER, 0);
    spiTransmitCommand(0x70);
    spiReceiveData(nt61522_chip_id_, sizeof(nt61522_chip_id_));
    setPinCs(TARGET_MASTER, 1);
    EPD_Check_Busy(1);

    memset(nt61522_chip_id_, 0, sizeof(nt61522_chip_id_));
    setPinCs(TARGET_SLAVE, 0);
    spiTransmitCommand(0x70);
    spiReceiveData(nt61522_chip_id_, sizeof(nt61522_chip_id_));
    setPinCs(TARGET_SLAVE, 1);
    EPD_Check_Busy(1);
}

void ePaperPort::NT61522_Init() {
EpdType_DispatchNT61522Init(*this);
}


void ePaperPort::NT61522_Display() {
    int64_t start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "EPD step NT61522_Display start");    


    EpdType_DispatchNT61522Display(*this);

#if 0
    LOG_Purple("Sleep-1 %s>%d",__func__,__LINE__);
    // Request Network_sleep_Task to enter sleep.
    // 通知 Network_sleep_Task 进入休眠流程。
    LOG_Purple("request sleep_group bit0 %s>%d",__func__,__LINE__);
    ESP_LOGI(TAG, "NT61522_Display request sleep_group bit0");
    xEventGroupSetBits(sleep_group, (EventBits_t)(1U << 0));//会导至 SLeep  
#endif
    EPD_sleep();  // 加上这个后， 要重新 init  ,否则不会刷图
    ESP_LOGI(TAG, "EPD step NT61522_Display done elapsed_ms=%lld target=%d",
             (long long)((esp_timer_get_time() - start_us) / 1000),EPD_which_one_);
}

void ePaperPort::NT61522_Init_display()
{
    int64_t start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "EPD step NT61522_Init_display start");
    sleep_time=0;// delay network sleep time

     EpdType_DispatchNT61522InitDisplay(*this);

     ESP_LOGI(TAG, "EPD step NT61522_Init_display done elapsed_ms=%lld  target=%d",
             (long long)((esp_timer_get_time() - start_us) / 1000),EPD_which_one_);
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
    int64_t start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "EPD step NT61522_Display_net start size=%u", (unsigned int)imageSize);

    EpdType_DispatchNT61522DisplayNet(*this, imageData, imageSize);

    ESP_LOGI(TAG, "EPD step NT61522_Display_net done size=%u elapsed_ms=%lld, target=%d",
             (unsigned int)imageSize,
             (long long)((esp_timer_get_time() - start_us) / 1000),EPD_which_one_);
}
