#include "ble_data_handler.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "ch583_wifi_uart_protocol.h"
#include "debug_output.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "server_network_sta.h"
#include "server_network_sta_wifi_credential.h"
#include "server_network_sta_wifi_recovery.h"
#include "server_network_sta_wifi_ssid_candidate.h"
#include "server_network_sta_wifi_ssid_candidate_store.h"
#include "server_network_sta_wifi_work_time.h"
#include "tdx_cfg.h"
#include "user_app.h"

static const char *TAG = "ble_data";

static void log_invalid_wifi_ssid_bytes(const char *ssid)
{
    if (ssid == NULL) {
        return;
    }

    size_t ssid_length = strlen(ssid);
    char hex_text[SERVER_NETWORK_STA_WIFI_SSID_MAX_BYTES * 3U + 1U] = {0};
    size_t output_offset = 0;
    for (size_t index = 0;
         index < ssid_length &&
         index < SERVER_NETWORK_STA_WIFI_SSID_MAX_BYTES;
         index++) {
        int written = snprintf(hex_text + output_offset,
                               sizeof(hex_text) - output_offset,
                               index + 1U < ssid_length ? "%02X " : "%02X",
                               (unsigned int)(uint8_t)ssid[index]);
        if (written <= 0 || (size_t)written >=
                                sizeof(hex_text) - output_offset) {
            break;
        }
        output_offset += (size_t)written;
    }

    ESP_LOGW(TAG, "WiFi invalid SSID bytes len=%u hex=%s",
             (unsigned int)ssid_length, hex_text);
}

static_assert(WIFI_CONFIG_RESULT_TIMEOUT_MS <=
                  WIFI_CONNECT_POWER_GUARD_MAX_MS,
              "WiFi config result timeout must not exceed its power guard");

typedef struct {
    char func[32];
    char ssid[SERVER_NETWORK_STA_WIFI_SSID_BUFFER_SIZE];
    char key[SERVER_NETWORK_STA_WIFI_PASSWORD_BUFFER_SIZE];
    server_network_sta_ssid_candidate_config_t candidate_config;
} wifi_config_json_t;

typedef struct {
    char func[32];
    int seconds;
} wifi_work_time_json_t;

static wifi_config_json_t wifi_cfg;
static wifi_work_time_json_t wifi_work_time_cfg;
static uint8_t Bl_Data_Ready = 0;
static uint8_t Wifi_connect_OK = 0;
uint8_t net_connect_OK = 0;
bool WiFi_config_net = false;
bool WiFi_config_from_ch583 = false;
bool WiFi_config_from_ble = false;

static int parse_wifi_candidate_config(
    const cJSON *root,
    server_network_sta_ssid_candidate_config_t *config,
    const char **message)
{
    if (root == NULL || config == NULL || message == NULL) {
        return TDX_JSON_RESULT_WIFI_SSID_INVALID;
    }
    memset(config, 0, sizeof(*config));
    config->selected_index = -1;
    *message = "ssid invalid";

    const cJSON *ssid_item = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    const cJSON *key_item = cJSON_GetObjectItemCaseSensitive(root, "key");
    const cJSON *hex_item = cJSON_GetObjectItemCaseSensitive(root, "ssidHex");
    const cJSON *country_item = cJSON_GetObjectItemCaseSensitive(root, "country");
    if (ssid_item == NULL) {
        *message = "ssid missing";
        return TDX_JSON_RESULT_WIFI_SSID_MISSING;
    }
    if (key_item == NULL) {
        *message = "key missing";
        return TDX_JSON_RESULT_WIFI_KEY_MISSING;
    }
    if (!cJSON_IsString(ssid_item) || ssid_item->valuestring == NULL) {
        return TDX_JSON_RESULT_WIFI_SSID_INVALID;
    }
    if (!cJSON_IsString(key_item) || key_item->valuestring == NULL) {
        *message = "key invalid";
        return TDX_JSON_RESULT_WIFI_KEY_INVALID;
    }

    server_network_sta_wifi_credential_result_t credential_result =
        ServerNetworkStaWifiCredential_Validate(ssid_item->valuestring,
                                                key_item->valuestring);
    if (credential_result != SERVER_NETWORK_STA_WIFI_CREDENTIAL_OK) {
        bool ssid_error = ServerNetworkStaWifiCredential_ResultIsSsidError(
            credential_result);
        *message = ssid_error ? "ssid invalid" : "key invalid";
        return ssid_error ? TDX_JSON_RESULT_WIFI_SSID_INVALID
                          : TDX_JSON_RESULT_WIFI_KEY_INVALID;
    }
    strlcpy(config->display_ssid, ssid_item->valuestring,
            sizeof(config->display_ssid));
    strlcpy(config->password, key_item->valuestring,
            sizeof(config->password));

    if (country_item == NULL) {
        strlcpy(config->country, SERVER_NETWORK_STA_WIFI_DEFAULT_COUNTRY,
                sizeof(config->country));
    } else if (!cJSON_IsString(country_item) ||
               country_item->valuestring == NULL ||
               strlen(country_item->valuestring) != 2U) {
        *message = "country invalid";
        return TDX_JSON_RESULT_WIFI_COUNTRY_INVALID;
    } else {
        strlcpy(config->country, country_item->valuestring,
                sizeof(config->country));
    }
    if (!ServerNetworkStaWifiSsidCandidate_IsCountrySupported(
            config->country)) {
        *message = "country invalid";
        return TDX_JSON_RESULT_WIFI_COUNTRY_INVALID;
    }

    if (!cJSON_IsArray(hex_item)) {
        return TDX_JSON_RESULT_WIFI_SSID_INVALID;
    }
    int candidate_count = cJSON_GetArraySize(hex_item);
    int required_count =
        ServerNetworkStaWifiSsidCandidate_DisplayNeedsGbk(
            config->display_ssid) ? 2 : 1;
    if (candidate_count != required_count) {
        return TDX_JSON_RESULT_WIFI_SSID_INVALID;
    }
    config->candidate_count = (uint8_t)candidate_count;
    for (int index = 0; index < candidate_count; index++) {
        const cJSON *candidate_item = cJSON_GetArrayItem(hex_item, index);
        if (!cJSON_IsString(candidate_item) ||
            candidate_item->valuestring == NULL) {
            return TDX_JSON_RESULT_WIFI_SSID_INVALID;
        }
        server_network_sta_ssid_candidate_result_t decode_result =
            ServerNetworkStaWifiSsidCandidate_DecodeHex(
                candidate_item->valuestring,
                index == 0 ? SERVER_NETWORK_STA_SSID_ENCODING_UTF8
                           : SERVER_NETWORK_STA_SSID_ENCODING_GBK,
                &config->candidates[index]);
        if (decode_result != SERVER_NETWORK_STA_SSID_CANDIDATE_OK) {
            return TDX_JSON_RESULT_WIFI_SSID_INVALID;
        }
    }
    server_network_sta_ssid_candidate_result_t validation =
        ServerNetworkStaWifiSsidCandidate_ValidateConfig(config);
    if (validation != SERVER_NETWORK_STA_SSID_CANDIDATE_OK) {
        if (validation == SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_COUNTRY) {
            *message = "country invalid";
            return TDX_JSON_RESULT_WIFI_COUNTRY_INVALID;
        }
        return TDX_JSON_RESULT_WIFI_SSID_INVALID;
    }
    return TDX_JSON_RESULT_OK;
}

#define WIFI_INFO_NOTIFY_RETRY_COUNT 3
#define WIFI_INFO_NOTIFY_RETRY_DELAY_MS 150
#define WIFI_INFO_IP_READY_RECHECK_COUNT 3
#define WIFI_INFO_IP_READY_RECHECK_DELAY_MS 300

#if USER_BLE_ENABLE
typedef struct {
    uint16_t len;
    uint8_t data[USER_BLE_JSON_BUF_SIZE];
} user_ble_write_msg_t;

static QueueHandle_t s_ble_write_queue = NULL;
static TaskHandle_t s_ble_write_task = NULL;
#endif

typedef bool (*json_sender_t)(const char *json);

static bool ble_send_json(const char *json)
{
    if (json == NULL) {
        return false;
    }
    ESP_LOGI(TAG, "BLE TX JSON: %s", json);
#if USER_BLE_ENABLE
    return SendData_indicate((uint8_t *)json, (uint16_t)strlen(json)) == ESP_OK;
#else
    return ch583_wifi_uart_send_wifi_data(json) == 0;
#endif
}

static bool ch583_send_json(const char *json)
{
    if (json == NULL) {
        return false;
    }
    ESP_LOGI(TAG, "CH583 TX JSON: %s", json);
    return ch583_wifi_uart_send_wifi_data(json) == 0;
}

static json_sender_t s_active_send_json = ble_send_json;

static bool nvs_has_nonempty_str(const char *name_space, const char *key)
{
    nvs_handle_t handle = 0;
    size_t len = 0;
    bool has_value = false;

    if (nvs_open(name_space, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    if (nvs_get_str(handle, key, NULL, &len) == ESP_OK && len > 1) {
        has_value = true;
    }
    nvs_close(handle);
    return has_value;
}

static bool nvs_has_nonempty_blob_string(const char *name_space, const char *key)
{
    nvs_handle_t handle = 0;
    size_t len = 0;
    bool has_value = false;

    if (nvs_open(name_space, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    if (nvs_get_blob(handle, key, NULL, &len) == ESP_OK && len > 0) {
        uint8_t *value = (uint8_t *)malloc(len);
        if (value != NULL) {
            if (nvs_get_blob(handle, key, value, &len) == ESP_OK && value[0] != '\0') {
                has_value = true;
            }
            free(value);
        }
    }
    nvs_close(handle);
    return has_value;
}

static bool ble_has_saved_wifi_info(void)
{
    if (ServerNetworkStaWifiSsidCandidateStore_HasConfig()) {
        return true;
    }
    if (nvs_has_nonempty_str("wifi", "ssid")) {
        return true;
    }
    return nvs_has_nonempty_blob_string("nvs.net80211", "sta.ssid");
}

static void send_simple_result_with_sender(json_sender_t send_json,
                                           const char *func,
                                           int result,
                                           const char *message)
{
    char json[192];

    if (message == NULL || message[0] == '\0') {
        snprintf(json, sizeof(json), "{\"func\":\"%s\",\"result\":%d}",
                 func, result);
    } else {
        snprintf(json, sizeof(json), "{\"func\":\"%s\",\"result\":%d,\"message\":\"%s\"}",
                 func, result, message);
    }
    if (!send_json(json) && result != TDX_JSON_RESULT_BLE_SEND_FAILED) {
        ESP_LOGE(TAG, "JSON response send failed func=%s result=%d", func, result);
        snprintf(json, sizeof(json),
                 "{\"func\":\"ble_json_result\",\"result\":%d,\"message\":\"response send failed\"}",
                 TDX_JSON_RESULT_BLE_SEND_FAILED);
        (void)send_json(json);
    }
}

static bool send_base_info_to_mobile(void)
{
    char ip_str[sizeof("255.255.255.255")];
    char ssid_str[SERVER_NETWORK_STA_WIFI_SSID_BUFFER_SIZE] = {0};
    char ble_ver_str[4];
    char version_str[40];

    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    uint8_t ble_ver = ch583_wifi_uart_get_ble_ver();

    esp_netif_ip_info_t ip = {};
    esp_netif_t *esp_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    wifi_ap_record_t ap_info = {};

    if (esp_netif == NULL ||
        esp_netif_get_ip_info(esp_netif, &ip) != ESP_OK ||
        ip.ip.addr == 0) {
        ESP_LOGW(TAG, "wifi_info_result not sent: STA IP is not ready");
        return false;
    }

    net_connect_OK = 1;
    working_time = 0;

    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip.ip));

    if (ServerNetworkStaWifiSsidCandidateStore_GetDisplaySsid(
            ssid_str, sizeof(ssid_str)) == ESP_OK) {
        /* Candidate connections can use GBK bytes on air; JSON always uses
         * the separately stored UTF-8 display SSID. */
    } else if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        size_t ssid_length = strnlen((const char *)ap_info.ssid,
                                     sizeof(ap_info.ssid));
        memcpy(ssid_str, ap_info.ssid, ssid_length);
        ssid_str[ssid_length] = '\0';
    }
    snprintf(ble_ver_str, sizeof(ble_ver_str), "%u", (unsigned int)ble_ver);
    snprintf(version_str, sizeof(version_str), "%s:%s",
             app != NULL ? app->version : "", ble_ver_str);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL ||
        cJSON_AddStringToObject(root, "func", "wifi_info_result") == NULL ||
        cJSON_AddNumberToObject(root, "result", TDX_JSON_RESULT_OK) == NULL ||
        cJSON_AddStringToObject(root, "message", "wifi info") == NULL ||
        cJSON_AddStringToObject(root, "stage", ip_str) == NULL ||
        cJSON_AddStringToObject(root, "WiFi", ssid_str) == NULL ||
        cJSON_AddStringToObject(root, "version", version_str) == NULL ||
        cJSON_AddStringToObject(root, "date",
                               app != NULL ? app->date : "") == NULL ||
        cJSON_AddStringToObject(root, "running",
                               running != NULL ? running->label : "") == NULL) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "wifi_info_result JSON allocation failed");
        return false;
    }
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json_str == NULL) {
        ESP_LOGE(TAG, "wifi_info_result JSON serialization failed");
        return false;
    }

    ESP_LOGI(TAG, "wifi_info_result send start ip=%s len=%u",
             ip_str,
             (unsigned int)strlen(json_str));

    for (int attempt = 1; attempt <= WIFI_INFO_NOTIFY_RETRY_COUNT; attempt++) {
        if (s_active_send_json(json_str)) {
            ESP_LOGI(TAG, "wifi_info_result sent ip=%s attempt=%d", ip_str, attempt);
#if (USER_BLE_ENABLE == 1)
            UserDebugOutput_Printf("JSON:\n%s\n", json_str);
#endif
            cJSON_free(json_str);
            return true;
        }

        if (attempt < WIFI_INFO_NOTIFY_RETRY_COUNT) {
            ESP_LOGW(TAG, "wifi_info_result send failed ip=%s attempt=%d, retry",
                     ip_str,
                     attempt);
            vTaskDelay(pdMS_TO_TICKS(WIFI_INFO_NOTIFY_RETRY_DELAY_MS));
        }
    }

    ESP_LOGE(TAG, "wifi_info_result send failed ip=%s attempts=%d",
             ip_str,
             WIFI_INFO_NOTIFY_RETRY_COUNT);
    cJSON_free(json_str);
    return false;
}

void send_base_info_to_mobile_old(void)
{
        char ip_str[sizeof("255.255.255.255")];
        char json_str[384];
        const esp_app_desc_t *app = esp_app_get_description();
        const esp_partition_t *running = esp_ota_get_running_partition();

        esp_netif_ip_info_t ip = {};
        esp_netif_t *esp_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (esp_netif != NULL &&
            esp_netif_get_ip_info(esp_netif, &ip) == ESP_OK &&
            ip.ip.addr != 0) {
            net_connect_OK =1;
            working_time = 0;
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip.ip));
            snprintf(json_str, sizeof(json_str),
                     "{\"func\":\"wifi_info_result\",\"result\":%d,\"message\":\"wifi info\",\"stage\":\"%s\","
                     "\"project\":\"%s\","
                     "\"version\":\"%s\","
                     "\"date\":\"%s\","
                     "\"time\":\"%s\","
                     "\"idf\":\"%s\","
                     "\"running\":\"%s\"}",
                     TDX_JSON_RESULT_OK,
                     ip_str,
                     app != NULL ? app->project_name : "",
                     app != NULL ? app->version : "",
                     app != NULL ? app->date : "",
                     app != NULL ? app->time : "",
                     app != NULL ? app->idf_ver : "",
                     running != NULL ? running->label : "");
            #if(USER_BLE_ENABLE == 1)
             (void)s_active_send_json(json_str);
             UserDebugOutput_Printf("JSON:\n%s\n", json_str);
            #else
             (void)s_active_send_json(json_str);
            #endif

            //UserDebugOutput_Printf("  ETHIP: " IPSTR "\r\n", IP2STR(&ip.ip));
            //UserDebugOutput_Printf("  ETHMASK: " IPSTR "\r\n", IP2STR(&ip.netmask));
            //UserDebugOutput_Printf("  ETHGW: " IPSTR "\r\n", IP2STR(&ip.gw));

        }
}

static TaskHandle_t s_wifi_connect_task = NULL;
static json_sender_t s_wifi_connect_reply_sender = NULL;
static bool s_wifi_connect_notify_result = false;
static const char *s_wifi_connect_result_func = NULL;
static bool s_wifi_connect_new_credential = false;
static TickType_t s_wifi_connect_request_start_tick = 0;
static bool s_wifi_wakeup_observer_active = false;
static uint32_t s_wifi_wakeup_observer_generation = 0;
// Only a wifi_wakeup-owned worker accepts one latest saved credential as pending work.
static portMUX_TYPE s_wifi_connect_flow_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_wifi_connect_submit_in_progress = false;
static bool s_wifi_connect_wakeup_flow = false;
static uint8_t s_wifi_pending_config_state = WIFI_PENDING_CONFIG_NONE;
static uint32_t s_wifi_pending_config_generation = 0;
static json_sender_t s_wifi_pending_config_sender = NULL;
static TickType_t s_wifi_pending_config_start_tick = 0;

static bool wifi_connect_task_is_active(void)
{
    bool active = false;
    portENTER_CRITICAL(&s_wifi_connect_flow_lock);
    active = s_wifi_connect_task != NULL ||
             s_wifi_connect_submit_in_progress;
    portEXIT_CRITICAL(&s_wifi_connect_flow_lock);
    return active;
}

static bool wifi_wakeup_observer_is_current(uint32_t generation)
{
    bool current = false;
    portENTER_CRITICAL(&s_wifi_connect_flow_lock);
    current = s_wifi_wakeup_observer_active &&
              generation != 0 &&
              generation == s_wifi_wakeup_observer_generation;
    portEXIT_CRITICAL(&s_wifi_connect_flow_lock);
    return current;
}

static bool cancel_wifi_wakeup_observer_for_new_credential(void)
{
    bool cancelled = false;
    portENTER_CRITICAL(&s_wifi_connect_flow_lock);
    if (s_wifi_wakeup_observer_active) {
        /* Invalidate only the read-only observer; the WiFi manager is untouched. */
        s_wifi_wakeup_observer_generation++;
        if (s_wifi_wakeup_observer_generation == 0) {
            s_wifi_wakeup_observer_generation++;
        }
        s_wifi_wakeup_observer_active = false;
        cancelled = true;
    }
    portEXIT_CRITICAL(&s_wifi_connect_flow_lock);

    if (cancelled) {
        ESP_LOGI(TAG,
                 "wifi_wakeup existing progress observer cancelled by new credential");
    }
    return cancelled;
}

static uint32_t reserve_wifi_config_behind_wakeup(
    json_sender_t reply_sender,
    TickType_t request_start_tick)
{
    uint32_t generation = 0;
    portENTER_CRITICAL(&s_wifi_connect_flow_lock);
    if ((s_wifi_connect_task != NULL ||
         s_wifi_connect_submit_in_progress) &&
        s_wifi_connect_wakeup_flow) {
        generation = ++s_wifi_pending_config_generation;
        if (generation == 0) {
            generation = ++s_wifi_pending_config_generation;
        }
        s_wifi_pending_config_state = WIFI_PENDING_CONFIG_SAVING;
        s_wifi_pending_config_sender = reply_sender;
        s_wifi_pending_config_start_tick = request_start_tick;
    }
    portEXIT_CRITICAL(&s_wifi_connect_flow_lock);
    return generation;
}

static bool finish_wifi_config_reservation(uint32_t generation, bool saved)
{
    bool finished = false;
    portENTER_CRITICAL(&s_wifi_connect_flow_lock);
    if (generation != 0 &&
        generation == s_wifi_pending_config_generation &&
        s_wifi_pending_config_state == WIFI_PENDING_CONFIG_SAVING) {
        bool wakeup_owner_active =
            (s_wifi_connect_task != NULL ||
             s_wifi_connect_submit_in_progress) &&
            s_wifi_connect_wakeup_flow;
        if (saved && wakeup_owner_active) {
            s_wifi_pending_config_state = WIFI_PENDING_CONFIG_READY;
            finished = true;
        } else {
            s_wifi_pending_config_state = WIFI_PENDING_CONFIG_NONE;
            s_wifi_pending_config_sender = NULL;
            s_wifi_pending_config_start_tick = 0;
            finished = !saved;
        }
    }
    portEXIT_CRITICAL(&s_wifi_connect_flow_lock);
    return finished;
}

static bool wifi_config_is_queued_behind_wakeup(void)
{
    bool queued = false;
    portENTER_CRITICAL(&s_wifi_connect_flow_lock);
    queued = s_wifi_pending_config_state != WIFI_PENDING_CONFIG_NONE;
    portEXIT_CRITICAL(&s_wifi_connect_flow_lock);
    return queued;
}

static int take_pending_wifi_config_or_finish(
    json_sender_t *reply_sender,
    TickType_t *request_start_tick)
{
    int action = WIFI_PENDING_TAKE_FINISH;
    portENTER_CRITICAL(&s_wifi_connect_flow_lock);
    if (s_wifi_pending_config_state == WIFI_PENDING_CONFIG_READY) {
        if (reply_sender != NULL) {
            *reply_sender = s_wifi_pending_config_sender;
        }
        if (request_start_tick != NULL) {
            *request_start_tick = s_wifi_pending_config_start_tick;
        }
        s_wifi_pending_config_state = WIFI_PENDING_CONFIG_NONE;
        s_wifi_pending_config_sender = NULL;
        s_wifi_pending_config_start_tick = 0;
        s_wifi_connect_wakeup_flow = false;
        action = WIFI_PENDING_TAKE_READY;
    } else if (s_wifi_pending_config_state == WIFI_PENDING_CONFIG_SAVING) {
        action = WIFI_PENDING_TAKE_WAIT;
    } else {
        s_wifi_connect_wakeup_flow = false;
        s_wifi_connect_task = NULL;
    }
    portEXIT_CRITICAL(&s_wifi_connect_flow_lock);
    return action;
}

static void wait_wifi_connect_task_published(void)
{
    while (true) {
        bool published = false;
        portENTER_CRITICAL(&s_wifi_connect_flow_lock);
        published = !s_wifi_connect_submit_in_progress;
        portEXIT_CRITICAL(&s_wifi_connect_flow_lock);
        if (published) {
            return;
        }
        vTaskDelay(1);
    }
}

static const char *wifi_stage_from_status(const server_network_sta_status_t& status)
{
    switch (status.state) {
    case SERVER_NETWORK_STA_STATE_READY: return "ready";
    case SERVER_NETWORK_STA_STATE_CONNECTING: return "connecting";
    case SERVER_NETWORK_STA_STATE_DISCONNECTING: return "disconnecting";
    case SERVER_NETWORK_STA_STATE_WAITING_IP: return "waiting_ip";
    case SERVER_NETWORK_STA_STATE_RETRY_WAIT: return "retry_wait";
    case SERVER_NETWORK_STA_STATE_GOT_IP: return "got_ip";
    case SERVER_NETWORK_STA_STATE_STARTING_SERVICES: return "starting_services";
    case SERVER_NETWORK_STA_STATE_AUTH_FAILED: return "auth_failed";
    case SERVER_NETWORK_STA_STATE_NO_CONFIG: return "no_wifi";
    case SERVER_NETWORK_STA_STATE_FAILED: return "error";
    case SERVER_NETWORK_STA_STATE_IDLE: return "idle";
    default: return "unknown";
    }
}

static uint32_t wifi_retry_after_ms(const server_network_sta_status_t& status)
{
    switch (status.retry_type) {
    case SERVER_NETWORK_RETRY_WIFI: return status.wifi_retry_after_ms;
    case SERVER_NETWORK_RETRY_HTTP: return status.http_retry_after_ms;
    case SERVER_NETWORK_RETRY_MDNS: return status.mdns_retry_after_ms;
    default: return 0;
    }
}

static bool wifi_status_is_progressing(const server_network_sta_status_t& status)
{
    return status.state == SERVER_NETWORK_STA_STATE_CONNECTING ||
           status.state == SERVER_NETWORK_STA_STATE_DISCONNECTING ||
           status.state == SERVER_NETWORK_STA_STATE_WAITING_IP ||
           status.state == SERVER_NETWORK_STA_STATE_RETRY_WAIT ||
           status.state == SERVER_NETWORK_STA_STATE_GOT_IP ||
           status.state == SERVER_NETWORK_STA_STATE_STARTING_SERVICES;
}

static bool wifi_status_is_ready(const server_network_sta_status_t& status)
{
    return status.state == SERVER_NETWORK_STA_STATE_READY &&
           status.has_ip &&
           status.http_ready &&
           status.ip[0] != '\0';
}

typedef enum {
    WIFI_CONFIG_OBSERVE_READY,
    WIFI_CONFIG_OBSERVE_TIMEOUT,
    WIFI_CONFIG_OBSERVE_AUTH_FAILED
} wifi_config_observe_result_t;

static wifi_config_observe_result_t wait_wifi_config_result_until_deadline(
    TickType_t request_start_tick)
{
    const TickType_t timeout_ticks =
        pdMS_TO_TICKS(WIFI_CONFIG_RESULT_TIMEOUT_MS);
    const TickType_t poll_ticks =
        pdMS_TO_TICKS(WIFI_CONFIG_RESULT_STATUS_POLL_MS);
    server_network_sta_status_t status = {};
    bool status_error_logged = false;

    while ((xTaskGetTickCount() - request_start_tick) < timeout_ticks) {
        esp_err_t status_ret = ServerNetworkSta_GetStatus(&status);
        if (status_ret != ESP_OK) {
            if (!status_error_logged) {
                ESP_LOGE(TAG,
                         "WiFi config result status read failed; waiting for absolute deadline ret=%s",
                         esp_err_to_name(status_ret));
                status_error_logged = true;
            }
        } else {
            if (wifi_status_is_ready(status)) {
                ESP_LOGI(TAG,
                         "WiFi config connected before deadline ip=%s elapsed_ms=%lu",
                         status.ip,
                         (unsigned long)pdTICKS_TO_MS(
                             xTaskGetTickCount() - request_start_tick));
                return WIFI_CONFIG_OBSERVE_READY;
            }
            if (status.state == SERVER_NETWORK_STA_STATE_AUTH_FAILED ||
                status.last_result == TDX_JSON_RESULT_WIFI_AUTH_FAILED) {
                ESP_LOGE(TAG,
                         "WiFi config authentication failed elapsed_ms=%lu reason=%d",
                         (unsigned long)pdTICKS_TO_MS(
                             xTaskGetTickCount() - request_start_tick),
                         status.disconnect_reason);
                return WIFI_CONFIG_OBSERVE_AUTH_FAILED;
            }
        }

        TickType_t elapsed_ticks = xTaskGetTickCount() - request_start_tick;
        if (elapsed_ticks >= timeout_ticks) {
            break;
        }
        TickType_t remaining_ticks = timeout_ticks - elapsed_ticks;
        vTaskDelay(poll_ticks < remaining_ticks ? poll_ticks : remaining_ticks);
    }

    /* Re-read at the boundary so READY cannot race with the final 1307. */
    server_network_sta_status_t boundary_status = {};
    if (ServerNetworkSta_GetStatus(&boundary_status) == ESP_OK) {
        status = boundary_status;
    }
    if (wifi_status_is_ready(status)) {
        ESP_LOGI(TAG,
                 "WiFi config connected at deadline boundary ip=%s elapsed_ms=%lu",
                 status.ip,
                 (unsigned long)pdTICKS_TO_MS(
                     xTaskGetTickCount() - request_start_tick));
        return WIFI_CONFIG_OBSERVE_READY;
    }
    if (status.state == SERVER_NETWORK_STA_STATE_AUTH_FAILED ||
        status.last_result == TDX_JSON_RESULT_WIFI_AUTH_FAILED) {
        ESP_LOGE(TAG,
                 "WiFi config authentication failed at deadline reason=%d",
                 status.disconnect_reason);
        return WIFI_CONFIG_OBSERVE_AUTH_FAILED;
    }

    ESP_LOGW(TAG,
             "WiFi config final timeout elapsed_ms=%lu state=%s last=%d reason=%d",
             (unsigned long)pdTICKS_TO_MS(
                 xTaskGetTickCount() - request_start_tick),
             ServerNetworkSta_StateName(status.state),
             status.last_result,
             status.disconnect_reason);
    return WIFI_CONFIG_OBSERVE_TIMEOUT;
}

static int wait_wifi_wakeup_ready_during_grace(
    TickType_t request_start_tick,
    server_network_sta_status_t *final_status,
    uint32_t observer_generation)
{
    const TickType_t total_timeout_ticks =
        pdMS_TO_TICKS(WIFI_CONNECT_POWER_GUARD_MAX_MS);
    const TickType_t grace_timeout_ticks =
        pdMS_TO_TICKS(WIFI_WAKEUP_EARLY_1307_GRACE_MS);
    const TickType_t poll_ticks =
        pdMS_TO_TICKS(WIFI_WAKEUP_RESULT_POLL_DELAY_MS);
    const TickType_t grace_start_tick = xTaskGetTickCount();
    server_network_sta_status_t status = {};

    while (true) {
        if (observer_generation != 0 &&
            !wifi_wakeup_observer_is_current(observer_generation)) {
            if (final_status != NULL) {
                *final_status = status;
            }
            return WIFI_WAKEUP_WAIT_CONFIG_QUEUED;
        }
        if (wifi_config_is_queued_behind_wakeup()) {
            if (final_status != NULL) {
                *final_status = status;
            }
            ESP_LOGI(TAG, "wifi_wakeup deferred notify cancelled by new credential");
            return WIFI_WAKEUP_WAIT_CONFIG_QUEUED;
        }

        if (ServerNetworkSta_GetStatus(&status) != ESP_OK) {
            ESP_LOGE(TAG, "wifi_wakeup deferred result status read failed");
            if (final_status != NULL) {
                memset(final_status, 0, sizeof(*final_status));
            }
            return WIFI_WAKEUP_WAIT_STATUS_ERROR;
        }

        if (wifi_status_is_ready(status)) {
            if (final_status != NULL) {
                *final_status = status;
            }
            ESP_LOGI(TAG,
                     "wifi_wakeup recovered before notify timeout ip=%s elapsed_ms=%lu",
                     status.ip,
                     (unsigned long)pdTICKS_TO_MS(xTaskGetTickCount() -
                                                 request_start_tick));
            return WIFI_WAKEUP_WAIT_READY;
        }

        if (!wifi_status_is_progressing(status)) {
            if (final_status != NULL) {
                *final_status = status;
            }
            ESP_LOGW(TAG,
                     "wifi_wakeup deferred result stopped state=%s last=%d reason=%d",
                     ServerNetworkSta_StateName(status.state),
                     status.last_result,
                     status.disconnect_reason);
            return WIFI_WAKEUP_WAIT_TERMINAL;
        }

        TickType_t now = xTaskGetTickCount();
        TickType_t total_elapsed_ticks = now - request_start_tick;
        TickType_t grace_elapsed_ticks = now - grace_start_tick;
        if (total_elapsed_ticks >= total_timeout_ticks ||
            grace_elapsed_ticks >= grace_timeout_ticks) {
            /*
             * Re-read once at the boundary so READY cannot race with the
             * final 1307 notification.
             */
            server_network_sta_status_t boundary_status = {};
            if (ServerNetworkSta_GetStatus(&boundary_status) == ESP_OK) {
                status = boundary_status;
            }
            if (final_status != NULL) {
                *final_status = status;
            }
            if (wifi_status_is_ready(status)) {
                ESP_LOGI(TAG,
                         "wifi_wakeup recovered at notify timeout boundary ip=%s",
                         status.ip);
                return WIFI_WAKEUP_WAIT_READY;
            }
            ESP_LOGW(TAG,
                     "wifi_wakeup grace expired state=%s last=%d grace_ms=%lu total_ms=%lu",
                     ServerNetworkSta_StateName(status.state),
                     status.last_result,
                     (unsigned long)pdTICKS_TO_MS(grace_elapsed_ticks),
                     (unsigned long)pdTICKS_TO_MS(total_elapsed_ticks));
            return WIFI_WAKEUP_WAIT_NOTIFY_TIMEOUT;
        }

        TickType_t total_remaining_ticks =
            total_timeout_ticks - total_elapsed_ticks;
        TickType_t grace_remaining_ticks =
            grace_timeout_ticks - grace_elapsed_ticks;
        TickType_t delay_ticks = poll_ticks;
        if (delay_ticks > total_remaining_ticks) {
            delay_ticks = total_remaining_ticks;
        }
        if (delay_ticks > grace_remaining_ticks) {
            delay_ticks = grace_remaining_ticks;
        }
        vTaskDelay(delay_ticks);
    }
}

static int wait_wifi_wakeup_power_guard_or_pending_config(
    TickType_t request_start_tick,
    server_network_sta_status_t *final_status)
{
    const TickType_t timeout_ticks =
        pdMS_TO_TICKS(WIFI_CONNECT_POWER_GUARD_MAX_MS);
    const TickType_t poll_ticks =
        pdMS_TO_TICKS(WIFI_WAKEUP_RESULT_POLL_DELAY_MS);
    server_network_sta_status_t status = {};
    bool status_error_logged = false;

    while ((xTaskGetTickCount() - request_start_tick) < timeout_ticks) {
        if (wifi_config_is_queued_behind_wakeup()) {
            if (final_status != NULL) {
                *final_status = status;
            }
            return WIFI_WAKEUP_WAIT_CONFIG_QUEUED;
        }
        if (ServerNetworkSta_GetStatus(&status) != ESP_OK) {
            if (!status_error_logged) {
                ESP_LOGE(TAG,
                         "wifi_wakeup power guard status read failed; guard remains active");
                status_error_logged = true;
            }
            vTaskDelay(poll_ticks);
            continue;
        }
        if (wifi_status_is_ready(status)) {
            if (final_status != NULL) {
                *final_status = status;
            }
            return WIFI_WAKEUP_WAIT_READY;
        }
        if (!wifi_status_is_progressing(status)) {
            if (final_status != NULL) {
                *final_status = status;
            }
            return WIFI_WAKEUP_WAIT_TERMINAL;
        }
        vTaskDelay(poll_ticks);
    }

    if (final_status != NULL) {
        (void)ServerNetworkSta_GetStatus(final_status);
    }
    return WIFI_WAKEUP_WAIT_POWER_GUARD_TIMEOUT;
}

static bool notify_wifi_info_if_ip_ready(json_sender_t reply_sender,
                                         const char *reason,
                                         bool allow_recheck,
                                         bool *ip_ready_out)
{
    server_network_sta_status_t status = {};

    if (ip_ready_out != NULL) {
        *ip_ready_out = false;
    }

    if (reply_sender == NULL) {
        ESP_LOGE(TAG, "wifi_info_result notify skipped reason=%s sender=NULL",
                 reason != NULL ? reason : "<null>");
        return false;
    }

    s_active_send_json = reply_sender;
    if (ServerNetworkSta_GetStatus(&status) != ESP_OK) {
        ESP_LOGE(TAG, "wifi_info_result notify status read failed reason=%s",
                 reason != NULL ? reason : "<null>");
        return false;
    }

    for (int recheck = 1;
         (!(status.state == SERVER_NETWORK_STA_STATE_READY &&
            status.has_ip &&
            status.http_ready &&
            status.ip[0] != '\0')) &&
         allow_recheck &&
         recheck <= WIFI_INFO_IP_READY_RECHECK_COUNT;
         recheck++) {
        ESP_LOGW(TAG,
                 "wifi_info_result notify waiting for network ready reason=%s state=%d last=%d http_ready=%d ip=%s recheck=%d/%d",
                 reason != NULL ? reason : "<null>",
                 (int)status.state,
                 status.last_result,
                 status.http_ready ? 1 : 0,
                 status.ip[0] != '\0' ? status.ip : "<empty>",
                 recheck,
                 WIFI_INFO_IP_READY_RECHECK_COUNT);
        vTaskDelay(pdMS_TO_TICKS(WIFI_INFO_IP_READY_RECHECK_DELAY_MS));
        memset(&status, 0, sizeof(status));
        if (ServerNetworkSta_GetStatus(&status) != ESP_OK) {
            ESP_LOGE(TAG, "wifi_info_result notify status reread failed reason=%s recheck=%d/%d",
                     reason != NULL ? reason : "<null>",
                     recheck,
                     WIFI_INFO_IP_READY_RECHECK_COUNT);
            return false;
        }
    }

    if (status.state == SERVER_NETWORK_STA_STATE_READY &&
        status.has_ip &&
        status.http_ready &&
        status.ip[0] != '\0') {
        if (ip_ready_out != NULL) {
            *ip_ready_out = true;
        }
        ESP_LOGI(TAG, "wifi_info_result notify call send_base_info reason=%s ip=%s",
                 reason != NULL ? reason : "<null>",
                 status.ip);
        return send_base_info_to_mobile();
    }

    ESP_LOGW(TAG,
             "wifi_info_result notify not ready reason=%s state=%d last=%d http_ready=%d ip=%s",
             reason != NULL ? reason : "<null>",
             (int)status.state,
             status.last_result,
             status.http_ready ? 1 : 0,
             status.ip[0] != '\0' ? status.ip : "<empty>");
    return false;
}

typedef struct {
    json_sender_t reply_sender;
    TickType_t request_start_tick;
    uint32_t generation;
} wifi_wakeup_observer_context_t;

static void wifi_wakeup_existing_progress_observer_task(void *arg)
{
    wifi_wakeup_observer_context_t *context =
        static_cast<wifi_wakeup_observer_context_t *>(arg);
    json_sender_t reply_sender = context != NULL ? context->reply_sender : NULL;
    TickType_t request_start_tick = context != NULL ?
                                    context->request_start_tick :
                                    xTaskGetTickCount();
    uint32_t generation = context != NULL ? context->generation : 0;
    free(context);

    server_network_sta_status_t final_status = {};
    int wait_result = wait_wifi_wakeup_ready_during_grace(
        request_start_tick, &final_status, generation);
    bool observer_current = wifi_wakeup_observer_is_current(generation);
    if (observer_current) {
        if (wait_result == WIFI_WAKEUP_WAIT_READY) {
            (void)notify_wifi_info_if_ip_ready(reply_sender,
                                               "wifi_wakeup_existing_progress",
                                               false,
                                               NULL);
        } else if (wait_result != WIFI_WAKEUP_WAIT_CONFIG_QUEUED) {
            ESP_LOGW(TAG,
                     "wifi_wakeup existing progress final result=1307 state=%s reason=%d",
                     ServerNetworkSta_StateName(final_status.state),
                     final_status.disconnect_reason);
            send_simple_result_with_sender(reply_sender,
                                           "wifi_wakeup_result",
                                           TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT,
                                           "WiFi connect timed out");
        }
    }

    portENTER_CRITICAL(&s_wifi_connect_flow_lock);
    if (generation == s_wifi_wakeup_observer_generation) {
        s_wifi_wakeup_observer_active = false;
    }
    portEXIT_CRITICAL(&s_wifi_connect_flow_lock);
    vTaskDelete(NULL);
}

static esp_err_t start_wifi_wakeup_existing_progress_observer(
    json_sender_t reply_sender)
{
    wifi_wakeup_observer_context_t *context =
        static_cast<wifi_wakeup_observer_context_t *>(
            calloc(1, sizeof(*context)));
    if (context == NULL) {
        return ESP_ERR_NO_MEM;
    }
    context->reply_sender = reply_sender;
    context->request_start_tick = xTaskGetTickCount();

    portENTER_CRITICAL(&s_wifi_connect_flow_lock);
    if (s_wifi_wakeup_observer_active) {
        portEXIT_CRITICAL(&s_wifi_connect_flow_lock);
        free(context);
        return ESP_OK;
    }
    s_wifi_wakeup_observer_generation++;
    if (s_wifi_wakeup_observer_generation == 0) {
        s_wifi_wakeup_observer_generation++;
    }
    context->generation = s_wifi_wakeup_observer_generation;
    s_wifi_wakeup_observer_active = true;
    portEXIT_CRITICAL(&s_wifi_connect_flow_lock);

    BaseType_t task_ret = xTaskCreate(
        wifi_wakeup_existing_progress_observer_task,
        "wifi_wakeup_observe",
        4096,
        context,
        4,
        NULL);
    if (task_ret != pdPASS) {
        portENTER_CRITICAL(&s_wifi_connect_flow_lock);
        if (context->generation == s_wifi_wakeup_observer_generation) {
            s_wifi_wakeup_observer_active = false;
        }
        portEXIT_CRITICAL(&s_wifi_connect_flow_lock);
        free(context);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG,
             "wifi_wakeup existing progress observer started timeout_ms=%u",
             WIFI_WAKEUP_EARLY_1307_GRACE_MS);
    return ESP_OK;
}

static void wifi_connect_task(void *arg)
{
    (void)arg;
    wait_wifi_connect_task_published();
    json_sender_t reply_sender = s_wifi_connect_reply_sender;
    bool notify_result = s_wifi_connect_notify_result;
    const char *result_func = s_wifi_connect_result_func != NULL
                                  ? s_wifi_connect_result_func
                                  : "wifi_wakeup_result";
    bool new_credential = s_wifi_connect_new_credential;
    TickType_t request_start_tick = s_wifi_connect_request_start_tick != 0
                                        ? s_wifi_connect_request_start_tick
                                        : xTaskGetTickCount();
    s_wifi_connect_reply_sender = NULL;
    s_wifi_connect_notify_result = false;
    s_wifi_connect_result_func = NULL;
    s_wifi_connect_new_credential = false;
    s_wifi_connect_request_start_tick = 0;

    while (true) {
        bool is_wakeup_result =
            strcmp(result_func, "wifi_wakeup_result") == 0;
        uint8_t init_result = new_credential
                                  ? ::User_Network_mode_app_new_credential("/data")
                                  : ::User_Network_mode_app_init("/data");
        int connect_result = ServerNetworkSta_GetLastConnectResult();
        ESP_LOGI(TAG,
                 "BLE/CH583 WiFi initial wait finished func=%s init=%u connect_result=%d",
                 result_func,
                 (unsigned int)init_result,
                 connect_result);

        bool wakeup_ready_after_wait = false;
        bool config_ready_after_wait = false;
        bool config_timed_out_after_wait = false;
        int wakeup_wait_result = WIFI_WAKEUP_WAIT_TERMINAL;
        server_network_sta_status_t wakeup_status = {};
        bool cancel_wakeup_for_config =
            is_wakeup_result && wifi_config_is_queued_behind_wakeup();
        if (init_result != SERVER_NETWORK_STA_CONNECT_SUPERSEDED &&
            init_result != SERVER_NETWORK_STA_OK &&
            connect_result == TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT &&
            is_wakeup_result &&
            !cancel_wakeup_for_config) {
            esp_err_t status_ret =
                ServerNetworkSta_GetStatus(&wakeup_status);
            if (status_ret == ESP_OK &&
                wifi_status_is_progressing(wakeup_status)) {
                ESP_LOGW(TAG,
                         "wifi_wakeup early 1307 suppressed grace_ms=%u state=%s "
                         "retry=%lu retry_after_ms=%lu",
                         WIFI_WAKEUP_EARLY_1307_GRACE_MS,
                         ServerNetworkSta_StateName(wakeup_status.state),
                         (unsigned long)wakeup_status.wifi_retry_count,
                         (unsigned long)wifi_retry_after_ms(wakeup_status));
                wakeup_wait_result =
                    wait_wifi_wakeup_ready_during_grace(request_start_tick,
                                                        &wakeup_status,
                                                        0);
                wakeup_ready_after_wait =
                    wakeup_wait_result == WIFI_WAKEUP_WAIT_READY;
                cancel_wakeup_for_config =
                    wakeup_wait_result == WIFI_WAKEUP_WAIT_CONFIG_QUEUED;
                if (!wakeup_ready_after_wait) {
                    connect_result = wakeup_status.last_result;
                }
            } else if (status_ret != ESP_OK) {
                ESP_LOGE(TAG,
                         "wifi_wakeup early 1307 status read failed err=%s",
                         esp_err_to_name(status_ret));
                wakeup_wait_result = WIFI_WAKEUP_WAIT_STATUS_ERROR;
            }
        }

        if (init_result != SERVER_NETWORK_STA_CONNECT_SUPERSEDED &&
            init_result != SERVER_NETWORK_STA_OK &&
            connect_result == TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT &&
            new_credential &&
            !is_wakeup_result) {
            /* A manager request can fail on one NO_AP scan; protocol 1307 is
             * reserved for the complete new-credential result window. */
            uint32_t elapsed_ms = (uint32_t)pdTICKS_TO_MS(
                xTaskGetTickCount() - request_start_tick);
            uint32_t remaining_ms =
                elapsed_ms < WIFI_CONFIG_RESULT_TIMEOUT_MS
                    ? WIFI_CONFIG_RESULT_TIMEOUT_MS - elapsed_ms
                    : 0U;
            if (remaining_ms > 0U) {
                ESP_LOGW(TAG,
                         "WiFi config early 1307 deferred elapsed_ms=%lu remaining_ms=%lu",
                         (unsigned long)elapsed_ms,
                         (unsigned long)remaining_ms);
            }
            wifi_config_observe_result_t observe_result =
                wait_wifi_config_result_until_deadline(request_start_tick);
            config_ready_after_wait =
                observe_result == WIFI_CONFIG_OBSERVE_READY;
            if (observe_result == WIFI_CONFIG_OBSERVE_AUTH_FAILED) {
                connect_result = TDX_JSON_RESULT_WIFI_AUTH_FAILED;
            } else if (!config_ready_after_wait) {
                connect_result = TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT;
                config_timed_out_after_wait = true;
            }
        }

        if (is_wakeup_result && wifi_config_is_queued_behind_wakeup()) {
            cancel_wakeup_for_config = true;
        }

        if (!cancel_wakeup_for_config) {
            if (notify_result && reply_sender != NULL) {
                if (init_result == SERVER_NETWORK_STA_CONNECT_SUPERSEDED) {
                    send_simple_result_with_sender(reply_sender,
                                                   result_func,
                                                   TDX_JSON_RESULT_BUSY,
                                                   "WiFi connect request superseded");
                } else if (init_result == SERVER_NETWORK_STA_OK ||
                           wakeup_ready_after_wait ||
                           config_ready_after_wait) {
                    bool ip_ready = false;
                    const char *notify_reason =
                        wakeup_ready_after_wait
                            ? "wifi_wakeup_retry_ready"
                            : config_ready_after_wait
                                  ? "wifi_config_retry_ready"
                                  : "wifi_connect_task";
                    if (!notify_wifi_info_if_ip_ready(
                            reply_sender,
                            notify_reason,
                            !(wakeup_ready_after_wait ||
                              config_ready_after_wait),
                            &ip_ready)) {
                        if (ip_ready) {
                            ESP_LOGE(TAG,
                                     "WiFi got IP but wifi_info_result send failed");
                        } else {
                            ESP_LOGW(TAG,
                                     "WiFi connect result OK but network was not ready for wifi_info_result");
                            send_simple_result_with_sender(
                                reply_sender,
                                result_func,
                                TDX_JSON_RESULT_WIFI_GOT_IP_FAILED,
                                "WiFi network was not ready");
                        }
                    }
                } else if (connect_result ==
                           TDX_JSON_RESULT_WIFI_AUTH_FAILED) {
                    send_simple_result_with_sender(
                        reply_sender,
                        result_func,
                        TDX_JSON_RESULT_WIFI_AUTH_FAILED,
                        "WiFi authentication failed");
                } else if (connect_result ==
                           TDX_JSON_RESULT_WIFI_GOT_IP_FAILED) {
                    send_simple_result_with_sender(
                        reply_sender,
                        result_func,
                        TDX_JSON_RESULT_WIFI_GOT_IP_FAILED,
                        "WiFi did not obtain IP");
                } else {
                    send_simple_result_with_sender(
                        reply_sender,
                        result_func,
                        TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT,
                        "WiFi connect timed out");
                }
            } else {
                ESP_LOGW(TAG,
                         "WiFi connect task finished without notify sender notify=%d sender_set=%d result=%d",
                         notify_result ? 1 : 0,
                         reply_sender != NULL ? 1 : 0,
                         connect_result);
            }
        }

        if (new_credential) {
            ServerNetworkStaWifiRecovery_OnCredentialResultFinished();
        }

        if (is_wakeup_result && wifi_config_is_queued_behind_wakeup()) {
            cancel_wakeup_for_config = true;
        }
        bool keep_power_guard_observer =
            is_wakeup_result &&
            !cancel_wakeup_for_config &&
            ((wakeup_wait_result == WIFI_WAKEUP_WAIT_NOTIFY_TIMEOUT &&
              wifi_status_is_progressing(wakeup_status)) ||
             wakeup_wait_result == WIFI_WAKEUP_WAIT_STATUS_ERROR);
        if (keep_power_guard_observer) {
            int guard_wait_result =
                wait_wifi_wakeup_power_guard_or_pending_config(
                    request_start_tick,
                    &wakeup_status);
            cancel_wakeup_for_config =
                guard_wait_result == WIFI_WAKEUP_WAIT_CONFIG_QUEUED;
            if (!cancel_wakeup_for_config) {
                const char *clear_reason =
                    guard_wait_result == WIFI_WAKEUP_WAIT_READY
                        ? "ready_after_1307"
                        : guard_wait_result == WIFI_WAKEUP_WAIT_TERMINAL
                              ? "terminal_after_1307"
                              : "deadline";
                ServerNetworkStaWifiWorkTime_ClearWifiConnectGuard(
                    clear_reason);
            }
        } else if (!cancel_wakeup_for_config) {
            const char *clear_reason =
                (init_result == SERVER_NETWORK_STA_OK ||
                 wakeup_ready_after_wait ||
                 config_ready_after_wait)
                    ? "ready"
                    : config_timed_out_after_wait
                          ? "timeout"
                          : "terminal";
            ServerNetworkStaWifiWorkTime_ClearWifiConnectGuard(clear_reason);
        }

        json_sender_t pending_sender = NULL;
        TickType_t pending_request_start_tick = 0;
        int pending_action = WIFI_PENDING_TAKE_WAIT;
        while (pending_action == WIFI_PENDING_TAKE_WAIT) {
            pending_action =
                take_pending_wifi_config_or_finish(&pending_sender,
                                                   &pending_request_start_tick);
            if (pending_action == WIFI_PENDING_TAKE_WAIT) {
                vTaskDelay(pdMS_TO_TICKS(WIFI_WAKEUP_RESULT_POLL_DELAY_MS));
            }
        }
        if (pending_action != WIFI_PENDING_TAKE_READY) {
            break;
        }

        ESP_LOGI(TAG, "apply queued WiFi credential after wifi_wakeup");
        TickType_t pending_elapsed_ticks =
            xTaskGetTickCount() - pending_request_start_tick;
        uint32_t pending_elapsed_ms =
            (uint32_t)pdTICKS_TO_MS(pending_elapsed_ticks);
        uint32_t pending_guard_remaining_ms =
            pending_elapsed_ms < WIFI_CONNECT_POWER_GUARD_MAX_MS
                ? WIFI_CONNECT_POWER_GUARD_MAX_MS - pending_elapsed_ms
                : 1U;
        ServerNetworkStaWifiWorkTime_StartWifiConnectGuard(
            pending_guard_remaining_ms);
        request_start_tick = pending_request_start_tick;
        reply_sender = pending_sender;
        notify_result = true;
        result_func = "wifi_result";
        new_credential = true;
    }

    WiFi_config_from_ch583 = false;
    WiFi_config_from_ble = false;
    vTaskDelete(NULL);
}

static esp_err_t submit_wifi_connect(json_sender_t reply_sender,
                                     bool notify_result,
                                     const char *result_func,
                                     bool new_credential,
                                     TickType_t request_start_tick)
{
    portENTER_CRITICAL(&s_wifi_connect_flow_lock);
    if (s_wifi_connect_task != NULL || s_wifi_connect_submit_in_progress) {
        portEXIT_CRITICAL(&s_wifi_connect_flow_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_wifi_connect_submit_in_progress = true;
    s_wifi_connect_reply_sender = reply_sender;
    s_wifi_connect_notify_result = notify_result;
    s_wifi_connect_result_func = result_func;
    s_wifi_connect_new_credential = new_credential;
    s_wifi_connect_request_start_tick = request_start_tick;
    s_wifi_connect_wakeup_flow =
        result_func != NULL &&
        strcmp(result_func, "wifi_wakeup_result") == 0;
    portEXIT_CRITICAL(&s_wifi_connect_flow_lock);
    uint32_t elapsed_ms = (uint32_t)pdTICKS_TO_MS(
        xTaskGetTickCount() - request_start_tick);
    uint32_t guard_remaining_ms =
        elapsed_ms < WIFI_CONNECT_POWER_GUARD_MAX_MS
            ? WIFI_CONNECT_POWER_GUARD_MAX_MS - elapsed_ms
            : 1U;
    ServerNetworkStaWifiWorkTime_StartWifiConnectGuard(guard_remaining_ms);
    TaskHandle_t created_task = NULL;
    if (xTaskCreate(wifi_connect_task,
                    "ble_wifi_connect",
                    6144,
                    NULL,
                    4,
                    &created_task) != pdPASS) {
        portENTER_CRITICAL(&s_wifi_connect_flow_lock);
        s_wifi_connect_reply_sender = NULL;
        s_wifi_connect_notify_result = false;
        s_wifi_connect_result_func = NULL;
        s_wifi_connect_new_credential = false;
        s_wifi_connect_request_start_tick = 0;
        s_wifi_connect_submit_in_progress = false;
        s_wifi_connect_wakeup_flow = false;
        portEXIT_CRITICAL(&s_wifi_connect_flow_lock);
        ServerNetworkStaWifiWorkTime_ClearWifiConnectGuard(
            "task_create_failed");
        return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&s_wifi_connect_flow_lock);
    s_wifi_connect_task = created_task;
    s_wifi_connect_submit_in_progress = false;
    portEXIT_CRITICAL(&s_wifi_connect_flow_lock);
    return ESP_OK;
}

int parse_wifi_config_json(const char *json_str, wifi_config_json_t *out)
{
    cJSON *root = NULL;
    cJSON *item_func = NULL;

    if (json_str == NULL || out == NULL) {
        LOG_ERROR("Invalid parameter");
        return -1;
    }

    /* 涓枃娉ㄩ噴锛?      鍏堟竻绌鸿緭鍑虹粨鏋勪綋锛岄伩鍏嶆畫鐣欐棫鏁版嵁    */
    memset(out, 0, sizeof(*out));

    root = cJSON_Parse(json_str);
    if (root == NULL) {
                LOG_Purple("%s>%d  Invalid JSON",__func__,__LINE__);
        return -1;
    }

    item_func = cJSON_GetObjectItem(root, "func");

    /* 涓枃娉ㄩ噴锛?      妫€鏌ュ瓧娈垫槸鍚﹀瓨鍦ㄤ笖鏄瓧绗︿覆    */

    if (!cJSON_IsString(item_func) || item_func->valuestring == NULL) {
        cJSON_Delete(root);
        LOG_Purple("%s>%d  Invalid JSON",__func__,__LINE__);
        return -1;
    }

    const char *validation_message = "ssid invalid";
    int validation_result = parse_wifi_candidate_config(
        root, &out->candidate_config, &validation_message);
    if (validation_result != TDX_JSON_RESULT_OK) {
        ESP_LOGW(TAG, "WiFi candidate JSON rejected result=%d message=%s",
                 validation_result, validation_message);
        cJSON_Delete(root);
        return -1;
    }

    // Copy only after validating complete UTF-8 byte sequences and lengths.
    snprintf(out->func, sizeof(out->func), "%s", item_func->valuestring);
    strlcpy(out->ssid, out->candidate_config.display_ssid, sizeof(out->ssid));
    strlcpy(out->key, out->candidate_config.password, sizeof(out->key));

    cJSON_Delete(root);
    Bl_Data_Ready =1;
    TickType_t config_request_start_tick = xTaskGetTickCount();

    // Keep this scope local; s_wifi_connect_task is the single BLE/CH583 BUSY guard.
    {
            uint32_t pending_reservation =
                reserve_wifi_config_behind_wakeup(s_active_send_json,
                                                  config_request_start_tick);
            if (pending_reservation == 0 &&
                wifi_connect_task_is_active()) {
                send_simple_result_with_sender(s_active_send_json,
                                               "wifi_result",
                                               TDX_JSON_RESULT_BUSY,
                                               "WiFi connect already in progress");
                return 0;
            }
            ServerNetworkStaWifiRecovery_OnCredentialResultStarted();
            UserDebugOutput_Printf("The Wifi info parsed ok, save to NVS\n\r");
            esp_err_t save_ret = ServerNetworkStaWifiSsidCandidateStore_Save(
                &out->candidate_config);
            if (save_ret != ESP_OK) {
                (void)finish_wifi_config_reservation(pending_reservation,
                                                     false);
                ServerNetworkStaWifiRecovery_OnCredentialResultFinished();
                send_simple_result_with_sender(s_active_send_json,
                                               "wifi_result",
                                               TDX_JSON_RESULT_WIFI_SAVE_FAILED,
                                               "save WiFi config failed");
                return 0;
            }
            ESP_LOGI(TAG,
                     "WiFi SSID candidates saved ssid=%s count=%u country=%s",
                     out->candidate_config.display_ssid,
                     (unsigned int)out->candidate_config.candidate_count,
                     out->candidate_config.country);
            (void)cancel_wifi_wakeup_observer_for_new_credential();
            esp_err_t recovery_ret =
                ServerNetworkStaWifiRecovery_OnCredentialsChanged();
            if (recovery_ret != ESP_OK) {
                ESP_LOGE(TAG,
                         "WiFi recovery reset after credential save failed ret=%s",
                         esp_err_to_name(recovery_ret));
            }

            if (pending_reservation != 0) {
                if (finish_wifi_config_reservation(pending_reservation, true)) {
                    WiFi_config_net = true;
                    Wifi_connect_OK = 1;
                    ESP_LOGI(TAG,
                             "WiFi config queued behind wifi_wakeup; latest saved credential wins");
                    send_simple_result_with_sender(
                        s_active_send_json,
                        "wifi_result",
                        TDX_JSON_RESULT_OK,
                        "WiFi config saved and queued");
                    return 0;
                }
                if (wifi_connect_task_is_active()) {
                    ESP_LOGW(TAG,
                             "WiFi pending reservation superseded before publication");
                    send_simple_result_with_sender(
                        s_active_send_json,
                        "wifi_result",
                        TDX_JSON_RESULT_BUSY,
                        "WiFi config superseded by a newer request");
                    return 0;
                }
                ESP_LOGW(TAG,
                         "WiFi wakeup owner ended during config save; submit saved credential directly");
            }

            esp_err_t submit_ret = submit_wifi_connect(
                s_active_send_json,
                true,
                "wifi_result",
                true,
                config_request_start_tick);
            if (submit_ret != ESP_OK) {
                ServerNetworkStaWifiRecovery_OnCredentialResultFinished();
                send_simple_result_with_sender(s_active_send_json,
                                               "wifi_result",
                                               submit_ret == ESP_ERR_INVALID_STATE
                                                   ? TDX_JSON_RESULT_BUSY
                                                   : TDX_JSON_RESULT_WIFI_CONNECT_SUBMIT_FAILED,
                                               "submit WiFi connect failed");
                return 0;
            }

            WiFi_config_net = true;
            Wifi_connect_OK = 1;
            send_simple_result_with_sender(s_active_send_json,
                                           "wifi_result",
                                           TDX_JSON_RESULT_OK,
                                           "WiFi config saved and connect submitted");

            //LOG_Purple("esp_restart .. %s>%d",__func__,__LINE__);
            //vTaskDelay(pdMS_TO_TICKS(1000));                    
            //esp_restart();                            // Restart device to apply new WiFi configuration
    }
    return 0;
}

int parse_wifi_wakeup_json(const char *json_str, wifi_config_json_t *out)
{
    cJSON *root = NULL;
    cJSON *item_func = NULL;

    if (json_str == NULL || out == NULL) {
        LOG_ERROR("Invalid parameter");
        return -1;
    }

    
    memset(out, 0, sizeof(*out));

    root = cJSON_Parse(json_str);
    if (root == NULL) {
        LOG_Purple("%s>%d  Invalid JSON",__func__,__LINE__);
        return -1;
    }

    item_func = cJSON_GetObjectItem(root, "func");

    
    if (!cJSON_IsString(item_func) || item_func->valuestring == NULL) {
        cJSON_Delete(root);
        LOG_ERROR("xxInvalid JSON");
        return -1;
    }

    snprintf(out->func, sizeof(out->func), "%s", item_func->valuestring);
    if (strcmp(out->func, "wifi_wakeup") == 0) {
        UserDebugOutput_Printf("wakeup-ok\r\n");
    }
    else
    {
        UserDebugOutput_Printf("wakeup-fail\r\n");
        return -1;
    }
    cJSON_Delete(root);
    server_network_sta_status_t status = {};
    (void)ServerNetworkSta_GetStatus(&status);
    if (status.state == SERVER_NETWORK_STA_STATE_READY &&
        status.has_ip && status.ip[0] != '\0')
    {
       ServerNetworkStaWifiWorkTime_ClearWifiConnectGuard(
           "wakeup_already_ready");
       (void)notify_wifi_info_if_ip_ready(s_active_send_json, "wifi_wakeup_got_ip", false, NULL);
    }
    else if (wifi_status_is_progressing(status))
    {
        /*
         * This path observes an existing manager attempt and does not create
         * wifi_connect_task, so use the bounded auto-expiring guard directly.
         */
        (void)ServerNetworkStaWifiWorkTime_StartWifiConnectGuardIfInactive(
            WIFI_CONNECT_POWER_GUARD_MAX_MS);
        char reply_json[256];
        snprintf(reply_json, sizeof(reply_json),
                 "{\"func\":\"wifi_wakeup_result\",\"result\":%d,"
                 "\"message\":\"WiFi operation in progress\",\"stage\":\"%s\","
                 "\"state\":\"%s\",\"retry_type\":%d,\"retry_after_ms\":%lu}",
                 TDX_JSON_RESULT_OK,
                 wifi_stage_from_status(status),
                 ServerNetworkSta_StateName(status.state),
                 (int)status.retry_type,
                 (unsigned long)wifi_retry_after_ms(status));
        (void)s_active_send_json(reply_json);
        ESP_LOGI(TAG, "WiFi wakeup reports existing state=%s retry_ms=%lu",
                 ServerNetworkSta_StateName(status.state),
                 (unsigned long)wifi_retry_after_ms(status));
        esp_err_t observer_ret =
            start_wifi_wakeup_existing_progress_observer(s_active_send_json);
        if (observer_ret != ESP_OK) {
            ESP_LOGE(TAG,
                     "wifi_wakeup existing progress observer start failed ret=%s",
                     esp_err_to_name(observer_ret));
            send_simple_result_with_sender(s_active_send_json,
                                           "wifi_wakeup_result",
                                           TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT,
                                           "WiFi connect timed out");
        }
    }
    else if (status.state == SERVER_NETWORK_STA_STATE_AUTH_FAILED)
    {
        send_simple_result_with_sender(s_active_send_json,
                                       "wifi_wakeup_result",
                                       TDX_JSON_RESULT_WIFI_AUTH_FAILED,
                                       "WiFi authentication failed");
    }
    else if (!ble_has_saved_wifi_info()) {
        ESP_LOGW(TAG, "No saved WiFi credential");
        char reply_json[160];
        snprintf(reply_json, sizeof(reply_json),
                    "{\"func\":\"wifi_wakeup_result\",\"result\":%d,\"message\":\"wakeup No-WiFi\",\"stage\":\"error\"}",
                    TDX_JSON_RESULT_BLE_NO_SAVED_WIFI);

            #if(USER_BLE_ENABLE == 1)
             (void)s_active_send_json(reply_json);
                // 杈撳嚭缁撴灉
                UserDebugOutput_Printf("JSON:\n%s\n", reply_json);
            #else
             (void)s_active_send_json(reply_json);
            #endif
    }
    else {
        if (wifi_connect_task_is_active()) {
            ESP_LOGW(TAG,
                     "WiFi wakeup ignored: connection task already in progress");
            return 0;
        }
        esp_err_t submit_ret = submit_wifi_connect(
            s_active_send_json,
            true,
            "wifi_wakeup_result",
            false,
            xTaskGetTickCount());
        if (submit_ret == ESP_OK) {
            char reply_json[192];
            snprintf(reply_json, sizeof(reply_json),
                     "{\"func\":\"wifi_wakeup_result\",\"result\":%d,\"message\":\"WiFi wakeup submitted\",\"stage\":\"connecting\"}",
                     TDX_JSON_RESULT_OK);
            (void)s_active_send_json(reply_json);
        } else if (submit_ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "WiFi wakeup ignored: connection task already in progress");
        } else {
            send_simple_result_with_sender(s_active_send_json,
                                           "wifi_wakeup_result",
                                           TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT,
                                           "WiFi wakeup submit failed");
        }
    }

    return 0;
}

int parse_wifi_work_time_json(const char *json_str, wifi_work_time_json_t *out)
{
    cJSON *root = NULL;
    cJSON *item_func = NULL;
    cJSON *item_seconds = NULL;
    cJSON *item_time = NULL;
    cJSON *item_duration = NULL;
    char reply_json[160];
    esp_err_t set_ret = ESP_OK;

    if (json_str == NULL || out == NULL) {
        LOG_ERROR("Invalid parameter");
        return -1;
    }

    memset(out, 0, sizeof(*out));

    root = cJSON_Parse(json_str);
    if (root == NULL) {
        LOG_Purple("%s>%d  Invalid JSON",__func__,__LINE__);
        return -1;
    }

    item_func = cJSON_GetObjectItem(root, "func");
    item_seconds = cJSON_GetObjectItem(root, "seconds");
    item_time = cJSON_GetObjectItem(root, "time");

    if (!cJSON_IsString(item_func) || item_func->valuestring == NULL) {
        cJSON_Delete(root);
                LOG_Purple("%s>%d  Invalid JSON",__func__,__LINE__);
        return -1;
    }

    snprintf(out->func, sizeof(out->func), "%s", item_func->valuestring);

    if (strcmp(out->func, "set_wifi_work_time") != 0 && strcmp(out->func, "wifi_standby") != 0) {
        cJSON_Delete(root);
        return -1;
    }

    item_duration = cJSON_IsNumber(item_seconds) ? item_seconds : item_time;
    if (!cJSON_IsNumber(item_duration)) {
        cJSON_Delete(root);
        LOG_ERROR("set_wifi_work_time JSON missing seconds/time");
        snprintf(reply_json, sizeof(reply_json),
                 "{\"func\":\"set_wifi_work_time_result\",\"result\":%d,\"message\":\"set wifi work time failed\"}",
                 TDX_JSON_RESULT_WIFI_WORK_TIME_MISSING);
            #if(USER_BLE_ENABLE == 1)
             (void)s_active_send_json(reply_json);
             UserDebugOutput_Printf("JSON:\n%s\n", reply_json);
            #else
             (void)s_active_send_json(reply_json);
            #endif
        return 0;
    }

    if (item_duration->valueint < SERVER_NETWORK_STA_WIFI_WORK_TIME_MIN_SECONDS ||
        item_duration->valueint > SERVER_NETWORK_STA_WIFI_WORK_TIME_MAX_SECONDS) {
        cJSON_Delete(root);
        LOG_ERROR("set_wifi_work_time JSON invalid");
        snprintf(reply_json, sizeof(reply_json),
                 "{\"func\":\"set_wifi_work_time_result\",\"result\":%d,\"message\":\"set wifi work time failed\"}",
                 TDX_JSON_RESULT_WIFI_WORK_TIME_RANGE);
            #if(USER_BLE_ENABLE == 1)
             (void)s_active_send_json(reply_json);
             UserDebugOutput_Printf("JSON:\n%s\n", reply_json);
            #else
             (void)s_active_send_json(reply_json);
            #endif
        return 0;
    }

    out->seconds = item_duration->valueint;
    set_ret = ServerNetworkStaWifiWorkTime_SetAndSave((uint32_t)out->seconds);
    cJSON_Delete(root);

    if (set_ret == ESP_OK) {
        snprintf(reply_json, sizeof(reply_json),
                 "{\"func\":\"set_wifi_work_time_result\",\"result\":%d}",
                 TDX_JSON_RESULT_OK);
    } else {
        snprintf(reply_json, sizeof(reply_json),
                 "{\"func\":\"set_wifi_work_time_result\",\"result\":%d,\"message\":\"set wifi work time failed\"}",
                 set_ret == ESP_ERR_INVALID_STATE
                     ? TDX_JSON_RESULT_WIFI_WORK_TIME_APPLY_FAILED
                     : TDX_JSON_RESULT_WIFI_WORK_TIME_SAVE_FAILED);
    }

            #if(USER_BLE_ENABLE == 1)
             (void)s_active_send_json(reply_json);
             UserDebugOutput_Printf("JSON:\n%s\n", reply_json);
            #else
             (void)s_active_send_json(reply_json);
            #endif

    return 0;
}

static void handle_wifi_json_text_with_sender(const char *json_text,
                                              json_sender_t send_json,
                                              bool reply_to_ch583)
{
    if (json_text == NULL || json_text[0] == '\0') {
        send_simple_result_with_sender(send_json, "ble_json_result", TDX_JSON_RESULT_BLE_JSON_EMPTY, "empty json");
        return;
    }

    if (ServerNetworkStaWifiCredential_JsonHasEmbeddedNul(
            json_text, strlen(json_text))) {
        ESP_LOGW(TAG, "BLE/CH583 JSON rejected reason=embedded_nul");
        send_simple_result_with_sender(send_json,
                                       "ble_json_result",
                                       TDX_JSON_RESULT_BLE_JSON_PARSE_FAILED,
                                       "invalid json");
        return;
    }

    ESP_LOGI(TAG, "RX JSON ch583=%d: %s", reply_to_ch583 ? 1 : 0, json_text);

    cJSON *root = cJSON_Parse(json_text);
    if (root == NULL) {
        send_simple_result_with_sender(send_json,
                                       "ble_json_result",
                                       TDX_JSON_RESULT_BLE_JSON_PARSE_FAILED,
                                       "invalid json");
        return;
    }
    cJSON *func_item = cJSON_GetObjectItemCaseSensitive(root, "func");
    if (!cJSON_IsString(func_item) || func_item->valuestring == NULL) {
        cJSON_Delete(root);
        send_simple_result_with_sender(send_json,
                                       "ble_json_result",
                                       TDX_JSON_RESULT_BLE_FUNC_UNSUPPORTED,
                                       "missing func");
        return;
    }

    char func[32];
    strlcpy(func, func_item->valuestring, sizeof(func));
    if (strcmp(func, "wifi") == 0) {
        server_network_sta_ssid_candidate_config_t candidate_config = {};
        const char *validation_message = "ssid invalid";
        int validation_result = parse_wifi_candidate_config(
            root, &candidate_config, &validation_message);
        if (validation_result != TDX_JSON_RESULT_OK) {
            const cJSON *ssid_item =
                cJSON_GetObjectItemCaseSensitive(root, "ssid");
            if (cJSON_IsString(ssid_item) && ssid_item->valuestring != NULL) {
                server_network_sta_wifi_credential_result_t ssid_validation =
                    ServerNetworkStaWifiCredential_Validate(
                        ssid_item->valuestring, "");
                if (ssid_validation ==
                    SERVER_NETWORK_STA_WIFI_CREDENTIAL_SSID_INVALID_UTF8) {
                    log_invalid_wifi_ssid_bytes(ssid_item->valuestring);
                }
            }
            ESP_LOGW(TAG, "WiFi request rejected result=%d message=%s",
                     validation_result, validation_message);
            cJSON_Delete(root);
            send_simple_result_with_sender(send_json,
                                           "wifi_result",
                                           validation_result,
                                           validation_message);
            return;
        }
    }
    cJSON_Delete(root);
    s_active_send_json = send_json;

    if (reply_to_ch583) {
        WiFi_config_from_ch583 = true;
    } else {
        WiFi_config_from_ble = true;
    }
    if (strcmp(func, "wifi") == 0 && parse_wifi_config_json(json_text, &wifi_cfg) == 0) {
        UserDebugOutput_Printf("ssid=%s\n", wifi_cfg.ssid);
        return;
    }
    if (reply_to_ch583) {
        WiFi_config_from_ch583 = false;
    } else {
        WiFi_config_from_ble = false;
    }
    if (strcmp(func, "wifi_wakeup") == 0 && parse_wifi_wakeup_json(json_text, &wifi_cfg) == 0) {
        UserDebugOutput_Printf("wakeup ok\r\n");
        return;
    }
    if ((strcmp(func, "set_wifi_work_time") == 0 || strcmp(func, "wifi_standby") == 0) &&
        parse_wifi_work_time_json(json_text, &wifi_work_time_cfg) == 0) {
        UserDebugOutput_Printf("func=%s\n", wifi_work_time_cfg.func);
        UserDebugOutput_Printf("seconds=%d\n", wifi_work_time_cfg.seconds);
        return;
    }

    send_simple_result_with_sender(send_json, "ble_json_result", TDX_JSON_RESULT_BLE_FUNC_UNSUPPORTED, "unsupported func");
}

void User_HandleWifiJsonText(const char *json_text)
{
    handle_wifi_json_text_with_sender(json_text, ble_send_json, false);
}

void User_HandleWifiJsonTextFromCh583(const char *json_text)
{
    handle_wifi_json_text_with_sender(json_text, ch583_send_json, true);
}

#if USER_BLE_ENABLE
static void User_HandleWifiJsonBytes(const uint8_t *json_data, uint16_t json_len)
{
    char json_text[USER_BLE_JSON_BUF_SIZE + 1];
    size_t copy_len = json_len;

    if (json_data == NULL || json_len == 0) {
        User_HandleWifiJsonText("");
        return;
    }
    if (copy_len > USER_BLE_JSON_BUF_SIZE) {
        copy_len = USER_BLE_JSON_BUF_SIZE;
    }

    memcpy(json_text, json_data, copy_len);
    json_text[copy_len] = '\0';
    User_HandleWifiJsonText(json_text);
}

static void User_BleWriteTask(void *arg)
{
    (void)arg;

    while (true) {
        user_ble_write_msg_t *msg = NULL;
        if (xQueueReceive(s_ble_write_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (msg != NULL) {
            User_HandleWifiJsonBytes(msg->data, msg->len);
            free(msg);
        }
    }
}

esp_err_t UserBleDataHandler_Init(void)
{
    if (s_ble_write_queue == NULL) {
        s_ble_write_queue = xQueueCreate(USER_BLE_WRITE_QUEUE_LENGTH, sizeof(user_ble_write_msg_t *));
        if (s_ble_write_queue == NULL) {
            ESP_LOGE(TAG, "create BLE write queue failed");
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_ble_write_task == NULL) {
        BaseType_t ret = xTaskCreate(User_BleWriteTask,
                                     "ble_write",
                                     USER_BLE_WRITE_TASK_STACK_SIZE,
                                     NULL,
                                     USER_BLE_WRITE_TASK_PRIORITY,
                                     &s_ble_write_task);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "create BLE write task failed");
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(TAG, "BLE data handler ready queue=%d json_max=%d",
             USER_BLE_WRITE_QUEUE_LENGTH, USER_BLE_JSON_BUF_SIZE);
    return ESP_OK;
}

esp_err_t User_QueueBleWriteBytes(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) {
        ESP_LOGW(TAG, "BLE write empty");
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ble_write_queue == NULL) {
        ESP_LOGW(TAG, "BLE write queue not ready");
        return ESP_ERR_INVALID_STATE;
    }

    user_ble_write_msg_t *msg = (user_ble_write_msg_t *)calloc(1, sizeof(user_ble_write_msg_t));
    if (msg == NULL) {
        ESP_LOGE(TAG, "alloc BLE write msg failed len=%u", (unsigned int)len);
        return ESP_ERR_NO_MEM;
    }

    msg->len = len > USER_BLE_JSON_BUF_SIZE ? USER_BLE_JSON_BUF_SIZE : len;
    memcpy(msg->data, data, msg->len);

    if (xQueueSend(s_ble_write_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "BLE write queue full, drop len=%u", (unsigned int)msg->len);
        free(msg);
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "BLE write queued len=%u", (unsigned int)msg->len);
    return ESP_OK;
}
#else
esp_err_t UserBleDataHandler_Init(void)
{
    ESP_LOGI(TAG, "BLE data handler disabled by USER_BLE_ENABLE=0");
    return ESP_OK;
}

esp_err_t User_QueueBleWriteBytes(const uint8_t *data, uint16_t len)
{
    (void)data;
    (void)len;
    return ESP_ERR_NOT_SUPPORTED;
}
#endif
