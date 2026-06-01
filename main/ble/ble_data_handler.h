#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t UserBleDataHandler_Init(void);
esp_err_t User_QueueBleWriteBytes(const uint8_t *data, uint16_t len);
void send_base_info_to_mobile(void);

#ifdef __cplusplus
}
#endif
