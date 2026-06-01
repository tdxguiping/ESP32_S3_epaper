#include "ble_data_handler.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "server_network_sta_wifi_work_time.h"
#include "tdx_cfg.h"
#include "user_app.h"

#if USER_BLE_ENABLE

static const char *TAG = "ble_data";

typedef struct {
    uint16_t len;
    uint8_t data[USER_BLE_JSON_BUF_SIZE];
} user_ble_write_msg_t;

static QueueHandle_t s_ble_write_queue = NULL;
static TaskHandle_t s_ble_write_task = NULL;

static const char *find_json_key(const char *body, const char *key)
{
    char pattern[64];
    const char *pos = body;

    if (body == NULL || key == NULL) {
        return NULL;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    while ((pos = strstr(pos, pattern)) != NULL) {
        const char *after = pos + strlen(pattern);
        while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n') {
            after++;
        }
        if (*after == ':') {
            return pos;
        }
        pos += strlen(pattern);
    }
    return NULL;
}

static bool extract_json_string(const char *body, const char *key, char *out, size_t out_size)
{
    const char *pos = find_json_key(body, key);
    size_t len = 0;

    if (pos == NULL || out == NULL || out_size == 0) {
        return false;
    }

    pos += strlen(key) + 2;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    if (*pos != ':') {
        return false;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    if (*pos != '"') {
        return false;
    }
    pos++;

    while (pos[len] != '\0' && pos[len] != '"' && len + 1 < out_size) {
        out[len] = pos[len];
        len++;
    }
    out[len] = '\0';
    return len > 0 && pos[len] == '"';
}

static bool json_func_equals(const char *body, const char *func)
{
    char value[48];
    return extract_json_string(body, "func", value, sizeof(value)) && strcmp(value, func) == 0;
}

static bool parse_json_int_field(const char *body, const char *key, int *out)
{
    const char *pos = find_json_key(body, key);
    char *end_ptr = NULL;
    long value = 0;

    if (pos == NULL || out == NULL) {
        return false;
    }

    pos += strlen(key) + 2;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    if (*pos != ':') {
        return false;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }

    errno = 0;
    value = strtol(pos, &end_ptr, 10);
    if (errno != 0 || end_ptr == pos || value < INT32_MIN || value > INT32_MAX) {
        return false;
    }

    *out = (int)value;
    return true;
}

static void ble_send_json(const char *json)
{
    if (json == NULL) {
        return;
    }
    ESP_LOGI(TAG, "BLE TX JSON: %s", json);
    SendData_indicate((uint8_t *)json, (uint16_t)strlen(json));
}

static void send_simple_result(const char *func, int result, const char *message)
{
    char json[192];

    if (message == NULL || message[0] == '\0') {
        snprintf(json, sizeof(json), "{\"func\":\"%s\",\"result\":%d}", func, result);
    } else {
        snprintf(json, sizeof(json), "{\"func\":\"%s\",\"result\":%d,\"message\":\"%s\"}",
                 func, result, message);
    }
    ble_send_json(json);
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

    // English: Store WiFi credentials in the namespace read by the current Server Network STA path.
    // 中文：把 WiFi 账号保存到当前 Server Network STA 启动路径读取的 NVS 命名空间。
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

    ESP_LOGI(TAG, "BLE WiFi config save ret=%s ssid=%s password_len=%u",
             esp_err_to_name(ret), ssid, (unsigned int)strlen(password));
    return ret;
}

static bool handle_wifi_config_json(const char *json_text)
{
    char ssid[33] = {0};
    char password[65] = {0};

    if (!json_func_equals(json_text, "wifi_config") &&
        !json_func_equals(json_text, "set_wifi") &&
        find_json_key(json_text, "ssid") == NULL) {
        return false;
    }

    bool got_ssid = extract_json_string(json_text, "ssid", ssid, sizeof(ssid));
    bool got_password = extract_json_string(json_text, "password", password, sizeof(password));
    if (!got_password) {
        got_password = extract_json_string(json_text, "pswd", password, sizeof(password));
    }

    ESP_LOGI(TAG, "BLE wifi_config got_ssid=%d got_password=%d ssid=%s password_len=%u",
             got_ssid ? 1 : 0, got_password ? 1 : 0, ssid, (unsigned int)strlen(password));

    if (!got_ssid || !got_password) {
        send_simple_result("wifi_config_result", 1, "invalid wifi config");
        return true;
    }

    esp_err_t ret = save_wifi_config_to_nvs(ssid, password);
    if (ret == ESP_OK) {
        send_simple_result("wifi_config_result", 0, NULL);
    } else {
        send_simple_result("wifi_config_result", 1, "save wifi config failed");
    }
    return true;
}

static bool handle_wifi_wakeup_json(const char *json_text)
{
    if (!json_func_equals(json_text, "wifi_wakeup")) {
        return false;
    }

    // English: Keep this hook lightweight because the current project has no separate wakeup manager.
    // 中文：当前项目还没有独立唤醒管理器，这里只确认收到唤醒指令并返回结果。
    ESP_LOGI(TAG, "BLE wifi_wakeup received");
    send_simple_result("wifi_wakeup_result", 0, NULL);
    return true;
}

static bool handle_wifi_work_time_json(const char *json_text)
{
    int seconds = 0;

    if (!json_func_equals(json_text, "set_wifi_work_time")) {
        return false;
    }

    if (!parse_json_int_field(json_text, "seconds", &seconds)) {
        (void)parse_json_int_field(json_text, "time", &seconds);
    }

    if (seconds < SERVER_NETWORK_STA_WIFI_WORK_TIME_MIN_SECONDS ||
        seconds > SERVER_NETWORK_STA_WIFI_WORK_TIME_MAX_SECONDS) {
        send_simple_result("set_wifi_work_time_result", 1, "set wifi work time failed");
        return true;
    }

    esp_err_t ret = ServerNetworkStaWifiWorkTime_SetAndSave((uint32_t)seconds);
    ESP_LOGI(TAG, "BLE set_wifi_work_time seconds=%d ret=%s", seconds, esp_err_to_name(ret));
    if (ret == ESP_OK) {
        send_simple_result("set_wifi_work_time_result", 0, NULL);
    } else {
        send_simple_result("set_wifi_work_time_result", 1, "set wifi work time failed");
    }
    return true;
}

static void User_HandleWifiJsonText(const char *json_text)
{
    if (json_text == NULL || json_text[0] == '\0') {
        send_simple_result("ble_json_result", 1, "empty json");
        return;
    }

    ESP_LOGI(TAG, "BLE RX JSON: %s", json_text);

    if (handle_wifi_config_json(json_text)) {
        return;
    }
    if (handle_wifi_wakeup_json(json_text)) {
        return;
    }
    if (handle_wifi_work_time_json(json_text)) {
        return;
    }

    send_simple_result("ble_json_result", 1, "unsupported func");
}

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

#endif
