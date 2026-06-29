#include "server_network_sta_slideshow_control.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "file_serving_example_common.h"
#include "server_network_sta_slideshow.h"
#include "tdx_cfg.h"

static const char *TAG = "server_sta_slide_ctl";

typedef struct {
    int sw;
    uint32_t interval;
    bool random;
} slideshow_control_t;

static bool json_func_equals(const char *body, const char *func)
{
    const char *pos = body != NULL ? strstr(body, "\"func\"") : NULL;
    if (pos == NULL || func == NULL) {
        return false;
    }

    pos += strlen("\"func\"");
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

static const char *find_json_key(const char *body, const char *key)
{
    char pattern[64];
    const char *pos = body;
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
    if (errno != 0 || end_ptr == pos) {
        return false;
    }
    *out = (int)value;
    return true;
}

static bool parse_json_u32(const char *body, const char *key, uint32_t *out)
{
    const char *pos = find_json_key(body, key);
    char *end_ptr = NULL;
    unsigned long value = 0;
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
    if (*pos == '-') {
        return false;
    }

    errno = 0;
    value = strtoul(pos, &end_ptr, 10);
    if (errno != 0 || end_ptr == pos || value > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool parse_json_bool_optional(const char *body, const char *key, bool *out, bool *present)
{
    const char *pos = find_json_key(body, key);
    if (present != NULL) {
        *present = false;
    }
    if (pos == NULL) {
        return true;
    }
    if (present != NULL) {
        *present = true;
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

    if (strncmp(pos, "true", 4) == 0 || *pos == '1') {
        *out = true;
        return true;
    }
    if (strncmp(pos, "false", 5) == 0 || *pos == '0') {
        *out = false;
        return true;
    }
    return false;
}

static esp_err_t send_set_slideshow_result(httpd_req_t *req, int result, const char *message)
{
    char json[160];
    if (result == TDX_JSON_RESULT_OK) {
        snprintf(json, sizeof(json),
                 "{\"func\":\"set_slideshow_result\",\"result\":%d}",
                 TDX_JSON_RESULT_OK);
    } else {
        snprintf(json, sizeof(json),
                 "{\"func\":\"set_slideshow_result\",\"result\":%d,\"message\":\"%s\"}",
                 result,
                 message != NULL ? message : "set slideshow failed");
    }

    ESP_LOGI(TAG, "set_slideshow response: %s", json);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static bool read_existing_bool(const char *path, const char *key, bool default_value)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return default_value;
    }

    char buf[192] = {0};
    size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[len] = '\0';

    bool value = default_value;
    bool present = false;
    if (parse_json_bool_optional(buf, key, &value, &present) && present) {
        return value;
    }
    return default_value;
}

static uint32_t read_existing_interval(const char *path, uint32_t default_value)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return default_value;
    }

    char buf[192] = {0};
    size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[len] = '\0';

    uint32_t interval = default_value;
    if (parse_json_u32(buf, "interval", &interval) &&
        interval >= TDX_SLIDESHOW_INTERVAL_MIN_SECONDS &&
        interval <= TDX_SLIDESHOW_INTERVAL_MAX_SECONDS) {
        return interval;
    }
    return default_value;
}

static esp_err_t ensure_paths(const char *base_path, char *bin_dir, size_t bin_dir_size,
                              char *control_path, size_t control_path_size,
                              char *config_path, size_t config_path_size)
{
    struct stat st = {0};
    snprintf(bin_dir, bin_dir_size, "%s/bin_img", base_path);
    snprintf(control_path, control_path_size, "%s/%s", bin_dir, TDX_SLIDESHOW_CONTROL_FILE);
    snprintf(config_path, config_path_size, "%s/%s", bin_dir, TDX_SLIDESHOW_CONFIG_FILE);

    if (!example_storage_supports_directories()) {
        return ESP_OK;
    }

    if (stat(bin_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        ESP_LOGE(TAG, "set_slideshow bin dir missing: %s", bin_dir);
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

static bool slideshow_config_has_files(const char *config_path)
{
    FILE *fp = fopen(config_path, "rb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "set_slideshow config missing: %s", config_path);
        return false;
    }

    char buf[256] = {0};
    size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[len] = '\0';

    const char *array = strstr(buf, "\"fileNames\"");
    if (array == NULL) {
        return false;
    }
    array = strchr(array, '[');
    if (array == NULL) {
        return false;
    }
    return strchr(array, '"') != NULL;
}

static esp_err_t write_control_file(const char *control_path, const slideshow_control_t *control)
{
    char json[160];
    snprintf(json, sizeof(json), "{\"sw\":%d,\"interval\":%lu,\"random\":%s,\"run_mode\":%d}",
             control->sw,
             (unsigned long)control->interval,
             control->random ? "true" : "false",
             TDX_SLIDESHOW_RUN_MODE);

    FILE *fp = fopen(control_path, "wb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "set_slideshow open failed path=%s errno=%d", control_path, errno);
        return ESP_FAIL;
    }

    size_t len = strlen(json);
    size_t written = fwrite(json, 1, len, fp);
    fclose(fp);
    ESP_LOGI(TAG, "set_slideshow write control path=%s len=%u written=%u json=%s",
             control_path, (unsigned int)len, (unsigned int)written, json);
    return written == len ? ESP_OK : ESP_FAIL;
}

static esp_err_t parse_set_slideshow_request(const char *body,
                                             const char *control_path,
                                             slideshow_control_t *control)
{
    memset(control, 0, sizeof(*control));
    if (!json_func_equals(body, "set_slideshow")) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!parse_json_int(body, "sw", &control->sw) || (control->sw != 0 && control->sw != 1)) {
        return ESP_ERR_INVALID_ARG;
    }

    bool interval_present = parse_json_u32(body, "interval", &control->interval);
    if (control->sw == 1 && !interval_present) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!interval_present) {
        control->interval = read_existing_interval(control_path, TDX_SLIDESHOW_INTERVAL_MIN_SECONDS);
    }
    if (control->interval < TDX_SLIDESHOW_INTERVAL_MIN_SECONDS ||
        control->interval > TDX_SLIDESHOW_INTERVAL_MAX_SECONDS) {
        return ESP_ERR_INVALID_SIZE;
    }

    bool random_present = false;
    control->random = read_existing_bool(control_path, "random", false);
    if (!parse_json_bool_optional(body, "random", &control->random, &random_present)) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "set_slideshow parsed sw=%d interval=%lu random=%d random_present=%d",
             control->sw, (unsigned long)control->interval, control->random ? 1 : 0, random_present ? 1 : 0);
    return ESP_OK;
}

esp_err_t ServerNetworkStaSlideshowControl_ProcessJson(httpd_req_t *req,
                                                       const char *body,
                                                       size_t body_len,
                                                       const char *base_path)
{
    (void)body_len;
    char bin_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    char control_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    char config_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];

    if (!json_func_equals(body, "set_slideshow")) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (ensure_paths(base_path, bin_dir, sizeof(bin_dir),
                     control_path, sizeof(control_path),
                     config_path, sizeof(config_path)) != ESP_OK) {
        return send_set_slideshow_result(req, TDX_JSON_RESULT_STORAGE_NOT_READY, "storage not ready");
    }

    slideshow_control_t control;
    esp_err_t ret = parse_set_slideshow_request(body, control_path, &control);
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (ret == ESP_ERR_INVALID_SIZE) {
        return send_set_slideshow_result(req, TDX_JSON_RESULT_SLIDESHOW_INTERVAL_INVALID,
                                         "invalid interval");
    }
    if (ret != ESP_OK) {
        return send_set_slideshow_result(req, TDX_JSON_RESULT_PARAM_INVALID,
                                         "invalid slideshow parameters");
    }

    if (control.sw == 1 && !slideshow_config_has_files(config_path)) {
        return send_set_slideshow_result(req, TDX_JSON_RESULT_FILE_NAMES_MISSING,
                                         "slideshow fileNames missing");
    }
    if (write_control_file(control_path, &control) != ESP_OK) {
        return send_set_slideshow_result(req, TDX_JSON_RESULT_SLIDESHOW_CONTROL_SAVE_FAILED,
                                         "save slideshow control failed");
    }
    esp_err_t random_save_ret = app_nvs_write_str(TDX_SLIDESHOW_RANDOM_NVS_KEY,
                                                  control.random ? "true" : "false");
    g_slideshow_random_enable = control.random ? 1 : 0;
    ESP_LOGI(TAG, "set_slideshow save random=%d ret=%s",
             g_slideshow_random_enable, esp_err_to_name(random_save_ret));
    if (random_save_ret != ESP_OK) {
        return send_set_slideshow_result(req, TDX_JSON_RESULT_SLIDESHOW_CONTROL_SAVE_FAILED,
                                         "save slideshow random failed");
    }

    if (control.sw == 1) {
        esp_err_t start_ret = ServerNetworkStaSlideshow_StartSaved(base_path);
        if (start_ret != ESP_OK) {
            ESP_LOGW(TAG, "set_slideshow runtime start failed ret=%s", esp_err_to_name(start_ret));
            return send_set_slideshow_result(req, TDX_JSON_RESULT_SLIDESHOW_RUNTIME_FAILED,
                                             "start slideshow runtime failed");
        }
    } else {
        ServerNetworkStaSlideshow_Stop();
        ESP_LOGI(TAG, "set_slideshow disabled, current displayed image is unchanged");
    }
    return send_set_slideshow_result(req, TDX_JSON_RESULT_OK, NULL);
}
