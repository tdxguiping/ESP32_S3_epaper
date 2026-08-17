#include "usb_console_worker.h"

#include <stdbool.h>
#include <stdint.h>
#include "esp_log.h"
#include "server_network_sta.h"
#include "tdx_cfg.h"

static const char *TAG = "usb_console_worker";
static bool s_initialized;
static bool s_wifi_connect_running;

static void handle_wifi_connect(void)
{
    if (s_wifi_connect_running) {
        ESP_LOGW(TAG, "wifi connect request ignored because previous connect is running");
        return;
    }

    s_wifi_connect_running = true;
    ESP_LOGI(TAG, "wifi connect start base_path=%s", USB_CONSOLE_BASE_PATH);
    uint8_t ret = User_Network_mode_app_new_credential(USB_CONSOLE_BASE_PATH);
    ESP_LOGI(TAG, "wifi connect done ret=0x%02x", ret);
    s_wifi_connect_running = false;
}

esp_err_t UsbConsoleWorker_Init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    s_initialized = true;
    ESP_LOGI(TAG, "inline mode enabled, no worker task or queue");
    return ESP_OK;
}

esp_err_t UsbConsoleWorker_SubmitJob(const char *name, usb_console_worker_job_fn_t job, void *ctx)
{
    if (!s_initialized || job == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "inline job start name=%s", name != NULL ? name : "unknown");
    job(ctx);
    ESP_LOGI(TAG, "inline job done name=%s", name != NULL ? name : "unknown");
    return ESP_OK;
}

esp_err_t UsbConsoleWorker_SubmitWifiConnect(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "inline worker not ready");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "wifi connect inline start");
    handle_wifi_connect();
    return ESP_OK;
}
