#include "epd_type_800_480_4s_75_DKE.h"

#include "display_bsp.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace {
constexpr size_t kDkeImageSize = 800U * 480U / 4U;
constexpr size_t kDkeYieldInterval = 512U;
}

void EpdType800480_4S_75_DKE_Display(ePaperPort &epd,
                                     const uint8_t *display_buf,
                                     size_t display_size)
{
    if (display_buf == nullptr || display_size != kDkeImageSize) {
        ESP_LOGE("Display", "EPD 800x480 4color DKE rejected input=%u expected=%u",
                 (unsigned int)display_size,
                 (unsigned int)kDkeImageSize);
        return;
    }

    epd.EpdType800480_4S_75_DKE_Init();
    epd.EpdType800480_4S_75_DKE_NT61522_DisplayNet(display_buf, display_size);
    epd.EpdType800480_4S_75_DKE_UpdateAndSleep();
}

void ePaperPort::EpdType800480_4S_75_DKE_Sleep()
{
    EpdType800480_4S_75_DKE_WriteCommand(0x04);
    EpdType800480_4S_75_DKE_WaitBusy("sleep_power_on");

    EpdType800480_4S_75_DKE_WriteCommand(0x12);
    EpdType800480_4S_75_DKE_WriteData(0x00);
    EpdType800480_4S_75_DKE_WaitBusy("sleep_refresh");

    EpdType800480_4S_75_DKE_WriteCommand(0x02);
    EpdType800480_4S_75_DKE_WriteData(0x00);
    EpdType800480_4S_75_DKE_WaitBusy("sleep_power_off");

    EpdType800480_4S_75_DKE_WriteCommand(0x07);
    EpdType800480_4S_75_DKE_WriteData(0xA5);
}

void ePaperPort::EpdType800480_4S_75_DKE_Init()
{
    EpdType800480_4S_75_DKE_Reset();

    EpdType800480_4S_75_DKE_WriteCommand(0x4D);
    EpdType800480_4S_75_DKE_WriteData(0x78);

    EpdType800480_4S_75_DKE_WriteCommand(0x00);
    EpdType800480_4S_75_DKE_WriteData(0x2F);
    EpdType800480_4S_75_DKE_WriteData(0x29);

    EpdType800480_4S_75_DKE_WriteCommand(0xE3);
    EpdType800480_4S_75_DKE_WriteData(0x88);

    EpdType800480_4S_75_DKE_WriteCommand(0x50);
    EpdType800480_4S_75_DKE_WriteData(0x37);

    EpdType800480_4S_75_DKE_WriteCommand(0x61);
    EpdType800480_4S_75_DKE_WriteData(X_Addr_Start_H);
    EpdType800480_4S_75_DKE_WriteData(X_Addr_Start_L);
    EpdType800480_4S_75_DKE_WriteData(Y_Addr_Start_H);
    EpdType800480_4S_75_DKE_WriteData(Y_Addr_Start_L);

    EpdType800480_4S_75_DKE_WriteCommand(0x65);
    EpdType800480_4S_75_DKE_WriteData(0x00);
    EpdType800480_4S_75_DKE_WriteData(0x00);
    EpdType800480_4S_75_DKE_WriteData(0x00);
    EpdType800480_4S_75_DKE_WriteData(0x00);

    EpdType800480_4S_75_DKE_WriteCommand(0xF0);
    EpdType800480_4S_75_DKE_WriteData(0x5F);

    EpdType800480_4S_75_DKE_WriteCommand(0xE9);
    EpdType800480_4S_75_DKE_WriteData(0x01);

    EpdType800480_4S_75_DKE_WriteCommand(0x30);
    EpdType800480_4S_75_DKE_WriteData(0x08);

    ESP_LOGI(TAG, "EPD 800x480 4color DKE init target=%u", (unsigned int)EPD_which_one_);
}

void ePaperPort::EpdType800480_4S_75_DKE_Display()
{
    if (!EnsureDispBuffer()) {
        ESP_LOGE(TAG, "EPD 800x480 4color DKE display buffer not ready");
        return;
    }

    EpdType800480_4S_75_DKE_WriteCommand(0x10);
    for (uint32_t i = 0; i < (uint32_t)DisplayLen; ++i) {
        EpdType800480_4S_75_DKE_WriteData(DispBuffer[i]);
        if ((i + 1U) % kDkeYieldInterval == 0U) {
            // English: Yield during long SPI writes so the idle task can feed the watchdog.
            // Chinese: Long SPI writes yield CPU to avoid blocking the idle watchdog.
            vTaskDelay(1);
        }
    }

    ESP_LOGI(TAG, "EPD 800x480 4color DKE data loaded target=%u size=%u",
             (unsigned int)EPD_which_one_,
             (unsigned int)DisplayLen);
    EpdType800480_4S_75_DKE_UpdateAndSleep();
}

void ePaperPort::EpdType800480_4S_75_DKE_NT61522_DisplayNet(const uint8_t *imageData,
                                                            size_t imageSize)
{
    if (imageData == nullptr || imageSize != kDkeImageSize) {
        ESP_LOGE(TAG, "EPD 800x480 4color DKE image size invalid input=%u expected=%u",
                 (unsigned int)imageSize,
                 (unsigned int)kDkeImageSize);
        return;
    }

    EpdType800480_4S_75_DKE_WriteCommand(0x10);
    for (size_t i = 0; i < imageSize; ++i) {
        EpdType800480_4S_75_DKE_WriteData(imageData[i]);
        if ((i + 1U) % kDkeYieldInterval == 0U) {
            // English: Yield during long SPI writes so the idle task can feed the watchdog.
            // Chinese: Long SPI writes yield CPU to avoid blocking the idle watchdog.
            vTaskDelay(1);
        }
    }

    ESP_LOGI(TAG, "EPD 800x480 4color DKE data loaded target=%u size=%u",
             (unsigned int)EPD_which_one_,
             (unsigned int)imageSize);
}

void ePaperPort::EpdType800480_4S_75_DKE_Reset()
{
    Set_CSIOLevel(1);
    delay_ms(100);
    Set_ResetIOLevel(0);
    delay_ms(50);
    Set_ResetIOLevel(1);
    delay_ms(50);
}

void ePaperPort::EpdType800480_4S_75_DKE_WriteCommand(uint8_t command)
{
    Set_CSIOLevel(1);
    Set_CSIOLevel(0);
    Set_DCIOLevel(0);
    delay_us(5);
    SPI_Write(command);
    delay_us(5);
    Set_CSIOLevel(1);
}

void ePaperPort::EpdType800480_4S_75_DKE_WriteData(uint8_t data)
{
    Set_CSIOLevel(1);
    Set_CSIOLevel(0);
    Set_DCIOLevel(1);
    delay_us(5);
    SPI_Write(data);
    delay_us(5);
    Set_CSIOLevel(1);
}

void ePaperPort::EpdType800480_4S_75_DKE_WaitBusy(const char *step)
{
    int64_t start_us = esp_timer_get_time();
    uint32_t loops = 0;

    while (Get_BusyIOLevel() != 1U) {
        delay_us(5);
        ++loops;
        if ((loops % 10000U) == 0U) {
            vTaskDelay(1);
        }
        if (((esp_timer_get_time() - start_us) / 1000) > 45000) {
            ESP_LOGE("epd_display", "EPD DKE busy timeout step=%s level=%u",
                     step != nullptr ? step : "unknown",
                     (unsigned int)Get_BusyIOLevel());
            return;
        }
    }

    delay_us(100);
}

void ePaperPort::EpdType800480_4S_75_DKE_UpdateAndSleep()
{
    int64_t start_us = esp_timer_get_time();
    ESP_LOGI("epd_display", "EPD 800x480 4color DKE update start");

    ESP_LOGI("epd_display", "EPD DKE power on busy=%u", (unsigned int)Get_BusyIOLevel());
    EpdType800480_4S_75_DKE_WriteCommand(0x04);
    EpdType800480_4S_75_DKE_WaitBusy("power_on");

    ESP_LOGI("epd_display", "EPD DKE refresh busy=%u", (unsigned int)Get_BusyIOLevel());
    EpdType800480_4S_75_DKE_WriteCommand(0x12);
    EpdType800480_4S_75_DKE_WriteData(0x00);
    EpdType800480_4S_75_DKE_WaitBusy("refresh");

    ESP_LOGI("epd_display", "EPD DKE power off busy=%u", (unsigned int)Get_BusyIOLevel());
    EpdType800480_4S_75_DKE_WriteCommand(0x02);
    EpdType800480_4S_75_DKE_WriteData(0x00);
    EpdType800480_4S_75_DKE_WaitBusy("power_off");

    EpdType800480_4S_75_DKE_WriteCommand(0x07);
    EpdType800480_4S_75_DKE_WriteData(0xA5);
    delay_ms(200);
    isEPDInit = false;

    ESP_LOGI("epd_display", "EPD 800x480 4color DKE update done elapsed_ms=%lld",
             (long long)((esp_timer_get_time() - start_us) / 1000));
}
