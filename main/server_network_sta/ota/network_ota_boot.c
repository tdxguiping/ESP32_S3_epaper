#include "network_ota_boot.h"

#include <stdbool.h>

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "server_network_sta_wifi_work_time.h"
#include "tdx_cfg.h"

static const char *TAG = "net-ota-boot";
static bool s_boot_was_pending_verify;
static bool s_pending_verify;

static const char *ota_state_name(esp_ota_img_states_t state)
{
    switch (state) {
    case ESP_OTA_IMG_NEW:
        return "ESP_OTA_IMG_NEW";
    case ESP_OTA_IMG_PENDING_VERIFY:
        return "ESP_OTA_IMG_PENDING_VERIFY";
    case ESP_OTA_IMG_VALID:
        return "ESP_OTA_IMG_VALID";
    case ESP_OTA_IMG_INVALID:
        return "ESP_OTA_IMG_INVALID";
    case ESP_OTA_IMG_ABORTED:
        return "ESP_OTA_IMG_ABORTED";
    case ESP_OTA_IMG_UNDEFINED:
        return "ESP_OTA_IMG_UNDEFINED";
    default:
        return "UNKNOWN";
    }
}

esp_err_t NetworkOtaBoot_Init(void)
{
    __atomic_store_n(&s_boot_was_pending_verify, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_pending_verify, false, __ATOMIC_RELEASE);

    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition == NULL) {
        ESP_LOGE(TAG, "OTA boot state failed: running partition is null");
        return ESP_ERR_NOT_FOUND;
    }

    if (running_partition->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        return ESP_OK;
    }

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t err = esp_ota_get_state_partition(running_partition, &state);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA boot state failed: get state ret=%s",
                 esp_err_to_name(err));
        return err;
    }

    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        return ESP_OK;
    }

    __atomic_store_n(&s_boot_was_pending_verify, true, __ATOMIC_RELEASE);
    __atomic_store_n(&s_pending_verify, true, __ATOMIC_RELEASE);

    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGW(TAG,
             "pending verify version=%s partition=%s addr=0x%lx state=%s reset_reason=%d",
             app != NULL ? app->version : "<unknown>",
             running_partition->label,
             (unsigned long)running_partition->address,
             ota_state_name(state),
             (int)esp_reset_reason());
    return ESP_OK;
}

bool NetworkOtaBoot_WasPendingVerify(void)
{
    return __atomic_load_n(&s_boot_was_pending_verify, __ATOMIC_ACQUIRE);
}

bool NetworkOtaBoot_IsPendingVerify(void)
{
    return __atomic_load_n(&s_pending_verify, __ATOMIC_ACQUIRE);
}

void NetworkOtaBoot_EnablePendingProtection(void)
{
    if (!NetworkOtaBoot_IsPendingVerify()) {
        return;
    }

    ServerNetworkStaWifiWorkTime_SetOtaPendingVerify(true);
}

static esp_err_t confirm_current_image(const char *phase, bool log_timing)
{
    uint32_t elapsed_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition == NULL) {
        ESP_LOGE(TAG,
                 "confirm current image failed phase=%s stage=running_partition elapsed_ms=%lu",
                 phase,
                 (unsigned long)elapsed_ms);
        return ESP_ERR_NOT_FOUND;
    }

    if (running_partition->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        return ESP_OK;
    }

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t err = esp_ota_get_state_partition(running_partition, &state);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "confirm current image failed phase=%s stage=get_state ret=%s elapsed_ms=%lu",
                 phase,
                 esp_err_to_name(err),
                 (unsigned long)elapsed_ms);
        return err;
    }
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        return ESP_OK;
    }

    const esp_app_desc_t *app = esp_app_get_description();
    if (log_timing) {
        ESP_LOGI(TAG,
                 "local image confirmation begin version=%s partition=%s elapsed_ms=%lu",
                 app != NULL ? app->version : "<unknown>",
                 running_partition->label,
                 (unsigned long)elapsed_ms);
    }

    if (!NetworkOtaBoot_IsPendingVerify()) {
        /*
         * Retry the authoritative otadata check here so an early transient read
         * failure cannot leave a pending image unconfirmed after HTTP recovery is ready.
         */
        __atomic_store_n(&s_boot_was_pending_verify, true, __ATOMIC_RELEASE);
        __atomic_store_n(&s_pending_verify, true, __ATOMIC_RELEASE);
        ServerNetworkStaWifiWorkTime_SetOtaPendingVerify(true);
        ESP_LOGW(TAG, "pending verify rediscovered before image confirmation");
    }

    err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "confirm current image failed phase=%s stage=mark_valid ret=%s elapsed_ms=%lu",
                 phase,
                 esp_err_to_name(err),
                 (unsigned long)elapsed_ms);
        return err;
    }

    __atomic_store_n(&s_pending_verify, false, __ATOMIC_RELEASE);
    ServerNetworkStaWifiWorkTime_SetOtaPendingVerify(false);

    elapsed_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if (log_timing && elapsed_ms > USER_OTA_LOCAL_CONFIRM_WARNING_MS) {
        ESP_LOGW(TAG,
                 "local image confirmation late elapsed_ms=%lu warning_ms=%u power_cut_limit_ms=%u",
                 (unsigned long)elapsed_ms,
                 (unsigned int)USER_OTA_LOCAL_CONFIRM_WARNING_MS,
                 (unsigned int)USER_OTA_CH583_POWER_CUT_LIMIT_MS);
    }
    ESP_LOGI(TAG,
             "current image confirmed version=%s partition=%s phase=%s elapsed_ms=%lu",
             app != NULL ? app->version : "<unknown>",
             running_partition->label,
             phase,
             (unsigned long)elapsed_ms);
    return ESP_OK;
}

esp_err_t NetworkOtaBoot_ConfirmAfterLocalInit(void)
{
    return confirm_current_image("local_ready", true);
}

esp_err_t NetworkOtaBoot_ConfirmCurrentImage(void)
{
    return confirm_current_image("http_ready_retry", false);
}
