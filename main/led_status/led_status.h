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
    USER_LED_STATE_EPD_FINISHED,
    USER_LED_STATE_SUCCESS,
    USER_LED_STATE_WIFI_FAIL,
    USER_LED_STATE_OPERATION_FAIL,
} user_led_state_t;

typedef enum {
    USER_LED_ACTIVITY_NETWORK = 0,
    USER_LED_ACTIVITY_UART_RX,
    USER_LED_ACTIVITY_UART_TX,
    USER_LED_ACTIVITY_EPD,
    USER_LED_ACTIVITY_COUNT,
} user_led_activity_t;

esp_err_t UserLedStatus_Init(void);
void UserLedStatus_Set(user_led_state_t state);
void UserLedStatus_ActivityBegin(user_led_activity_t source);
void UserLedStatus_ActivityEnd(user_led_activity_t source);
void UserLedStatus_PreparePowerOff(void);

#ifdef __cplusplus
}
#endif
