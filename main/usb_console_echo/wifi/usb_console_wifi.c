#include "usb_console_wifi.h"

#include <stdbool.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"
#include "server_network_sta.h"
#include "server_network_sta_wifi_credential.h"
#include "server_network_sta_wifi_ssid_candidate_store.h"
#include "tdx_cfg.h"
#include "usb_console_common.h"
#include "usb_console_worker.h"

static const char *TAG = "usb_console_wifi";

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
    char ssid[SERVER_NETWORK_STA_WIFI_SSID_BUFFER_SIZE] = {0};
    char password[SERVER_NETWORK_STA_WIFI_PASSWORD_BUFFER_SIZE] = {0};

    if (request != NULL && response != NULL &&
        UsbConsoleCommon_JsonFuncEquals(request->body, "wifi_status")) {
        server_network_sta_status_t status = {0};
        esp_err_t ret = ServerNetworkSta_GetStatus(&status);
        if (ret != ESP_OK) {
            return ret;
        }
        return UsbConsoleCommon_SetJsonf(
            response, 200, "OK",
            "{\"func\":\"wifi_status_result\",\"state\":\"%s\",\"state_id\":%d,"
            "\"retry_type\":%d,\"ap_connected\":%d,\"has_ip\":%d,\"ip\":\"%s\","
            "\"http_running\":%d,\"http_ready\":%d,\"mdns_ready\":%d,"
            "\"wifi_retry_count\":%lu,\"wifi_retry_after_ms\":%lu,"
            "\"http_retry_count\":%lu,\"http_retry_after_ms\":%lu,"
            "\"mdns_retry_count\":%lu,\"mdns_retry_after_ms\":%lu,"
            "\"last_result\":%d,\"connection_generation\":%lu,"
            "\"credential_generation\":%lu,\"disconnect_purpose\":%d,"
            "\"disconnect_purpose_name\":\"%s\","
            "\"ready_stable_remaining_ms\":%lu,"
            "\"disconnect_reason\":%d,\"rssi\":%d}",
            ServerNetworkSta_StateName(status.state), (int)status.state,
            (int)status.retry_type, status.ap_connected ? 1 : 0,
            status.has_ip ? 1 : 0, status.ip,
            status.http_running ? 1 : 0, status.http_ready ? 1 : 0,
            status.mdns_ready ? 1 : 0,
            (unsigned long)status.wifi_retry_count,
            (unsigned long)status.wifi_retry_after_ms,
            (unsigned long)status.http_retry_count,
            (unsigned long)status.http_retry_after_ms,
            (unsigned long)status.mdns_retry_count,
            (unsigned long)status.mdns_retry_after_ms,
            status.last_result,
            (unsigned long)status.connection_generation,
            (unsigned long)status.credential_generation,
            status.disconnect_purpose,
            ServerNetworkSta_DisconnectPurposeName(status.disconnect_purpose),
            (unsigned long)status.ready_stable_remaining_ms,
            status.disconnect_reason, status.rssi);
    }

    if (request == NULL || response == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (ServerNetworkStaWifiCredential_JsonHasEmbeddedNul(
            request->body, request->body_len)) {
        ESP_LOGW(TAG, "wifi JSON rejected reason=embedded_nul body_len=%u",
                 (unsigned int)request->body_len);
        return UsbConsoleCommon_SetJsonf(
            response,
            200,
            "OK",
            "{\"func\":\"wifi_result\",\"result\":%d,\"message\":\"invalid json\",\"error\":\"embedded_nul\"}",
            TDX_JSON_RESULT_JSON_INVALID);
    }

    if (!UsbConsoleCommon_JsonFuncEquals(request->body, "wifi")) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    cJSON *root = cJSON_ParseWithLength(request->body, request->body_len);
    cJSON *ssid_item = root != NULL
                           ? cJSON_GetObjectItemCaseSensitive(root, "ssid")
                           : NULL;
    cJSON *key_item = root != NULL
                          ? cJSON_GetObjectItemCaseSensitive(root, "key")
                          : NULL;
    bool has_ssid = ssid_item != NULL;
    bool has_key = key_item != NULL;
    server_network_sta_wifi_credential_result_t validation =
        SERVER_NETWORK_STA_WIFI_CREDENTIAL_OK;
    if (cJSON_IsString(ssid_item) && ssid_item->valuestring != NULL &&
        cJSON_IsString(key_item) && key_item->valuestring != NULL) {
        validation = ServerNetworkStaWifiCredential_Validate(
            ssid_item->valuestring, key_item->valuestring);
    } else if (has_ssid && has_key) {
        validation = !cJSON_IsString(ssid_item)
                         ? SERVER_NETWORK_STA_WIFI_CREDENTIAL_SSID_INVALID_UTF8
                         : SERVER_NETWORK_STA_WIFI_CREDENTIAL_PASSWORD_INVALID_UTF8;
    }

    if (!has_ssid || !has_key ||
        validation != SERVER_NETWORK_STA_WIFI_CREDENTIAL_OK) {
        bool ssid_error = has_ssid && has_key &&
                          ServerNetworkStaWifiCredential_ResultIsSsidError(
                              validation);
        int result = !has_ssid ? TDX_JSON_RESULT_WIFI_SSID_MISSING :
                     !has_key ? TDX_JSON_RESULT_WIFI_KEY_MISSING :
                     ssid_error ? TDX_JSON_RESULT_WIFI_SSID_INVALID :
                     TDX_JSON_RESULT_WIFI_KEY_INVALID;
        const char *error = !has_ssid ? "ssid_missing" :
                            !has_key ? "key_missing" :
                            ssid_error ? "ssid_invalid" :
                            "key_invalid";
        size_t ssid_length = cJSON_IsString(ssid_item) &&
                                     ssid_item->valuestring != NULL
                                 ? strlen(ssid_item->valuestring)
                                 : 0U;
        size_t password_length = cJSON_IsString(key_item) &&
                                         key_item->valuestring != NULL
                                     ? strlen(key_item->valuestring)
                                     : 0U;
        ESP_LOGW(TAG, "wifi invalid request ssid_len=%u password_len=%u",
                 (unsigned int)ssid_length,
                 (unsigned int)password_length);
        cJSON_Delete(root);
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"wifi_result\",\"result\":%d,\"message\":\"invalid wifi config\",\"error\":\"%s\"}",
                                         result,
                                         error);
    }

    memcpy(ssid, ssid_item->valuestring,
           strlen(ssid_item->valuestring) + 1U);
    memcpy(password, key_item->valuestring,
           strlen(key_item->valuestring) + 1U);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "wifi request ssid=%s password_len=%u body_len=%u",
             ssid,
             (unsigned int)strlen(password),
             (unsigned int)request->body_len);

    esp_err_t old_ret = save_wifi_namespace(ssid, password);
    esp_err_t net_ret = save_net80211_namespace(ssid, password);
    esp_err_t candidate_ret = old_ret == ESP_OK && net_ret == ESP_OK
                                  ? ServerNetworkStaWifiSsidCandidateStore_Clear()
                                  : ESP_OK;
    ESP_LOGI(TAG, "wifi nvs result wifi=%s net80211=%s candidate_clear=%s",
             esp_err_to_name(old_ret),
             esp_err_to_name(net_ret),
             esp_err_to_name(candidate_ret));
    if (old_ret != ESP_OK || net_ret != ESP_OK || candidate_ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi save failed wifi=%s net80211=%s candidate_clear=%s",
                 esp_err_to_name(old_ret),
                 esp_err_to_name(net_ret),
                 esp_err_to_name(candidate_ret));
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"wifi_result\",\"result\":%d,\"message\":\"save wifi failed\",\"error\":\"wifi_save_failed\"}",
                                         TDX_JSON_RESULT_WIFI_SAVE_FAILED);
    }

    esp_err_t submit_ret = UsbConsoleWorker_SubmitWifiConnect();
    ESP_LOGI(TAG, "wifi connect submit ret=%s", esp_err_to_name(submit_ret));
    if (submit_ret != ESP_OK) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"wifi_result\",\"result\":%d,\"message\":\"connect submit failed\",\"error\":\"wifi_connect_submit_failed\"}",
                                         TDX_JSON_RESULT_WIFI_CONNECT_SUBMIT_FAILED);
    }

    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"wifi_result\",\"result\":%d,\"message\":\"ok\"}",
                                     TDX_JSON_RESULT_OK);
}
