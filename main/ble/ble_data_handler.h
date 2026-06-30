#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t UserBleDataHandler_Init(void);
esp_err_t User_QueueBleWriteBytes(const uint8_t *data, uint16_t len);
void User_HandleWifiJsonText(const char *json_text);
void User_HandleWifiJsonTextFromCh583(const char *json_text);

extern bool WiFi_config_net;
extern bool WiFi_config_from_ch583;
extern bool WiFi_config_from_ble;

#ifdef __cplusplus
}
#endif
