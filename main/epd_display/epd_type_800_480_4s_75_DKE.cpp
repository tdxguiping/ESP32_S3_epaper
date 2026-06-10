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
    EPD_WriteCMD(0x04);
    EPD_Check_Busy();

    EPD_WriteCMD(0x12);
    EPD_WriteDATA(0x00);
    EPD_Check_Busy();

    EPD_WriteCMD(0x02);
    EPD_WriteDATA(0x00);
    EPD_Check_Busy();

    EPD_WriteCMD(0x07);
    EPD_WriteDATA(0xA5);
}

void ePaperPort::EpdType800480_4S_75_DKE_Init()
{
    EPD_Reset();

    EPD_WriteCMD(0x4D);
    EPD_WriteDATA(0x78);

    EPD_WriteCMD(0x00);
    EPD_WriteDATA(0x2F);
    EPD_WriteDATA(0x29);

    EPD_WriteCMD(0xE3);
    EPD_WriteDATA(0x88);

    EPD_WriteCMD(0x50);
    EPD_WriteDATA(0x37);

    EPD_WriteCMD(0x61);
    EPD_WriteDATA(X_Addr_Start_H);
    EPD_WriteDATA(X_Addr_Start_L);
    EPD_WriteDATA(Y_Addr_Start_H);
    EPD_WriteDATA(Y_Addr_Start_L);

    EPD_WriteCMD(0x65);
    EPD_WriteDATA(0x00);
    EPD_WriteDATA(0x00);
    EPD_WriteDATA(0x00);
    EPD_WriteDATA(0x00);

    EPD_WriteCMD(0xF0);
    EPD_WriteDATA(0x5F);

    EPD_WriteCMD(0xE9);
    EPD_WriteDATA(0x01);

    EPD_WriteCMD(0x30);
    EPD_WriteDATA(0x08);

    ESP_LOGI(TAG, "EPD 800x480 4color DKE init target=%u", (unsigned int)EPD_which_one_);
}

void ePaperPort::EpdType800480_4S_75_DKE_Display()
{
    if (!EnsureDispBuffer()) {
        ESP_LOGE(TAG, "EPD 800x480 4color DKE display buffer not ready");
        return;
    }

    EPD_WriteCMD(0x10);
    for (uint32_t i = 0; i < (uint32_t)DisplayLen; ++i) {
        EPD_WriteDATA(DispBuffer[i]);
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

    EPD_WriteCMD(0x10);
    for (size_t i = 0; i < imageSize; ++i) {
        EPD_WriteDATA(imageData[i]);
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

void ePaperPort::EpdType800480_4S_75_DKE_UpdateAndSleep()
{
    int64_t start_us = esp_timer_get_time();
    ESP_LOGI("epd_display", "EPD 800x480 4color DKE update start");

    EPD_WriteCMD(0x04);
    EPD_Check_Busy();

    EPD_WriteCMD(0x12);
    EPD_WriteDATA(0x00);
    EPD_Check_Busy();

    EPD_WriteCMD(0x02);
    EPD_WriteDATA(0x00);
    EPD_Check_Busy();

    EPD_WriteCMD(0x07);
    EPD_WriteDATA(0xA5);
    delay_ms(200);
    isEPDInit = false;

    ESP_LOGI("epd_display", "EPD 800x480 4color DKE update done elapsed_ms=%lld",
             (long long)((esp_timer_get_time() - start_us) / 1000));
}
