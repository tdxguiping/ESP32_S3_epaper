#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "epd_type.h"

#ifdef __cplusplus
#include "display_bsp.h"
extern ePaperPort ePaperDisplay;
extern "C" {
#endif

esp_err_t ServerNetworkStaEpdDisplay_Init(void);
esp_err_t ServerNetworkStaEpdDisplay_Queue(const uint8_t *display_buf, size_t display_size);
esp_err_t ServerNetworkStaEpdDisplay_QueueToScreen(const uint8_t *display_buf, size_t display_size, uint8_t epd_which_one);
void test_epd_display_EPD_1600_1200_79(void);
void test_epd_display_EPD_1600_1200_133(void);
void test_epd_display_EPD_EPD_1024_600(void);
void test_epd_display_EPD_800_480(void);
void test_epd_display_EPD_1360_480_1085(void);
void test_epd_display_EPD_800_480_4S_75_2(void);
void test_epd_display_EPD_800_480_4S_75_3(void);
void test_epd_display_EPD_1360_480_1085_3COLOR_horizontal(void);
void test_epd_display_EPD_1360_480_1085_3COLOR_vertical(void);
void test_epd_display_EPD_1360_480_1085_3COLOR_const(void);
void test_epd_display(void);

#ifdef __cplusplus
}
#endif
