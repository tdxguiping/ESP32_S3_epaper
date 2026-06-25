#include "server_network_sta_delete.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "esp_log.h"
#include "file_serving_example_common.h"
#include "server_network_sta_slideshow.h"
#include "tdx_cfg.h"

static const char *TAG = "server_sta_delete";

typedef struct {
    char file_names[SERVER_NETWORK_STA_DELETE_MAX_FILES][TDX_SLIDESHOW_FILE_NAME_MAX_LEN];
    size_t file_count;
} delete_request_t;

static bool json_func_equals(const char *body, const char *func)
{
    const char *pos = strstr(body, "\"func\"");
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

typedef enum {
    DELETE_PARSE_OK = 0,
    DELETE_PARSE_MISSING,
    DELETE_PARSE_INVALID_NAME,
    DELETE_PARSE_INVALID_JSON,
} delete_parse_result_t;

static delete_parse_result_t parse_file_names(const char *body, delete_request_t *request)
{
    const char *pos = find_json_key(body, "fileNames");
    if (pos == NULL || request == NULL) {
        return DELETE_PARSE_MISSING;
    }

    memset(request, 0, sizeof(*request));
    pos += strlen("fileNames") + 2;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    if (*pos != ':') {
        return DELETE_PARSE_INVALID_JSON;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    if (*pos != '[') {
        return DELETE_PARSE_INVALID_JSON;
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
            return DELETE_PARSE_INVALID_JSON;
        }
        pos++;

        char file_name[TDX_SLIDESHOW_FILE_NAME_MAX_LEN] = {0};
        size_t len = 0;
        while (*pos != '\0' && *pos != '"' && len + 1 < sizeof(file_name)) {
            file_name[len++] = *pos++;
        }
        if (*pos != '"') {
            return DELETE_PARSE_INVALID_JSON;
        }
        pos++;
        file_name[len] = '\0';

        if (!file_name_is_safe(file_name) || request->file_count >= SERVER_NETWORK_STA_DELETE_MAX_FILES) {
            return DELETE_PARSE_INVALID_NAME;
        }
        strlcpy(request->file_names[request->file_count], file_name,
                sizeof(request->file_names[request->file_count]));
        request->file_count++;
    }

    if (!closed) {
        return DELETE_PARSE_INVALID_JSON;
    }
    return request->file_count > 0 ? DELETE_PARSE_OK : DELETE_PARSE_MISSING;
}

static esp_err_t send_delete_result(httpd_req_t *req, int result, const char *message)
{
    char json[160];
    if (result == TDX_JSON_RESULT_OK) {
        snprintf(json, sizeof(json),
                 "{\"func\":\"delete_result\",\"result\":%d}",
                 TDX_JSON_RESULT_OK);
    } else {
        snprintf(json, sizeof(json),
                 "{\"func\":\"delete_result\",\"result\":%d,\"message\":\"%s\"}",
                 result,
                 message != NULL ? message : "delete failed");
    }

    ESP_LOGI(TAG, "delete response: %s", json);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static bool delete_one_path(const char *path)
{
    if (unlink(path) == 0) {
        ESP_LOGI(TAG, "delete removed path=%s", path);
        return true;
    }
    if (errno == ENOENT) {
        ESP_LOGI(TAG, "delete path already missing=%s", path);
        return false;
    }
    ESP_LOGE(TAG, "delete failed path=%s errno=%d", path, errno);
    return false;
}

static bool name_in_delete_request(const delete_request_t *request, const char *file_name)
{
    for (size_t i = 0; i < request->file_count; i++) {
        if (strcmp(request->file_names[i], file_name) == 0) {
            return true;
        }
    }
    return false;
}

static bool extract_json_string_value(const char *body, const char *key, char *out, size_t out_size)
{
    const char *pos = find_json_key(body, key);
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

    size_t len = 0;
    while (*pos != '\0' && *pos != '"' && len + 1 < out_size) {
        out[len++] = *pos++;
    }
    if (*pos != '"') {
        return false;
    }
    out[len] = '\0';
    return true;
}

static bool parse_json_u32_default(const char *body, const char *key, uint32_t default_value, uint32_t *out)
{
    const char *pos = find_json_key(body, key);
    char *end_ptr = NULL;
    unsigned long value = 0;
    if (out == NULL) {
        return false;
    }
    if (pos == NULL) {
        *out = default_value;
        return true;
    }

    pos += strlen(key) + 2;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    if (*pos != ':') {
        *out = default_value;
        return false;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    if (*pos == '-') {
        *out = default_value;
        return false;
    }

    errno = 0;
    value = strtoul(pos, &end_ptr, 10);
    if (errno != 0 || end_ptr == pos ||
        value < TDX_SLIDESHOW_INTERVAL_MIN_SECONDS ||
        value > TDX_SLIDESHOW_INTERVAL_MAX_SECONDS) {
        *out = default_value;
        return false;
    }
    *out = (uint32_t)value;
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

static void cleanup_last_cast_if_deleted(const char *base_path, const delete_request_t *request)
{
    char record_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 32];
    snprintf(record_path, sizeof(record_path), "%s/bin_img/%s", base_path, SERVER_NETWORK_STA_LAST_CAST_FILE);

    FILE *fp = fopen(record_path, "rb");
    if (fp == NULL) {
        return;
    }

    char buf[256] = {0};
    size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[len] = '\0';

    char file_name[TDX_SLIDESHOW_FILE_NAME_MAX_LEN] = {0};
    if (extract_json_string_value(buf, "fileName", file_name, sizeof(file_name)) &&
        name_in_delete_request(request, file_name)) {
        ESP_LOGI(TAG, "delete cleanup last_cast file=%s", file_name);
        unlink(record_path);
    }
}

static bool append_json_file_name(char *json, size_t json_size, size_t *used,
                                  const char *file_name, bool first)
{
    int written = snprintf(json + *used, json_size - *used, "%s\"%s\"",
                           first ? "" : ",", file_name);
    if (written < 0 || *used + (size_t)written >= json_size) {
        return false;
    }
    *used += (size_t)written;
    return true;
}

static void cleanup_slideshow_if_deleted(const char *base_path, const delete_request_t *request)
{
    char config_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    char control_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    snprintf(config_path, sizeof(config_path), "%s/bin_img/%s", base_path, TDX_SLIDESHOW_CONFIG_FILE);
    snprintf(control_path, sizeof(control_path), "%s/bin_img/%s", base_path, TDX_SLIDESHOW_CONTROL_FILE);

    FILE *fp = fopen(config_path, "rb");
    if (fp == NULL) {
        return;
    }

    // Stop the old runtime before replacing its file list. If an EPD refresh is active,
    // the slideshow task records the next pending image only after that refresh completes.
    ServerNetworkStaSlideshow_Stop();

    char *buf = (char *)malloc(SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX);
    char *json = (char *)malloc(SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX);
    if (buf == NULL || json == NULL) {
        fclose(fp);
        free(buf);
        free(json);
        return;
    }

    size_t len = fread(buf, 1, SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX - 1, fp);
    fclose(fp);
    buf[len] = '\0';

    const char *array = find_json_key(buf, "fileNames");
    if (array == NULL) {
        free(buf);
        free(json);
        return;
    }
    array = strchr(array, '[');
    if (array == NULL) {
        free(buf);
        free(json);
        return;
    }
    array++;

    size_t used = 0;
    size_t kept = 0;
    bool config_updated = false;
    uint32_t interval = TDX_SLIDESHOW_INTERVAL_MIN_SECONDS;
    bool random = parse_json_bool_default(buf, "random", false);
    (void)parse_json_u32_default(buf, "interval", TDX_SLIDESHOW_INTERVAL_MIN_SECONDS, &interval);

    // Preserve slideshow timing fields while removing deleted names from the queue.
    // 删除轮播队列中的目标文件名时保留 interval/random，避免影响已有轮播设置。
    int written = snprintf(json, SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX, "{\"fileNames\":[");
    if (written < 0 || (size_t)written >= SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX) {
        free(buf);
        free(json);
        return;
    }
    used = (size_t)written;

    while (*array != '\0' && *array != ']') {
        while (*array == ' ' || *array == '\t' || *array == '\r' || *array == '\n' || *array == ',') {
            array++;
        }
        if (*array != '"') {
            break;
        }
        array++;
        char file_name[TDX_SLIDESHOW_FILE_NAME_MAX_LEN] = {0};
        size_t name_len = 0;
        while (*array != '\0' && *array != '"' && name_len + 1 < sizeof(file_name)) {
            file_name[name_len++] = *array++;
        }
        if (*array != '"') {
            break;
        }
        array++;
        file_name[name_len] = '\0';

        if (file_name_is_safe(file_name) && !name_in_delete_request(request, file_name)) {
            if (!append_json_file_name(json, SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX, &used,
                                       file_name, kept == 0)) {
                break;
            }
            kept++;
        }
    }

    written = snprintf(json + used, SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX - used,
                       "],\"interval\":%lu,\"random\":%s}",
                       (unsigned long)interval, random ? "true" : "false");
    if (written >= 0 && used + (size_t)written < SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX) {
        fp = fopen(config_path, "wb");
        if (fp != NULL) {
            size_t json_len = strlen(json);
            size_t write_len = fwrite(json, 1, json_len, fp);
            int close_ret = fclose(fp);
            config_updated = write_len == json_len && close_ret == 0;
            if (config_updated) {
                ESP_LOGI(TAG, "delete updated slideshow config kept=%u", (unsigned int)kept);
            } else {
                ESP_LOGE(TAG, "delete update slideshow config failed written=%u expected=%u",
                         (unsigned int)write_len, (unsigned int)json_len);
            }
        }
    }

    if (kept == 0) {
        fp = fopen(control_path, "wb");
        if (fp != NULL) {
            char control[160];
            snprintf(control, sizeof(control), "{\"sw\":0,\"interval\":%lu,\"random\":%s,\"run_mode\":%d}",
                     (unsigned long)interval, random ? "true" : "false", TDX_SLIDESHOW_RUN_MODE);
            fwrite(control, 1, strlen(control), fp);
            fclose(fp);
            ESP_LOGI(TAG, "delete disabled slideshow because list is empty");
        }
    }

    free(buf);
    free(json);

    if (kept > 0 && config_updated) {
        esp_err_t restart_ret = ServerNetworkStaSlideshow_StartSaved(base_path);
        ESP_LOGI(TAG, "delete restart slideshow kept=%u ret=%s",
                 (unsigned int)kept, esp_err_to_name(restart_ret));
    } else if (kept > 0) {
        ESP_LOGE(TAG, "delete keeps slideshow stopped because config update failed");
    }
}

esp_err_t ServerNetworkStaDelete_ProcessJson(httpd_req_t *req,
                                             const char *body,
                                             size_t body_len,
                                             const char *base_path)
{
    (void)body_len;
    if (!json_func_equals(body, "delete")) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    delete_request_t request;
    delete_parse_result_t parse_result = parse_file_names(body, &request);
    if (parse_result != DELETE_PARSE_OK) {
        int result = parse_result == DELETE_PARSE_INVALID_NAME ? TDX_JSON_RESULT_FILE_NAME_INVALID :
                     parse_result == DELETE_PARSE_INVALID_JSON ? TDX_JSON_RESULT_JSON_INVALID :
                     TDX_JSON_RESULT_FILE_NAMES_MISSING;
        return send_delete_result(req, result, "delete failed");
    }

    char bin_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    char jpg_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    struct stat st = {0};
    snprintf(bin_dir, sizeof(bin_dir), "%s/bin_img", base_path);
    snprintf(jpg_dir, sizeof(jpg_dir), "%s/jpg_img", base_path);

    if (example_storage_supports_directories() &&
        (stat(bin_dir, &st) != 0 || !S_ISDIR(st.st_mode)) &&
        (stat(jpg_dir, &st) != 0 || !S_ISDIR(st.st_mode))) {
        ESP_LOGE(TAG, "delete image dirs missing bin=%s jpg=%s", bin_dir, jpg_dir);
        return send_delete_result(req, TDX_JSON_RESULT_DELETE_FAILED, "delete failed");
    }

    int removed_count = 0;
    for (size_t i = 0; i < request.file_count; i++) {
        char bin_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + TDX_SLIDESHOW_FILE_NAME_MAX_LEN + 24];
        char jpg_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + TDX_SLIDESHOW_FILE_NAME_MAX_LEN + 24];
        snprintf(bin_path, sizeof(bin_path), "%s/%s.bin", bin_dir, request.file_names[i]);
        snprintf(jpg_path, sizeof(jpg_path), "%s/%s.jpg", jpg_dir, request.file_names[i]);

        if (delete_one_path(bin_path)) {
            removed_count++;
        }
        if (delete_one_path(jpg_path)) {
            removed_count++;
        }
    }

    if (removed_count <= 0) {
        return send_delete_result(req, TDX_JSON_RESULT_DELETE_FAILED, "delete failed");
    }

    cleanup_last_cast_if_deleted(base_path, &request);
    cleanup_slideshow_if_deleted(base_path, &request);
    ESP_LOGI(TAG, "delete success removed_count=%d request_count=%u",
             removed_count, (unsigned int)request.file_count);
    return send_delete_result(req, TDX_JSON_RESULT_OK, NULL);
}
