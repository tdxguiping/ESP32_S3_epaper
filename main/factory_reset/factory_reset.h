#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    FACTORY_RESET_TRIGGER_GPIO28 = 0,
    FACTORY_RESET_TRIGGER_DEVICE_INFO_KEY_PB1,
    FACTORY_RESET_TRIGGER_KEY_EVENT_PB1_PRESS,
} factory_reset_trigger_t;

esp_err_t FactoryReset_Init(const char *base_path);
esp_err_t FactoryReset_HandleStartupWelcome(void);
bool FactoryReset_IsBusy(void);
esp_err_t FactoryReset_Request(factory_reset_trigger_t trigger,
                               uint16_t protocol_seq);
