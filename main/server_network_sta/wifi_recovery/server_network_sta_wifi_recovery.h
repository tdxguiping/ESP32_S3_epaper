#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// One cold-start connection window is observed without changing WiFi manager retries.
#define WIFI_RECOVERY_POWER_CYCLE_ENABLE       1
#define WIFI_RECOVERY_CONNECT_WINDOW_MS        20000U
#define WIFI_RECOVERY_WAKE_SECONDS             1U
#define WIFI_RECOVERY_STATUS_POLL_MS           200U
#define WIFI_RECOVERY_TASK_STACK_SIZE          3072U
#define WIFI_RECOVERY_TASK_PRIORITY            4U
#define WIFI_RECOVERY_NVS_NAMESPACE            "wifi_recovery"
#define WIFI_RECOVERY_NVS_ATTEMPTED_KEY        "attempted"

#if WIFI_RECOVERY_CONNECT_WINDOW_MS < 20000U
#error "WiFi recovery window must allow association and DHCP recovery"
#endif

#if WIFI_RECOVERY_WAKE_SECONDS < 1U
#error "WiFi recovery wake interval must be at least one second"
#endif

// Load the persistent one-shot marker after NVS is ready.
esp_err_t ServerNetworkStaWifiRecovery_Init(void);

// Start one absolute cold-start observation window. An active window is never refreshed.
esp_err_t ServerNetworkStaWifiRecovery_Start(void);

// A successfully saved credential is a new recovery session.
esp_err_t ServerNetworkStaWifiRecovery_OnCredentialsChanged(void);

// Clear recovery state for factory credentials without arming a hard recovery.
esp_err_t ServerNetworkStaWifiRecovery_OnFactoryCredentialsChanged(void);

// Release an existing observer after the factory connection check finishes.
void ServerNetworkStaWifiRecovery_OnFactoryTestFinished(void);

// Factory reset clears the recovery session without starting a new observer.
esp_err_t ServerNetworkStaWifiRecovery_Clear(void);

// Clear the marker after the recovered connection reaches stable READY.
esp_err_t ServerNetworkStaWifiRecovery_OnReadyStable(void);

#ifdef __cplusplus
}
#endif
