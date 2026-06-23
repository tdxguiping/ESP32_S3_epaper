#include "epd_type_4color_common.h"
#include "display_bsp.h"
#include "esp_log.h"
#include "esp_timer.h"

void ePaperPort::Epaper_Update() {   
LOG_Purple("%s>%d",__func__,__LINE__);

  int64_t start_us = esp_timer_get_time();
  ESP_LOGI("epd_display", "EPD step Epaper_Update start");

  EPD_WriteCMD(0x12);
  EPD_WriteDATA(0x00);
    EPD_Check_Busy(24);
	delay_ms(20);
 
  EPD_WriteCMD(0x02);
  EPD_WriteDATA(0x00);
    EPD_Check_Busy(24);	
//	
////	delay_ms(30);
//	
    EPD_WriteCMD(0x07);		// Deep_sleep
    EPD_WriteDATA(0xa5);   	
    isEPDInit = false;
    ESP_LOGI("epd_display", "EPD step Epaper_Update done elapsed_ms=%lld",
             (long long)((esp_timer_get_time() - start_us) / 1000));
}
