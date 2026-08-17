#include "usb_console_slideshow.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "epd_display_mode.h"
#include "app_persistent_state.h"
#include "esp_log.h"
#include "server_network_sta_slideshow.h"
#include "server_network_sta_slideshow_control.h"
#include "server_network_sta_time.h"
#include "tdx_cfg.h"
#include "tdx_shared_spi.h"
#include "usb_console_common.h"

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
    while (*end_ptr == ' ' || *end_ptr == '\t' || *end_ptr == '\r' || *end_ptr == '\n') {
        end_ptr++;
    }
    if (*end_ptr != ',' && *end_ptr != '}') {
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
    value = strtoll(pos, &end_ptr, 10);
    if (end_ptr == pos || value <= 0) {
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

static int validate_file_names(const char *body, size_t *file_count, bool check_files)
{
    const char *pos = body != NULL ? strstr(body, "\"fileNames\"") : NULL;
    if (pos == NULL || (pos = strchr(pos, '[')) == NULL) {
        return TDX_JSON_RESULT_FILE_NAMES_MISSING;
    }
    pos++;
    size_t count = 0;
    while (*pos != '\0') {
        while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == ',') {
            pos++;
        }
        if (*pos == ']') {
            if (count > 0 && file_count != NULL) {
                *file_count = count;
            }
            return count > 0 ? TDX_JSON_RESULT_OK : TDX_JSON_RESULT_FILE_NAMES_MISSING;
        }
        if (*pos++ != '"') {
            return TDX_JSON_RESULT_JSON_INVALID;
        }
        char name[TDX_SLIDESHOW_FILE_NAME_MAX_LEN] = {0};
        size_t len = 0;
        bool name_too_long = false;
        while (*pos != '\0' && *pos != '"') {
            if (len < TDX_IMAGE_BASE_NAME_MAX_BYTES) {
                name[len] = *pos;
            } else {
                name_too_long = true;
            }
            len++;
            pos++;
        }
        if (*pos != '"') {
            return TDX_JSON_RESULT_JSON_INVALID;
        }
        pos++;
        if (name_too_long) {
            ESP_LOGE("usb_slideshow",
                     "start_slideshow rejected: fileName too long index=%u len=%u max=%u",
                     (unsigned int)count,
                     (unsigned int)len,
                     (unsigned int)TDX_IMAGE_BASE_NAME_MAX_BYTES);
            return TDX_JSON_RESULT_FILE_NAME_INVALID;
        }
        name[len] = '\0';
        if (!UsbConsoleCommon_FileNameIsSafe(name)) {
            return TDX_JSON_RESULT_FILE_NAME_INVALID;
        }
        count++;
        if (count > TDX_SLIDESHOW_MAX_FILES) {
            return TDX_JSON_RESULT_FILE_NAMES_TOO_MANY;
        }
        if (check_files) {
            char path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + TDX_SLIDESHOW_FILE_NAME_MAX_LEN + 24];
            struct stat st = {0};
            snprintf(path, sizeof(path), "%s/bin_img/%s.bin", USB_CONSOLE_BASE_PATH, name);
            if (TdxSharedSpi_Lock(portMAX_DELAY) != ESP_OK) {
                return TDX_JSON_RESULT_TIMEOUT;
            }
            if (stat(path, &st) != 0 || st.st_size <= 0) {
                TdxSharedSpi_Unlock();
                return TDX_JSON_RESULT_SLIDESHOW_FILE_NOT_FOUND;
            }
            TdxSharedSpi_Unlock();
        }
    }
    return TDX_JSON_RESULT_JSON_INVALID;
}

static bool usb_slideshow_force_random_config(const char *scope, bool random)
{
    if (random) {
        ESP_LOGW("usb_slideshow", "%s random permanently disabled, force random=false", scope);
    }
    return false;
}

static bool save_slideshow_persistent_state(const char *body,
                                            uint32_t interval,
                                            bool random,
                                            uint32_t start_index,
                                            int64_t timestamp,
                                            bool *control_stage_reached)
{
    (void)random;
    app_persistent_slideshow_config_t *config =
        (app_persistent_slideshow_config_t *)calloc(1, sizeof(*config));
    if (config == NULL) {
        return false;
    }
    const char *file_names_key = find_json_key(body, "fileNames");
    const char *array_start = file_names_key != NULL ? strchr(file_names_key, '[') : NULL;
    const char *array_end = array_start != NULL ? strchr(array_start, ']') : NULL;
    if (array_start == NULL || array_end == NULL || array_end < array_start) {
        free(config);
        return false;
    }
    const char *cursor = array_start + 1;
    while (cursor < array_end && config->file_count < TDX_SLIDESHOW_MAX_FILES) {
        const char *quote = strchr(cursor, '"');
        if (quote == NULL || quote >= array_end) {
            break;
        }
        const char *quote_end = strchr(quote + 1, '"');
        if (quote_end == NULL || quote_end > array_end) {
            free(config);
            return false;
        }
        size_t name_len = (size_t)(quote_end - quote - 1);
        if (name_len == 0 || name_len > TDX_IMAGE_BASE_NAME_MAX_BYTES) {
            free(config);
            return false;
        }
        memcpy(config->file_names[config->file_count], quote + 1, name_len);
        config->file_names[config->file_count][name_len] = '\0';
        config->file_count++;
        cursor = quote_end + 1;
    }
    config->start_index = start_index;
    config->interval = interval;
    config->random = false;
    app_persistent_slideshow_control_t control = {
        .enabled = true,
        .interval = interval,
        .random = false,
        .timestamp = timestamp,
        .anchor_epoch = timestamp,
    };
    esp_err_t ret = AppPersistentState_SaveSlideshowState(config,
                                                          &control,
                                                          control_stage_reached);
    free(config);
    return ret == ESP_OK;
}

static int apply_start_slideshow_timestamp(int64_t timestamp)
{
    if (!timestamp_reasonable(timestamp)) {
        ESP_LOGW("usb_slideshow", "start_slideshow timestamp invalid timestamp=%lld", (long long)timestamp);
        if (ServerNetworkStaTime_IsSntpSynced()) {
            (void)ServerNetworkStaTime_BackupCurrentToCh583("usb_start_slideshow_bad_timestamp_sntp_now");
        }
        return TDX_JSON_RESULT_SLIDESHOW_TIMESTAMP_INVALID;
    }

    bool time_from_sntp = ServerNetworkStaTime_IsSntpSynced();
    time_t now_time = 0;
    time(&now_time);
    char timestamp_text[32] = {0};
    char now_text[32] = {0};
    format_epoch_local(timestamp, timestamp_text, sizeof(timestamp_text));
    format_epoch_local((int64_t)now_time, now_text, sizeof(now_text));
    if (time_from_sntp) {
        (void)ServerNetworkStaTime_BackupTimestampToCh583(timestamp,
                                                          "usb_start_slideshow_timestamp");
        int64_t diff = (int64_t)now_time - timestamp;
        if (diff < 0) {
            diff = -diff;
        }
        if (diff > 5) {
            ESP_LOGW("usb_slideshow",
                     "start_slideshow reject time diff too large timestamp=%lld(%s) now=%lld(%s) diff=%lld limit=5",
                     (long long)timestamp,
                     timestamp_text,
                     (long long)now_time,
                     now_text,
                     (long long)diff);
            return TDX_JSON_RESULT_SLIDESHOW_TIME_DIFF_TOO_LARGE;
        }
        if (timestamp > (int64_t)now_time) {
            ESP_LOGI("usb_slideshow",
                     "start_slideshow future timestamp accepted ahead=%lld; rtc slideshow applies display lead",
                     (long long)(timestamp - (int64_t)now_time));
        }
        ESP_LOGI("usb_slideshow",
                 "start_slideshow time source=sntp timestamp=%lld(%s) now=%lld(%s) diff=%lld",
                 (long long)timestamp,
                 timestamp_text,
                 (long long)now_time,
                 now_text,
                 (long long)diff);
        return TDX_JSON_RESULT_OK;
    }

    esp_err_t time_ret = ServerNetworkStaTime_SetTimestamp(timestamp);
    if (time_ret != ESP_OK) {
        ESP_LOGW("usb_slideshow",
                 "start_slideshow timestamp set rtc failed timestamp=%lld(%s) ret=%s",
                 (long long)timestamp,
                 timestamp_text,
                 esp_err_to_name(time_ret));
        return TDX_JSON_RESULT_SLIDESHOW_TIME_SET_FAILED;
    }
    ESP_LOGI("usb_slideshow",
             "start_slideshow time source=timestamp set rtc timestamp=%lld(%s)",
             (long long)timestamp,
             timestamp_text);
    return TDX_JSON_RESULT_OK;
}

esp_err_t UsbConsoleSlideshow_Handle(const usb_console_http_request_t *request,
                                     usb_console_http_response_t *response)
{
    return UsbConsoleCommon_SubmitAsyncRequest(request, response, "slideshow", UsbConsoleSlideshow_Process);
}

esp_err_t UsbConsoleSlideshow_Process(const usb_console_http_request_t *request,
                                     usb_console_http_response_t *response)
{
    if (request == NULL || response == NULL ||
        !UsbConsoleCommon_JsonFuncEquals(request->body, "start_slideshow")) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    size_t file_count = 0;
    int validate_ret = validate_file_names(request->body, &file_count, false);
    if (validate_ret != TDX_JSON_RESULT_OK) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"start_slideshow_result\",\"result\":%d}",
                                         validate_ret);
    }

    if (find_json_key(request->body, "startIndex") == NULL) {
        ESP_LOGE("usb_slideshow",
                 "start_slideshow rejected: startIndex missing count=%u",
                 (unsigned int)file_count);
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"start_slideshow_result\",\"result\":%d,"
                                         "\"message\":\"startIndex missing\"}",
                                         TDX_JSON_RESULT_SLIDESHOW_START_INDEX_MISSING);
    }
    uint32_t start_index = 0;
    if (!parse_json_u32(request->body, "startIndex", &start_index) ||
        start_index >= file_count) {
        ESP_LOGE("usb_slideshow",
                 "start_slideshow rejected: invalid startIndex=%lu count=%u",
                 (unsigned long)start_index,
                 (unsigned int)file_count);
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"start_slideshow_result\",\"result\":%d,"
                                         "\"message\":\"invalid startIndex\"}",
                                         TDX_JSON_RESULT_SLIDESHOW_START_INDEX_INVALID);
    }

    validate_ret = validate_file_names(request->body, NULL, true);
    if (validate_ret != TDX_JSON_RESULT_OK) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"start_slideshow_result\",\"result\":%d}",
                                         validate_ret);
    }

    uint32_t interval = 0;
    if (!parse_json_u32(request->body, "interval", &interval) ||
        interval < TDX_SLIDESHOW_INTERVAL_MIN_SECONDS ||
        interval > TDX_SLIDESHOW_INTERVAL_MAX_SECONDS) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"start_slideshow_result\",\"result\":%d,"
                                         "\"message\":\"invalid interval\"}",
                                         TDX_JSON_RESULT_SLIDESHOW_INTERVAL_INVALID);
    }

    bool random = usb_slideshow_force_random_config("usb start_slideshow",
                                                    parse_json_bool_default(request->body, "random", false));
    int64_t timestamp = 0;
    if (!parse_json_i64(request->body, "timestamp", &timestamp) ||
        !timestamp_reasonable(timestamp)) {
        if (ServerNetworkStaTime_IsSntpSynced()) {
            (void)ServerNetworkStaTime_BackupCurrentToCh583("usb_start_slideshow_bad_timestamp_sntp_now");
        }
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"start_slideshow_result\",\"result\":%d,"
                                         "\"message\":\"invalid timestamp\"}",
                                         TDX_JSON_RESULT_SLIDESHOW_TIMESTAMP_INVALID);
    }
    int time_result = apply_start_slideshow_timestamp(timestamp);
    if (time_result != TDX_JSON_RESULT_OK) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"start_slideshow_result\",\"result\":%d}",
                                         time_result);
    }

    bool control_stage_reached = false;
    if (!save_slideshow_persistent_state(request->body,
                                         interval,
                                         random,
                                         start_index,
                                         timestamp,
                                         &control_stage_reached)) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"start_slideshow_result\",\"result\":%d,"
                                         "\"message\":\"%s\"}",
                                         control_stage_reached ?
                                         TDX_JSON_RESULT_SLIDESHOW_CONTROL_SAVE_FAILED :
                                         TDX_JSON_RESULT_SLIDESHOW_CONFIG_SAVE_FAILED,
                                         control_stage_reached ?
                                         "save slideshow control failed" : "save config failed");
    }
    esp_err_t mode_ret = EpdDisplayMode_SetBySlideshowSwitch(true);
    if (mode_ret != ESP_OK) {
        (void)ServerNetworkStaSlideshowControl_Disable(USB_CONSOLE_BASE_PATH);
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"start_slideshow_result\",\"result\":%d,"
                                         "\"message\":\"save display mode failed\"}",
                                         TDX_JSON_RESULT_SLIDESHOW_CONFIG_SAVE_FAILED);
    }
    esp_err_t start_ret = ServerNetworkStaSlideshow_StartSavedForNewCommand(USB_CONSOLE_BASE_PATH);
    if (start_ret != ESP_OK) {
        ESP_LOGW("usb_slideshow", "start_slideshow rtc runtime start failed ret=%s",
                 esp_err_to_name(start_ret));
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"start_slideshow_result\",\"result\":%d,"
                                         "\"message\":\"start slideshow runtime failed\"}",
                                         TDX_JSON_RESULT_SLIDESHOW_RUNTIME_FAILED);
    }
    ESP_LOGI("usb_slideshow",
             "start_slideshow saved list and started rtc interval=%lu random=%d start_index=%lu timestamp=%lld anchor=%lld",
             (unsigned long)interval,
             random ? 1 : 0,
             (unsigned long)start_index,
             (long long)timestamp,
             (long long)timestamp);

    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"start_slideshow_result\",\"result\":%d}",
                                     TDX_JSON_RESULT_OK);
}
