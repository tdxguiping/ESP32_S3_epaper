#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    USER_LED_STATE_OFF = 0,
    USER_LED_STATE_BOOTING,
    USER_LED_STATE_WIFI_CONNECTING,
    USER_LED_STATE_SERVER_READY,
    USER_LED_STATE_TRANSFER,
    USER_LED_STATE_EPD_REFRESH,
    USER_LED_STATE_SUCCESS,
    USER_LED_STATE_WIFI_FAIL,
    USER_LED_STATE_OPERATION_FAIL,
} user_led_state_t;

esp_err_t UserLedStatus_Init(void);
void UserLedStatus_Set(user_led_state_t state);

#ifdef __cplusplus
}
#endif
