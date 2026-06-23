#include "epd_type_1360_480_1085.h"
#include "display_bsp.h"
#include "esp_timer.h"

namespace {
uint8_t convert_1085_color_byte(uint8_t value)
{
    uint8_t converted = 0;

    converted |= (value & 0x40) ? ((value & 0x80) ? 0x40 : 0xC0) : (value & 0xC0);
    converted |= (value & 0x10) ? ((value & 0x20) ? 0x10 : 0x30) : (value & 0x30);
    converted |= (value & 0x04) ? ((value & 0x08) ? 0x04 : 0x0C) : (value & 0x0C);
    converted |= (value & 0x01) ? ((value & 0x02) ? 0x01 : 0x03) : (value & 0x03);

    return converted;
}
}

void ePaperPort::EPD_Check_Busy_1085(uint16_t loop_counter)
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
        printf("@%d.", i);

        if (i > loop_counter) {
            int elapsed_ms = (int)((esp_timer_get_time() - start_us) / 1000);
            ESP_LOGE(TAG, "EPD busy timeout level=%d loops=%ld elapsed_ms=%d",
                     Get_BusyIOLevel(), (long)i, elapsed_ms);
            return;
        }
    }
}

void EpdType1360480_1085_Display(ePaperPort &epd, const uint8_t *display_buf, size_t display_size)
{
    static const size_t expected_image_size = Source_BITS * Gate_BITS / 4;
    if (display_buf == nullptr || display_size != expected_image_size) {
        ESP_LOGE("Display", "EPD 1360x480 display rejected input=%u expected=%u",
                 (unsigned int)display_size,
                 (unsigned int)expected_image_size);
        return;
    }

    epd.EPD_Init();
    epd.NT61522_Display_net(display_buf, display_size);
    epd.EpdType1360480_1085_Update();
}

void ePaperPort::EpdType1360480_1085_Sleep()
{
    EPD_WriteCMD(0x07);
    EPD_WriteDATA(0xA5);
    isEPDInit = false;
}

void ePaperPort::EpdType1360480_1085_Init()
{
    EPD_Reset();
    EPD_Check_Busy_1085(2);
    Epaper_Init();
}

void ePaperPort::EpdType1360480_1085_Display()
{
    if (!EnsureDispBuffer()) {
        ESP_LOGE(TAG, "EPD 1360x480 display buffer not ready");
        return;
    }
    EpdType1360480_1085_NT61522_DisplayNet(DispBuffer, (size_t)DisplayLen);
}

void ePaperPort::EpdType1360480_1085_NT61522_DisplayNet(const uint8_t *imageData, size_t imageSize)
{
    static const size_t expected_image_size = Source_BITS * Gate_BITS / 4;

    if (imageData == nullptr || imageSize == 0) {
        ESP_LOGE(TAG, "EPD 1360x480 image data invalid");
        return;
    }
    if (imageSize != expected_image_size) {
        ESP_LOGE(TAG, "EPD 1360x480 image size invalid input=%u expected=%u",
                 (unsigned int)imageSize,
                 (unsigned int)expected_image_size);
        return;
    }

    uint8_t converted[256];
    auto send_internal_controller = [this, imageData, imageSize, &converted](uint8_t selector) {
        EPD_WriteCMD(0xA2);
        EPD_WriteDATA(selector);
        EPD_WriteCMD(0x10);

        size_t offset = 0;
        while (offset < imageSize) {
            size_t chunk = imageSize - offset;
            if (chunk > sizeof(converted)) {
                chunk = sizeof(converted);
            }
            for (size_t i = 0; i < chunk; ++i) {
                converted[i] = convert_1085_color_byte(imageData[offset + i]);
            }
            EPD_Sendbuffera(converted, (int)chunk);
            offset += chunk;
        }
    };

    // This panel has one physical CS. Command 0xA2 selects its internal master/slave.
    int64_t stage_start_us = esp_timer_get_time();
    send_internal_controller(0x01);
    ESP_LOGI(TAG, "EPD 1360x480 internal master loaded size=%u elapsed_ms=%lld",
             (unsigned int)imageSize,
             (long long)((esp_timer_get_time() - stage_start_us) / 1000));

    stage_start_us = esp_timer_get_time();
    send_internal_controller(0x02);
    ESP_LOGI(TAG, "EPD 1360x480 internal slave loaded size=%u elapsed_ms=%lld",
             (unsigned int)imageSize,
             (long long)((esp_timer_get_time() - stage_start_us) / 1000));

    EPD_WriteCMD(0xA2);
    EPD_WriteDATA(0x00);
}

void ePaperPort::EpdType1360480_1085_Update()
{
    int64_t start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "EPD 1360x480 refresh start");

    EPD_WriteCMD(0x12);
    EPD_WriteDATA(0x00);
    EPD_Check_Busy_1085(31);
    delay_ms(20);

    EPD_WriteCMD(0x02);
    EPD_WriteDATA(0x00);
    EPD_Check_Busy_1085(31);

    EpdType1360480_1085_Sleep();
    ESP_LOGI(TAG, "EPD 1360x480 refresh done elapsed_ms=%lld",
             (long long)((esp_timer_get_time() - start_us) / 1000));
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
    EPD_Check_Busy_1085(2);  //while(1);
}
