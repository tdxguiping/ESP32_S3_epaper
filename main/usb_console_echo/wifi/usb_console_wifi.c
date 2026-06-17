#include "usb_console_wifi.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "tdx_cfg.h"
#include "usb_console_common.h"
#include "usb_console_worker.h"

static const char *TAG = "usb_console_wifi";

static bool wifi_text_is_valid(const char *text, size_t max_len)
{
    size_t len = text != NULL ? strlen(text) : 0;
    return len > 0 && len < max_len;
}

static esp_err_t save_wifi_namespace(const char *ssid, const char *password)
{
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open("wifi", NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_str(handle, "ssid", ssid);
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, "password", password);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static esp_err_t save_net80211_namespace(const char *ssid, const char *password)
{
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open("nvs.net80211", NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_blob(handle, "sta.ssid", ssid, strlen(ssid) + 1);
    if (ret == ESP_OK) {
        ret = nvs_set_blob(handle, "sta.pswd", password, strlen(password) + 1);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

esp_err_t UsbConsoleWifi_Handle(const usb_console_http_request_t *request,
                                usb_console_http_response_t *response)
{
    char ssid[33] = {0};
    char password[65] = {0};

    if (request == NULL || response == NULL ||
        !UsbConsoleCommon_JsonFuncEquals(request->body, "wifi")) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    bool has_ssid = UsbConsoleCommon_JsonString(request->body, "ssid", ssid, sizeof(ssid));
    bool has_key = UsbConsoleCommon_JsonString(request->body, "key", password, sizeof(password));
    if (!has_ssid || !has_key || !wifi_text_is_valid(ssid, sizeof(ssid)) || strlen(password) >= sizeof(password)) {
        int result = !has_ssid ? TDX_JSON_RESULT_WIFI_SSID_MISSING :
                     !has_key ? TDX_JSON_RESULT_WIFI_KEY_MISSING :
                     !wifi_text_is_valid(ssid, sizeof(ssid)) ? TDX_JSON_RESULT_WIFI_SSID_INVALID :
                     TDX_JSON_RESULT_WIFI_KEY_INVALID;
        ESP_LOGW(TAG, "wifi invalid request ssid_len=%u password_len=%u",
                 (unsigned int)strlen(ssid),
                 (unsigned int)strlen(password));
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"wifi_result\",\"result\":%d,\"message\":\"invalid wifi config\"}",
                                         result);
    }

    ESP_LOGI(TAG, "wifi request ssid=%s password=%s body_len=%u",
             ssid,
             password,
             (unsigned int)request->body_len);

    esp_err_t old_ret = save_wifi_namespace(ssid, password);
    esp_err_t net_ret = save_net80211_namespace(ssid, password);
    ESP_LOGI(TAG, "wifi nvs result wifi=%s net80211=%s",
             esp_err_to_name(old_ret),
             esp_err_to_name(net_ret));
    if (old_ret != ESP_OK || net_ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi save failed wifi=%s net80211=%s",
                 esp_err_to_name(old_ret),
                 esp_err_to_name(net_ret));
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"wifi_result\",\"result\":%d,\"message\":\"save wifi failed\"}",
                                         TDX_JSON_RESULT_WIFI_SAVE_FAILED);
    }

    ESP_LOGI(TAG, "wifi saved ssid=%s password=%s",
             ssid,
             password);
    esp_err_t submit_ret = UsbConsoleWorker_SubmitWifiConnect();
    ESP_LOGI(TAG, "wifi connect submit ret=%s", esp_err_to_name(submit_ret));
    if (submit_ret != ESP_OK) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"wifi_result\",\"result\":%d,\"message\":\"connect submit failed\"}",
                                         TDX_JSON_RESULT_WIFI_CONNECT_SUBMIT_FAILED);
    }

    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"wifi_result\",\"result\":%d,\"message\":\"ok\"}",
                                     TDX_JSON_RESULT_OK);
}
