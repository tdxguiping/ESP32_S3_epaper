#include "epd_type_800_480.h"
#include "display_bsp.h"
#include "debug_output.h"
#include "esp_timer.h"

void ePaperPort::EPD_Check_Busy_480(uint16_t loop_counter)
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
            UserDebugOutput_Printf("Check Busy over\r\n");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        i++;
        UserDebugOutput_Printf("=%d.", i);

        if (i > loop_counter) {
            int elapsed_ms = (int)((esp_timer_get_time() - start_us) / 1000);
            ESP_LOGE(TAG, "EPD busy timeout level=%d loops=%ld elapsed_ms=%d",
                     Get_BusyIOLevel(), (long)i, elapsed_ms);
            EpdType_ReportDisplayFailure(ESP_ERR_TIMEOUT);
            return;
        }
    }
}

void ePaperPort::EPD_TurnOnDisplay_480(void)
{
    EPD_SendCommand(0x04);
    EPD_Check_Busy_480(25);
    EPD_SendCommand(0x06);
    EPD_SendData(0x6F);
    EPD_SendData(0x1F);
    EPD_SendData(0x17);
    EPD_SendData(0x49);
    EPD_SendCommand(0x12);
    EPD_SendData(0x00);
    EPD_Check_Busy_480(25);
    EPD_SendCommand(0x02);
    EPD_SendData(0x00);
    EPD_Check_Busy_480(25);
}

void EpdType800480_Display(ePaperPort &epd, const uint8_t *display_buf, size_t display_size)
{
    static const size_t expected_image_size = 800U * 480U / 2U;
    if (display_buf == nullptr || display_size != expected_image_size) {
        ESP_LOGE("Display", "EPD 800x480 rejected input=%u expected=%u",
                 (unsigned int)display_size,
                 (unsigned int)expected_image_size);
        return;
    }

    epd.EPD_Init();
    epd.NT61522_Init_display();
    if (epd.NT61522_Display_net(display_buf, display_size) != ESP_OK) {
        return;
    }
    epd.Epaper_Update();
}

void ePaperPort::EpdType800480_Sleep()
{
    EPD_WriteCMD(0x02);
    delay_ms(30);
    EPD_Check_Busy_480(2);
    delay_ms(100);
    EPD_WriteCMD(0x07);
    EPD_WriteDATA(0xA5);
}





void ePaperPort::EpdType800480_Init()
{

    EPD_Reset();
    EPD_LoopBusy(1);
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
    EPD_LoopBusy(1);
    //EPD_DispClear(ColorWhite);
}



void ePaperPort::EpdType800480_Display()
{
    // EPD_PixelRotate();
    // EPD_SendCommand(0x10);
    // EPD_Sendbuffera(RotationBuffer, DisplayLen);
    // EPD_TurnOnDisplay_480();

    //memcpy(RotationBuffer, DispBuffer, DisplayLen);
    if (!EnsureDispBuffer()) {
        ESP_LOGE(TAG, "EPD_Display 800x480 aborted, DispBuffer not ready");
        EpdType_ReportDisplayFailure(ESP_ERR_NO_MEM);
        return;
    }
    EPD_SendCommand(0x10);
    EPD_Sendbuffera(DispBuffer, DisplayLen);
    EPD_TurnOnDisplay_480();
    ReleaseRotationBuffer();
    ReleaseDispBuffer();
}

void ePaperPort::EpdType800480_NT61522_Display()
{
    EPD_TurnOnDisplay_480();
}

void ePaperPort::EpdType800480_NT61522_InitDisplay()
{
    EPD_Init();  

    EPD_SendCommand(0x10);

}

void ePaperPort::EpdType800480_NT61522_DisplayNet(const uint8_t *imageData, size_t imageSize)
{
    static const size_t expected_image_size = 800U * 480U / 2U;
    if (imageData == nullptr || imageSize != expected_image_size) {
        ESP_LOGE(TAG, "EPD 800x480 image size invalid input=%u expected=%u",
                 (unsigned int)imageSize,
                 (unsigned int)expected_image_size);
        return;
    }

    EPD_Sendbuffera(const_cast<uint8_t *>(imageData), (int)imageSize);
}
