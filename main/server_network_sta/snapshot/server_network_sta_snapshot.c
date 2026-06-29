#include "server_network_sta_snapshot.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_check.h"
#include "esp_log.h"
#include "file_serving_example_common.h"
#include "tdx_cfg.h"
#include "tdx_shared_spi.h"

static const char *TAG = "server_sta_snap";

typedef struct {
    int sw;
    uint32_t interval;
    bool random;
    char file_names[TDX_SLIDESHOW_MAX_FILES][TDX_SLIDESHOW_FILE_NAME_MAX_LEN];
    size_t file_count;
} snapshot_slideshow_t;

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

static bool has_jpg_extension(const char *name)
{
    size_t len = name != NULL ? strlen(name) : 0;
    if (len <= 4) {
        return false;
    }
    return strcmp(name + len - 4, ".jpg") == 0 || strcmp(name + len - 4, ".JPG") == 0;
}

static bool file_name_is_safe(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return false;
    }
    if (strstr(name, "..") != NULL || strchr(name, '/') != NULL || strchr(name, '\\') != NULL || strchr(name, '"') != NULL) {
        return false;
    }
    return true;
}

static const char *snapshot_image_entry_name(const char *entry_name)
{
    const char *prefix = "jpg_img/";
    if (entry_name == NULL) {
        return NULL;
    }
    if (example_storage_supports_directories()) {
        return entry_name;
    }
    return strncmp(entry_name, prefix, strlen(prefix)) == 0 ? entry_name + strlen(prefix) : NULL;
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

    value = strtol(pos, &end_ptr, 10);
    if (end_ptr == pos) {
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

    value = strtoul(pos, &end_ptr, 10);
    if (end_ptr == pos || value > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool parse_json_bool(const char *body, const char *key, bool *out)
{
    const char *pos = find_json_key(body, key);
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

static bool read_text_file(const char *path, char *buf, size_t buf_size)
{
    if (path == NULL || buf == NULL || buf_size == 0) {
        return false;
    }
    if (TdxSharedSpi_Lock(portMAX_DELAY) != ESP_OK) {
        return false;
    }
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        TdxSharedSpi_Unlock();
        return false;
    }

    size_t len = fread(buf, 1, buf_size - 1, fp);
    fclose(fp);
    TdxSharedSpi_Unlock();
    buf[len] = '\0';
    return true;
}

static void parse_slideshow_file_names(const char *json, snapshot_slideshow_t *slideshow)
{
    const char *pos = find_json_key(json, "fileNames");
    if (pos == NULL || slideshow == NULL) {
        return;
    }

    pos = strchr(pos, '[');
    if (pos == NULL) {
        return;
    }
    pos++;

    while (*pos != '\0' && *pos != ']' && slideshow->file_count < TDX_SLIDESHOW_MAX_FILES) {
        while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == ',') {
            pos++;
        }
        if (*pos == ']') {
            break;
        }
        if (*pos != '"') {
            return;
        }
        pos++;

        char file_name[TDX_SLIDESHOW_FILE_NAME_MAX_LEN] = {0};
        size_t len = 0;
        while (*pos != '\0' && *pos != '"' && len + 1 < sizeof(file_name)) {
            file_name[len++] = *pos++;
        }
        if (*pos != '"') {
            return;
        }
        pos++;
        file_name[len] = '\0';

        if (file_name_is_safe(file_name)) {
            strlcpy(slideshow->file_names[slideshow->file_count],
                    file_name,
                    sizeof(slideshow->file_names[slideshow->file_count]));
            slideshow->file_count++;
        }
    }
}

static void read_slideshow_state(const char *base_path, snapshot_slideshow_t *slideshow)
{
    char bin_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    char config_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    char control_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    char config_buf[SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX];
    char control_buf[256];

    memset(slideshow, 0, sizeof(*slideshow));
    snprintf(bin_dir, sizeof(bin_dir), "%s/bin_img", base_path);
    snprintf(config_path, sizeof(config_path), "%s/%s", bin_dir, TDX_SLIDESHOW_CONFIG_FILE);
    snprintf(control_path, sizeof(control_path), "%s/%s", bin_dir, TDX_SLIDESHOW_CONTROL_FILE);

    if (read_text_file(config_path, config_buf, sizeof(config_buf))) {
        parse_slideshow_file_names(config_buf, slideshow);
        parse_json_u32(config_buf, "interval", &slideshow->interval);
        parse_json_bool(config_buf, "random", &slideshow->random);
    }

    if (read_text_file(control_path, control_buf, sizeof(control_buf))) {
        int sw = 0;
        bool enable = false;
        if (parse_json_int(control_buf, "sw", &sw)) {
            slideshow->sw = (sw != 0) ? 1 : 0;
        } else if (parse_json_bool(control_buf, "enable", &enable)) {
            slideshow->sw = enable ? 1 : 0;
        }
        parse_json_u32(control_buf, "interval", &slideshow->interval);
        parse_json_bool(control_buf, "random", &slideshow->random);
    }

    if (slideshow->file_count == 0) {
        slideshow->sw = 0;
        slideshow->interval = 0;
        slideshow->random = false;
    }
}

static esp_err_t append_text(char *json, size_t json_size, size_t *used, const char *text)
{
    size_t len = strlen(text);
    if (*used + len + 1 >= json_size) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(json + *used, text, len);
    *used += len;
    json[*used] = '\0';
    return ESP_OK;
}

static esp_err_t append_format(char *json, size_t json_size, size_t *used, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int written = vsnprintf(json + *used, json_size - *used, fmt, ap);
    va_end(ap);
    if (written < 0 || *used + (size_t)written >= json_size) {
        return ESP_ERR_NO_MEM;
    }
    *used += (size_t)written;
    return ESP_OK;
}

static esp_err_t append_images_json(char *json, size_t json_size, size_t *used, const char *base_path)
{
    char jpg_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    snprintf(jpg_dir, sizeof(jpg_dir), "%s/jpg_img", base_path);
    const char *scan_dir = example_storage_supports_directories() ? jpg_dir : base_path;

    esp_err_t lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }
    DIR *dir = opendir(scan_dir);
    if (dir == NULL) {
        TdxSharedSpi_Unlock();
        ESP_LOGE(TAG, "snapshot image directory open failed path=%s", scan_dir);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t append_ret = append_text(json, json_size, used, "\"images\":[");
    if (append_ret != ESP_OK) {
        closedir(dir);
        TdxSharedSpi_Unlock();
        return append_ret;
    }

    int count = 0;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = snapshot_image_entry_name(entry->d_name);
        if (name == NULL || !has_jpg_extension(name) || !file_name_is_safe(name)) {
            continue;
        }

        char file_name[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX] = {0};
        size_t name_len = strlen(name);
        size_t stem_len = name_len - 4;
        if (stem_len == 0 || stem_len >= sizeof(file_name)) {
            continue;
        }
        memcpy(file_name, name, stem_len);
        file_name[stem_len] = '\0';

        esp_err_t ret = append_format(json,
                                      json_size,
                                      used,
                                      "%s{\"fileName\":\"%s\",\"thumbnailUrl\":\"%s%s\"}",
                                      count > 0 ? "," : "",
                                      file_name,
                                      SERVER_NETWORK_STA_THUMB_URI_PREFIX,
                                      name);
        if (ret != ESP_OK) {
            closedir(dir);
            TdxSharedSpi_Unlock();
            return ret;
        }
        count++;
    }
    closedir(dir);
    TdxSharedSpi_Unlock();

    return append_text(json, json_size, used, "]");
}

static esp_err_t append_slideshow_json(char *json, size_t json_size, size_t *used,
                                       const snapshot_slideshow_t *slideshow)
{
    ESP_RETURN_ON_ERROR(append_format(json,
                                      json_size,
                                      used,
                                      ",\"slideshow\":{\"sw\":%d,\"fileNames\":[",
                                      slideshow->sw),
                        TAG,
                        "append slideshow begin failed");

    if (slideshow->file_count > 0) {
        for (size_t i = 0; i < slideshow->file_count; i++) {
            ESP_RETURN_ON_ERROR(append_format(json,
                                              json_size,
                                              used,
                                              "%s\"%s\"",
                                              i > 0 ? "," : "",
                                              slideshow->file_names[i]),
                                TAG,
                                "append slideshow file failed");
        }
    }

    return append_format(json,
                         json_size,
                         used,
                         "],\"interval\":%lu,\"random\":%s}}",
                         (unsigned long)slideshow->interval,
                         slideshow->random ? "true" : "false");
}

esp_err_t ServerNetworkStaSnapshot_ProcessJson(httpd_req_t *req,
                                               const char *body,
                                               size_t body_len,
                                               const char *base_path)
{
    (void)body_len;
    if (body == NULL || !json_func_equals(body, "get_snapshot")) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    char *json = (char *)malloc(SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX);
    if (json == NULL) {
        char error_json[144];
        snprintf(error_json, sizeof(error_json),
                 "{\"func\":\"get_snapshot_result\",\"result\":%d,\"message\":\"snapshot allocation failed\"}",
                 TDX_JSON_RESULT_NO_MEMORY);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, error_json);
    }

    snapshot_slideshow_t slideshow;
    read_slideshow_state(base_path, &slideshow);

    size_t used = 0;
    esp_err_t ret = append_text(json, SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX, &used,
                                "{\"func\":\"get_snapshot_result\",\"result\":0,");
    bool image_read_failed = false;
    if (ret == ESP_OK) {
        ret = append_images_json(json, SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX, &used, base_path);
        image_read_failed = (ret == ESP_ERR_NOT_FOUND);
    }
    if (ret == ESP_OK) {
        ret = append_slideshow_json(json, SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX, &used, &slideshow);
    }

    if (ret != ESP_OK) {
        char error_json[144];
        snprintf(error_json, sizeof(error_json),
                 "{\"func\":\"get_snapshot_result\",\"result\":%d,\"message\":\"%s\"}",
                 image_read_failed ? TDX_JSON_RESULT_IMAGES_READ_FAILED
                                   : TDX_JSON_RESULT_SNAPSHOT_BUILD_FAILED,
                 image_read_failed ? "image list read failed" : "snapshot build failed");
        free(json);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, error_json);
    }

    ESP_LOGI(TAG, "get_snapshot images/slideshow response len=%u sw=%d files=%u",
             (unsigned int)used,
             slideshow.sw,
             (unsigned int)slideshow.file_count);
    httpd_resp_set_type(req, "application/json");
    ret = httpd_resp_sendstr(req, json);
    free(json);
    return ret;
}
