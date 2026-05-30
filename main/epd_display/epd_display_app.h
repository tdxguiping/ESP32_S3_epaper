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

#ifdef __cplusplus
}
#endif
