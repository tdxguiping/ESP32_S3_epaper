#pragma once

#include <stdbool.h>

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
// Queue a base business state; the LED task owns all state transitions and physical output.
void UserLedStatus_Set(user_led_state_t state);
// Reference-count long-running activity sources without exposing LED timing to callers.
void UserLedStatus_ActivityBegin(user_led_activity_t source);
void UserLedStatus_ActivityEnd(user_led_activity_t source);
// Set or clear persistent faults through dedicated APIs so unrelated fault classes stay independent.
void UserLedStatus_SetWifiNoConfig(bool active);
void UserLedStatus_SetWifiAuthFailed(bool active);
void UserLedStatus_SetHttpFailed(bool active);
void UserLedStatus_SetStorageFailed(bool active);
void UserLedStatus_SetFatalError(bool active);
void UserLedStatus_ShowSuccess(void);
void UserLedStatus_ShowOperationFail(void);
void UserLedStatus_OtaBegin(void);
void UserLedStatus_OtaEnd(bool preparing_restart);
void UserLedStatus_FactoryResetBegin(void);
void UserLedStatus_FactoryResetEnd(void);
// Keep the restart indication separate from fatal faults even though both use RED solid.
void UserLedStatus_SetRestartPending(bool active);
// Show the shutdown countdown before the final synchronous physical LED shutdown.
void UserLedStatus_SetPowerOffPending(bool active);
// Wait until the LED task has stopped both blink engines and forced PB5/PB6 off.
esp_err_t UserLedStatus_PreparePowerOffSync(void);
// Compatibility wrapper for older callers that do not consume the synchronous result.
void UserLedStatus_PreparePowerOff(void);

#ifdef __cplusplus
}
#endif
