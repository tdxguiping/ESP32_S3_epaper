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
#include "tdx_cfg.h"

static const char *TAG = "server_sta_wifi_time";
static int s_wifi_work_seconds = 0;
static TickType_t s_wifi_work_start_tick = 0;
static TickType_t s_last_network_data_tick = 0;

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

static esp_err_t send_wifi_work_time_result(httpd_req_t *req, bool ok, const char *message)
{
    char json[176];
    if (ok) {
        snprintf(json, sizeof(json), "{\"func\":\"set_wifi_work_time_result\",\"result\":0}");
    } else {
        snprintf(json, sizeof(json),
                 "{\"func\":\"set_wifi_work_time_result\",\"result\":1,\"message\":\"%s\"}",
                 message != NULL ? message : "set wifi work time failed");
    }

    ESP_LOGI(TAG, "set_wifi_work_time response: %s", json);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

void ServerNetworkStaWifiWorkTime_OnNetworkData(void)
{
    s_last_network_data_tick = xTaskGetTickCount();
    if (s_wifi_work_seconds > 0) {
        ESP_LOGI(TAG, "network activity keeps WiFi alive seconds=%d elapsed_ms=%u",
                 s_wifi_work_seconds,
                 (unsigned int)((s_last_network_data_tick - s_wifi_work_start_tick) * portTICK_PERIOD_MS));
    }
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
    if (!parse_json_int(body, "seconds", &seconds) ||
        seconds < SERVER_NETWORK_STA_WIFI_WORK_TIME_MIN_SECONDS ||
        seconds > SERVER_NETWORK_STA_WIFI_WORK_TIME_MAX_SECONDS) {
        ESP_LOGW(TAG, "set_wifi_work_time invalid seconds body=%s", body != NULL ? body : "<null>");
        return send_wifi_work_time_result(req, false, "set wifi work time failed");
    }

    // Store the requested online window and reset the activity timer for future power-mode integration.
    // 保存本次请求的在线窗口并重置活动计时，方便后续接入低功耗管理时直接使用。
    s_wifi_work_seconds = seconds;
    s_wifi_work_start_tick = xTaskGetTickCount();
    s_last_network_data_tick = s_wifi_work_start_tick;

    ESP_LOGI(TAG, "set_wifi_work_time updated seconds=%d max=%d start_tick=%u",
             s_wifi_work_seconds,
             SERVER_NETWORK_STA_WIFI_WORK_TIME_MAX_SECONDS,
             (unsigned int)s_wifi_work_start_tick);
    ESP_LOGI(TAG, "set_wifi_work_time power manager hook is not present, WiFi/HTTP stay controlled by current project flow");
    return send_wifi_work_time_result(req, true, NULL);
}
