#include "epd_type_1360_480_1085_3color.h"

#include "display_bsp.h"
#include "esp_timer.h"

namespace {
constexpr size_t kSourceBytes = 85U;
constexpr size_t kGateBits = 480U;
constexpr size_t kHalfScreenSize = kSourceBytes * kGateBits;
constexpr size_t kPlaneSize = kHalfScreenSize * 2U;
constexpr size_t kImageSize = kPlaneSize * 2U;

void write_half_plane_to_target(ePaperPort &epd, EP_Target_t target, uint8_t command, const uint8_t *data)
{
    epd.EPD_WriteCMD_Target(target, command);
    epd.EPD_WriteMultiData_Target(target, const_cast<uint8_t *>(data), (unsigned int)kHalfScreenSize);
}
}

void ePaperPort::EPD_Check_Busy_1085_3c(uint16_t loop_counter)
{
    int16_t i;
    int64_t start_us = esp_timer_get_time();

    if (loop_counter > 31) {
        loop_counter = 31;
    }
    i = 0;
    while (1) {
        int level = Get_BusyIOLevel();
        if (level) {
            printf("Check Busy over\r\n");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        i++;
        printf("#%d.", i);

        if (i > loop_counter) {
            int elapsed_ms = (int)((esp_timer_get_time() - start_us) / 1000);
            ESP_LOGE(TAG, "EPD busy timeout level=%d loops=%ld elapsed_ms=%d",
                     Get_BusyIOLevel(), (long)i, elapsed_ms);
            EpdType_ReportDisplayFailure(ESP_ERR_TIMEOUT);
            return;
        }
    }
}

void EpdType1360480_1085_3Color_Display(ePaperPort &epd,
                                         const uint8_t *display_buf,
                                         size_t display_size)
{
    if (display_buf == nullptr || display_size != kImageSize) {
        ESP_LOGE("Display", "EPD 1360x480 3color rejected input=%u expected=%u",
                 (unsigned int)display_size,
                 (unsigned int)kImageSize);
        return;
    }

    epd.EpdType1360480_1085_3Color_Init();
    epd.EpdType1360480_1085_3Color_DisplayNet(display_buf, display_size);
    epd.EpdType1360480_1085_3Color_UpdateAndSleep();
}

void ePaperPort::EpdType1360480_1085_3Color_Sleep()
{
    EPD_WriteCMD_Target(TARGET_BOTH, 0x07);
    EPD_WriteDATA_Target(TARGET_BOTH, 0xA5);
    isEPDInit = false;
}

void ePaperPort::EpdType1360480_1085_3Color_Init()
{
    int64_t start_us = esp_timer_get_time();
    setPinCsAll(GPIO_HIGH);
    Set_ResetIOLevel(GPIO_LOW);
    delay_ms(10);
    Set_ResetIOLevel(GPIO_HIGH);
    delay_ms(10);
    EPD_Check_Busy_1085_3c(2);

    EPD_WriteCMD_Target(TARGET_BOTH, 0x08);
    EPD_WriteDATA_Target(TARGET_BOTH, 0x00);

    EPD_WriteCMD_Target(TARGET_BOTH, 0xF8);
    EPD_WriteDATA_Target(TARGET_BOTH, 0x60);
    EPD_WriteDATA_Target(TARGET_BOTH, 0xA5);

    EPD_WriteCMD_Target(TARGET_BOTH, 0xF8);
    EPD_WriteDATA_Target(TARGET_BOTH, 0x93);
    EPD_WriteDATA_Target(TARGET_BOTH, 0x18);

    EPD_WriteCMD_Target(TARGET_BOTH, 0xF8);
    EPD_WriteDATA_Target(TARGET_BOTH, 0x73);
    EPD_WriteDATA_Target(TARGET_BOTH, 0x05);

    EPD_WriteCMD_Target(TARGET_BOTH, 0xF8);
    EPD_WriteDATA_Target(TARGET_BOTH, 0x92);
    EPD_WriteDATA_Target(TARGET_BOTH, 0x00);

    EPD_WriteCMD_Target(TARGET_BOTH, 0xF8);
    EPD_WriteDATA_Target(TARGET_BOTH, 0xA8);
    EPD_WriteDATA_Target(TARGET_BOTH, 0x3A);

    EPD_WriteCMD_Target(TARGET_BOTH, 0xF8);
    EPD_WriteDATA_Target(TARGET_BOTH, 0x88);
    EPD_WriteDATA_Target(TARGET_BOTH, 0x02);

    ESP_LOGI(TAG, "EPD 1360x480 3color init reference_mode dual_cs=%d,%d elapsed_ms=%lld",
             cs_, cs_2_,
             (long long)((esp_timer_get_time() - start_us) / 1000));
}

void ePaperPort::EpdType1360480_1085_3Color_Display()
{
    if (!EnsureDispBuffer()) {
        ESP_LOGE(TAG, "EPD 1360x480 3color display buffer not ready");
        EpdType_ReportDisplayFailure(ESP_ERR_NO_MEM);
        return;
    }
}


void ePaperPort::EpdType1360480_1085_3Color_DisplayNet(const uint8_t *image_data,
                                                        size_t image_size)
{
    if (image_data == nullptr || image_size != kImageSize) {
        ESP_LOGE(TAG, "EPD 1360x480 3color image size invalid input=%u expected=%u",
                 (unsigned int)image_size,
                 (unsigned int)kImageSize);
        return;
    }

    const uint8_t *image_b_l = image_data;
    const uint8_t *image_r_l = image_data + kHalfScreenSize;
    const uint8_t *image_b_r = image_data + (kHalfScreenSize * 2U);
    const uint8_t *image_r_r = image_data + (kHalfScreenSize * 3U);

    int64_t stage_start_us = esp_timer_get_time();
    write_half_plane_to_target(*this, TARGET_MASTER, 0x10, image_b_l);
    write_half_plane_to_target(*this, TARGET_MASTER, 0x13, image_r_l);
    write_half_plane_to_target(*this, TARGET_SLAVE, 0x10, image_b_r);
    write_half_plane_to_target(*this, TARGET_SLAVE, 0x13, image_r_r);
    ESP_LOGI(TAG, "EPD 1360x480 3color data loaded half=%u plane=%u image=%u elapsed_ms=%lld",
             (unsigned int)kHalfScreenSize,
             (unsigned int)kPlaneSize,
             (unsigned int)kImageSize,
             (long long)((esp_timer_get_time() - stage_start_us) / 1000));
}

void ePaperPort::EpdType1360480_1085_3Color_UpdateAndSleep()
{
    int64_t start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "EPD 1360x480 3color refresh start");

    EPD_WriteCMD_Target(TARGET_BOTH, 0x04);
    EPD_Check_Busy_1085_3c(25);
    delay_ms(100);

    EPD_WriteCMD_Target(TARGET_BOTH, 0x12);
    delay_ms(10);
    EPD_Check_Busy_1085_3c(25);

    EPD_WriteCMD_Target(TARGET_BOTH, 0x04);
    EPD_Check_Busy_1085_3c(25);
    delay_ms(100);

    EpdType1360480_1085_3Color_Sleep();

    ESP_LOGI(TAG, "EPD 1360x480 3color refresh done elapsed_ms=%lld",
             (long long)((esp_timer_get_time() - start_us) / 1000));
}
