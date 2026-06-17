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
static uint8_t wifi_config_had_doing = 0;
bool WiFi_config_net = false;
bool WiFi_config_from_ch583 = false;
bool WiFi_config_from_ble = false;

#if USER_BLE_ENABLE
typedef struct {
    uint16_t len;
    uint8_t data[USER_BLE_JSON_BUF_SIZE];
} user_ble_write_msg_t;

static QueueHandle_t s_ble_write_queue = NULL;
static TaskHandle_t s_ble_write_task = NULL;
#endif

static void ble_send_json(const char *json)
{
    if (json == NULL) {
        return;
    }
    ESP_LOGI(TAG, "BLE TX JSON: %s", json);
#if USER_BLE_ENABLE
    SendData_indicate((uint8_t *)json, (uint16_t)strlen(json));
#else
    ch583_wifi_uart_send_wifi_data(json);
#endif
}

static void ch583_send_json(const char *json)
{
    if (json == NULL) {
        return;
    }
    ESP_LOGI(TAG, "CH583 TX JSON: %s", json);
    ch583_wifi_uart_send_wifi_data(json);
}

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

static void send_simple_result_with_sender(void (*send_json)(const char *),
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
    send_json(json);
}

void send_base_info_to_mobile(void)
{
        char ip_str[sizeof("255.255.255.255")];
        char json_str[384];
        const esp_app_desc_t *app = esp_app_get_description();
        const esp_partition_t *running = esp_ota_get_running_partition();

        esp_netif_ip_info_t ip;
        esp_netif_t *esp_netif;
        // we can use esp_netif_next_unsafe since we one time initialize the network and we don't de-init
        esp_netif = esp_netif_next_unsafe(NULL);
        if(esp_netif != NULL) {
            esp_netif_get_ip_info(esp_netif, &ip);
            esp_netif = esp_netif_next_unsafe(esp_netif);
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
             SendData_indicate((uint8_t *)json_str, strlen(json_str));
             printf("JSON:\n%s\n", json_str);
            #else
             ch583_wifi_uart_send_wifi_data((const char *)json_str);
            #endif

            //printf("  ETHIP: " IPSTR "\r\n", IP2STR(&ip.ip));
            //printf("  ETHMASK: " IPSTR "\r\n", IP2STR(&ip.netmask));
            //printf("  ETHGW: " IPSTR "\r\n", IP2STR(&ip.gw));

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

    void AddSsid(const std::string& ssid, const std::string& password)
    {
        nvs_handle_t nvs_handle = 0;
        esp_err_t ret = nvs_open("wifi", NVS_READWRITE, &nvs_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SsidManager AddSsid open wifi failed: %s", esp_err_to_name(ret));
            return;
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
    }
};

class WifiConfigurationAp {
public:
    static WifiConfigurationAp& GetInstance()
    {
        static WifiConfigurationAp instance;
        return instance;
    }

    void Save(const std::string& ssid, const std::string& password)
    {
        SsidManager::GetInstance().AddSsid(ssid, password);
        (void)save_wifi_config_to_nvs(ssid.c_str(), password.c_str());
    }
};

static void User_Network_mode_app_init(void)
{
    (void)::User_Network_mode_app_init("/data");
}

static uint8_t check_net_state(void)
{
    esp_netif_ip_info_t ip = {};
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    if (netif == NULL) {
        netif = esp_netif_next_unsafe(NULL);
    }
    if (netif != NULL && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0) {
        return 1;
    }
    return 0;
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

    if(wifi_config_had_doing == 0)
    {
            auto& wifi_ap = WifiConfigurationAp::GetInstance();
            std::string wifi_ssid = out->ssid;
            std::string wifi_password = out->key;
            // 3. 鎶婄粨鏋勪綋鐨勫€煎杩?JSON            
            snprintf(reply_json, sizeof(reply_json),
                     "{\"func\":\"wifi_result\",\"result\":%d,\"message\":\"Find wifi\",\"stage\":\"%s\"}",
                     TDX_JSON_RESULT_OK,
                     wifi_cfg.ssid);

             WiFi_config_net =true;

            #if(USER_BLE_ENABLE == 1)
             SendData_indicate((uint8_t *)reply_json, strlen(reply_json));
            // 杈撳嚭缁撴灉
             printf("JSON:\n%s\n", reply_json);
            #else
             ch583_wifi_uart_send_wifi_data((const char *)reply_json);
            #endif

            printf("The Wifi info parsed ok, save to NVS\n\r");
            SsidManager::GetInstance().Clear();
            wifi_ap.Save(wifi_ssid, wifi_password);
            Wifi_connect_OK =1;
            User_Network_mode_app_init();

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
        printf("wakeup-ok\r\n");
    }
    else
    {
        printf("wakeup-fail\r\n");
        return -1;
    }
    cJSON_Delete(root);
    if(check_net_state()==1)
    {
       send_base_info_to_mobile();      
    }
    else if (!ble_has_saved_wifi_info()) {
        ESP_LOGW(TAG, "No saved WiFi credential");
        char reply_json[160];
        snprintf(reply_json, sizeof(reply_json),
                    "{\"func\":\"wifi_wakeup_result\",\"result\":%d,\"message\":\"wakeup No-WiFi\",\"stage\":\"error\"}",
                    TDX_JSON_RESULT_BLE_NO_SAVED_WIFI);

            #if(USER_BLE_ENABLE == 1)
             SendData_indicate((uint8_t *)reply_json, strlen(reply_json));
                // 杈撳嚭缁撴灉
                printf("JSON:\n%s\n", reply_json);
            #else
             ch583_wifi_uart_send_wifi_data((const char *)reply_json);
            #endif
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
             SendData_indicate((uint8_t *)reply_json, strlen(reply_json));
             printf("JSON:\n%s\n", reply_json);
            #else
             ch583_wifi_uart_send_wifi_data((const char *)reply_json);
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
             SendData_indicate((uint8_t *)reply_json, strlen(reply_json));
             printf("JSON:\n%s\n", reply_json);
            #else
             ch583_wifi_uart_send_wifi_data((const char *)reply_json);
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
                 TDX_JSON_RESULT_WIFI_WORK_TIME_SAVE_FAILED);
    }

            #if(USER_BLE_ENABLE == 1)
             SendData_indicate((uint8_t *)reply_json, strlen(reply_json));
             printf("JSON:\n%s\n", reply_json);
            #else
             ch583_wifi_uart_send_wifi_data((const char *)reply_json);
            #endif

    return 0;
}

static void handle_wifi_json_text_with_sender(const char *json_text,
                                              void (*send_json)(const char *),
                                              bool reply_to_ch583)
{
    if (json_text == NULL || json_text[0] == '\0') {
        send_simple_result_with_sender(send_json, "ble_json_result", TDX_JSON_RESULT_BLE_JSON_EMPTY, "empty json");
        return;
    }

    ESP_LOGI(TAG, "RX JSON ch583=%d: %s", reply_to_ch583 ? 1 : 0, json_text);

    if (reply_to_ch583) {
        WiFi_config_from_ch583 = true;
    } else {
        WiFi_config_from_ble = true;
    }
    if (parse_wifi_config_json(json_text, &wifi_cfg) == 0) {
        printf("ssid=%s\n", wifi_cfg.ssid);
        printf("password=%s\n", wifi_cfg.key);
        return;
    }
    if (reply_to_ch583) {
        WiFi_config_from_ch583 = false;
    } else {
        WiFi_config_from_ble = false;
    }
    if (parse_wifi_wakeup_json(json_text, &wifi_cfg) == 0) {
        printf("wakeup ok\r\n");
        return;
    }
    if (parse_wifi_work_time_json(json_text, &wifi_work_time_cfg) == 0) {
        printf("func=%s\n", wifi_work_time_cfg.func);
        printf("seconds=%d\n", wifi_work_time_cfg.seconds);
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
