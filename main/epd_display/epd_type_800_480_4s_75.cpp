#include "epd_type_800_480_4s_75.h"
#include "display_bsp.h"
#include "debug_output.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace {
constexpr size_t kEpd4sYieldInterval = 512U;
}

void ePaperPort::EPD_Check_Busy_4s75(uint16_t loop_counter)
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
        UserDebugOutput_Printf("&%d.", i);

        if (i > loop_counter) {
            int elapsed_ms = (int)((esp_timer_get_time() - start_us) / 1000);
            ESP_LOGE(TAG, "EPD busy timeout level=%d loops=%ld elapsed_ms=%d",
                     Get_BusyIOLevel(), (long)i, elapsed_ms);
            EpdType_ReportDisplayFailure(ESP_ERR_TIMEOUT);
            return;
        }
    }
}

void EpdType800480_4S_75_Display(ePaperPort &epd, const uint8_t *display_buf, size_t display_size)
{
    static const size_t expected_image_size = 800U * 480U / 4U;
    if (display_buf == nullptr || display_size != expected_image_size) {
        ESP_LOGE("Display", "EPD 800x480 4color rejected input=%u expected=%u",
                 (unsigned int)display_size,
                 (unsigned int)expected_image_size);
        return;
    }

    epd.EPD_Init();
    epd.NT61522_Display_net(display_buf, display_size);
    epd.Epaper_Update_and_Deepsleep();
}

void ePaperPort::EpdType800480_4S_75_Sleep()
{

	EPD_SendCommand(0x04);   //Power on
    EPD_Check_Busy_4s75(2);
 
	EPD_SendCommand(0x12); //Update  
    EPD_SendData(0x00); 	
	EPD_Check_Busy_4s75(2);

	EPD_SendCommand(0x02); //Power off
	EPD_SendData(0x00);
	EPD_Check_Busy_4s75(2);
  
	EPD_SendCommand(0x07); //Power off
	EPD_SendData(0xA5);
   //delay_ms(200); 

}

void ePaperPort::EpdType800480_4S_75_Init()
{
    Epaper_Initial();
}

void ePaperPort::EpdType800480_4S_75_Display()
{
    EPD_WriteCMD(0x10);
    for (uint32_t i = 0; i < (uint32_t)DisplayLen; ++i) {
        EPD_WriteDATA(DispBuffer[i]);
        if ((i + 1U) % kEpd4sYieldInterval == 0U) {
            // English: Yield during long SPI writes so the idle task can feed the watchdog.
            // 中文：长时间 SPI 写入期间主动让出 CPU，避免 idle 任务无法喂 watchdog。
            vTaskDelay(1);
        }
    }
    ESP_LOGI(TAG, "EPD 800x480 4color data loaded target=%u size=%u",
             (unsigned int)EPD_which_one_,
             (unsigned int)DisplayLen);
    Epaper_Update_and_Deepsleep();		
}

void ePaperPort::EpdType800480_4S_75_NT61522_DisplayNet(const uint8_t *imageData, size_t imageSize)
{
    static const size_t expected_image_size = 800U * 480U / 4U;

    if (imageData == nullptr || imageSize != expected_image_size) {
        ESP_LOGE(TAG, "EPD 800x480 4color image size invalid input=%u expected=%u",
                 (unsigned int)imageSize,
                 (unsigned int)expected_image_size);
        return;
    }

    EPD_WriteCMD(0x10);

    // Way -1 
    EPD_WriteMultiData_Target(TARGET_MASTER, const_cast<uint8_t *>(imageData), (unsigned int)imageSize);

    // Way -2
    // for (size_t i = 0; i < imageSize; ++i) {
    //     EPD_WriteDATA(imageData[i]);
    //     if ((i + 1U) % kEpd4sYieldInterval == 0U) {
    //         // English: Yield during long SPI writes so the idle task can feed the watchdog.
    //         // 中文：长时间 SPI 写入期间主动让出 CPU，避免 idle 任务无法喂 watchdog。
    //         vTaskDelay(1);
    //     }
    // }
    ESP_LOGI(TAG, "EPD 800x480 4color data loaded target=%u size=%u",
             (unsigned int)EPD_which_one_,
             (unsigned int)imageSize);
}

void ePaperPort::Epaper_Initial() {   
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

void ePaperPort::Epaper_Update_and_Deepsleep() {   
    int64_t start_us = esp_timer_get_time();
    ESP_LOGI("epd_display", "EPD step Epaper_Update_and_Deepsleep start");

	EPD_WriteCMD(0x04);   //Power on
    EPD_Check_Busy_4s75(31);
 
	EPD_WriteCMD(0x12); //Update  
    EPD_WriteDATA(0x00); 	
	EPD_Check_Busy_4s75(31);

	EPD_WriteCMD(0x02); //Power off
	EPD_WriteDATA(0x00);
	EPD_Check_Busy_4s75(31);
  
	EPD_WriteCMD(0x07); //Power off
	EPD_WriteDATA(0xA5);
    delay_ms(200); 
    isEPDInit = false;
    ESP_LOGI("epd_display", "EPD step Epaper_Update_and_Deepsleep done elapsed_ms=%lld",
             (long long)((esp_timer_get_time() - start_us) / 1000));
}
