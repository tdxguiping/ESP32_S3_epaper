#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ServerNetworkStaWifiWorkTime_ProcessJson(httpd_req_t *req,
                                                   const char *body,
                                                   size_t body_len);
esp_err_t ServerNetworkStaWifiWorkTime_Init(void);
esp_err_t ServerNetworkStaWifiWorkTime_SetAndSave(uint32_t seconds);
void ServerNetworkStaWifiWorkTime_OnNetworkData(void);
void ServerNetworkStaWifiWorkTime_OnHttpNetworkActivity(void);
void ServerNetworkStaWifiWorkTime_OnCh583Activity(void);
void ServerNetworkStaWifiWorkTime_OnCh583Initialized(void);
void ServerNetworkStaWifiWorkTime_RequestOneShotPowerOffCountdown(uint32_t seconds);
void ServerNetworkStaWifiWorkTime_SetFactoryResetGuard(bool active);
void ServerNetworkStaWifiWorkTime_RequestFactoryResetPowerCycle(uint32_t wake_seconds);
void ServerNetworkStaWifiWorkTime_SetImageSaveInProgress(bool in_progress);
bool ServerNetworkStaWifiWorkTime_IsImageSaveInProgress(void);
void ServerNetworkStaWifiWorkTime_SetDailyImageInProgress(bool in_progress);
bool ServerNetworkStaWifiWorkTime_IsDailyImageInProgress(void);
void ServerNetworkStaWifiWorkTime_SetOtaWriteInProgress(bool in_progress);
void ServerNetworkStaWifiWorkTime_SetOtaReceiveInProgress(bool in_progress);
void ServerNetworkStaWifiWorkTime_SetOtaPendingVerify(bool pending);
// Hold CH583 power-off while one BLE/CH583 WiFi request is still within its absolute deadline.
void ServerNetworkStaWifiWorkTime_StartWifiConnectGuard(uint32_t timeout_ms);
// Start the guard only when no valid guard exists; an active deadline is never refreshed.
bool ServerNetworkStaWifiWorkTime_StartWifiConnectGuardIfInactive(uint32_t timeout_ms);
// Release only the dedicated WiFi connection guard; other power guards are unaffected.
void ServerNetworkStaWifiWorkTime_ClearWifiConnectGuard(const char *reason);
// Return the remaining absolute guard time. An expired guard clears itself and returns zero.
uint32_t ServerNetworkStaWifiWorkTime_GetWifiConnectGuardRemainingMs(void);

#ifdef __cplusplus
}
#endif
