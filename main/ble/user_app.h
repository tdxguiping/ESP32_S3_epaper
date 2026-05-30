#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

extern bool is_connected;

void Init_Bl(void);
void SendData_indicate(uint8_t *data, uint16_t len);
void Tdx01_indicate(uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif
