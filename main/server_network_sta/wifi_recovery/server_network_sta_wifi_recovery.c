#include "server_network_sta_wifi_recovery.h"

#include <stdbool.h>
#include <stdint.h>

#include "epd_display_app.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "server_network_sta.h"
#include "server_network_sta_wifi_work_time.h"

static const char *TAG = "wifi_recovery";

static TaskHandle_t s_recovery_task;
static bool s_initialized;
static bool s_power_cycle_attempted;
static uint32_t s_window_start_tick_encoded;
static bool s_factory_session_suppressed;
static bool s_factory_session_finished;
static bool s_credential_result_pending;

static esp_err_t save_attempted_marker(bool attempted)
{
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(WIFI_RECOVERY_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    if (attempted) {
        ret = nvs_set_u8(handle, WIFI_RECOVERY_NVS_ATTEMPTED_KEY, 1U);
    } else {
        ret = nvs_erase_key(handle, WIFI_RECOVERY_NVS_ATTEMPTED_KEY);
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ret = ESP_OK;
        }
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static bool status_has_usable_ip(const server_network_sta_status_t *status)
{
    return status != NULL && status->has_ip && status->ip[0] != '\0';
}

static bool status_is_recoverable_wifi_progress(
    const server_network_sta_status_t *status)
{
    if (status == NULL) {
        return false;
    }
    if (status->state == SERVER_NETWORK_STA_STATE_CONNECTING ||
        status->state == SERVER_NETWORK_STA_STATE_WAITING_IP ||
        status->state == SERVER_NETWORK_STA_STATE_DISCONNECTING) {
        return true;
    }
    return status->state == SERVER_NETWORK_STA_STATE_RETRY_WAIT &&
           status->retry_type == SERVER_NETWORK_RETRY_WIFI;
}

esp_err_t ServerNetworkStaWifiRecovery_OnReadyStable(void)
{
    if (!s_power_cycle_attempted) {
        return ESP_OK;
    }

    esp_err_t ret = save_attempted_marker(false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi recovery marker clear failed ret=%s",
                 esp_err_to_name(ret));
        return ret;
    }
    s_power_cycle_attempted = false;
    ESP_LOGI(TAG, "WiFi recovery marker cleared after READY stable");
    return ESP_OK;
}

esp_err_t ServerNetworkStaWifiRecovery_OnCredentialsChanged(void)
{
    __atomic_store_n(&s_credential_result_pending, true, __ATOMIC_RELEASE);
    __atomic_store_n(&s_factory_session_finished, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_factory_session_suppressed, false, __ATOMIC_RELEASE);
    ServerNetworkStaWifiWorkTime_CancelWifiRecoveryPowerCycle(
        "new_credential");
    esp_err_t ret = save_attempted_marker(false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi recovery marker clear for new credential failed ret=%s",
                 esp_err_to_name(ret));
        return ret;
    }
    s_power_cycle_attempted = false;
    __atomic_store_n(&s_window_start_tick_encoded,
                     (uint32_t)xTaskGetTickCount() + 1U,
                     __ATOMIC_RELEASE);
    ESP_LOGI(TAG, "WiFi recovery marker cleared for new credential");
    return ServerNetworkStaWifiRecovery_Start();
}

void ServerNetworkStaWifiRecovery_OnCredentialResultStarted(void)
{
    __atomic_store_n(&s_credential_result_pending, true, __ATOMIC_RELEASE);
    ServerNetworkStaWifiWorkTime_CancelWifiRecoveryPowerCycle(
        "credential_result_started");
}

void ServerNetworkStaWifiRecovery_OnCredentialResultFinished(void)
{
    __atomic_store_n(&s_credential_result_pending, false, __ATOMIC_RELEASE);
}

esp_err_t ServerNetworkStaWifiRecovery_OnFactoryCredentialsChanged(void)
{
    __atomic_store_n(&s_credential_result_pending, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_factory_session_finished, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_factory_session_suppressed, true, __ATOMIC_RELEASE);
    ServerNetworkStaWifiWorkTime_CancelWifiRecoveryPowerCycle(
        "factory_credential");
    esp_err_t ret = save_attempted_marker(false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi recovery marker clear for factory credential failed ret=%s",
                 esp_err_to_name(ret));
        return ret;
    }
    s_power_cycle_attempted = false;
    return ESP_OK;
}

void ServerNetworkStaWifiRecovery_OnFactoryTestFinished(void)
{
    __atomic_store_n(&s_factory_session_finished, true, __ATOMIC_RELEASE);
}

esp_err_t ServerNetworkStaWifiRecovery_Clear(void)
{
    __atomic_store_n(&s_credential_result_pending, false, __ATOMIC_RELEASE);
    // Stop the active observer so it cannot recreate a recovery request after Factory Reset.
    __atomic_store_n(&s_factory_session_suppressed, true, __ATOMIC_RELEASE);
    __atomic_store_n(&s_factory_session_finished, true, __ATOMIC_RELEASE);
    ServerNetworkStaWifiWorkTime_CancelWifiRecoveryPowerCycle(
        "recovery_state_clear");
    esp_err_t ret = save_attempted_marker(false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi recovery marker clear failed ret=%s",
                 esp_err_to_name(ret));
        return ret;
    }
    s_power_cycle_attempted = false;
    ESP_LOGI(TAG, "WiFi recovery cleared for Factory Reset");
    return ESP_OK;
}

static void wifi_recovery_task(void *arg)
{
    (void)arg;
    const TickType_t window_ticks =
        pdMS_TO_TICKS(WIFI_RECOVERY_CONNECT_WINDOW_MS);
    const TickType_t poll_ticks = pdMS_TO_TICKS(WIFI_RECOVERY_STATUS_POLL_MS);
    bool epd_wait_logged = false;
    bool request_submitted = false;
    bool terminal_logged = false;

    ESP_LOGI(TAG,
             "WiFi recovery observer started timeout_ms=%u attempted=%d",
             (unsigned int)WIFI_RECOVERY_CONNECT_WINDOW_MS,
             s_power_cycle_attempted ? 1 : 0);

    while (true) {
        server_network_sta_status_t status = {0};
        esp_err_t status_ret = ServerNetworkSta_GetStatus(&status);
        if (status_ret != ESP_OK) {
            ESP_LOGE(TAG, "WiFi recovery status read failed ret=%s",
                     esp_err_to_name(status_ret));
            break;
        }

        if (__atomic_load_n(&s_factory_session_suppressed,
                            __ATOMIC_ACQUIRE)) {
            if (__atomic_load_n(&s_factory_session_finished,
                                __ATOMIC_ACQUIRE)) {
                break;
            }
            vTaskDelay(poll_ticks);
            continue;
        }

        /*
         * A new credential owns the full protocol result window. Do not let
         * hard recovery power-cycle CH583 before its final result is handled.
         */
        if (__atomic_load_n(&s_credential_result_pending,
                            __ATOMIC_ACQUIRE)) {
            vTaskDelay(poll_ticks);
            continue;
        }

        if (status.state == SERVER_NETWORK_STA_STATE_READY &&
            status_has_usable_ip(&status) &&
            status.ready_stable_remaining_ms == 0) {
            (void)ServerNetworkStaWifiRecovery_OnReadyStable();
            break;
        }

        if (status_has_usable_ip(&status)) {
            if (request_submitted) {
                ServerNetworkStaWifiWorkTime_CancelWifiRecoveryPowerCycle(
                    "got_ip");
                request_submitted = false;
            }
            vTaskDelay(poll_ticks);
            continue;
        }

        if (s_power_cycle_attempted) {
            // The second boot stays powered and lets the existing manager retry.
            vTaskDelay(poll_ticks);
            continue;
        }

        uint32_t start_encoded =
            __atomic_load_n(&s_window_start_tick_encoded, __ATOMIC_ACQUIRE);
        TickType_t start_tick = start_encoded != 0 ?
                                (TickType_t)(start_encoded - 1U) :
                                xTaskGetTickCount();
        if ((xTaskGetTickCount() - start_tick) < window_ticks) {
            terminal_logged = false;
            vTaskDelay(poll_ticks);
            continue;
        }

        if (!status_is_recoverable_wifi_progress(&status)) {
            if (!terminal_logged) {
                terminal_logged = true;
                ESP_LOGW(TAG,
                         "WiFi recovery skipped terminal state=%s retry_type=%d reason=%d",
                         ServerNetworkSta_StateName(status.state),
                         (int)status.retry_type,
                         status.disconnect_reason);
            }
            vTaskDelay(poll_ticks);
            continue;
        }
        terminal_logged = false;

        // Recheck cancellation immediately before committing the recovery marker.
        if (__atomic_load_n(&s_factory_session_suppressed,
                            __ATOMIC_ACQUIRE)) {
            vTaskDelay(poll_ticks);
            continue;
        }

        // EPD must be exactly idle before the automatic recovery is requested.
        if (ServerNetworkStaEpdDisplay_IsBusy()) {
            if (!epd_wait_logged) {
                epd_wait_logged = true;
                ESP_LOGW(TAG,
                         "WiFi recovery postponed because EPD is BUSY state=%s",
                         ServerNetworkSta_StateName(status.state));
            }
            vTaskDelay(poll_ticks);
            continue;
        }

        esp_err_t marker_ret = save_attempted_marker(true);
        if (marker_ret != ESP_OK) {
            ESP_LOGE(TAG,
                     "WiFi recovery marker save failed; keep power on ret=%s",
                     esp_err_to_name(marker_ret));
            break;
        }
        s_power_cycle_attempted = true;

        esp_err_t request_ret =
            ServerNetworkStaWifiWorkTime_RequestWifiRecoveryPowerCycle(
                WIFI_RECOVERY_WAKE_SECONDS);
        if (request_ret != ESP_OK) {
            ESP_LOGE(TAG,
                     "WiFi recovery power cycle request failed ret=%s",
                     esp_err_to_name(request_ret));
            (void)save_attempted_marker(false);
            s_power_cycle_attempted = false;
            break;
        }
        request_submitted = true;
        ESP_LOGW(TAG,
                 "WiFi recovery deadline reached state=%s retry=%lu reason=%d wake_seconds=%u",
                 ServerNetworkSta_StateName(status.state),
                 (unsigned long)status.wifi_retry_count,
                 status.disconnect_reason,
                 (unsigned int)WIFI_RECOVERY_WAKE_SECONDS);
        vTaskDelay(poll_ticks);
    }

    s_recovery_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t ServerNetworkStaWifiRecovery_Init(void)
{
#if !WIFI_RECOVERY_POWER_CYCLE_ENABLE
    s_initialized = true;
    return ESP_OK;
#else
    nvs_handle_t handle = 0;
    uint8_t attempted = 0;
    esp_err_t ret = nvs_open(WIFI_RECOVERY_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_OK) {
        ret = nvs_get_u8(handle, WIFI_RECOVERY_NVS_ATTEMPTED_KEY, &attempted);
        nvs_close(handle);
    }
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK;
        attempted = 0;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi recovery marker load failed ret=%s",
                 esp_err_to_name(ret));
        return ret;
    }
    s_power_cycle_attempted = attempted != 0;
    s_initialized = true;
    ESP_LOGI(TAG, "WiFi recovery initialized attempted=%d",
             s_power_cycle_attempted ? 1 : 0);
    return ESP_OK;
#endif
}

esp_err_t ServerNetworkStaWifiRecovery_Start(void)
{
#if !WIFI_RECOVERY_POWER_CYCLE_ENABLE
    return ESP_OK;
#else
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_recovery_task != NULL) {
        return ESP_OK;
    }
    __atomic_store_n(&s_window_start_tick_encoded,
                     (uint32_t)xTaskGetTickCount() + 1U,
                     __ATOMIC_RELEASE);
    BaseType_t task_ret = xTaskCreate(wifi_recovery_task,
                                      "wifi_recovery",
                                      WIFI_RECOVERY_TASK_STACK_SIZE,
                                      NULL,
                                      WIFI_RECOVERY_TASK_PRIORITY,
                                      &s_recovery_task);
    if (task_ret != pdPASS) {
        s_recovery_task = NULL;
        ESP_LOGE(TAG, "WiFi recovery observer task create failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
#endif
}
