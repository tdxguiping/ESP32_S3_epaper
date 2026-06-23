#include "server_network_sta_wifi_work_time.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "ch583_wifi_uart_protocol.h"
#include "tdx_cfg.h"

static const char *TAG = "server_sta_wifi_time";
static TickType_t s_wifi_work_start_tick = 0;
static TickType_t s_last_network_data_tick = 0;
static TaskHandle_t s_work_state_task = NULL;
static bool s_ota_in_progress = false;

// Keep these globals compatible with the old sleep/work-time flow for BLE and HTTP handlers.
uint32_t working_time = 0;
uint32_t server_required_continue_work_time = USER_WORK_STATE_DEFAULT_CONTINUE_SECONDS;
uint32_t wifi_standby_time_s = USER_WORK_STATE_DEFAULT_STANDBY_SECONDS;

// Store all runtime sleep/work values in one NVS blob so future power-mode changes stay centralized.
typedef struct __attribute__((packed)) {
    uint16_t sleep_time_value;
    uint32_t working_time_value;
    uint32_t server_required_continue_work_time_value;
    uint32_t wifi_standby_time_s_value;
} user_work_state_nvs_blob_t;

static uint32_t clamp_continue_seconds(uint32_t seconds)
{
    if (seconds < USER_WORK_STATE_MIN_CONTINUE_SECONDS) {
        return USER_WORK_STATE_MIN_CONTINUE_SECONDS;
    }
    if (seconds > USER_WORK_STATE_MAX_CONTINUE_SECONDS) {
        return USER_WORK_STATE_MAX_CONTINUE_SECONDS;
    }
    return seconds;
}

static void log_work_state_blob(const char *label, const user_work_state_nvs_blob_t *blob)
{
    if (blob == NULL) {
        return;
    }
    ESP_LOGI(TAG,
             "%s sleep_time=%u working_time=%lu continue=%lu standby=%lu",
             label != NULL ? label : "work_state",
             (unsigned int)blob->sleep_time_value,
             (unsigned long)blob->working_time_value,
             (unsigned long)blob->server_required_continue_work_time_value,
             (unsigned long)blob->wifi_standby_time_s_value);
}

static user_work_state_nvs_blob_t make_work_state_blob(void)
{
    user_work_state_nvs_blob_t blob = {
        .sleep_time_value = sleep_time,
        .working_time_value = working_time,
        .server_required_continue_work_time_value = server_required_continue_work_time,
        .wifi_standby_time_s_value = wifi_standby_time_s,
    };
    return blob;
}

static void apply_work_state_blob(const user_work_state_nvs_blob_t *blob)
{
    if (blob == NULL) {
        return;
    }

    sleep_time = blob->sleep_time_value;
    working_time = 0;
    server_required_continue_work_time = clamp_continue_seconds(blob->server_required_continue_work_time_value);
    wifi_standby_time_s = blob->wifi_standby_time_s_value != 0 ?
                          blob->wifi_standby_time_s_value :
                          USER_WORK_STATE_DEFAULT_STANDBY_SECONDS;

    s_wifi_work_start_tick = xTaskGetTickCount();
    s_last_network_data_tick = s_wifi_work_start_tick;
    ESP_LOGI(TAG,
             "restore globals sleep_time=%u working_time=%lu continue=%lu standby=%lu",
             (unsigned int)sleep_time,
             (unsigned long)working_time,
             (unsigned long)server_required_continue_work_time,
             (unsigned long)wifi_standby_time_s);
}

static esp_err_t load_work_state_from_nvs(user_work_state_nvs_blob_t *blob, size_t *stored_size)
{
    nvs_handle_t handle = 0;
    size_t size = 0;

    if (blob == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = nvs_open(USER_WORK_STATE_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "open work state nvs failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_get_blob(handle, USER_WORK_STATE_NVS_KEY, NULL, &size);
    if (ret == ESP_OK && size == sizeof(*blob)) {
        ret = nvs_get_blob(handle, USER_WORK_STATE_NVS_KEY, blob, &size);
    } else if (ret == ESP_OK) {
        ESP_LOGW(TAG, "work state blob size mismatch stored=%u expected=%u",
                 (unsigned int)size, (unsigned int)sizeof(*blob));
        ret = ESP_ERR_INVALID_SIZE;
    }
    nvs_close(handle);

    if (stored_size != NULL) {
        *stored_size = size;
    }
    return ret;
}

static esp_err_t save_work_state_to_nvs(void)
{
    nvs_handle_t handle = 0;
    user_work_state_nvs_blob_t blob = make_work_state_blob();

    esp_err_t ret = nvs_open(USER_WORK_STATE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "open work state nvs for write failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_blob(handle, USER_WORK_STATE_NVS_KEY, &blob, sizeof(blob));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);

    if (ret == ESP_OK) {
        user_work_state_nvs_blob_t read_blob = {0};
        size_t stored_size = 0;
        log_work_state_blob("save value", &blob);
        esp_err_t read_ret = load_work_state_from_nvs(&read_blob, &stored_size);
        if (read_ret == ESP_OK && memcmp(&blob, &read_blob, sizeof(blob)) == 0) {
            ESP_LOGI(TAG, "work state verify ok stored_size=%u", (unsigned int)stored_size);
        } else {
            ESP_LOGW(TAG, "work state verify failed read_ret=%s stored_size=%u",
                     esp_err_to_name(read_ret), (unsigned int)stored_size);
        }
    } else {
        ESP_LOGE(TAG, "save work state failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static bool parse_app_nvs_u32(const char *value, uint32_t *out_value)
{
    char *end_ptr = NULL;
    unsigned long parsed = 0;

    if (value == NULL || out_value == NULL || value[0] == '\0') {
        return false;
    }

    errno = 0;
    parsed = strtoul(value, &end_ptr, 10);
    if (errno != 0 || end_ptr == value || *end_ptr != '\0' || parsed > UINT32_MAX) {
        return false;
    }

    *out_value = (uint32_t)parsed;
    return true;
}

static esp_err_t save_work_time_vars_to_app_nvs(void)
{
    char value[16];
    esp_err_t ret = ESP_OK;
    esp_err_t write_ret = ESP_OK;

    snprintf(value, sizeof(value), "%lu", (unsigned long)server_required_continue_work_time);
    write_ret = app_nvs_write_str(SERVER_REQUIRED_CONTINUE_WORK_TIME_NVS_KEY, value);
    if (write_ret != ESP_OK && ret == ESP_OK) {
        ret = write_ret;
    }

    snprintf(value, sizeof(value), "%lu", (unsigned long)wifi_standby_time_s);
    write_ret = app_nvs_write_str(WIFI_STANDBY_TIME_S_NVS_KEY, value);
    if (write_ret != ESP_OK && ret == ESP_OK) {
        ret = write_ret;
    }

    ESP_LOGI(TAG, "save app nvs continue=%lu standby=%lu ret=%s",
             (unsigned long)server_required_continue_work_time,
             (unsigned long)wifi_standby_time_s,
             esp_err_to_name(ret));
    return ret;
}

static void load_work_time_vars_from_app_nvs(void)
{
    char value[16];
    char default_value[16];
    uint32_t parsed = 0;

    snprintf(default_value, sizeof(default_value), "%lu",
             (unsigned long)server_required_continue_work_time);
    if (app_nvs_read_str(SERVER_REQUIRED_CONTINUE_WORK_TIME_NVS_KEY,
                         value,
                         sizeof(value),
                         default_value) == ESP_OK &&
        parse_app_nvs_u32(value, &parsed)) {
        server_required_continue_work_time = clamp_continue_seconds(parsed);
    }

    snprintf(default_value, sizeof(default_value), "%lu",
             (unsigned long)wifi_standby_time_s);
    if (app_nvs_read_str(WIFI_STANDBY_TIME_S_NVS_KEY,
                         value,
                         sizeof(value),
                         default_value) == ESP_OK &&
        parse_app_nvs_u32(value, &parsed) &&
        parsed != 0) {
        wifi_standby_time_s = parsed;
    }

    ESP_LOGI(TAG, "load app nvs continue=%lu standby=%lu",
             (unsigned long)server_required_continue_work_time,
             (unsigned long)wifi_standby_time_s);
}

static uint32_t update_working_time_seconds(void)
{
    TickType_t now = xTaskGetTickCount();
    if (s_wifi_work_start_tick == 0) {
        s_wifi_work_start_tick = now;
    }
    working_time = (uint32_t)(((now - s_wifi_work_start_tick) * portTICK_PERIOD_MS) / 1000U);
    return working_time;
}

static void work_state_task(void *arg)
{
    uint8_t counter = 0;
    (void)arg;

    while (true) {
        uint32_t elapsed = update_working_time_seconds();
        uint32_t clamped_continue_time = clamp_continue_seconds(server_required_continue_work_time);
        if (clamped_continue_time != server_required_continue_work_time) {
            server_required_continue_work_time = clamped_continue_time;
            (void)save_work_state_to_nvs();
            (void)save_work_time_vars_to_app_nvs();
        }

        uint32_t remaining = server_required_continue_work_time > elapsed ?
                             server_required_continue_work_time - elapsed :
                             0;



        counter++;
        if(counter >30)
        {
         counter = 0;
         ESP_LOGI(TAG, "work_state status elapsed=%lu target=%lu remaining=%lu standby=%lu",
                 (unsigned long)elapsed,
                 (unsigned long)server_required_continue_work_time,
                 (unsigned long)remaining,
                 (unsigned long)wifi_standby_time_s);
        }



        if (elapsed > server_required_continue_work_time) {
            if (s_ota_in_progress) {
                if (counter == 0) {
                    ESP_LOGI(TAG,
                             "working_time timeout ignored during OTA elapsed=%lu target=%lu standby=%lu",
                             (unsigned long)elapsed,
                             (unsigned long)server_required_continue_work_time,
                             (unsigned long)wifi_standby_time_s);
                }
            } else {
                ESP_LOGI(TAG,
                         "working_time timeout, send CH583 power off elapsed=%lu target=%lu standby=%lu",
                         (unsigned long)elapsed,
                         (unsigned long)server_required_continue_work_time,
                         (unsigned long)wifi_standby_time_s);
                int power_off_ret = ch583_wifi_uart_send_power_off();
                if (power_off_ret < 0) {
                    ESP_LOGW(TAG, "CH583 power off command failed ret=%d", power_off_ret);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(USER_WORK_STATE_TASK_INTERVAL_MS));
    }
}

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

static bool json_func_equals(const char *body, const char *func)
{
    const char *pos = find_json_key(body, "func");
    if (pos == NULL || func == NULL) {
        return false;
    }

    pos += strlen("func") + 2;
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

    size_t func_len = strlen(func);
    return strncmp(pos, func, func_len) == 0 && pos[func_len] == '"';
}

static bool parse_json_int(const char *body, const char *key, int *out)
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

static esp_err_t send_wifi_work_time_result(httpd_req_t *req, int result, const char *message)
{
    char json[176];
    if (result == TDX_JSON_RESULT_OK) {
        snprintf(json, sizeof(json),
                 "{\"func\":\"set_wifi_work_time_result\",\"result\":%d}",
                 TDX_JSON_RESULT_OK);
    } else {
        snprintf(json, sizeof(json),
                 "{\"func\":\"set_wifi_work_time_result\",\"result\":%d,\"message\":\"%s\"}",
                 result,
                 message != NULL ? message : "set wifi work time failed");
    }

    ESP_LOGI(TAG, "set_wifi_work_time response: %s", json);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

void ServerNetworkStaWifiWorkTime_OnNetworkData(void)
{
    working_time = 0;
    s_last_network_data_tick = xTaskGetTickCount();
    s_wifi_work_start_tick = s_last_network_data_tick;
    if (server_required_continue_work_time > 0) {
        ESP_LOGI(TAG, "activity reset working_time continue=%lu elapsed_ms=%u",
                 (unsigned long)server_required_continue_work_time,
                 (unsigned int)((s_last_network_data_tick - s_wifi_work_start_tick) * portTICK_PERIOD_MS));
    }
}

void ServerNetworkStaWifiWorkTime_SetOtaInProgress(bool in_progress)
{
    s_ota_in_progress = in_progress;
    ESP_LOGI(TAG, "ota in progress=%d", in_progress ? 1 : 0);
}

esp_err_t ServerNetworkStaWifiWorkTime_Init(void)
{
    user_work_state_nvs_blob_t blob = {0};
    size_t stored_size = 0;

    s_wifi_work_start_tick = xTaskGetTickCount();
    s_last_network_data_tick = s_wifi_work_start_tick;

    esp_err_t ret = load_work_state_from_nvs(&blob, &stored_size);
    if (ret == ESP_OK) {
        log_work_state_blob("read value", &blob);
        apply_work_state_blob(&blob);
    } else {
        sleep_time = 0;
        working_time = 0;
        server_required_continue_work_time = USER_WORK_STATE_DEFAULT_CONTINUE_SECONDS;
        wifi_standby_time_s = USER_WORK_STATE_DEFAULT_STANDBY_SECONDS;
        ESP_LOGW(TAG, "use default work state ret=%s stored_size=%u continue=%lu standby=%lu",
                 esp_err_to_name(ret), (unsigned int)stored_size,
                 (unsigned long)server_required_continue_work_time,
                 (unsigned long)wifi_standby_time_s);
        ret = save_work_state_to_nvs();
    }

    load_work_time_vars_from_app_nvs();
    esp_err_t app_nvs_ret = save_work_time_vars_to_app_nvs();
    if (ret == ESP_OK && app_nvs_ret != ESP_OK) {
        ret = app_nvs_ret;
    }

    if (s_work_state_task == NULL) {
        BaseType_t task_ret = xTaskCreate(work_state_task,
                                          "work_state",
                                          USER_WORK_STATE_TASK_STACK_SIZE,
                                          NULL,
                                          USER_WORK_STATE_TASK_PRIORITY,
                                          &s_work_state_task);
        if (task_ret != pdPASS) {
            ESP_LOGE(TAG, "create work_state task failed");
            return ESP_ERR_NO_MEM;
        }
    }

    return ret;
}

esp_err_t ServerNetworkStaWifiWorkTime_SetAndSave(uint32_t seconds)
{
    if (seconds == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_work_state_task == NULL) {
        ESP_LOGE(TAG, "set work time apply failed because work_state task is not ready");
        return ESP_ERR_INVALID_STATE;
    }

    server_required_continue_work_time = clamp_continue_seconds(seconds);
    wifi_standby_time_s = seconds;
    working_time = 0;
    s_wifi_work_start_tick = xTaskGetTickCount();
    s_last_network_data_tick = s_wifi_work_start_tick;

    ESP_LOGI(TAG, "set work time requested=%lu continue=%lu standby=%lu",
             (unsigned long)seconds,
             (unsigned long)server_required_continue_work_time,
             (unsigned long)wifi_standby_time_s);
    esp_err_t ret = save_work_state_to_nvs();
    esp_err_t app_nvs_ret = save_work_time_vars_to_app_nvs();
    if (ret == ESP_OK && app_nvs_ret != ESP_OK) {
        ret = app_nvs_ret;
    }
    ESP_LOGI(TAG, "set work time save ret=%s app_nvs_ret=%s",
             esp_err_to_name(ret),
             esp_err_to_name(app_nvs_ret));
    return ret;
}

esp_err_t ServerNetworkStaWifiWorkTime_ProcessJson(httpd_req_t *req,
                                                   const char *body,
                                                   size_t body_len)
{
    (void)body_len;
    if (!json_func_equals(body, "set_wifi_work_time")) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    int seconds = 0;
    if (!parse_json_int(body, "seconds", &seconds) &&
        !parse_json_int(body, "time", &seconds)) {
        ESP_LOGW(TAG, "set_wifi_work_time invalid seconds body=%s", body != NULL ? body : "<null>");
        return send_wifi_work_time_result(req,
                                          TDX_JSON_RESULT_WIFI_WORK_TIME_MISSING,
                                          "set wifi work time failed");
    }
    if (seconds < SERVER_NETWORK_STA_WIFI_WORK_TIME_MIN_SECONDS ||
        seconds > SERVER_NETWORK_STA_WIFI_WORK_TIME_MAX_SECONDS) {
        ESP_LOGW(TAG, "set_wifi_work_time seconds out of range seconds=%d body=%s",
                 seconds, body != NULL ? body : "<null>");
        return send_wifi_work_time_result(req,
                                          TDX_JSON_RESULT_WIFI_WORK_TIME_RANGE,
                                          "set wifi work time failed");
    }

    esp_err_t set_ret = ServerNetworkStaWifiWorkTime_SetAndSave((uint32_t)seconds);
    if (set_ret != ESP_OK) {
        ESP_LOGE(TAG, "set_wifi_work_time save failed: %s", esp_err_to_name(set_ret));
        return send_wifi_work_time_result(req,
                                          set_ret == ESP_ERR_INVALID_STATE ?
                                              TDX_JSON_RESULT_WIFI_WORK_TIME_APPLY_FAILED :
                                              TDX_JSON_RESULT_WIFI_WORK_TIME_SAVE_FAILED,
                                          "set wifi work time failed");
    }

    ESP_LOGI(TAG, "set_wifi_work_time updated seconds=%d max=%d working_time=%lu",
             seconds,
             SERVER_NETWORK_STA_WIFI_WORK_TIME_MAX_SECONDS,
             (unsigned long)update_working_time_seconds());
    ESP_LOGI(TAG, "set_wifi_work_time saved, CH583 power-off timeout is enabled");
    return send_wifi_work_time_result(req, TDX_JSON_RESULT_OK, NULL);
}
