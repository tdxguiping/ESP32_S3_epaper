#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t EpdDisplayMode_Init(void);
esp_err_t EpdDisplayMode_Set(uint8_t mode);
esp_err_t EpdDisplayMode_SetBySlideshowSwitch(bool sw);
uint8_t EpdDisplayMode_Get(void);
const char *EpdDisplayMode_ToString(uint8_t mode);

#ifdef __cplusplus
}
#endif
