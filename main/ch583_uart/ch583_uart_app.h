#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t Ch583UartApp_Init(void);
void Ch583UartApp_SetBleDataBusinessReady(void);

#ifdef __cplusplus
}
#endif
