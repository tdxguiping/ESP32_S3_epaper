#include "server_network_sta_slideshow.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "esp_log.h"
#include "tdx_cfg.h"

static const char *TAG = "server_sta_slide";

typedef struct {
    char file_names[TDX_SLIDESHOW_MAX_FILES][TDX_SLIDESHOW_FILE_NAME_MAX_LEN];
    size_t file_count;
    int interval;
    bool random;
} slideshow_request_t;

static bool json_func_equals(const char *body, const char *func)
{
    char pattern[96] = {0};
    snprintf(pattern, sizeof(pattern), "\"func\":\"%s\"", func);
    if (strstr(body, pattern) != NULL) {
        return true;
    }

    snprintf(pattern, sizeof(pattern), "\"func\" : \"%s\"", func);
    return strstr(body, pattern) != NULL;
}

static const char *find_json_key(const char *body, const char *key)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    return strstr(body, pattern);
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

static bool parse_json_bool_default(const char *body, const char *key, bool default_value)
{
    const char *pos = find_json_key(body, key);
    if (pos == NULL) {
        return default_value;
    }

    pos += strlen(key) + 2;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    if (*pos != ':') {
        return default_value;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }

    if (strncmp(pos, "true", 4) == 0 || *pos == '1') {
        return true;
    }
    if (strncmp(pos, "false", 5) == 0 || *pos == '0') {
        return false;
    }
    return default_value;
}

static bool file_name_is_safe(const char *file_name)
{
    if (file_name == NULL || file_name[0] == '\0') {
        return false;
    }
    if (strstr(file_name, "..") != NULL || strchr(file_name, '/') != NULL ||
        strchr(file_name, '\\') != NULL || strchr(file_name, '"') != NULL) {
        return false;
    }
    return strlen(file_name) < TDX_SLIDESHOW_FILE_NAME_MAX_LEN;
}

static bool parse_file_names(const char *body, slideshow_request_t *request)
{
    const char *pos = find_json_key(body, "fileNames");
    if (pos == NULL || request == NULL) {
        return false;
    }

    pos += strlen("fileNames") + 2;
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
    if (*pos != '[') {
        return false;
    }
    pos++;

    bool closed = false;
    while (*pos != '\0') {
        while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == ',') {
            pos++;
        }
        if (*pos == ']') {
            closed = true;
            break;
        }
        if (*pos != '"') {
            return false;
        }
        pos++;

        char file_name[TDX_SLIDESHOW_FILE_NAME_MAX_LEN] = {0};
        size_t len = 0;
        while (*pos != '\0' && *pos != '"' && len + 1 < sizeof(file_name)) {
            file_name[len++] = *pos++;
        }
        if (*pos != '"') {
            return false;
        }
        pos++;
        file_name[len] = '\0';

        if (!file_name_is_safe(file_name) || request->file_count >= TDX_SLIDESHOW_MAX_FILES) {
            return false;
        }
        strlcpy(request->file_names[request->file_count], file_name,
                sizeof(request->file_names[request->file_count]));
        request->file_count++;
    }

    return closed && request->file_count > 0;
}

static esp_err_t send_start_slideshow_result(httpd_req_t *req, bool ok, const char *message)
{
    char json[160];
    if (ok) {
        snprintf(json, sizeof(json), "{\"func\":\"start_slideshow_result\",\"result\":0}");
    } else {
        snprintf(json, sizeof(json),
                 "{\"func\":\"start_slideshow_result\",\"result\":1,\"message\":\"%s\"}",
                 message != NULL ? message : "start slideshow failed");
    }

    ESP_LOGI(TAG, "start_slideshow response: %s", json);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t ensure_bin_dir(const char *base_path, char *bin_dir, size_t bin_dir_size)
{
    struct stat st = {0};
    snprintf(bin_dir, bin_dir_size, "%s/bin_img", base_path);
    if (stat(bin_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        ESP_LOGE(TAG, "slideshow bin dir missing: %s", bin_dir);
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

static esp_err_t check_slideshow_files_exist(const char *bin_dir, const slideshow_request_t *request)
{
    for (size_t i = 0; i < request->file_count; i++) {
        char path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + TDX_SLIDESHOW_FILE_NAME_MAX_LEN + 24];
        struct stat st = {0};
        snprintf(path, sizeof(path), "%s/%s.bin", bin_dir, request->file_names[i]);
        if (stat(path, &st) != 0 || st.st_size <= 0) {
            ESP_LOGE(TAG, "slideshow file missing index=%u path=%s",
                     (unsigned int)i, path);
            return ESP_ERR_NOT_FOUND;
        }
        ESP_LOGI(TAG, "slideshow file ok index=%u path=%s size=%u",
                 (unsigned int)i, path, (unsigned int)st.st_size);
    }
    return ESP_OK;
}

static esp_err_t write_text_file(const char *path, const char *data)
{
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "slideshow open failed path=%s errno=%d", path, errno);
        return ESP_FAIL;
    }

    size_t len = strlen(data);
    size_t written = fwrite(data, 1, len, fp);
    fclose(fp);
    ESP_LOGI(TAG, "slideshow write path=%s len=%u written=%u",
             path, (unsigned int)len, (unsigned int)written);
    return written == len ? ESP_OK : ESP_FAIL;
}

static esp_err_t save_slideshow_config(const char *bin_dir, const slideshow_request_t *request)
{
    char path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    char *json = (char *)malloc(SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX);
    size_t used = 0;
    if (json == NULL) {
        ESP_LOGE(TAG, "slideshow config json alloc failed");
        return ESP_ERR_NO_MEM;
    }

    snprintf(path, sizeof(path), "%s/%s", bin_dir, TDX_SLIDESHOW_CONFIG_FILE);
    int written = snprintf(json, SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX, "{\"fileNames\":[");
    if (written < 0 || (size_t)written >= SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX) {
        free(json);
        return ESP_FAIL;
    }
    used = (size_t)written;

    for (size_t i = 0; i < request->file_count; i++) {
        written = snprintf(json + used, SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX - used, "%s\"%s\"",
                           i > 0 ? "," : "", request->file_names[i]);
        if (written < 0 || used + (size_t)written >= SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX) {
            free(json);
            return ESP_FAIL;
        }
        used += (size_t)written;
    }

    written = snprintf(json + used, SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX - used,
                       "],\"interval\":%d,\"random\":%s}",
                       request->interval, request->random ? "true" : "false");
    if (written < 0 || used + (size_t)written >= SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX) {
        free(json);
        return ESP_FAIL;
    }

    esp_err_t ret = write_text_file(path, json);
    free(json);
    return ret;
}

static esp_err_t save_slideshow_control(const char *bin_dir, const slideshow_request_t *request)
{
    char path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    char json[160];
    snprintf(path, sizeof(path), "%s/%s", bin_dir, TDX_SLIDESHOW_CONTROL_FILE);
    snprintf(json, sizeof(json), "{\"enable\":true,\"interval\":%d,\"random\":%s,\"run_mode\":%d}",
             request->interval,
             request->random ? "true" : "false",
             TDX_SLIDESHOW_RUN_MODE);
    return write_text_file(path, json);
}

static esp_err_t parse_start_slideshow_request(const char *body, slideshow_request_t *request)
{
    memset(request, 0, sizeof(*request));
    if (!json_func_equals(body, "start_slideshow")) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!parse_file_names(body, request)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!parse_json_int(body, "interval", &request->interval) ||
        request->interval < TDX_SLIDESHOW_INTERVAL_MIN_SECONDS ||
        request->interval > TDX_SLIDESHOW_INTERVAL_MAX_SECONDS) {
        return ESP_ERR_INVALID_SIZE;
    }
    request->random = parse_json_bool_default(body, "random", false);
    return ESP_OK;
}

esp_err_t ServerNetworkStaSlideshow_ProcessJson(httpd_req_t *req,
                                                const char *body,
                                                size_t body_len,
                                                const char *base_path)
{
    (void)body_len;
    slideshow_request_t request;
    esp_err_t ret = parse_start_slideshow_request(body, &request);
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (ret == ESP_ERR_INVALID_ARG) {
        return send_start_slideshow_result(req, false, "invalid fileNames");
    }
    if (ret == ESP_ERR_INVALID_SIZE) {
        return send_start_slideshow_result(req, false, "invalid interval");
    }
    if (ret != ESP_OK) {
        return send_start_slideshow_result(req, false, "start slideshow failed");
    }

    char bin_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    if (ensure_bin_dir(base_path, bin_dir, sizeof(bin_dir)) != ESP_OK) {
        return send_start_slideshow_result(req, false, "sd card not ready");
    }
    if (check_slideshow_files_exist(bin_dir, &request) != ESP_OK) {
        return send_start_slideshow_result(req, false, "file not found");
    }
    if (save_slideshow_config(bin_dir, &request) != ESP_OK ||
        save_slideshow_control(bin_dir, &request) != ESP_OK) {
        return send_start_slideshow_result(req, false, "save config failed");
    }
    esp_err_t random_save_ret = app_nvs_write_str(TDX_SLIDESHOW_RANDOM_NVS_KEY,
                                                  request.random ? "true" : "false");
    g_slideshow_random_enable = request.random ? 1 : 0;
    ESP_LOGI(TAG, "start_slideshow save random=%d ret=%s",
             g_slideshow_random_enable, esp_err_to_name(random_save_ret));

    ESP_LOGI(TAG, "start_slideshow ready count=%u interval=%d random=%d run_mode=%d",
             (unsigned int)request.file_count,
             request.interval,
             request.random ? 1 : 0,
             TDX_SLIDESHOW_RUN_MODE);
    if (TDX_SLIDESHOW_RUN_MODE == TDX_SLIDESHOW_RUN_MODE_DEEP_SLEEP) {
        ESP_LOGI(TAG, "start_slideshow deep sleep mode configured, display recovery task is not present in current project");
    } else {
        ESP_LOGI(TAG, "start_slideshow software mode configured, display task is not present in current project");
    }

    return send_start_slideshow_result(req, true, NULL);
}
