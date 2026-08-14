#ifndef WIFI_TIME_WAKE_H
#define WIFI_TIME_WAKE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CONFIG.h"

typedef enum
{
    WIFI_TIME_WAKE_OK = 0,
    WIFI_TIME_WAKE_BAD_LEN,
    WIFI_TIME_WAKE_BAD_ARG,
    WIFI_TIME_WAKE_BAD_TIME
} wifi_time_wake_status_t;

void WifiTimeWake_Init(tmosTaskID task_id, uint16_t timed_wake_event);
void WifiTimeWake_LoadTimeBackup(void);

wifi_time_wake_status_t WifiTimeWake_TimeSet(char *arg, uint16_t len);
wifi_time_wake_status_t WifiTimeWake_TimeGetStatus(char *status_buf, uint16_t status_buf_size);

wifi_time_wake_status_t WifiTimeWake_SetWakeTimerOn(uint32_t seconds);
wifi_time_wake_status_t WifiTimeWake_SetWakeTimerOff(void);

void WifiTimeWake_BeginWakeSession(uint8_t from_timed_wake);
void WifiTimeWake_SaveTimeIfDirtyOrValid(void);
void WifiTimeWake_FinalizeForWifiOff(void);

uint8_t WifiTimeWake_IsTimedWakeArmed(void);
uint8_t WifiTimeWake_OnTimedWakeEvent(void);

#ifdef __cplusplus
}
#endif

#endif
