#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t TdxSharedSpi_Init(void);
esp_err_t TdxSharedSpi_Lock(TickType_t ticks_to_wait);
void TdxSharedSpi_Unlock(void);

#ifdef __cplusplus
}
#endif
