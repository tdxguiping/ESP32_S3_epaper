#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
#include "display_bsp.h"
extern ePaperPort ePaperDisplay;
extern "C" {
#endif

esp_err_t ServerNetworkStaEpdDisplay_Init(void);
esp_err_t ServerNetworkStaEpdDisplay_Queue(const uint8_t *display_buf, size_t display_size);
esp_err_t ServerNetworkStaEpdDisplay_QueueToScreen(const uint8_t *display_buf, size_t display_size, uint8_t epd_which_one);

#ifdef __cplusplus
}
#endif
