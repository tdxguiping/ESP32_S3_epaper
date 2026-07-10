#include "server_network_sta_slideshow_control.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>

#include "epd_display_mode.h"
#include "esp_log.h"
#include "file_serving_example_common.h"
#include "server_network_sta_slideshow.h"
#include "server_network_sta_time.h"
#include "tdx_cfg.h"
#include "tdx_shared_spi.h"

static const char *TAG = "server_sta_slide_ctl";

typedef struct {
    int sw;
    uint32_t interval;
    bool random;
    bool random_present;
    int64_t timestamp;
    int64_t anchor_epoch;
} slideshow_control_t;

#define SLIDESHOW_CONTROL_ERR_TIMESTAMP ((esp_err_t)0x7001)
#define SLIDESHOW_CONTROL_ERR_TIME_DEPRECATED ((esp_err_t)0x7002)

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

static bool parse_json_i64(const char *body, const char *key, int64_t *out)
{
    const char *pos = find_json_key(body, key);
    char *end_ptr = NULL;
    long long value = 0;
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
    value = strtoll(pos, &end_ptr, 10);
    if (errno != 0 || end_ptr == pos || value <= 0) {
        return false;
    }
    while (*end_ptr == ' ' || *end_ptr == '\t' || *end_ptr == '\r' || *end_ptr == '\n') {
        end_ptr++;
    }
    if (*end_ptr != ',' && *end_ptr != '}') {
        return false;
    }
    *out = (int64_t)value;
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

static int64_t slideshow_next_epoch(int64_t anchor_epoch, uint32_t interval, int64_t now_epoch)
{
    if (now_epoch <= anchor_epoch) {
        return anchor_epoch;
    }

    int64_t overdue = now_epoch - anchor_epoch;
    if (overdue <= 5) {
        return anchor_epoch;
    }

    int64_t steps = overdue / (int64_t)interval + 1;
    return anchor_epoch + steps * (int64_t)interval;
}

static bool timestamp_reasonable(int64_t timestamp)
{
    if (timestamp <= 0) {
        return false;
    }
    time_t t = (time_t)timestamp;
    struct tm tm_value = {0};
    localtime_r(&t, &tm_value);
    return tm_value.tm_year >= (2026 - 1900);
}

static void format_epoch_local(int64_t epoch, char *buf, size_t buf_size)
{
    if (buf == NULL || buf_size == 0) {
        return;
    }
    time_t t = (time_t)epoch;
    struct tm tm_value = {0};
    localtime_r(&t, &tm_value);
    strftime(buf, buf_size, "%Y-%m-%d %H:%M:%S", &tm_value);
}

static esp_err_t send_set_slideshow_result(httpd_req_t *req,
                                           const server_network_sta_slideshow_control_result_t *result)
{
    char json[384];
    if (result != NULL && result->result == TDX_JSON_RESULT_OK) {
        snprintf(json, sizeof(json),
                 "{\"func\":\"set_slideshow_result\",\"result\":%d,"
                 "\"sw\":%d,\"interval\":%lu,\"random\":%s,"
                 "\"timestamp\":%lld,"
                 "\"time_source\":\"%s\","
                 "\"time_diff\":%lld,"
                 "\"anchor_epoch\":%lld,\"now_epoch\":%lld,"
                 "\"next_epoch\":%lld,\"remain\":%lu}",
                 TDX_JSON_RESULT_OK,
                 result->sw,
                 (unsigned long)result->interval,
                 result->random ? "true" : "false",
                 (long long)result->timestamp,
                 result->time_source,
                 (long long)result->time_diff,
                 (long long)result->anchor_epoch,
                 (long long)result->now_epoch,
                 (long long)result->next_epoch,
                 (unsigned long)result->remain);
    } else {
        int code = result != NULL ? result->result : TDX_JSON_RESULT_INTERNAL_ERROR;
        if (result != NULL && code == TDX_JSON_RESULT_SLIDESHOW_TIME_DIFF_TOO_LARGE) {
            snprintf(json, sizeof(json),
                     "{\"func\":\"set_slideshow_result\",\"result\":%d,"
                     "\"message\":\"%s\",\"timestamp\":%lld,"
                     "\"now_epoch\":%lld,\"time_diff\":%lld,"
                     "\"time_source\":\"%s\"}",
                     code,
                     result->message[0] != '\0' ? result->message : "timestamp differs from SNTP time",
                     (long long)result->timestamp,
                     (long long)result->now_epoch,
                     (long long)result->time_diff,
                     result->time_source);
        } else {
            snprintf(json, sizeof(json),
                     "{\"func\":\"set_slideshow_result\",\"result\":%d,\"message\":\"%s\"}",
                     code,
                     result != NULL && result->message[0] != '\0' ? result->message : "set slideshow failed");
        }
    }

    ESP_LOGI(TAG, "set_slideshow response: %s", json);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static bool read_existing_bool(const char *path, const char *key, bool default_value)
{
    if (TdxSharedSpi_Lock(portMAX_DELAY) != ESP_OK) {
        return default_value;
    }
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        TdxSharedSpi_Unlock();
        return default_value;
    }

    char buf[192] = {0};
    size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    TdxSharedSpi_Unlock();
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
    if (TdxSharedSpi_Lock(portMAX_DELAY) != ESP_OK) {
        return default_value;
    }
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        TdxSharedSpi_Unlock();
        return default_value;
    }

    char buf[192] = {0};
    size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    TdxSharedSpi_Unlock();
    buf[len] = '\0';

    uint32_t interval = default_value;
    if (parse_json_u32(buf, "interval", &interval) &&
        interval >= TDX_SLIDESHOW_INTERVAL_MIN_SECONDS &&
        interval <= TDX_SLIDESHOW_INTERVAL_MAX_SECONDS) {
        return interval;
    }
    return default_value;
}

static void build_paths(const char *base_path, char *bin_dir, size_t bin_dir_size,
                        char *control_path, size_t control_path_size,
                        char *config_path, size_t config_path_size)
{
    snprintf(bin_dir, bin_dir_size, "%s/bin_img", base_path);
    snprintf(control_path, control_path_size, "%s/%s", bin_dir, TDX_SLIDESHOW_CONTROL_FILE);
    snprintf(config_path, config_path_size, "%s/%s", bin_dir, TDX_SLIDESHOW_CONFIG_FILE);
}

static esp_err_t ensure_paths(const char *base_path, char *bin_dir, size_t bin_dir_size,
                              char *control_path, size_t control_path_size,
                              char *config_path, size_t config_path_size)
{
    struct stat st = {0};
    build_paths(base_path, bin_dir, bin_dir_size, control_path, control_path_size, config_path, config_path_size);

    if (!example_storage_supports_directories()) {
        return ESP_OK;
    }

    esp_err_t lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }
    if (stat(bin_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        TdxSharedSpi_Unlock();
        ESP_LOGE(TAG, "set_slideshow bin dir missing: %s", bin_dir);
        return ESP_ERR_NOT_FOUND;
    }
    TdxSharedSpi_Unlock();
    return ESP_OK;
}

static bool slideshow_config_has_files(const char *config_path)
{
    if (TdxSharedSpi_Lock(portMAX_DELAY) != ESP_OK) {
        return false;
    }
    FILE *fp = fopen(config_path, "rb");
    if (fp == NULL) {
        TdxSharedSpi_Unlock();
        ESP_LOGE(TAG, "set_slideshow config missing: %s", config_path);
        return false;
    }

    char buf[256] = {0};
    size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    TdxSharedSpi_Unlock();
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
    char json[256];
    snprintf(json, sizeof(json),
             "{\"sw\":%d,\"interval\":%lu,\"random\":%s,"
             "\"timestamp\":%lld,\"anchor_epoch\":%lld}",
             control->sw,
             (unsigned long)control->interval,
             control->random ? "true" : "false",
             (long long)control->timestamp,
             (long long)control->anchor_epoch);

    FILE *fp = NULL;
    esp_err_t lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }
    fp = fopen(control_path, "wb");
    if (fp == NULL) {
        TdxSharedSpi_Unlock();
        ESP_LOGE(TAG, "set_slideshow open failed path=%s errno=%d", control_path, errno);
        return ESP_FAIL;
    }

    size_t len = strlen(json);
    size_t written = fwrite(json, 1, len, fp);
    fclose(fp);
    TdxSharedSpi_Unlock();
    ESP_LOGI(TAG, "set_slideshow write control path=%s len=%u written=%u json=%s",
             control_path, (unsigned int)len, (unsigned int)written, json);
    return written == len ? ESP_OK : ESP_FAIL;
}

static bool slideshow_control_force_random_config(const char *scope, bool random)
{
#if TDX_SLIDESHOW_RANDOM_ENABLE
    (void)scope;
    return random;
#else
    if (random) {
        ESP_LOGW(TAG, "%s random disabled temporarily, force random=false", scope);
    }
    return false;
#endif
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

    control->random = false;
    if (!parse_json_bool_optional(body, "random", &control->random, &control->random_present)) {
        return ESP_ERR_INVALID_ARG;
    }
    control->random = slideshow_control_force_random_config("set_slideshow", control->random);
    if (find_json_key(body, "datetime") != NULL || find_json_key(body, "timezone") != NULL) {
        return SLIDESHOW_CONTROL_ERR_TIME_DEPRECATED;
    }
    if (control->sw == 1) {
        if (!parse_json_i64(body, "timestamp", &control->timestamp) ||
            !timestamp_reasonable(control->timestamp)) {
            return SLIDESHOW_CONTROL_ERR_TIMESTAMP;
        }
        control->anchor_epoch = control->timestamp;
    } else {
        (void)parse_json_i64(body, "timestamp", &control->timestamp);
        if (timestamp_reasonable(control->timestamp)) {
            control->anchor_epoch = control->timestamp;
        }
    }
    ESP_LOGI(TAG,
             "set_slideshow parsed sw=%d interval=%lu random=%d random_present=%d timestamp=%lld anchor=%lld",
             control->sw,
             (unsigned long)control->interval,
             control->random ? 1 : 0,
             control->random_present ? 1 : 0,
             (long long)control->timestamp,
             (long long)control->anchor_epoch);
    return ESP_OK;
}

static void set_apply_result(server_network_sta_slideshow_control_result_t *result,
                             int code,
                             const char *message)
{
    if (result == NULL) {
        return;
    }
    result->result = code;
    strlcpy(result->message, message != NULL ? message : "", sizeof(result->message));
}

esp_err_t ServerNetworkStaSlideshowControl_ApplyJson(const char *body,
                                                     const char *base_path,
                                                     server_network_sta_slideshow_control_result_t *result)
{
    char bin_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    char control_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    char config_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];

    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));

    if (!json_func_equals(body, "set_slideshow")) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    build_paths(base_path, bin_dir, sizeof(bin_dir),
                control_path, sizeof(control_path),
                config_path, sizeof(config_path));

    slideshow_control_t control;
    esp_err_t ret = parse_set_slideshow_request(body, control_path, &control);
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (ret == ESP_ERR_INVALID_SIZE) {
        set_apply_result(result, TDX_JSON_RESULT_SLIDESHOW_INTERVAL_INVALID, "invalid interval");
        return ESP_OK;
    }
    if (ret == SLIDESHOW_CONTROL_ERR_TIMESTAMP) {
        if (ServerNetworkStaTime_IsSntpSynced()) {
            (void)ServerNetworkStaTime_BackupCurrentToCh583("set_slideshow_bad_timestamp_sntp_now");
        }
        set_apply_result(result, TDX_JSON_RESULT_SLIDESHOW_TIMESTAMP_INVALID, "invalid timestamp");
        return ESP_OK;
    }
    if (ret == SLIDESHOW_CONTROL_ERR_TIME_DEPRECATED) {
        set_apply_result(result, TDX_JSON_RESULT_SLIDESHOW_TIMEZONE_DEPRECATED, "datetime/timezone deprecated");
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        set_apply_result(result, TDX_JSON_RESULT_PARAM_INVALID, "invalid slideshow parameters");
        return ESP_OK;
    }

    bool time_from_sntp = ServerNetworkStaTime_IsSntpSynced();
    int64_t checked_time_diff = 0;
    if (timestamp_reasonable(control.timestamp) && (time_from_sntp || control.sw == 0)) {
        (void)ServerNetworkStaTime_BackupTimestampToCh583(control.timestamp,
                                                          "set_slideshow_timestamp");
    } else if (control.sw == 0 && time_from_sntp) {
        (void)ServerNetworkStaTime_BackupCurrentToCh583("set_slideshow_sw0_sntp_now");
    }
    if (control.sw == 1) {
        time_t now_for_diff = 0;
        time(&now_for_diff);
        if (time_from_sntp) {
            strlcpy(result->time_source, "sntp", sizeof(result->time_source));
            int64_t diff = (int64_t)now_for_diff - control.timestamp;
            if (diff < 0) {
                diff = -diff;
            }
            result->timestamp = control.timestamp;
            result->now_epoch = (int64_t)now_for_diff;
            result->time_diff = diff;
            checked_time_diff = diff;
            if (diff > 5) {
                char timestamp_text[32] = {0};
                char now_text[32] = {0};
                format_epoch_local(control.timestamp, timestamp_text, sizeof(timestamp_text));
                format_epoch_local((int64_t)now_for_diff, now_text, sizeof(now_text));
                ESP_LOGW(TAG,
                         "set_slideshow reject time diff too large timestamp=%lld(%s) now=%lld(%s) diff=%lld limit=5",
                         (long long)control.timestamp,
                         timestamp_text,
                         (long long)now_for_diff,
                         now_text,
                         (long long)diff);
                set_apply_result(result,
                                 TDX_JSON_RESULT_SLIDESHOW_TIME_DIFF_TOO_LARGE,
                                 "timestamp differs from SNTP time");
                strlcpy(result->time_source, "sntp", sizeof(result->time_source));
                return ESP_OK;
            }
            char timestamp_text[32] = {0};
            char now_text[32] = {0};
            format_epoch_local(control.timestamp, timestamp_text, sizeof(timestamp_text));
            format_epoch_local((int64_t)now_for_diff, now_text, sizeof(now_text));
            ESP_LOGI(TAG,
                     "set_slideshow time source=sntp timestamp=%lld(%s) now=%lld(%s) diff=%lld",
                     (long long)control.timestamp,
                     timestamp_text,
                     (long long)now_for_diff,
                     now_text,
                     (long long)diff);
        } else {
            esp_err_t time_ret = ServerNetworkStaTime_SetTimestamp(control.timestamp);
            if (time_ret != ESP_OK) {
                char timestamp_text[32] = {0};
                format_epoch_local(control.timestamp, timestamp_text, sizeof(timestamp_text));
                ESP_LOGW(TAG,
                         "set_slideshow timestamp set rtc failed timestamp=%lld(%s) ret=%s",
                         (long long)control.timestamp,
                         timestamp_text,
                         esp_err_to_name(time_ret));
                set_apply_result(result, TDX_JSON_RESULT_SLIDESHOW_TIME_SET_FAILED, "timestamp set rtc failed");
                return ESP_OK;
            }
            strlcpy(result->time_source, "timestamp", sizeof(result->time_source));
            result->time_diff = 0;
            checked_time_diff = 0;
            char timestamp_text[32] = {0};
            format_epoch_local(control.timestamp, timestamp_text, sizeof(timestamp_text));
            ESP_LOGI(TAG,
                     "set_slideshow time source=timestamp set rtc timestamp=%lld(%s)",
                     (long long)control.timestamp,
                     timestamp_text);
        }
    }

    if (ensure_paths(base_path, bin_dir, sizeof(bin_dir),
                     control_path, sizeof(control_path),
                     config_path, sizeof(config_path)) != ESP_OK) {
        set_apply_result(result, TDX_JSON_RESULT_STORAGE_NOT_READY, "storage not ready");
        return ESP_OK;
    }

    if (!control.random_present) {
        control.random = read_existing_bool(control_path, "random", false);
        control.random = slideshow_control_force_random_config("saved show_control", control.random);
    }

    if (control.sw == 1 && !slideshow_config_has_files(config_path)) {
        set_apply_result(result, TDX_JSON_RESULT_FILE_NAMES_MISSING, "slideshow fileNames missing");
        return ESP_OK;
    }

    if (write_control_file(control_path, &control) != ESP_OK) {
        set_apply_result(result, TDX_JSON_RESULT_SLIDESHOW_CONTROL_SAVE_FAILED, "save slideshow control failed");
        return ESP_OK;
    }
    esp_err_t mode_ret = EpdDisplayMode_SetBySlideshowSwitch(control.sw == 1);
    if (mode_ret != ESP_OK) {
        set_apply_result(result, TDX_JSON_RESULT_SLIDESHOW_CONTROL_SAVE_FAILED, "save display mode failed");
        return ESP_OK;
    }
    esp_err_t random_save_ret = app_nvs_write_str(TDX_SLIDESHOW_RANDOM_NVS_KEY,
                                                  control.random ? "true" : "false");
    g_slideshow_random_enable = control.random ? 1 : 0;
    ESP_LOGI(TAG, "set_slideshow save random=%d ret=%s",
             g_slideshow_random_enable, esp_err_to_name(random_save_ret));
    if (random_save_ret != ESP_OK) {
        set_apply_result(result, TDX_JSON_RESULT_SLIDESHOW_CONTROL_SAVE_FAILED, "save slideshow random failed");
        return ESP_OK;
    }

    if (control.sw == 1) {
        esp_err_t start_ret = ServerNetworkStaSlideshow_StartSavedResetInterval(base_path);
        if (start_ret != ESP_OK) {
            ESP_LOGW(TAG, "set_slideshow runtime start failed ret=%s", esp_err_to_name(start_ret));
            set_apply_result(result, TDX_JSON_RESULT_SLIDESHOW_RUNTIME_FAILED, "start slideshow runtime failed");
            return ESP_OK;
        }
    } else {
        ServerNetworkStaSlideshow_Stop();
        ESP_LOGI(TAG, "set_slideshow disabled, current displayed image is unchanged");
    }

    result->result = TDX_JSON_RESULT_OK;
    result->sw = control.sw;
    result->interval = control.interval;
    result->random = control.random;
    if (result->time_source[0] == '\0') {
        strlcpy(result->time_source, control.sw == 1 ? "rtc" : "none", sizeof(result->time_source));
    }
    result->timestamp = control.timestamp;
    result->anchor_epoch = control.anchor_epoch;
    time_t now = 0;
    time(&now);
    result->now_epoch = (int64_t)now;
    result->time_diff = control.sw == 1 ? checked_time_diff : 0;
    result->next_epoch = control.sw == 1 ?
                         slideshow_next_epoch(control.anchor_epoch, control.interval, result->now_epoch) :
                         0;
    result->remain = result->next_epoch > result->now_epoch ?
                     (uint32_t)(result->next_epoch - result->now_epoch) :
                     0;
    if (result->sw == 1) {
        char anchor_text[32] = {0};
        char now_text[32] = {0};
        char next_text[32] = {0};
        format_epoch_local(result->anchor_epoch, anchor_text, sizeof(anchor_text));
        format_epoch_local(result->now_epoch, now_text, sizeof(now_text));
        format_epoch_local(result->next_epoch, next_text, sizeof(next_text));
        ESP_LOGI(TAG,
                 "set_slideshow apply ok sw=%d source=%s anchor=%lld(%s) now=%lld(%s) next=%lld(%s) remain=%lu",
                 result->sw,
                 result->time_source,
                 (long long)result->anchor_epoch,
                 anchor_text,
                 (long long)result->now_epoch,
                 now_text,
                 (long long)result->next_epoch,
                 next_text,
                 (unsigned long)result->remain);
    } else {
        ESP_LOGI(TAG,
                 "set_slideshow apply ok sw=%d source=%s interval=%lu",
                 result->sw,
                 result->time_source,
                 (unsigned long)result->interval);
    }
    return ESP_OK;
}

esp_err_t ServerNetworkStaSlideshowControl_ProcessJson(httpd_req_t *req,
                                                       const char *body,
                                                       size_t body_len,
                                                       const char *base_path)
{
    (void)body_len;
    server_network_sta_slideshow_control_result_t result;
    esp_err_t ret = ServerNetworkStaSlideshowControl_ApplyJson(body, base_path, &result);
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (ret != ESP_OK) {
        memset(&result, 0, sizeof(result));
        set_apply_result(&result, TDX_JSON_RESULT_INTERNAL_ERROR, "set slideshow failed");
    }
    return send_set_slideshow_result(req, &result);
}
