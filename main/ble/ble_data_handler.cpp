#include "ble_data_handler.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

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
#include "server_network_sta_wifi_work_time.h"
#include "tdx_cfg.h"
#include "user_app.h"

static const char *TAG = "ble_data";

typedef struct {
    char func[32];
    char ssid[64];
    char key[64];
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
    char json_str[384];
    char ssid_str[33] = {0};
    char ble_ver_str[4];

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

    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        snprintf(ssid_str, sizeof(ssid_str), "%s", (const char *)ap_info.ssid);
    }
    snprintf(ble_ver_str, sizeof(ble_ver_str), "%u", (unsigned int)ble_ver);

    snprintf(json_str, sizeof(json_str),
             "{\"func\":\"wifi_info_result\","
             "\"result\":%d,"
             "\"message\":\"wifi info\","
             "\"stage\":\"%s\","
             "\"WiFi\":\"%s\","
             "\"version\":\"%s:%s\","
             "\"date\":\"%s\","
             "\"running\":\"%s\"}",
             TDX_JSON_RESULT_OK,
             ip_str,
             ssid_str,
             app != NULL ? app->version : "",
             ble_ver_str,
             app != NULL ? app->date : "",
             running != NULL ? running->label : "");

    ESP_LOGI(TAG, "wifi_info_result send start ip=%s len=%u",
             ip_str,
             (unsigned int)strlen(json_str));

    for (int attempt = 1; attempt <= WIFI_INFO_NOTIFY_RETRY_COUNT; attempt++) {
        if (s_active_send_json(json_str)) {
            ESP_LOGI(TAG, "wifi_info_result sent ip=%s attempt=%d", ip_str, attempt);
#if (USER_BLE_ENABLE == 1)
            UserDebugOutput_Printf("JSON:\n%s\n", json_str);
#endif
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

static bool is_valid_wifi_text(const char *text, size_t max_len)
{
    size_t len = 0;

    if (text == NULL || text[0] == '\0') {
        return false;
    }
    len = strlen(text);
    return len > 0 && len < max_len;
}

static esp_err_t save_wifi_config_to_nvs(const char *ssid, const char *password)
{
    nvs_handle_t handle = 0;

    if (!is_valid_wifi_text(ssid, 33) || password == NULL || strlen(password) >= 65) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = nvs_open("nvs.net80211", NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "open nvs.net80211 failed: %s", esp_err_to_name(ret));
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

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi config save failed ret=%s", esp_err_to_name(ret));
    }
    return ret;
}

class SsidManager {
public:
    static SsidManager& GetInstance()
    {
        static SsidManager instance;
        return instance;
    }

    void Clear()
    {
        nvs_handle_t nvs_handle = 0;
        esp_err_t ret = nvs_open("wifi", NVS_READWRITE, &nvs_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "SsidManager Clear open wifi failed: %s", esp_err_to_name(ret));
            return;
        }

        for (int i = 0; i < 10; i++) {
            char ssid_key[16];
            char password_key[16];
            if (i == 0) {
                snprintf(ssid_key, sizeof(ssid_key), "ssid");
                snprintf(password_key, sizeof(password_key), "password");
            } else {
                snprintf(ssid_key, sizeof(ssid_key), "ssid%d", i);
                snprintf(password_key, sizeof(password_key), "password%d", i);
            }
            (void)nvs_erase_key(nvs_handle, ssid_key);
            (void)nvs_erase_key(nvs_handle, password_key);
        }
        ret = nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "SsidManager Clear failed: %s", esp_err_to_name(ret));
        }
    }

    esp_err_t AddSsid(const std::string& ssid, const std::string& password)
    {
        nvs_handle_t nvs_handle = 0;
        esp_err_t ret = nvs_open("wifi", NVS_READWRITE, &nvs_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SsidManager AddSsid open wifi failed: %s", esp_err_to_name(ret));
            return ret;
        }

        ret = nvs_set_str(nvs_handle, "ssid", ssid.c_str());
        if (ret == ESP_OK) {
            ret = nvs_set_str(nvs_handle, "password", password.c_str());
        }
        if (ret == ESP_OK) {
            ret = nvs_commit(nvs_handle);
        }
        nvs_close(nvs_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SsidManager AddSsid failed: %s", esp_err_to_name(ret));
        }
        return ret;
    }
};

class WifiConfigurationAp {
public:
    static WifiConfigurationAp& GetInstance()
    {
        static WifiConfigurationAp instance;
        return instance;
    }

    esp_err_t Save(const std::string& ssid, const std::string& password)
    {
        esp_err_t wifi_ret = SsidManager::GetInstance().AddSsid(ssid, password);
        esp_err_t sta_ret = save_wifi_config_to_nvs(ssid.c_str(), password.c_str());
        return wifi_ret != ESP_OK ? wifi_ret : sta_ret;
    }
};

static TaskHandle_t s_wifi_connect_task = NULL;
static json_sender_t s_wifi_connect_reply_sender = NULL;
static bool s_wifi_connect_notify_result = false;
static const char *s_wifi_connect_result_func = NULL;
static bool s_wifi_connect_new_credential = false;

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

static void wifi_connect_task(void *arg)
{
    (void)arg;
    bool new_credential = s_wifi_connect_new_credential;
    uint8_t init_result = new_credential
                              ? ::User_Network_mode_app_new_credential("/data")
                              : ::User_Network_mode_app_init("/data");
    int connect_result = ServerNetworkSta_GetLastConnectResult();
    ESP_LOGI(TAG, "BLE/CH583 WiFi connect task finished init=%u connect_result=%d",
             (unsigned int)init_result,
             connect_result);
    json_sender_t reply_sender = s_wifi_connect_reply_sender;
    bool notify_result = s_wifi_connect_notify_result;
    const char *result_func = s_wifi_connect_result_func != NULL
                                  ? s_wifi_connect_result_func
                                  : "wifi_wakeup_result";
    s_wifi_connect_reply_sender = NULL;
    s_wifi_connect_notify_result = false;
    s_wifi_connect_result_func = NULL;
    s_wifi_connect_new_credential = false;
    if (notify_result && reply_sender != NULL) {
        if (init_result == SERVER_NETWORK_STA_CONNECT_SUPERSEDED) {
            send_simple_result_with_sender(reply_sender,
                                           result_func,
                                           TDX_JSON_RESULT_BUSY,
                                           "WiFi connect request superseded");
        } else if (init_result == SERVER_NETWORK_STA_OK) {
            bool ip_ready = false;
            if (!notify_wifi_info_if_ip_ready(reply_sender, "wifi_connect_task", true, &ip_ready)) {
                if (ip_ready) {
                    ESP_LOGE(TAG, "WiFi got IP but wifi_info_result send failed");
                } else {
                    ESP_LOGW(TAG, "WiFi connect result OK but network was not ready for wifi_info_result");
                    send_simple_result_with_sender(reply_sender,
                                                   result_func,
                                                   TDX_JSON_RESULT_WIFI_GOT_IP_FAILED,
                                                   "WiFi network was not ready");
                }
            }
        } else if (connect_result == TDX_JSON_RESULT_WIFI_AUTH_FAILED) {
            send_simple_result_with_sender(reply_sender,
                                           result_func,
                                           TDX_JSON_RESULT_WIFI_AUTH_FAILED,
                                           "WiFi authentication failed");
        } else if (connect_result == TDX_JSON_RESULT_WIFI_GOT_IP_FAILED) {
            send_simple_result_with_sender(reply_sender,
                                           result_func,
                                           TDX_JSON_RESULT_WIFI_GOT_IP_FAILED,
                                           "WiFi did not obtain IP");
        } else {
            send_simple_result_with_sender(reply_sender,
                                           result_func,
                                           TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT,
                                           "WiFi connect timed out");
        }
    } else {
        ESP_LOGW(TAG, "WiFi connect task finished without notify sender notify=%d sender_set=%d result=%d",
                 notify_result ? 1 : 0,
                 reply_sender != NULL ? 1 : 0,
                 connect_result);
    }
    WiFi_config_from_ch583 = false;
    WiFi_config_from_ble = false;
    s_wifi_connect_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t submit_wifi_connect(json_sender_t reply_sender,
                                     bool notify_result,
                                     const char *result_func,
                                     bool new_credential)
{
    if (s_wifi_connect_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_wifi_connect_reply_sender = reply_sender;
    s_wifi_connect_notify_result = notify_result;
    s_wifi_connect_result_func = result_func;
    s_wifi_connect_new_credential = new_credential;
    if (xTaskCreate(wifi_connect_task,
                    "ble_wifi_connect",
                    6144,
                    NULL,
                    4,
                    &s_wifi_connect_task) != pdPASS) {
        s_wifi_connect_reply_sender = NULL;
        s_wifi_connect_notify_result = false;
        s_wifi_connect_result_func = NULL;
        s_wifi_connect_new_credential = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

int parse_wifi_config_json(const char *json_str, wifi_config_json_t *out)
{
    cJSON *root = NULL;
    cJSON *item_func = NULL;
    cJSON *item_ssid = NULL;
    cJSON *item_key = NULL;
    char reply_json[160];

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
    item_ssid = cJSON_GetObjectItem(root, "ssid");
    item_key = cJSON_GetObjectItem(root, "key");

    /* 涓枃娉ㄩ噴锛?      妫€鏌ュ瓧娈垫槸鍚﹀瓨鍦ㄤ笖鏄瓧绗︿覆    */

    if (!cJSON_IsString(item_func) || item_func->valuestring == NULL ||
        !cJSON_IsString(item_ssid) || item_ssid->valuestring == NULL ||
        !cJSON_IsString(item_key) || item_key->valuestring == NULL) {
        cJSON_Delete(root);
                LOG_Purple("%s>%d  Invalid JSON",__func__,__LINE__);
        return -1;
    }

    /* 涓枃娉ㄩ噴锛?
       瀹夊叏鎷疯礉鍒拌緭鍑虹粨鏋勪綋
    */
    snprintf(out->func, sizeof(out->func), "%s", item_func->valuestring);
    snprintf(out->ssid, sizeof(out->ssid), "%s", item_ssid->valuestring);
    snprintf(out->key, sizeof(out->key), "%s", item_key->valuestring);

    cJSON_Delete(root);
    Bl_Data_Ready =1;

    // Keep this scope local; s_wifi_connect_task is the single BLE/CH583 BUSY guard.
    {
            if (s_wifi_connect_task != NULL) {
                send_simple_result_with_sender(s_active_send_json,
                                               "wifi_result",
                                               TDX_JSON_RESULT_BUSY,
                                               "WiFi connect already in progress");
                return 0;
            }
            auto& wifi_ap = WifiConfigurationAp::GetInstance();
            std::string wifi_ssid = out->ssid;
            std::string wifi_password = out->key;
            // 3. 鎶婄粨鏋勪綋鐨勫€煎杩?JSON            
            snprintf(reply_json, sizeof(reply_json),
                     "{\"func\":\"wifi_result\",\"result\":%d,\"message\":\"Find wifi\",\"stage\":\"%s\"}",
                     TDX_JSON_RESULT_OK,
                     wifi_cfg.ssid);

            #if(USER_BLE_ENABLE == 1)
             (void)reply_json;
            // 杈撳嚭缁撴灉
             UserDebugOutput_Printf("JSON:\n%s\n", reply_json);
            #else
             (void)reply_json;
            #endif

            UserDebugOutput_Printf("The Wifi info parsed ok, save to NVS\n\r");
            SsidManager::GetInstance().Clear();
            esp_err_t save_ret = wifi_ap.Save(wifi_ssid, wifi_password);
            if (save_ret != ESP_OK) {
                send_simple_result_with_sender(s_active_send_json,
                                               "wifi_result",
                                               TDX_JSON_RESULT_WIFI_SAVE_FAILED,
                                               "save WiFi config failed");
                return 0;
            }

            esp_err_t submit_ret = submit_wifi_connect(s_active_send_json, true, "wifi_result", true);
            if (submit_ret != ESP_OK) {
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
       (void)notify_wifi_info_if_ip_ready(s_active_send_json, "wifi_wakeup_got_ip", false, NULL);
    }
    else if (wifi_status_is_progressing(status))
    {
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
        esp_err_t submit_ret = submit_wifi_connect(s_active_send_json, true, "wifi_wakeup_result", false);
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
        cJSON *ssid_item = cJSON_GetObjectItemCaseSensitive(root, "ssid");
        cJSON *key_item = cJSON_GetObjectItemCaseSensitive(root, "key");
        int validation_result = TDX_JSON_RESULT_OK;
        const char *validation_message = NULL;
        if (ssid_item == NULL) {
            validation_result = TDX_JSON_RESULT_WIFI_SSID_MISSING;
            validation_message = "ssid missing";
        } else if (key_item == NULL) {
            validation_result = TDX_JSON_RESULT_WIFI_KEY_MISSING;
            validation_message = "key missing";
        } else if (!cJSON_IsString(ssid_item) || ssid_item->valuestring == NULL ||
                   !is_valid_wifi_text(ssid_item->valuestring, 33)) {
            validation_result = TDX_JSON_RESULT_WIFI_SSID_INVALID;
            validation_message = "ssid invalid";
        } else if (!cJSON_IsString(key_item) || key_item->valuestring == NULL ||
                   strlen(key_item->valuestring) >= 65) {
            validation_result = TDX_JSON_RESULT_WIFI_KEY_INVALID;
            validation_message = "key invalid";
        }
        if (validation_result != TDX_JSON_RESULT_OK) {
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
