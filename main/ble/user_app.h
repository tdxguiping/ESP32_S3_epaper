#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

extern bool is_connected;

void Init_Bl(void);
void get_ble_mac_no_colon(char *out, size_t out_size);
esp_err_t SendData_indicate(uint8_t *data, uint16_t len);
void Tdx01_indicate(uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif
