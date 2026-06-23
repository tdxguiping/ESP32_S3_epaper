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
    EPD_Check_Busy_75_2(2);

    EpdType800480_4S_75_DKE_WriteCommand(0x12);
    EpdType800480_4S_75_DKE_WriteData(0x00);
    EPD_Check_Busy_75_2(2);

    EpdType800480_4S_75_DKE_WriteCommand(0x02);
    EpdType800480_4S_75_DKE_WriteData(0x00);
    EPD_Check_Busy_75_2(2);

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
    EPD_WriteMultiData_Target(TARGET_MASTER, const_cast<uint8_t *>(imageData), (unsigned int)imageSize);
    // for (size_t i = 0; i < imageSize; ++i) {
    //     EpdType800480_4S_75_DKE_WriteData(imageData[i]);
    //     // if ((i + 1U) % kDkeYieldInterval == 0U) {
    //     //     // English: Yield during long SPI writes so the idle task can feed the watchdog.
    //     //     // Chinese: Long SPI writes yield CPU to avoid blocking the idle watchdog.
    //     //     vTaskDelay(1);
    //     // }
    // }

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
    (void)step;
    EPD_Check_Busy_75_2(2);
}

void ePaperPort::EPD_Check_Busy_75_2(uint16_t loop_counter)
{
    int64_t start_us = esp_timer_get_time();
    int16_t i = 0;

    if (loop_counter > 31) {
        loop_counter = 31;
    }

    while (1) {
        int level = Get_BusyIOLevel();
        if (level) {
            printf("Check Busy over\r\n");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); //  1000ms = 1s
        i++;
        printf("<%d.", i);

        if (i > loop_counter) {
            int elapsed_ms = (int)((esp_timer_get_time() - start_us) / 1000);
            ESP_LOGE(TAG, "EPD DKE busy timeout level=%d loops=%ld elapsed_ms=%d",
                     Get_BusyIOLevel(), (long)i, elapsed_ms);
            return;
        }
    }
}

void ePaperPort::EpdType800480_4S_75_DKE_UpdateAndSleep()
{
    int64_t start_us = esp_timer_get_time();
    ESP_LOGI("epd_display", "EPD 800x480 4color DKE update start");
    
    EpdType800480_4S_75_DKE_WriteCommand(0x04);
    EPD_Check_Busy_75_2(25);
    
    EpdType800480_4S_75_DKE_WriteCommand(0x12);
    EpdType800480_4S_75_DKE_WriteData(0x00);
    EPD_Check_Busy_75_2(25);
    
    EpdType800480_4S_75_DKE_WriteCommand(0x02);
    EpdType800480_4S_75_DKE_WriteData(0x00);
    EPD_Check_Busy_75_2(25);

    EpdType800480_4S_75_DKE_WriteCommand(0x07);
    EpdType800480_4S_75_DKE_WriteData(0xA5);
    delay_ms(200);
    isEPDInit = false;

    ESP_LOGI("epd_display", "EPD 800x480 4color DKE update done elapsed_ms=%lld",
             (long long)((esp_timer_get_time() - start_us) / 1000));
}
