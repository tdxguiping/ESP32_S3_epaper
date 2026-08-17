#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t TdxSharedSpi_Init(void);
esp_err_t TdxSharedSpi_Lock(TickType_t ticks_to_wait);
esp_err_t TdxSharedSpi_LockForEpdSdPowerTest(TickType_t ticks_to_wait);
void TdxSharedSpi_UnlockForEpdSdPowerTest(void);
bool TdxSharedSpi_HasNormalRequests(void);
bool TdxSharedSpi_IsBusy(void);
void TdxSharedSpi_Unlock(void);

#ifdef __cplusplus
}
#endif
