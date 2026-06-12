#include "epd_type_800_480_4s_75_mofang.h"

#include "display_bsp.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace {
constexpr size_t kMofangImageSize = 800U * 480U / 4U;
constexpr size_t kMofangYieldInterval = 512U;
}

void EpdType800480_4S_75_Mofang_Display(ePaperPort &epd,
                                        const uint8_t *display_buf,
                                        size_t display_size)
{
    if (display_buf == nullptr || display_size != kMofangImageSize) {
        ESP_LOGE("Display", "EPD 800x480 4color mofang rejected input=%u expected=%u",
                 (unsigned int)display_size,
                 (unsigned int)kMofangImageSize);
        return;
    }

    epd.EpdType800480_4S_75_Mofang_Init();
    epd.EpdType800480_4S_75_Mofang_NT61522_DisplayNet(display_buf, display_size);
    epd.EpdType800480_4S_75_Mofang_UpdateAndSleep();
}

void ePaperPort::EpdType800480_4S_75_Mofang_Sleep()
{
    EpdType800480_4S_75_Mofang_WriteCommand(0x07);
    EpdType800480_4S_75_Mofang_WriteData(0xA5);
}

void ePaperPort::EpdType800480_4S_75_Mofang_Init()
{
    EpdType800480_4S_75_Mofang_Reset();
    EpdType800480_4S_75_Mofang_WaitBusy("init_reset");

    EpdType800480_4S_75_Mofang_WriteCommand(0x4D);
    EpdType800480_4S_75_Mofang_WriteData(0x78);

    EpdType800480_4S_75_Mofang_WriteCommand(0xB4);
    EpdType800480_4S_75_Mofang_WriteData(0xD0);

    EpdType800480_4S_75_Mofang_WriteCommand(0x00);
    EpdType800480_4S_75_Mofang_WriteData(0x0F);
    EpdType800480_4S_75_Mofang_WriteData(0x29);

    EpdType800480_4S_75_Mofang_WriteCommand(0x01);
    EpdType800480_4S_75_Mofang_WriteData(0x07);
    EpdType800480_4S_75_Mofang_WriteData(0x00);
    EpdType800480_4S_75_Mofang_WriteData(0x22);
    EpdType800480_4S_75_Mofang_WriteData(0x78);
    EpdType800480_4S_75_Mofang_WriteData(0x0A);
    EpdType800480_4S_75_Mofang_WriteData(0x22);

    EpdType800480_4S_75_Mofang_WriteCommand(0x03);
    EpdType800480_4S_75_Mofang_WriteData(0x10);
    EpdType800480_4S_75_Mofang_WriteData(0x54);
    EpdType800480_4S_75_Mofang_WriteData(0x44);

    EpdType800480_4S_75_Mofang_WriteCommand(0x06);
    EpdType800480_4S_75_Mofang_WriteData(0x0D);
    EpdType800480_4S_75_Mofang_WriteData(0x12);
    EpdType800480_4S_75_Mofang_WriteData(0x30);
    EpdType800480_4S_75_Mofang_WriteData(0x20);
    EpdType800480_4S_75_Mofang_WriteData(0x19);
    EpdType800480_4S_75_Mofang_WriteData(0x34);
    EpdType800480_4S_75_Mofang_WriteData(0x10);

    EpdType800480_4S_75_Mofang_WriteCommand(0x30);
    EpdType800480_4S_75_Mofang_WriteData(0x02);

    EpdType800480_4S_75_Mofang_WriteCommand(0x41);
    EpdType800480_4S_75_Mofang_WriteData(0x00);

    EpdType800480_4S_75_Mofang_WriteCommand(0x50);
    EpdType800480_4S_75_Mofang_WriteData(0x37);

    EpdType800480_4S_75_Mofang_WriteCommand(0x60);
    EpdType800480_4S_75_Mofang_WriteData(0x02);
    EpdType800480_4S_75_Mofang_WriteData(0x02);

    EpdType800480_4S_75_Mofang_WriteCommand(0x61);
    EpdType800480_4S_75_Mofang_WriteData(0x03);
    EpdType800480_4S_75_Mofang_WriteData(0x20);
    EpdType800480_4S_75_Mofang_WriteData(0x01);
    EpdType800480_4S_75_Mofang_WriteData(0xE0);

    EpdType800480_4S_75_Mofang_WriteCommand(0x65);
    EpdType800480_4S_75_Mofang_WriteData(0x00);
    EpdType800480_4S_75_Mofang_WriteData(0x00);
    EpdType800480_4S_75_Mofang_WriteData(0x00);
    EpdType800480_4S_75_Mofang_WriteData(0x00);

    EpdType800480_4S_75_Mofang_WriteCommand(0xE7);
    EpdType800480_4S_75_Mofang_WriteData(0x1C);

    EpdType800480_4S_75_Mofang_WriteCommand(0xE3);
    EpdType800480_4S_75_Mofang_WriteData(0x22);

    EpdType800480_4S_75_Mofang_WriteCommand(0xE0);
    EpdType800480_4S_75_Mofang_WriteData(0x00);

    EpdType800480_4S_75_Mofang_WriteCommand(0x4D);
    EpdType800480_4S_75_Mofang_WriteData(0x78);

    EpdType800480_4S_75_Mofang_WriteCommand(0x00);
    EpdType800480_4S_75_Mofang_WriteData(0x0F);
    EpdType800480_4S_75_Mofang_WriteData(0x29);

    EpdType800480_4S_75_Mofang_WriteCommand(0x30);
    EpdType800480_4S_75_Mofang_WriteData(0x08);

    EpdType800480_4S_75_Mofang_WriteCommand(0xE9);
    EpdType800480_4S_75_Mofang_WriteData(0x01);

    ESP_LOGI(TAG, "EPD 800x480 4color mofang init target=%u", (unsigned int)EPD_which_one_);
}

void ePaperPort::EpdType800480_4S_75_Mofang_Display()
{
    if (!EnsureDispBuffer()) {
        ESP_LOGE(TAG, "EPD 800x480 4color mofang display buffer not ready");
        return;
    }

    EpdType800480_4S_75_Mofang_WriteCommand(0x10);
    for (uint32_t i = 0; i < (uint32_t)DisplayLen; ++i) {
        EpdType800480_4S_75_Mofang_WriteData(DispBuffer[i]);
        if ((i + 1U) % kMofangYieldInterval == 0U) {
            // English: Yield during long SPI writes so the idle task can feed the watchdog.
            // Chinese: Long SPI writes yield CPU to avoid blocking the idle watchdog.
            vTaskDelay(1);
        }
    }

    ESP_LOGI(TAG, "EPD 800x480 4color mofang data loaded target=%u size=%u",
             (unsigned int)EPD_which_one_,
             (unsigned int)DisplayLen);
    EpdType800480_4S_75_Mofang_UpdateAndSleep();
}

void ePaperPort::EpdType800480_4S_75_Mofang_NT61522_DisplayNet(const uint8_t *imageData,
                                                               size_t imageSize)
{
    if (imageData == nullptr || imageSize != kMofangImageSize) {
        ESP_LOGE(TAG, "EPD 800x480 4color mofang image size invalid input=%u expected=%u",
                 (unsigned int)imageSize,
                 (unsigned int)kMofangImageSize);
        return;
    }

    EpdType800480_4S_75_Mofang_WriteCommand(0x10);
    EPD_WriteMultiData_Target(TARGET_MASTER, const_cast<uint8_t *>(imageData), (unsigned int)imageSize);

    ESP_LOGI(TAG, "EPD 800x480 4color mofang data loaded target=%u size=%u",
             (unsigned int)EPD_which_one_,
             (unsigned int)imageSize);
}

void ePaperPort::EpdType800480_4S_75_Mofang_Reset()
{
    Set_CSIOLevel(1);
    Set_ResetIOLevel(0);
    delay_ms(50);
    Set_ResetIOLevel(1);
    delay_ms(50);
}

void ePaperPort::EpdType800480_4S_75_Mofang_WriteCommand(uint8_t command)
{
    Set_CSIOLevel(0);
    Set_DCIOLevel(0);
    delay_us(2);
    SPI_Write(command);
    Set_CSIOLevel(1);
}

void ePaperPort::EpdType800480_4S_75_Mofang_WriteData(uint8_t data)
{
    Set_CSIOLevel(0);
    Set_DCIOLevel(1);
    delay_us(1);
    SPI_Write(data);
    Set_CSIOLevel(1);
}

void ePaperPort::EpdType800480_4S_75_Mofang_WaitBusy(const char *step)
{
    int64_t start_us = esp_timer_get_time();
    uint16_t loops = 0;

    while (Get_BusyIOLevel() != 1U) {
        vTaskDelay(pdMS_TO_TICKS(1000)); //  1000ms = 1s
        printf(".%d",loops);
        if (((esp_timer_get_time() - start_us) / 1000) > 45000) {
            ESP_LOGE("epd_display", "EPD mofang busy timeout step=%s level=%u",
                     step != nullptr ? step : "unknown",
                     (unsigned int)Get_BusyIOLevel());
            return;
        }
    }
}

void ePaperPort::EpdType800480_4S_75_Mofang_UpdateAndSleep()
{
    int64_t start_us = esp_timer_get_time();
    ESP_LOGI("epd_display", "EPD 800x480 4color mofang update start");

    EpdType800480_4S_75_Mofang_WriteCommand(0x04);
    delay_ms(100);
    EpdType800480_4S_75_Mofang_WaitBusy("power_on");

    EpdType800480_4S_75_Mofang_WriteCommand(0x12);
    EpdType800480_4S_75_Mofang_WriteData(0x00);
    delay_ms(100);
    EpdType800480_4S_75_Mofang_WaitBusy("refresh");

    EpdType800480_4S_75_Mofang_WriteCommand(0x02);
    EpdType800480_4S_75_Mofang_WriteData(0x00);
    delay_ms(50);
    EpdType800480_4S_75_Mofang_WaitBusy("power_off");
    delay_ms(50);

    EpdType800480_4S_75_Mofang_WriteCommand(0x07);
    EpdType800480_4S_75_Mofang_WriteData(0xA5);
    isEPDInit = false;

    ESP_LOGI("epd_display", "EPD 800x480 4color mofang update done elapsed_ms=%lld",
             (long long)((esp_timer_get_time() - start_us) / 1000));
}
