#include "server_network_sta_slideshow.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <time.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "epd_type.h"
#include "file_serving_example_common.h"
#include "epd_display_app.h"
#include "epd_display_mode.h"
#include "server_network_sta.h"
#include "server_network_sta_time.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "tdx_cfg.h"
#include "tdx_shared_spi.h"

static const char *TAG = "server_sta_slide";
#define SLIDESHOW_TASK_STACK_SIZE (12 * 1024)
#define SLIDESHOW_TASK_PRIORITY 4
#define SLIDESHOW_PROGRESS_MAGIC 0x534C4450UL
#define SLIDESHOW_PROGRESS_VERSION 1U
#define SLIDESHOW_PROGRESS_SAVE_RETRIES 3

typedef struct {
    char file_names[TDX_SLIDESHOW_MAX_FILES][TDX_SLIDESHOW_FILE_NAME_MAX_LEN];
    size_t file_count;
    size_t start_index;
    uint32_t interval;
    bool random;
    int64_t timestamp;
    int64_t anchor_epoch;
} slideshow_request_t;

typedef struct {
    uint32_t magic;
    uint32_t config_hash;
    uint32_t random_seed;
    uint8_t version;
    uint8_t random;
    uint8_t order_count;
    uint8_t position;
    uint8_t order[TDX_SLIDESHOW_MAX_FILES];
    char pending_file[TDX_SLIDESHOW_FILE_NAME_MAX_LEN];
} slideshow_progress_t;

typedef struct {
    bool started;
    uint64_t slot;
    size_t current_index;
    int64_t slot_start_epoch;
    int64_t next_epoch;
} slideshow_schedule_position_t;

typedef enum {
    SLIDESHOW_RTC_WAIT_TARGET_REACHED = 0,
    SLIDESHOW_RTC_WAIT_STOPPED,
    SLIDESHOW_RTC_WAIT_SNTP_READY,
} slideshow_rtc_wait_result_t;

typedef struct {
    char base_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX];
    slideshow_request_t request;
    slideshow_progress_t progress;
    uint32_t initial_delay_seconds;
    bool rtc_enabled;
    bool force_first_display;
    bool sntp_schedule_enabled;
    bool consumed_slot_valid;
    uint64_t consumed_slot;
    uint64_t scheduled_slot;
    int64_t next_epoch;
    bool preload_failure_valid;
    esp_err_t preload_failure_result;
    char preload_failure_file[TDX_SLIDESHOW_FILE_NAME_MAX_LEN];
} slideshow_runtime_t;

typedef struct {
    char file_name[TDX_SLIDESHOW_FILE_NAME_MAX_LEN];
    uint8_t *buf;
    size_t len;
} slideshow_loaded_file_t;

typedef struct {
    char base_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX];
} slideshow_startup_delay_t;

static TaskHandle_t s_slideshow_task = NULL;
static TaskHandle_t s_slideshow_startup_delay_task = NULL;
static volatile bool s_slideshow_stop = false;
static portMUX_TYPE s_slideshow_timing_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_slideshow_interval_active = false;
static uint32_t s_slideshow_runtime_interval = 0;
static TickType_t s_slideshow_interval_start_tick = 0;
static int64_t s_slideshow_runtime_next_epoch = 0;
static bool s_slideshow_last_display_start_valid = false;
static uint32_t s_slideshow_last_display_interval = 0;
static TickType_t s_slideshow_last_display_start_tick = 0;

static void slideshow_begin_interval(uint32_t interval, TickType_t start_tick);
static uint32_t slideshow_rtc_display_lead_seconds(void);

static bool slideshow_wifi_has_ip(void)
{
    server_network_sta_status_t status = {0};
    if (ServerNetworkSta_GetStatus(&status) != ESP_OK) {
        return false;
    }
    /* Time synchronization only needs an IP; it does not depend on HTTP readiness. */
    return status.has_ip && status.ip[0] != '\0';
}

static bool slideshow_should_force_first_display_for_stale_time(bool *wifi_has_ip,
                                                                bool *sntp_synced)
{
    server_network_sta_time_info_t time_info = {0};
    bool has_ip = slideshow_wifi_has_ip();
    bool synced = ServerNetworkStaTime_IsSntpSynced();

    if (wifi_has_ip != NULL) {
        *wifi_has_ip = has_ip;
    }
    if (sntp_synced != NULL) {
        *sntp_synced = synced;
    }

    if (ServerNetworkStaTime_GetInfo(&time_info) != ESP_OK ||
        time_info.source != SERVER_NETWORK_STA_TIME_SOURCE_CH583_STALE) {
        return false;
    }

    return !has_ip || !synced;
}

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
    while (*end_ptr == ' ' || *end_ptr == '\t' || *end_ptr == '\r' || *end_ptr == '\n') {
        end_ptr++;
    }
    if (*end_ptr != ',' && *end_ptr != '}') {
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

static bool parse_json_string(const char *body, const char *key, char *out, size_t out_size)
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
    if (errno != 0 || end_ptr == pos) {
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

static int64_t slideshow_next_event_epoch(int64_t anchor_epoch, uint32_t interval, int64_t now_epoch)
{
    if (now_epoch < anchor_epoch) {
        return anchor_epoch;
    }

    int64_t steps = (now_epoch - anchor_epoch) / (int64_t)interval + 1;
    return anchor_epoch + steps * (int64_t)interval;
}

static bool slideshow_calculate_schedule_position(
    int64_t anchor_epoch,
    uint32_t interval,
    size_t file_count,
    size_t start_index,
    int64_t now_epoch,
    slideshow_schedule_position_t *position)
{
    if (position == NULL || anchor_epoch <= 0 ||
        interval < TDX_SLIDESHOW_INTERVAL_MIN_SECONDS ||
        interval > TDX_SLIDESHOW_INTERVAL_MAX_SECONDS ||
        file_count == 0 || start_index >= file_count || now_epoch <= 0) {
        return false;
    }

    memset(position, 0, sizeof(*position));
    if (now_epoch < anchor_epoch) {
        position->started = false;
        position->current_index = start_index;
        position->slot_start_epoch = anchor_epoch;
        position->next_epoch = anchor_epoch;
        return true;
    }

    uint64_t elapsed = (uint64_t)(now_epoch - anchor_epoch);
    uint64_t slot = elapsed / (uint64_t)interval;
    if (slot > (uint64_t)INT64_MAX / (uint64_t)interval) {
        return false;
    }
    int64_t slot_offset = (int64_t)(slot * (uint64_t)interval);
    if (anchor_epoch > INT64_MAX - slot_offset) {
        return false;
    }
    int64_t current_epoch = anchor_epoch + slot_offset;
    if (current_epoch > INT64_MAX - (int64_t)interval) {
        return false;
    }

    position->started = true;
    position->slot = slot;
    position->current_index =
        (start_index + (size_t)(slot % (uint64_t)file_count)) % file_count;
    position->slot_start_epoch = current_epoch;
    position->next_epoch = current_epoch + (int64_t)interval;
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

static bool slideshow_base_name_is_sha_tail(const char *file_name)
{
    if (file_name == NULL || strlen(file_name) != 16) {
        return false;
    }
    for (size_t i = 0; i < 16; ++i) {
        char c = file_name[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

static void slideshow_log_bin_sha256_tail(const char *file_name,
                                          const uint8_t *data,
                                          size_t len)
{
    uint8_t digest[32] = {0};
    char tail[17] = {0};
    int ret;

    if (!slideshow_base_name_is_sha_tail(file_name)) {
        ESP_LOGW(TAG, "slideshow bin sha256 skip invalid basename file=%s size=%u",
                 file_name != NULL ? file_name : "(null)",
                 (unsigned int)len);
        return;
    }
    if (data == NULL || len == 0) {
        ESP_LOGE(TAG, "slideshow bin sha256 failed file=%s size=%u ret=%d",
                 file_name,
                 (unsigned int)len,
                 ESP_ERR_INVALID_ARG);
        return;
    }

    ret = mbedtls_sha256(data, len, digest, 0);
    if (ret != 0) {
        ESP_LOGE(TAG, "slideshow bin sha256 failed file=%s size=%u ret=%d",
                 file_name,
                 (unsigned int)len,
                 ret);
        return;
    }

    for (size_t i = 24; i < 32; ++i) {
        snprintf(tail + ((i - 24) * 2), 3, "%02x", digest[i]);
    }

    if (strcasecmp(file_name, tail) == 0) {
        ESP_LOGI(TAG, "slideshow bin sha256 ok file=%s calc=%s size=%u",
                 file_name,
                 tail,
                 (unsigned int)len);
    } else {
        ESP_LOGE(TAG, "slideshow bin sha256 mismatch file=%s calc=%s size=%u",
                 file_name,
                 tail,
                 (unsigned int)len);
    }
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

        if (!file_name_is_safe(file_name)) {
            return false;
        }
        if (request->file_count >= TDX_SLIDESHOW_MAX_FILES) {
            continue;
        }
        strlcpy(request->file_names[request->file_count], file_name,
                sizeof(request->file_names[request->file_count]));
        request->file_count++;
    }

    return closed && request->file_count > 0;
}

static esp_err_t send_start_slideshow_result(httpd_req_t *req, int result, const char *message)
{
    char json[160];
    if (result == TDX_JSON_RESULT_OK) {
        snprintf(json, sizeof(json),
                 "{\"func\":\"start_slideshow_result\",\"result\":%d}",
                 TDX_JSON_RESULT_OK);
    } else {
        snprintf(json, sizeof(json),
                 "{\"func\":\"start_slideshow_result\",\"result\":%d,\"message\":\"%s\"}",
                 result,
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
    if (!example_storage_supports_directories()) {
        return ESP_OK;
    }
    esp_err_t lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }
    if (stat(bin_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        TdxSharedSpi_Unlock();
        ESP_LOGE(TAG, "slideshow bin dir missing: %s", bin_dir);
        return ESP_ERR_NOT_FOUND;
    }
    TdxSharedSpi_Unlock();
    return ESP_OK;
}

static esp_err_t check_slideshow_files_exist(const char *bin_dir, const slideshow_request_t *request)
{
    esp_err_t lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }
    for (size_t i = 0; i < request->file_count; i++) {
        char path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + TDX_SLIDESHOW_FILE_NAME_MAX_LEN + 24];
        struct stat st = {0};
        snprintf(path, sizeof(path), "%s/%s.bin", bin_dir, request->file_names[i]);
        if (stat(path, &st) != 0 || st.st_size <= 0) {
            TdxSharedSpi_Unlock();
            ESP_LOGE(TAG, "slideshow file missing index=%u path=%s",
                     (unsigned int)i, path);
            return ESP_ERR_NOT_FOUND;
        }
        ESP_LOGI(TAG, "slideshow file ok index=%u path=%s size=%u",
                 (unsigned int)i, path, (unsigned int)st.st_size);
    }
    TdxSharedSpi_Unlock();
    return ESP_OK;
}

static esp_err_t write_text_file(const char *path, const char *data)
{
    esp_err_t lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        TdxSharedSpi_Unlock();
        ESP_LOGE(TAG, "slideshow open failed path=%s errno=%d", path, errno);
        return ESP_FAIL;
    }

    size_t len = strlen(data);
    size_t written = fwrite(data, 1, len, fp);
    fclose(fp);
    TdxSharedSpi_Unlock();
    ESP_LOGI(TAG, "slideshow write path=%s len=%u written=%u",
             path, (unsigned int)len, (unsigned int)written);
    return written == len ? ESP_OK : ESP_FAIL;
}

static esp_err_t write_slideshow_rtc_control(const char *bin_dir, const slideshow_request_t *request)
{
    char path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    char json[256];

    if (bin_dir == NULL || request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(path, sizeof(path), "%s/%s", bin_dir, TDX_SLIDESHOW_CONTROL_FILE);
    snprintf(json, sizeof(json),
             "{\"func\":\"set_slideshow\",\"sw\":1,\"interval\":%lu,\"random\":%s,"
             "\"timestamp\":%lld,\"anchor_epoch\":%lld}",
             (unsigned long)request->interval,
             request->random ? "true" : "false",
             (long long)request->timestamp,
             (long long)request->anchor_epoch);

    ESP_LOGI(TAG,
             "start_slideshow write rtc control sw=1 interval=%lu random=%d timestamp=%lld anchor=%lld json=%s",
             (unsigned long)request->interval,
             request->random ? 1 : 0,
             (long long)request->timestamp,
             (long long)request->anchor_epoch,
             json);
    return write_text_file(path, json);
}

static esp_err_t start_slideshow_apply_timestamp(slideshow_request_t *request)
{
    if (request == NULL || !timestamp_reasonable(request->timestamp)) {
        ESP_LOGW(TAG,
                 "start_slideshow timestamp invalid timestamp=%lld",
                 request != NULL ? (long long)request->timestamp : 0LL);
        if (ServerNetworkStaTime_IsSntpSynced()) {
            (void)ServerNetworkStaTime_BackupCurrentToCh583("start_slideshow_bad_timestamp_sntp_now");
        }
        return TDX_JSON_RESULT_SLIDESHOW_TIMESTAMP_INVALID;
    }

    request->anchor_epoch = request->timestamp;
    bool time_from_sntp = ServerNetworkStaTime_IsSntpSynced();
    time_t now_time = 0;
    time(&now_time);
    char timestamp_text[32] = {0};
    char now_text[32] = {0};
    format_epoch_local(request->timestamp, timestamp_text, sizeof(timestamp_text));
    format_epoch_local((int64_t)now_time, now_text, sizeof(now_text));

    if (time_from_sntp) {
        (void)ServerNetworkStaTime_BackupTimestampToCh583(request->timestamp,
                                                          "start_slideshow_timestamp");
        int64_t diff = (int64_t)now_time - request->timestamp;
        if (diff < 0) {
            diff = -diff;
        }
        if (diff > 5) {
            ESP_LOGW(TAG,
                     "start_slideshow reject time diff too large timestamp=%lld(%s) now=%lld(%s) diff=%lld limit=5",
                     (long long)request->timestamp,
                     timestamp_text,
                     (long long)now_time,
                     now_text,
                     (long long)diff);
            return TDX_JSON_RESULT_SLIDESHOW_TIME_DIFF_TOO_LARGE;
        }
        if (request->timestamp > (int64_t)now_time) {
            uint32_t lead_seconds = slideshow_rtc_display_lead_seconds();
            int64_t ahead_seconds = request->timestamp - (int64_t)now_time;
            int64_t wait_seconds = ahead_seconds > (int64_t)lead_seconds ?
                                   ahead_seconds - (int64_t)lead_seconds : 0;
            ESP_LOGI(TAG,
                     "start_slideshow future timestamp accepted ahead=%lld lead=%lu wait_before_display=%lld",
                     (long long)ahead_seconds,
                     (unsigned long)lead_seconds,
                     (long long)wait_seconds);
        }
        ESP_LOGI(TAG,
                 "start_slideshow time source=sntp timestamp=%lld(%s) now=%lld(%s) diff=%lld",
                 (long long)request->timestamp,
                 timestamp_text,
                 (long long)now_time,
                 now_text,
                 (long long)diff);
        return ESP_OK;
    }

    esp_err_t time_ret = ServerNetworkStaTime_SetTimestamp(request->timestamp);
    if (time_ret != ESP_OK) {
        ESP_LOGW(TAG,
                 "start_slideshow timestamp set rtc failed timestamp=%lld(%s) ret=%s",
                 (long long)request->timestamp,
                 timestamp_text,
                 esp_err_to_name(time_ret));
        return TDX_JSON_RESULT_SLIDESHOW_TIME_SET_FAILED;
    }
    ESP_LOGI(TAG,
             "start_slideshow time source=timestamp set rtc timestamp=%lld(%s)",
             (long long)request->timestamp,
             timestamp_text);
    return ESP_OK;
}

static void slideshow_loaded_file_free(slideshow_loaded_file_t *loaded)
{
    if (loaded == NULL) {
        return;
    }
    if (loaded->buf != NULL) {
        heap_caps_free(loaded->buf);
    }
    memset(loaded, 0, sizeof(*loaded));
}

static bool slideshow_loaded_file_matches(const slideshow_loaded_file_t *loaded, const char *file_name)
{
    return loaded != NULL &&
           loaded->buf != NULL &&
           file_name != NULL &&
           strcmp(loaded->file_name, file_name) == 0;
}

static void slideshow_update_preload_failure(slideshow_runtime_t *runtime,
                                             const char *file_name,
                                             esp_err_t result)
{
    if (runtime == NULL) {
        return;
    }
    runtime->preload_failure_valid = result != ESP_OK &&
                                     file_name != NULL &&
                                     !s_slideshow_stop;
    runtime->preload_failure_result = result;
    if (runtime->preload_failure_valid && file_name != NULL) {
        strlcpy(runtime->preload_failure_file,
                file_name,
                sizeof(runtime->preload_failure_file));
    } else {
        runtime->preload_failure_file[0] = '\0';
    }
}

static bool slideshow_take_preload_failure(slideshow_runtime_t *runtime,
                                           const char *file_name,
                                           esp_err_t *result)
{
    if (runtime == NULL || file_name == NULL || !runtime->preload_failure_valid) {
        return false;
    }
    if (strcmp(runtime->preload_failure_file, file_name) != 0) {
        runtime->preload_failure_valid = false;
        runtime->preload_failure_file[0] = '\0';
        return false;
    }
    if (result != NULL) {
        *result = runtime->preload_failure_result;
    }
    runtime->preload_failure_valid = false;
    runtime->preload_failure_file[0] = '\0';
    return true;
}

static esp_err_t slideshow_load_file(const char *base_path,
                                     const char *file_name,
                                     slideshow_loaded_file_t *loaded,
                                     bool allow_internal_fallback)
{
    char path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + TDX_SLIDESHOW_FILE_NAME_MAX_LEN + 24];
    struct stat st = {0};

    if (base_path == NULL || !file_name_is_safe(file_name) || loaded == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_slideshow_stop) {
        ESP_LOGI(TAG, "slideshow preload skipped because stop requested file=%s", file_name);
        return ESP_ERR_INVALID_STATE;
    }

    slideshow_loaded_file_free(loaded);
    TickType_t preload_start_tick = xTaskGetTickCount();
    ESP_LOGI(TAG, "slideshow preload start file=%s", file_name);

    snprintf(path, sizeof(path), "%s/bin_img/%s.bin", base_path, file_name);
    esp_err_t lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }
    if (stat(path, &st) != 0 || st.st_size <= 0) {
        TdxSharedSpi_Unlock();
        ESP_LOGE(TAG, "slideshow file missing path=%s", path);
        return ESP_ERR_NOT_FOUND;
    }
    TdxSharedSpi_Unlock();
    if (s_slideshow_stop) {
        ESP_LOGI(TAG, "slideshow preload skipped after stat because stop requested file=%s", file_name);
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t *buf = (uint8_t *)heap_caps_malloc((size_t)st.st_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL && allow_internal_fallback) {
        buf = (uint8_t *)heap_caps_malloc((size_t)st.st_size, MALLOC_CAP_8BIT);
    }
    if (buf == NULL) {
        ESP_LOGE(TAG, "slideshow file alloc failed path=%s size=%u",
                 path, (unsigned int)st.st_size);
        return ESP_ERR_NO_MEM;
    }

    lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        heap_caps_free(buf);
        return lock_ret;
    }
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        TdxSharedSpi_Unlock();
        heap_caps_free(buf);
        ESP_LOGE(TAG, "slideshow file open failed path=%s errno=%d", path, errno);
        return ESP_FAIL;
    }

    size_t read_len = fread(buf, 1, (size_t)st.st_size, fp);
    fclose(fp);
    TdxSharedSpi_Unlock();
    if (s_slideshow_stop) {
        heap_caps_free(buf);
        ESP_LOGI(TAG, "slideshow preload skipped after read because stop requested file=%s", file_name);
        return ESP_ERR_INVALID_STATE;
    }
    if (read_len != (size_t)st.st_size) {
        heap_caps_free(buf);
        ESP_LOGE(TAG, "slideshow file read failed path=%s expect=%u actual=%u",
                 path, (unsigned int)st.st_size, (unsigned int)read_len);
        return ESP_FAIL;
    }

    slideshow_log_bin_sha256_tail(file_name, buf, read_len);
    strlcpy(loaded->file_name, file_name, sizeof(loaded->file_name));
    loaded->buf = buf;
    loaded->len = read_len;
    uint32_t preload_ms = (uint32_t)((xTaskGetTickCount() - preload_start_tick) * portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "slideshow preload done file=%s size=%u ms=%lu",
             file_name,
             (unsigned int)read_len,
             (unsigned long)preload_ms);
    return ESP_OK;
}

static esp_err_t slideshow_display_loaded_file_and_wait(slideshow_loaded_file_t *loaded,
                                                        uint32_t interval,
                                                        bool rtc_mode,
                                                        uint16_t position,
                                                        uint16_t total,
                                                        TickType_t *display_start_tick_out)
{
    if (loaded == NULL || loaded->buf == NULL || loaded->len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_slideshow_stop) {
        ESP_LOGI(TAG, "slideshow display skipped before queue because stop requested file=%s", loaded->file_name);
        slideshow_loaded_file_free(loaded);
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t display_start_tick = xTaskGetTickCount();
    if (display_start_tick_out != NULL) {
        *display_start_tick_out = display_start_tick;
    }
    if (interval > 0 && !rtc_mode) {
        slideshow_begin_interval(interval, display_start_tick);
        ESP_LOGI(TAG, "slideshow legacy_tick marker start file=%s interval=%lu tick=%lu",
                 loaded->file_name,
                 (unsigned long)interval,
                 (unsigned long)display_start_tick);
    } else if (rtc_mode) {
        ESP_LOGI(TAG, "slideshow rtc display start file=%s position=%u/%u interval=%lu tick_marker=%lu",
                 loaded->file_name,
                 (unsigned int)position,
                 (unsigned int)total,
                 (unsigned long)interval,
                 (unsigned long)display_start_tick);
    }
    ESP_LOGI(TAG, "slideshow display queue start file=%s size=%u tick_marker=%lu",
             loaded->file_name,
             (unsigned int)loaded->len,
             (unsigned long)display_start_tick);
    esp_err_t ret = ServerNetworkStaEpdDisplay_QueueToScreenAndWait(loaded->buf, loaded->len, 1);
    if (ret == ESP_OK) {
        uint32_t display_elapsed = (uint32_t)(((xTaskGetTickCount() - display_start_tick) * portTICK_PERIOD_MS) / 1000U);
        ESP_LOGI(TAG, "slideshow display done file=%s elapsed=%lu",
                 loaded->file_name,
                 (unsigned long)display_elapsed);
    }
    slideshow_loaded_file_free(loaded);
    return ret;
}

static esp_err_t display_slideshow_file_and_wait(const char *base_path,
                                                 const char *file_name,
                                                 uint32_t interval,
                                                 TickType_t *display_start_tick_out)
{
    slideshow_loaded_file_t loaded = {0};
    esp_err_t ret = slideshow_load_file(base_path, file_name, &loaded, true);
    if (ret != ESP_OK) {
        slideshow_loaded_file_free(&loaded);
        return ret;
    }
    return slideshow_display_loaded_file_and_wait(&loaded, interval, false, 0, 0, display_start_tick_out);
}

static uint32_t slideshow_hash_byte(uint32_t hash, uint8_t value)
{
    hash ^= value;
    return hash * 16777619UL;
}

static uint32_t slideshow_config_hash(const slideshow_request_t *request)
{
    uint32_t hash = 2166136261UL;
    if (request == NULL) {
        return 0;
    }

    hash = slideshow_hash_byte(hash, (uint8_t)request->file_count);
    hash = slideshow_hash_byte(hash, request->random ? 1U : 0U);
    uint32_t start_index = (uint32_t)request->start_index;
    for (size_t i = 0; i < sizeof(start_index); ++i) {
        hash = slideshow_hash_byte(hash, (uint8_t)(start_index >> (i * 8U)));
    }
    for (size_t i = 0; i < request->file_count; ++i) {
        const uint8_t *name = (const uint8_t *)request->file_names[i];
        while (*name != 0) {
            hash = slideshow_hash_byte(hash, *name++);
        }
        hash = slideshow_hash_byte(hash, 0);
    }
    return hash;
}

static uint32_t slideshow_random_next(uint32_t *state)
{
    uint32_t value = *state;
    if (value == 0) {
        value = 0x6D2B79F5UL;
    }
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static void slideshow_build_order(slideshow_progress_t *progress,
                                  size_t file_count,
                                  bool random,
                                  size_t start_index)
{
    progress->order_count = (uint8_t)file_count;
    for (size_t i = 0; i < file_count; ++i) {
        progress->order[i] = (uint8_t)(random ? i : (start_index + i) % file_count);
    }
    if (!random || file_count < 2) {
        return;
    }

    uint32_t state = progress->random_seed;
    for (size_t i = file_count - 1; i > 0; --i) {
        size_t other = (size_t)(slideshow_random_next(&state) % (i + 1));
        uint8_t tmp = progress->order[i];
        progress->order[i] = progress->order[other];
        progress->order[other] = tmp;
    }
}

static void slideshow_init_progress(const slideshow_request_t *request,
                                    slideshow_progress_t *progress)
{
    memset(progress, 0, sizeof(*progress));
    progress->magic = SLIDESHOW_PROGRESS_MAGIC;
    progress->version = SLIDESHOW_PROGRESS_VERSION;
    progress->config_hash = slideshow_config_hash(request);
    progress->random = request->random ? 1U : 0U;
    progress->random_seed = esp_random();
    if (progress->random_seed == 0) {
        progress->random_seed = 1;
    }
    slideshow_build_order(progress,
                          request->file_count,
                          request->random,
                          request->start_index);
    progress->position = 0;
    strlcpy(progress->pending_file,
            request->file_names[progress->order[0]],
            sizeof(progress->pending_file));
}

static bool slideshow_progress_valid(const slideshow_request_t *request,
                                     const slideshow_progress_t *progress)
{
    if (request == NULL || progress == NULL || request->file_count == 0 ||
        progress->magic != SLIDESHOW_PROGRESS_MAGIC ||
        progress->version != SLIDESHOW_PROGRESS_VERSION ||
        progress->config_hash != slideshow_config_hash(request) ||
        progress->random != (request->random ? 1U : 0U) ||
        progress->order_count != request->file_count ||
        progress->position >= progress->order_count ||
        progress->pending_file[0] == '\0' ||
        progress->pending_file[sizeof(progress->pending_file) - 1] != '\0') {
        return false;
    }

    bool seen[TDX_SLIDESHOW_MAX_FILES] = {false};
    for (size_t i = 0; i < progress->order_count; ++i) {
        uint8_t index = progress->order[i];
        if (index >= request->file_count || seen[index]) {
            return false;
        }
        seen[index] = true;
    }

    uint8_t pending_index = progress->order[progress->position];
    return strcmp(progress->pending_file, request->file_names[pending_index]) == 0;
}

static esp_err_t save_slideshow_progress(const slideshow_progress_t *progress)
{
    slideshow_progress_t verify;
    esp_err_t ret = ESP_FAIL;

    for (int attempt = 1; attempt <= SLIDESHOW_PROGRESS_SAVE_RETRIES; ++attempt) {
        ret = app_nvs_write_blob(TDX_SLIDESHOW_NVS_PROGRESS_KEY, progress, sizeof(*progress));
        if (ret == ESP_OK) {
            memset(&verify, 0, sizeof(verify));
            ret = app_nvs_read_blob(TDX_SLIDESHOW_NVS_PROGRESS_KEY, &verify, sizeof(verify));
            if (ret == ESP_OK && memcmp(&verify, progress, sizeof(verify)) == 0) {
                ESP_LOGI(TAG, "slideshow progress saved pending=%s position=%u/%u",
                         progress->pending_file,
                         (unsigned int)progress->position,
                         (unsigned int)progress->order_count);
                return ESP_OK;
            }
            ret = ESP_ERR_INVALID_CRC;
        }
        ESP_LOGW(TAG, "slideshow progress save attempt=%d ret=%s",
                 attempt, esp_err_to_name(ret));
        if (attempt < SLIDESHOW_PROGRESS_SAVE_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    return ret;
}

static esp_err_t load_or_create_slideshow_progress(const slideshow_request_t *request,
                                                   bool reset,
                                                   slideshow_progress_t *progress,
                                                   bool *loaded_existing)
{
    if (loaded_existing != NULL) {
        *loaded_existing = false;
    }
    if (!reset) {
        esp_err_t progress_read_ret = app_nvs_read_blob(TDX_SLIDESHOW_NVS_PROGRESS_KEY,
                                                        progress,
                                                        sizeof(*progress));
        if (progress_read_ret == ESP_OK && slideshow_progress_valid(request, progress)) {
            if (loaded_existing != NULL) {
                *loaded_existing = true;
            }
            ESP_LOGI(TAG, "slideshow resume pending=%s position=%u/%u",
                     progress->pending_file,
                     (unsigned int)progress->position,
                     (unsigned int)progress->order_count);
            return ESP_OK;
        }
    }

    slideshow_init_progress(request, progress);
    return save_slideshow_progress(progress);
}

static void prepare_next_slideshow_progress(const slideshow_request_t *request,
                                            const slideshow_progress_t *current,
                                            slideshow_progress_t *next)
{
    memcpy(next, current, sizeof(*next));
    if ((size_t)next->position + 1U < next->order_count) {
        next->position++;
    } else {
        next->position = 0;
        next->random_seed = esp_random();
        if (next->random_seed == 0) {
            next->random_seed = 1;
        }
        slideshow_build_order(next,
                              request->file_count,
                              request->random,
                              request->start_index);
    }
    strlcpy(next->pending_file,
            request->file_names[next->order[next->position]],
             sizeof(next->pending_file));
}

static bool slideshow_progress_select_index(const slideshow_request_t *request,
                                            slideshow_progress_t *progress,
                                            size_t file_index)
{
    if (request == NULL || progress == NULL || file_index >= request->file_count ||
        !slideshow_progress_valid(request, progress)) {
        return false;
    }

    for (size_t i = 0; i < progress->order_count; ++i) {
        if (progress->order[i] == file_index) {
            progress->position = (uint8_t)i;
            strlcpy(progress->pending_file,
                    request->file_names[file_index],
                    sizeof(progress->pending_file));
            return true;
        }
    }
    return false;
}

static bool slideshow_epoch_for_slot(const slideshow_request_t *request,
                                     uint64_t slot,
                                     int64_t *epoch)
{
    if (request == NULL || epoch == NULL || request->anchor_epoch <= 0 ||
        request->interval == 0 ||
        slot > (uint64_t)INT64_MAX / (uint64_t)request->interval) {
        return false;
    }
    int64_t offset = (int64_t)(slot * (uint64_t)request->interval);
    if (request->anchor_epoch > INT64_MAX - offset) {
        return false;
    }
    *epoch = request->anchor_epoch + offset;
    return true;
}

static bool slideshow_sync_sntp_target_to_now(slideshow_runtime_t *runtime,
                                               const char *reason)
{
    if (runtime == NULL || !runtime->sntp_schedule_enabled) {
        return true;
    }

    time_t now_time = 0;
    time(&now_time);
    slideshow_schedule_position_t schedule = {0};
    if (!slideshow_calculate_schedule_position(runtime->request.anchor_epoch,
                                                runtime->request.interval,
                                                runtime->request.file_count,
                                                runtime->request.start_index,
                                                (int64_t)now_time,
                                                &schedule)) {
        ESP_LOGE(TAG, "slideshow SNTP schedule calculation failed reason=%s now=%lld",
                 reason != NULL ? reason : "unknown",
                 (long long)now_time);
        return false;
    }

    if (!schedule.started) {
        if (runtime->scheduled_slot != 0 || runtime->consumed_slot_valid) {
            if (!slideshow_progress_select_index(&runtime->request,
                                                 &runtime->progress,
                                                 runtime->request.start_index)) {
                return false;
            }
            runtime->scheduled_slot = 0;
            runtime->consumed_slot_valid = false;
            runtime->next_epoch = runtime->request.anchor_epoch;
            ESP_LOGI(TAG,
                     "slideshow SNTP resync before anchor reason=%s file=%s now=%lld anchor=%lld",
                     reason != NULL ? reason : "unknown",
                     runtime->progress.pending_file,
                     (long long)now_time,
                     (long long)runtime->request.anchor_epoch);
        }
        return true;
    }

    bool normal_lead_window = false;
    if (runtime->consumed_slot_valid &&
        runtime->consumed_slot == schedule.slot + 1U) {
        int64_t consumed_epoch = 0;
        uint32_t lead_seconds = slideshow_rtc_display_lead_seconds();
        if (slideshow_epoch_for_slot(&runtime->request,
                                     runtime->consumed_slot,
                                     &consumed_epoch) &&
            consumed_epoch > (int64_t)lead_seconds &&
            (int64_t)now_time >= consumed_epoch - (int64_t)lead_seconds) {
            normal_lead_window = true;
        }
    }

    bool moved_forward = schedule.slot > runtime->scheduled_slot;
    bool moved_backward = runtime->consumed_slot_valid ?
                          schedule.slot < runtime->consumed_slot && !normal_lead_window :
                          schedule.slot < runtime->scheduled_slot;
    if (moved_forward || moved_backward) {
        uint64_t old_slot = runtime->scheduled_slot;
        if (!slideshow_progress_select_index(&runtime->request,
                                             &runtime->progress,
                                             schedule.current_index)) {
            return false;
        }
        runtime->scheduled_slot = schedule.slot;
        runtime->next_epoch = (int64_t)now_time;
        ESP_LOGI(TAG,
                 "slideshow SNTP resync reason=%s direction=%s old=%llu new=%llu index=%u file=%s now=%lld",
                 reason != NULL ? reason : "unknown",
                 moved_forward ? "forward" : "backward",
                 (unsigned long long)old_slot,
                 (unsigned long long)runtime->scheduled_slot,
                 (unsigned int)schedule.current_index,
                 runtime->progress.pending_file,
                 (long long)now_time);
    }
    return true;
}

static bool slideshow_prepare_next_sntp_progress(slideshow_runtime_t *runtime,
                                                 slideshow_progress_t *next,
                                                 uint64_t *next_slot_out,
                                                 int64_t *next_epoch)
{
    if (runtime == NULL || next == NULL || next_slot_out == NULL || next_epoch == NULL ||
        !runtime->sntp_schedule_enabled) {
        return false;
    }

    time_t now_time = 0;
    time(&now_time);
    slideshow_schedule_position_t schedule = {0};
    if (!slideshow_calculate_schedule_position(runtime->request.anchor_epoch,
                                                runtime->request.interval,
                                                runtime->request.file_count,
                                                runtime->request.start_index,
                                                (int64_t)now_time,
                                                &schedule)) {
        return false;
    }

    uint64_t next_slot = runtime->scheduled_slot + 1U;
    if (schedule.started && schedule.slot > runtime->scheduled_slot) {
        next_slot = schedule.slot;
    }
    size_t next_index =
        (runtime->request.start_index +
         (size_t)(next_slot % (uint64_t)runtime->request.file_count)) %
        runtime->request.file_count;
    memcpy(next, &runtime->progress, sizeof(*next));
    if (!slideshow_progress_select_index(&runtime->request, next, next_index)) {
        return false;
    }

    if (schedule.started && next_slot == schedule.slot) {
        *next_epoch = (int64_t)now_time;
    } else if (!slideshow_epoch_for_slot(&runtime->request, next_slot, next_epoch)) {
        return false;
    }
    *next_slot_out = next_slot;
    return true;
}

static bool slideshow_enable_sntp_schedule_if_ready(slideshow_runtime_t *runtime)
{
    if (runtime == NULL || runtime->sntp_schedule_enabled ||
        !ServerNetworkStaTime_IsSntpSynced()) {
        return true;
    }

    time_t now_time = 0;
    time(&now_time);
    slideshow_schedule_position_t schedule = {0};
    if (!slideshow_calculate_schedule_position(runtime->request.anchor_epoch,
                                                runtime->request.interval,
                                                runtime->request.file_count,
                                                runtime->request.start_index,
                                                (int64_t)now_time,
                                                &schedule) ||
        !slideshow_progress_select_index(&runtime->request,
                                         &runtime->progress,
                                         schedule.current_index)) {
        ESP_LOGE(TAG,
                 "slideshow SNTP switch failed anchor=%lld interval=%lu count=%u now=%lld",
                 (long long)runtime->request.anchor_epoch,
                 (unsigned long)runtime->request.interval,
                 (unsigned int)runtime->request.file_count,
                 (long long)now_time);
        return false;
    }

    runtime->sntp_schedule_enabled = true;
    runtime->consumed_slot_valid = false;
    runtime->scheduled_slot = schedule.slot;
    runtime->next_epoch = schedule.started ? (int64_t)now_time : schedule.next_epoch;
    ESP_LOGI(TAG,
             "slideshow SNTP ready, switch legacy to absolute slot=%llu index=%u file=%s now=%lld next=%lld",
             (unsigned long long)schedule.slot,
             (unsigned int)schedule.current_index,
             runtime->progress.pending_file,
             (long long)now_time,
             (long long)runtime->next_epoch);
    return true;
}

static void slideshow_begin_interval(uint32_t interval, TickType_t start_tick)
{
    if (interval < TDX_SLIDESHOW_INTERVAL_MIN_SECONDS) {
        interval = TDX_SLIDESHOW_INTERVAL_MIN_SECONDS;
    }

    portENTER_CRITICAL(&s_slideshow_timing_mux);
    s_slideshow_interval_active = true;
    s_slideshow_runtime_interval = interval;
    s_slideshow_interval_start_tick = start_tick;
    s_slideshow_runtime_next_epoch = 0;
    portEXIT_CRITICAL(&s_slideshow_timing_mux);
}

static void slideshow_begin_rtc_interval(uint32_t interval, int64_t next_epoch)
{
    if (interval < TDX_SLIDESHOW_INTERVAL_MIN_SECONDS) {
        interval = TDX_SLIDESHOW_INTERVAL_MIN_SECONDS;
    }

    portENTER_CRITICAL(&s_slideshow_timing_mux);
    s_slideshow_interval_active = true;
    s_slideshow_runtime_interval = interval;
    s_slideshow_interval_start_tick = xTaskGetTickCount();
    s_slideshow_runtime_next_epoch = next_epoch;
    portEXIT_CRITICAL(&s_slideshow_timing_mux);
}

static TickType_t slideshow_seconds_to_ticks(uint32_t seconds)
{
    uint64_t ticks = (uint64_t)seconds * (uint64_t)configTICK_RATE_HZ;
    if (ticks > (uint64_t)portMAX_DELAY) {
        ticks = (uint64_t)portMAX_DELAY;
    }
    return (TickType_t)ticks;
}

static bool slideshow_force_random_config(const char *scope, bool random)
{
    if (random) {
        ESP_LOGW(TAG, "%s random permanently disabled, force random=false", scope);
    }
    return false;
}

static uint32_t slideshow_rtc_display_lead_seconds(void)
{
    switch (EPD_type) {
    case EPD_TYPE_1600_1200_133_DKE:
        return 1;
    case EPD_TYPE_1600_1200_133:
        return 3;
    case  EPD_TYPE_1600_1200_79:
        return 4;
    default:
        return TDX_SLIDESHOW_RTC_DISPLAY_LEAD_SECONDS;
    }
}

static bool wait_slideshow_interval_from_start(TickType_t start_tick, uint32_t interval)
{
    if (interval < TDX_SLIDESHOW_INTERVAL_MIN_SECONDS) {
        interval = TDX_SLIDESHOW_INTERVAL_MIN_SECONDS;
    }

    TickType_t interval_ticks = slideshow_seconds_to_ticks(interval);
    TickType_t target_tick = start_tick + interval_ticks;
    TickType_t now = xTaskGetTickCount();
    uint32_t elapsed = (uint32_t)(((now - start_tick) * portTICK_PERIOD_MS) / 1000U);
    TickType_t remaining_ticks = (now - start_tick) < interval_ticks ? target_tick - now : 0;
    uint32_t remaining = (uint32_t)((remaining_ticks * portTICK_PERIOD_MS + 999U) / 1000U);

    ESP_LOGI(TAG, "slideshow legacy_tick wait interval=%lu elapsed=%lu remain=%lu",
             (unsigned long)interval,
             (unsigned long)elapsed,
             (unsigned long)remaining);

    while (remaining_ticks > 0 && !s_slideshow_stop) {
        TickType_t delay_ticks = remaining_ticks > pdMS_TO_TICKS(1000) ?
                                 pdMS_TO_TICKS(1000) :
                                 remaining_ticks;
        vTaskDelay(delay_ticks);
        now = xTaskGetTickCount();
        if ((now - start_tick) >= interval_ticks) {
            break;
        }
        remaining_ticks = target_tick - now;
    }

    now = xTaskGetTickCount();
    bool target_reached = (now - start_tick) >= interval_ticks;
    if (!target_reached && s_slideshow_stop) {
        uint32_t remain_ms = (uint32_t)((target_tick - now) * portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "slideshow legacy_tick stopped interval=%lu remain_ms=%lu",
                 (unsigned long)interval,
                 (unsigned long)remain_ms);
        portENTER_CRITICAL(&s_slideshow_timing_mux);
        s_slideshow_interval_active = false;
        portEXIT_CRITICAL(&s_slideshow_timing_mux);
        return false;
    }

    int32_t late_ms = 0;
    if (target_reached) {
        late_ms = (int32_t)((now - target_tick) * portTICK_PERIOD_MS);
    } else {
        late_ms = -(int32_t)((target_tick - now) * portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "slideshow legacy_tick target interval=%lu late_ms=%ld",
             (unsigned long)interval,
             (long)late_ms);

    portENTER_CRITICAL(&s_slideshow_timing_mux);
    s_slideshow_interval_active = false;
    portEXIT_CRITICAL(&s_slideshow_timing_mux);
    return target_reached;
}

static slideshow_rtc_wait_result_t wait_slideshow_rtc_epoch(int64_t target_epoch,
                                                             uint32_t interval,
                                                             bool watch_for_sntp)
{
    if (watch_for_sntp && ServerNetworkStaTime_IsSntpSynced()) {
        portENTER_CRITICAL(&s_slideshow_timing_mux);
        s_slideshow_interval_active = false;
        s_slideshow_runtime_next_epoch = 0;
        portEXIT_CRITICAL(&s_slideshow_timing_mux);
        return SLIDESHOW_RTC_WAIT_SNTP_READY;
    }
    uint32_t lead_seconds = slideshow_rtc_display_lead_seconds();
    int64_t display_epoch = target_epoch > (int64_t)lead_seconds ?
                            target_epoch - (int64_t)lead_seconds :
                            target_epoch;
    time_t now_time = 0;
    time(&now_time);
    int64_t now_epoch = (int64_t)now_time;
    int64_t remain = display_epoch > now_epoch ? display_epoch - now_epoch : 0;
    char target_text[32] = {0};
    char display_text[32] = {0};
    char now_text[32] = {0};

    slideshow_begin_rtc_interval(interval, target_epoch);
    format_epoch_local(target_epoch, target_text, sizeof(target_text));
    format_epoch_local(display_epoch, display_text, sizeof(display_text));
    format_epoch_local(now_epoch, now_text, sizeof(now_text));
    ESP_LOGI(TAG,
             "slideshow rtc wait target=%lld(%s) display_target=%lld(%s) now=%lld(%s) remain=%lld interval=%lu lead=%lu",
             (long long)target_epoch,
             target_text,
             (long long)display_epoch,
             display_text,
             (long long)now_epoch,
             now_text,
             (long long)remain,
             (unsigned long)interval,
             (unsigned long)lead_seconds);

    while (remain > 0 && !s_slideshow_stop) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (watch_for_sntp && ServerNetworkStaTime_IsSntpSynced()) {
            portENTER_CRITICAL(&s_slideshow_timing_mux);
            s_slideshow_interval_active = false;
            s_slideshow_runtime_next_epoch = 0;
            portEXIT_CRITICAL(&s_slideshow_timing_mux);
            return SLIDESHOW_RTC_WAIT_SNTP_READY;
        }
        time(&now_time);
        now_epoch = (int64_t)now_time;
        remain = display_epoch > now_epoch ? display_epoch - now_epoch : 0;
    }

    portENTER_CRITICAL(&s_slideshow_timing_mux);
    s_slideshow_interval_active = false;
    s_slideshow_runtime_next_epoch = 0;
    portEXIT_CRITICAL(&s_slideshow_timing_mux);

    if (s_slideshow_stop) {
        format_epoch_local(target_epoch, target_text, sizeof(target_text));
        format_epoch_local(display_epoch, display_text, sizeof(display_text));
        format_epoch_local(now_epoch, now_text, sizeof(now_text));
        ESP_LOGI(TAG,
                 "slideshow rtc wait stopped target=%lld(%s) display_target=%lld(%s) now=%lld(%s) remain=%lld lead=%lu",
                 (long long)target_epoch,
                 target_text,
                 (long long)display_epoch,
                 display_text,
                 (long long)now_epoch,
                 now_text,
                 (long long)remain,
                 (unsigned long)lead_seconds);
        return SLIDESHOW_RTC_WAIT_STOPPED;
    }

    format_epoch_local(target_epoch, target_text, sizeof(target_text));
    format_epoch_local(display_epoch, display_text, sizeof(display_text));
    format_epoch_local(now_epoch, now_text, sizeof(now_text));
    ESP_LOGI(TAG,
             "slideshow rtc display lead reached target=%lld(%s) display_target=%lld(%s) now=%lld(%s) lead=%lu target_delta=%lld",
             (long long)target_epoch,
             target_text,
             (long long)display_epoch,
             display_text,
             (long long)now_epoch,
             now_text,
             (unsigned long)lead_seconds,
             (long long)(now_epoch - target_epoch));
    return SLIDESHOW_RTC_WAIT_TARGET_REACHED;
}

static esp_err_t read_slideshow_config_file(const char *base_path, slideshow_request_t *request)
{
    char config_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    struct stat st = {0};

    if (base_path == NULL || request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(request, 0, sizeof(*request));

    snprintf(config_path, sizeof(config_path), "%s/bin_img/%s", base_path, TDX_SLIDESHOW_CONFIG_FILE);
    esp_err_t lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }
    if (stat(config_path, &st) != 0 || st.st_size <= 0) {
        TdxSharedSpi_Unlock();
        ESP_LOGI(TAG, "slideshow config missing path=%s", config_path);
        return ESP_ERR_NOT_FOUND;
    }
    TdxSharedSpi_Unlock();

    char *json = (char *)malloc((size_t)st.st_size + 1);
    if (json == NULL) {
        ESP_LOGE(TAG, "slideshow config alloc failed size=%u", (unsigned int)st.st_size);
        return ESP_ERR_NO_MEM;
    }

    lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        free(json);
        return lock_ret;
    }
    FILE *fp = fopen(config_path, "rb");
    if (fp == NULL) {
        TdxSharedSpi_Unlock();
        free(json);
        ESP_LOGE(TAG, "slideshow config open failed path=%s errno=%d", config_path, errno);
        return ESP_FAIL;
    }

    size_t read_len = fread(json, 1, (size_t)st.st_size, fp);
    fclose(fp);
    TdxSharedSpi_Unlock();
    json[read_len] = '\0';
    if (read_len != (size_t)st.st_size || !parse_file_names(json, request)) {
        free(json);
        ESP_LOGE(TAG, "slideshow config parse failed path=%s", config_path);
        return ESP_FAIL;
    }
    parse_json_u32(json, "interval", &request->interval);
    if (request->interval < TDX_SLIDESHOW_INTERVAL_MIN_SECONDS ||
        request->interval > TDX_SLIDESHOW_INTERVAL_MAX_SECONDS) {
        request->interval = TDX_SLIDESHOW_INTERVAL_MIN_SECONDS;
    }
    request->random = slideshow_force_random_config("saved slideshow_config",
                                                    parse_json_bool_default(json, "random", false));
    uint32_t parsed_start_index = 0;
    if (find_json_key(json, "startIndex") == NULL) {
        ESP_LOGE(TAG, "slideshow config invalid: startIndex missing path=%s", config_path);
        free(json);
        return TDX_JSON_RESULT_SLIDESHOW_START_INDEX_MISSING;
    }
    if (!parse_json_u32(json, "startIndex", &parsed_start_index) ||
        parsed_start_index >= request->file_count) {
        ESP_LOGE(TAG,
                 "slideshow config invalid: startIndex=%lu count=%u path=%s",
                 (unsigned long)parsed_start_index,
                 (unsigned int)request->file_count,
                 config_path);
        free(json);
        return TDX_JSON_RESULT_SLIDESHOW_START_INDEX_INVALID;
    }
    request->start_index = (size_t)parsed_start_index;
    free(json);
    return ESP_OK;
}

static bool read_slideshow_control_schedule(const char *base_path,
                                            uint32_t *interval,
                                            bool *random,
                                            int64_t *timestamp,
                                            int64_t *anchor_epoch)
{
    char control_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    char buf[512] = {0};
    int sw = 0;
    uint32_t parsed_interval = 0;
    int64_t parsed_timestamp = 0;
    int64_t parsed_anchor = 0;
    bool timestamp_present = false;
    bool anchor_present = false;

    if (base_path == NULL) {
        return false;
    }

    snprintf(control_path, sizeof(control_path), "%s/bin_img/%s", base_path, TDX_SLIDESHOW_CONTROL_FILE);
    if (TdxSharedSpi_Lock(portMAX_DELAY) != ESP_OK) {
        return false;
    }
    FILE *fp = fopen(control_path, "rb");
    if (fp == NULL) {
        TdxSharedSpi_Unlock();
        ESP_LOGI(TAG, "slideshow control missing path=%s", control_path);
        return false;
    }

    size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    TdxSharedSpi_Unlock();
    buf[len] = '\0';

    if (!parse_json_int(buf, "sw", &sw)) {
        ESP_LOGI(TAG, "slideshow control has no sw path=%s json=%s", control_path, buf);
        return false;
    }
    (void)parse_json_u32(buf, "interval", &parsed_interval);
    if (random != NULL) {
        *random = slideshow_force_random_config("saved show_control",
                                                parse_json_bool_default(buf, "random", *random));
    }
    timestamp_present = parse_json_i64(buf, "timestamp", &parsed_timestamp);
    anchor_present = parse_json_i64(buf, "anchor_epoch", &parsed_anchor);

    if (sw == 1) {
        if (parsed_interval < TDX_SLIDESHOW_INTERVAL_MIN_SECONDS ||
            parsed_interval > TDX_SLIDESHOW_INTERVAL_MAX_SECONDS ||
            !timestamp_present ||
            !anchor_present ||
            parsed_timestamp <= 0 ||
            parsed_anchor <= 0) {
            ESP_LOGE(TAG,
                     "slideshow control invalid: sw=1 requires interval=%u..%u and timestamp/anchor_epoch, legacy control rejected path=%s interval=%lu timestamp_present=%d timestamp=%lld anchor_present=%d anchor=%lld json=%s",
                     (unsigned int)TDX_SLIDESHOW_INTERVAL_MIN_SECONDS,
                     (unsigned int)TDX_SLIDESHOW_INTERVAL_MAX_SECONDS,
                     control_path,
                     (unsigned long)parsed_interval,
                     timestamp_present ? 1 : 0,
                     (long long)parsed_timestamp,
                     anchor_present ? 1 : 0,
                     (long long)parsed_anchor,
                     buf);
            return false;
        }
    }

    if (interval != NULL) {
        *interval = parsed_interval;
    }
    if (timestamp != NULL) {
        *timestamp = parsed_timestamp;
    }
    if (anchor_epoch != NULL) {
        *anchor_epoch = parsed_anchor;
    }
    ESP_LOGI(TAG, "slideshow control read sw=%d interval=%lu random=%d timestamp=%lld anchor=%lld json=%s",
             sw,
             (unsigned long)parsed_interval,
             random != NULL && *random ? 1 : 0,
             (long long)parsed_timestamp,
             (long long)parsed_anchor,
             buf);
    return sw == 1;
}

static bool read_slideshow_control_on(const char *base_path, uint32_t *interval, bool *random)
{
    return read_slideshow_control_schedule(base_path,
                                           interval,
                                           random,
                                           NULL,
                                           NULL);
}

bool ServerNetworkStaSlideshow_IsSavedEnabled(const char *base_path,
                                              uint32_t *interval,
                                              bool *random)
{
    return read_slideshow_control_on(base_path, interval, random);
}

bool ServerNetworkStaSlideshow_GetRuntimeTiming(uint32_t *interval,
                                                uint32_t *elapsed,
                                                bool *running)
{
    bool active = false;
    uint32_t current_interval = 0;
    TickType_t start_tick = 0;
    int64_t next_epoch = 0;

    portENTER_CRITICAL(&s_slideshow_timing_mux);
    active = s_slideshow_interval_active;
    current_interval = s_slideshow_runtime_interval;
    start_tick = s_slideshow_interval_start_tick;
    next_epoch = s_slideshow_runtime_next_epoch;
    portEXIT_CRITICAL(&s_slideshow_timing_mux);

    uint32_t current_elapsed = 0;
    if (active && next_epoch > 0) {
        time_t now_time = 0;
        time(&now_time);
        int64_t remain = next_epoch > (int64_t)now_time ? next_epoch - (int64_t)now_time : 0;
        current_elapsed = current_interval > (uint32_t)remain ?
                          current_interval - (uint32_t)remain :
                          0;
    } else if (active && start_tick != 0) {
        TickType_t now = xTaskGetTickCount();
        current_elapsed = (uint32_t)(((now - start_tick) * portTICK_PERIOD_MS) / 1000U);
    }

    if (interval != NULL) {
        *interval = current_interval;
    }
    if (elapsed != NULL) {
        *elapsed = current_elapsed;
    }
    if (running != NULL) {
        *running = active;
    }
    return active && current_interval > 0;
}

bool ServerNetworkStaSlideshow_GetScheduleTiming(uint32_t *interval,
                                                 int64_t *now_epoch,
                                                 int64_t *next_epoch,
                                                 uint32_t *remain,
                                                 bool *running)
{
    bool active = false;
    uint32_t current_interval = 0;
    int64_t current_next = 0;
    time_t now_time = 0;

    portENTER_CRITICAL(&s_slideshow_timing_mux);
    active = s_slideshow_interval_active;
    current_interval = s_slideshow_runtime_interval;
    current_next = s_slideshow_runtime_next_epoch;
    portEXIT_CRITICAL(&s_slideshow_timing_mux);

    time(&now_time);
    int64_t current_now = (int64_t)now_time;
    uint32_t current_remain = current_next > current_now ?
                              (uint32_t)(current_next - current_now) :
                              0;

    if (interval != NULL) {
        *interval = current_interval;
    }
    if (now_epoch != NULL) {
        *now_epoch = current_now;
    }
    if (next_epoch != NULL) {
        *next_epoch = current_next;
    }
    if (remain != NULL) {
        *remain = current_remain;
    }
    if (running != NULL) {
        *running = active && current_next > 0;
    }
    return active && current_interval > 0 && current_next > 0;
}

static void slideshow_record_display_start(uint32_t interval, TickType_t start_tick)
{
    if (interval < TDX_SLIDESHOW_INTERVAL_MIN_SECONDS) {
        interval = TDX_SLIDESHOW_INTERVAL_MIN_SECONDS;
    }

    portENTER_CRITICAL(&s_slideshow_timing_mux);
    s_slideshow_last_display_start_valid = true;
    s_slideshow_last_display_interval = interval;
    s_slideshow_last_display_start_tick = start_tick;
    portEXIT_CRITICAL(&s_slideshow_timing_mux);
}

static uint32_t slideshow_get_initial_delay_seconds(uint32_t interval)
{
    bool valid = false;
    uint32_t last_interval = 0;
    TickType_t start_tick = 0;

    if (interval < TDX_SLIDESHOW_INTERVAL_MIN_SECONDS) {
        interval = TDX_SLIDESHOW_INTERVAL_MIN_SECONDS;
    }

    portENTER_CRITICAL(&s_slideshow_timing_mux);
    valid = s_slideshow_last_display_start_valid;
    last_interval = s_slideshow_last_display_interval;
    start_tick = s_slideshow_last_display_start_tick;
    portEXIT_CRITICAL(&s_slideshow_timing_mux);

    if (!valid || start_tick == 0 || last_interval == 0) {
        return 0;
    }

    TickType_t now = xTaskGetTickCount();
    uint32_t elapsed = (uint32_t)(((now - start_tick) * portTICK_PERIOD_MS) / 1000U);
    if (elapsed >= interval) {
        return 0;
    }
    return interval - elapsed;
}

static void slideshow_task(void *arg)
{
    slideshow_runtime_t *runtime = (slideshow_runtime_t *)arg;
    if (runtime == NULL) {
        s_slideshow_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "slideshow task start count=%u interval=%lu random=%d start_index=%u pending_index=%u",
             (unsigned int)runtime->request.file_count,
             (unsigned long)runtime->request.interval,
             runtime->request.random ? 1 : 0,
             (unsigned int)runtime->request.start_index,
             (unsigned int)runtime->progress.order[runtime->progress.position]);

    slideshow_loaded_file_t loaded = {0};
    if (!runtime->rtc_enabled && runtime->initial_delay_seconds > 0) {
        TickType_t initial_start_tick = xTaskGetTickCount();
        ESP_LOGI(TAG, "slideshow initial delay seconds=%lu",
                 (unsigned long)runtime->initial_delay_seconds);
        slideshow_begin_interval(runtime->initial_delay_seconds, initial_start_tick);
        if (runtime->request.file_count > 0 && !s_slideshow_stop) {
            esp_err_t preload_ret = slideshow_load_file(runtime->base_path,
                                                       runtime->progress.pending_file,
                                                       &loaded,
                                                       false);
            if (preload_ret != ESP_OK) {
                ESP_LOGW(TAG, "slideshow preload initial failed file=%s ret=%s",
                         runtime->progress.pending_file,
                         esp_err_to_name(preload_ret));
            }
            slideshow_update_preload_failure(runtime,
                                             runtime->progress.pending_file,
                                             preload_ret);
        }
        (void)wait_slideshow_interval_from_start(initial_start_tick,
                                                 runtime->initial_delay_seconds);
    }

    while (!s_slideshow_stop && runtime->request.file_count > 0) {
        if (!slideshow_enable_sntp_schedule_if_ready(runtime)) {
            break;
        }
        if (!slideshow_sync_sntp_target_to_now(runtime, "before_load")) {
            break;
        }
        const char *file_name = runtime->progress.pending_file;
        char attempted_file[TDX_SLIDESHOW_FILE_NAME_MAX_LEN] = {0};
        strlcpy(attempted_file, file_name, sizeof(attempted_file));
        TickType_t display_start_tick = 0;
        esp_err_t event_ret = ESP_OK;

        if (slideshow_take_preload_failure(runtime, file_name, &event_ret)) {
            slideshow_loaded_file_free(&loaded);
        } else if (!slideshow_loaded_file_matches(&loaded, file_name)) {
            event_ret = slideshow_load_file(runtime->base_path, file_name, &loaded, true);
            if (event_ret != ESP_OK) {
                ESP_LOGW(TAG, "slideshow preload current failed file=%s ret=%s",
                         file_name,
                         esp_err_to_name(event_ret));
            }
        }

        if (runtime->rtc_enabled) {
            slideshow_rtc_wait_result_t wait_result =
                wait_slideshow_rtc_epoch(runtime->next_epoch,
                                         runtime->request.interval,
                                         !runtime->sntp_schedule_enabled);
            if (wait_result == SLIDESHOW_RTC_WAIT_STOPPED) {
                break;
            }
            if (wait_result == SLIDESHOW_RTC_WAIT_SNTP_READY) {
                if (event_ret != ESP_OK) {
                    slideshow_update_preload_failure(runtime,
                                                     attempted_file,
                                                     event_ret);
                }
                if (!slideshow_enable_sntp_schedule_if_ready(runtime)) {
                    break;
                }
                continue;
            }
            if (!slideshow_sync_sntp_target_to_now(runtime, "before_display")) {
                break;
            }
            file_name = runtime->progress.pending_file;
            if (strcmp(file_name, attempted_file) != 0) {
                strlcpy(attempted_file, file_name, sizeof(attempted_file));
                if (slideshow_take_preload_failure(runtime, file_name, &event_ret)) {
                    slideshow_loaded_file_free(&loaded);
                } else {
                    event_ret = slideshow_loaded_file_matches(&loaded, file_name) ?
                                ESP_OK :
                                slideshow_load_file(runtime->base_path, file_name, &loaded, true);
                }
                if (event_ret != ESP_OK) {
                    ESP_LOGW(TAG, "slideshow reload current failed file=%s ret=%s",
                             file_name,
                             esp_err_to_name(event_ret));
                }
            }
        }

        if (s_slideshow_stop) {
            break;
        }
        char consumed_file[TDX_SLIDESHOW_FILE_NAME_MAX_LEN] = {0};
        strlcpy(consumed_file, file_name, sizeof(consumed_file));
        if (event_ret == ESP_OK && slideshow_loaded_file_matches(&loaded, file_name)) {
            event_ret = slideshow_display_loaded_file_and_wait(&loaded,
                                                                runtime->request.interval,
                                                                runtime->rtc_enabled,
                                                                runtime->progress.position,
                                                                runtime->request.file_count,
                                                                &display_start_tick);
        } else {
            slideshow_loaded_file_free(&loaded);
            if (event_ret == ESP_OK) {
                event_ret = ESP_ERR_INVALID_STATE;
            }
        }
        if (display_start_tick == 0) {
            display_start_tick = xTaskGetTickCount();
            if (!runtime->rtc_enabled) {
                slideshow_begin_interval(runtime->request.interval, display_start_tick);
            }
        }

        slideshow_progress_t next;
        uint64_t consumed_slot = runtime->scheduled_slot;
        uint64_t next_slot = 0;
        int64_t sntp_next_epoch = 0;
        bool next_ready = true;
        if (runtime->sntp_schedule_enabled) {
            next_ready = slideshow_prepare_next_sntp_progress(runtime,
                                                              &next,
                                                              &next_slot,
                                                              &sntp_next_epoch);
            if (!next_ready && runtime->scheduled_slot < UINT64_MAX) {
                next_slot = runtime->scheduled_slot + 1U;
                size_t next_index =
                    (runtime->request.start_index +
                     (size_t)(next_slot % (uint64_t)runtime->request.file_count)) %
                    runtime->request.file_count;
                memcpy(&next, &runtime->progress, sizeof(next));
                next_ready = slideshow_progress_select_index(&runtime->request,
                                                              &next,
                                                              next_index) &&
                             slideshow_epoch_for_slot(&runtime->request,
                                                       next_slot,
                                                       &sntp_next_epoch);
                if (next_ready) {
                    ESP_LOGW(TAG,
                             "slideshow next SNTP progress recovered sequentially file=%s next=%s next_slot=%llu",
                             consumed_file,
                             next.pending_file,
                             (unsigned long long)next_slot);
                }
            }
        } else {
            prepare_next_slideshow_progress(&runtime->request, &runtime->progress, &next);
            next_ready = slideshow_progress_valid(&runtime->request, &next);
        }
        if (!next_ready) {
            ESP_LOGE(TAG,
                     "slideshow event consumed but next progress invalid file=%s result=%s, stop",
                     consumed_file,
                     esp_err_to_name(event_ret));
            break;
        }

        esp_err_t save_ret = save_slideshow_progress(&next);
        memcpy(&runtime->progress, &next, sizeof(runtime->progress));
        if (runtime->sntp_schedule_enabled) {
            runtime->consumed_slot = consumed_slot;
            runtime->consumed_slot_valid = true;
            runtime->scheduled_slot = next_slot;
        }
        slideshow_record_display_start(runtime->request.interval, display_start_tick);

        time_t now_time = 0;
        time(&now_time);
        if (runtime->rtc_enabled) {
            runtime->next_epoch = runtime->sntp_schedule_enabled ?
                                  sntp_next_epoch :
                                  slideshow_next_event_epoch(runtime->request.anchor_epoch,
                                                             runtime->request.interval,
                                                             (int64_t)now_time);
            slideshow_begin_rtc_interval(runtime->request.interval, runtime->next_epoch);
        }

        if (event_ret != ESP_OK || save_ret != ESP_OK) {
            ESP_LOGW(TAG,
                     "slideshow event consumed file=%s next=%s result=%s progress_save=%s sntp=%d consumed_slot=%llu next_slot=%llu next_epoch=%lld",
                     consumed_file,
                     runtime->progress.pending_file,
                     esp_err_to_name(event_ret),
                     esp_err_to_name(save_ret),
                     runtime->sntp_schedule_enabled ? 1 : 0,
                     (unsigned long long)consumed_slot,
                     (unsigned long long)next_slot,
                     (long long)runtime->next_epoch);
        } else if (runtime->rtc_enabled) {
            char now_text[32] = {0};
            char next_text[32] = {0};
            format_epoch_local((int64_t)now_time, now_text, sizeof(now_text));
            format_epoch_local(runtime->next_epoch, next_text, sizeof(next_text));
            ESP_LOGI(TAG,
                     "slideshow rtc next file=%s now=%lld(%s) next=%lld(%s) interval=%lu",
                     runtime->progress.pending_file,
                     (long long)now_time,
                     now_text,
                     (long long)runtime->next_epoch,
                     next_text,
                     (unsigned long)runtime->request.interval);
        }

        if (!s_slideshow_stop) {
            esp_err_t preload_ret = slideshow_load_file(runtime->base_path,
                                                       runtime->progress.pending_file,
                                                       &loaded,
                                                       false);
            if (preload_ret != ESP_OK) {
                ESP_LOGW(TAG, "slideshow preload next failed file=%s ret=%s",
                         runtime->progress.pending_file,
                         esp_err_to_name(preload_ret));
            }
            slideshow_update_preload_failure(runtime,
                                             runtime->progress.pending_file,
                                             preload_ret);
        }

        if (!runtime->rtc_enabled) {
            (void)wait_slideshow_interval_from_start(display_start_tick,
                                                     runtime->request.interval);
        }
    }

    ESP_LOGI(TAG, "slideshow task stop");
    portENTER_CRITICAL(&s_slideshow_timing_mux);
    s_slideshow_interval_active = false;
    s_slideshow_runtime_interval = 0;
    s_slideshow_interval_start_tick = 0;
    s_slideshow_runtime_next_epoch = 0;
    portEXIT_CRITICAL(&s_slideshow_timing_mux);
    slideshow_loaded_file_free(&loaded);
    free(runtime);
    s_slideshow_task = NULL;
    vTaskDelete(NULL);
}

void ServerNetworkStaSlideshow_Stop(void)
{
    s_slideshow_stop = true;
}

static esp_err_t start_slideshow_runtime(const char *base_path,
                                         const slideshow_request_t *request,
                                         const slideshow_progress_t *progress,
                                         bool reset_interval_if_running,
                                         bool force_first_display,
                                         bool sntp_schedule_enabled,
                                         uint64_t scheduled_slot,
                                         bool current_slot_already_displayed)
{
    if (base_path == NULL || request == NULL || progress == NULL ||
        request->file_count == 0 || !slideshow_progress_valid(request, progress)) {
        return ESP_ERR_INVALID_ARG;
    }

    bool was_running = s_slideshow_task != NULL;
    ServerNetworkStaSlideshow_Stop();
    TickType_t stop_start = xTaskGetTickCount();
    TickType_t stop_timeout = pdMS_TO_TICKS(USER_EPD_DISPLAY_WAIT_TIMEOUT_MS + 5000U);
    while (s_slideshow_task != NULL &&
           (xTaskGetTickCount() - stop_start) < stop_timeout) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (s_slideshow_task != NULL) {
        ESP_LOGE(TAG, "previous slideshow task did not stop");
        return ESP_ERR_TIMEOUT;
    }

    slideshow_runtime_t *runtime = (slideshow_runtime_t *)calloc(1, sizeof(*runtime));
    if (runtime == NULL) {
        return ESP_ERR_NO_MEM;
    }

    strlcpy(runtime->base_path, base_path, sizeof(runtime->base_path));
    memcpy(&runtime->request, request, sizeof(runtime->request));
    memcpy(&runtime->progress, progress, sizeof(runtime->progress));
    runtime->force_first_display = force_first_display;
    runtime->sntp_schedule_enabled = sntp_schedule_enabled;
    runtime->scheduled_slot = scheduled_slot;
    runtime->consumed_slot_valid = current_slot_already_displayed;
    if (current_slot_already_displayed && scheduled_slot > 0) {
        runtime->consumed_slot = scheduled_slot - 1U;
    }
    if (request->anchor_epoch > 0) {
        time_t now_time = 0;
        time(&now_time);
        runtime->rtc_enabled = true;
        runtime->next_epoch = slideshow_next_epoch(request->anchor_epoch,
                                                   request->interval,
                                                   (int64_t)now_time);
        if (current_slot_already_displayed) {
            if (!slideshow_epoch_for_slot(request,
                                          scheduled_slot,
                                          &runtime->next_epoch)) {
                free(runtime);
                return ESP_ERR_INVALID_ARG;
            }
        } else if (runtime->force_first_display) {
            runtime->next_epoch = (int64_t)now_time;
        }
        char anchor_text[32] = {0};
        char now_text[32] = {0};
        char next_text[32] = {0};
        format_epoch_local(request->anchor_epoch, anchor_text, sizeof(anchor_text));
        format_epoch_local((int64_t)now_time, now_text, sizeof(now_text));
        format_epoch_local(runtime->next_epoch, next_text, sizeof(next_text));
        ESP_LOGI(TAG,
                 "slideshow rtc schedule timestamp=%lld anchor=%lld(%s) now=%lld(%s) next=%lld(%s) interval=%lu force_first=%d sntp_slots=%d scheduled_slot=%llu",
                 (long long)request->timestamp,
                 (long long)request->anchor_epoch,
                 anchor_text,
                 (long long)now_time,
                 now_text,
                 (long long)runtime->next_epoch,
                 next_text,
                 (unsigned long)request->interval,
                 runtime->force_first_display ? 1 : 0,
                 runtime->sntp_schedule_enabled ? 1 : 0,
                 (unsigned long long)runtime->scheduled_slot);
    } else if (reset_interval_if_running && was_running) {
        runtime->initial_delay_seconds = request->interval;
        ESP_LOGI(TAG, "slideshow interval reset new=%lu initial_delay=%lu",
                 (unsigned long)request->interval,
                 (unsigned long)runtime->initial_delay_seconds);
    } else {
        runtime->initial_delay_seconds = slideshow_get_initial_delay_seconds(request->interval);
    }
    s_slideshow_stop = false;

    BaseType_t task_ret = xTaskCreate(slideshow_task,
                                      "slideshow",
                                      SLIDESHOW_TASK_STACK_SIZE,
                                      runtime,
                                      SLIDESHOW_TASK_PRIORITY,
                                      &s_slideshow_task);
    if (task_ret != pdPASS) {
        free(runtime);
        s_slideshow_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t ServerNetworkStaSlideshow_ShowFirst(const char *base_path)
{
    slideshow_request_t *request = (slideshow_request_t *)calloc(1, sizeof(*request));
    if (request == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = read_slideshow_config_file(base_path, request);
    if (ret != ESP_OK) {
        free(request);
        return ret;
    }
    ret = display_slideshow_file_and_wait(base_path,
                                          request->file_names[request->start_index],
                                          0,
                                          NULL);
    free(request);
    return ret;
}

static esp_err_t start_saved_slideshow_with_mode(const char *base_path,
                                                 bool reset_interval_if_running,
                                                 bool force_first_display,
                                                 bool reset_progress)
{
    slideshow_request_t *request = (slideshow_request_t *)calloc(1, sizeof(*request));
    if (request == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = read_slideshow_config_file(base_path, request);
    if (ret != ESP_OK) {
        free(request);
        return ret;
    }

    uint32_t interval = request->interval;
    bool random = request->random;
    int64_t timestamp = 0;
    int64_t anchor_epoch = 0;
    if (!read_slideshow_control_schedule(base_path,
                                         &interval,
                                         &random,
                                         &timestamp,
                                         &anchor_epoch)) {
        ServerNetworkStaSlideshow_Stop();
        free(request);
        return ESP_ERR_INVALID_STATE;
    }
    if (interval >= TDX_SLIDESHOW_INTERVAL_MIN_SECONDS &&
        interval <= TDX_SLIDESHOW_INTERVAL_MAX_SECONDS) {
        request->interval = interval;
    }
    request->random = random;
    request->timestamp = timestamp;
    request->anchor_epoch = anchor_epoch;
    if (!ServerNetworkStaTime_IsUsableForSlideshowRestore()) {
        ESP_LOGW(TAG,
                 "slideshow RTC restore blocked: no usable time source yet");
        free(request);
        return ESP_ERR_INVALID_STATE;
    }
    slideshow_progress_t progress;
    bool progress_loaded_existing = false;
    ret = load_or_create_slideshow_progress(request,
                                            reset_progress,
                                            &progress,
                                            &progress_loaded_existing);
    bool sntp_schedule_enabled = false;
    uint64_t scheduled_slot = 0;
    bool current_slot_already_displayed = false;
    bool sntp_synced = ServerNetworkStaTime_IsSntpSynced();
    if (ret != ESP_OK && sntp_synced) {
        slideshow_init_progress(request, &progress);
        ESP_LOGW(TAG,
                 "slideshow SNTP restore continue with RAM progress after NVS load/save failed ret=%s",
                 esp_err_to_name(ret));
        ret = ESP_OK;
    }
    if (ret == ESP_OK && sntp_synced) {
        time_t now_time = 0;
        time(&now_time);
        slideshow_schedule_position_t schedule = {0};
        if (!slideshow_calculate_schedule_position(request->anchor_epoch,
                                                    request->interval,
                                                    request->file_count,
                                                    request->start_index,
                                                    (int64_t)now_time,
                                                    &schedule)) {
            ESP_LOGE(TAG, "slideshow SNTP restore position calculation failed now=%lld",
                     (long long)now_time);
            ret = ESP_FAIL;
        } else {
            size_t expected_next_index =
                (schedule.current_index + 1U) % request->file_count;
            bool may_use_saved_progress = !reset_interval_if_running &&
                                          !force_first_display &&
                                          progress_loaded_existing &&
                                          schedule.started;
            current_slot_already_displayed = may_use_saved_progress &&
                strcmp(progress.pending_file,
                       request->file_names[expected_next_index]) == 0;
            size_t selected_index = current_slot_already_displayed ?
                                    expected_next_index : schedule.current_index;
            if (!slideshow_progress_select_index(request, &progress, selected_index)) {
                ESP_LOGE(TAG,
                         "slideshow SNTP restore progress select failed index=%u now=%lld",
                         (unsigned int)selected_index,
                         (long long)now_time);
                ret = ESP_FAIL;
            }
        }
        if (ret == ESP_OK) {
            sntp_schedule_enabled = true;
            scheduled_slot = schedule.slot +
                             (current_slot_already_displayed ? 1U : 0U);
            force_first_display = schedule.started && !current_slot_already_displayed;
            ESP_LOGI(TAG,
                     "slideshow SNTP restore start_index=%u slot=%llu current_index=%u current_file=%s pending_index=%u pending_file=%s now=%lld slot_start=%lld next=%lld current_displayed=%d action=%s",
                     (unsigned int)request->start_index,
                     (unsigned long long)schedule.slot,
                     (unsigned int)schedule.current_index,
                     request->file_names[schedule.current_index],
                     (unsigned int)(current_slot_already_displayed ?
                                    ((schedule.current_index + 1U) % request->file_count) :
                                    schedule.current_index),
                     progress.pending_file,
                     (long long)now_time,
                     (long long)schedule.slot_start_epoch,
                     (long long)schedule.next_epoch,
                     current_slot_already_displayed ? 1 : 0,
                     current_slot_already_displayed ? "wait_next" :
                     (schedule.started ? "display_current" : "wait_anchor"));
        }
    }
    if (ret == ESP_OK) {
        ret = start_slideshow_runtime(base_path,
                                      request,
                                      &progress,
                                      reset_interval_if_running,
                                      force_first_display,
                                      sntp_schedule_enabled,
                                      scheduled_slot,
                                      current_slot_already_displayed);
    }
    free(request);
    return ret;
}

esp_err_t ServerNetworkStaSlideshow_StartSaved(const char *base_path)
{
    return start_saved_slideshow_with_mode(base_path, false, false, false);
}

esp_err_t ServerNetworkStaSlideshow_StartSavedResetInterval(const char *base_path)
{
    return start_saved_slideshow_with_mode(base_path, true, false, false);
}

esp_err_t ServerNetworkStaSlideshow_StartSavedForNewCommand(const char *base_path)
{
    return start_saved_slideshow_with_mode(base_path, true, false, true);
}

static void slideshow_startup_delay_task(void *arg)
{
    slideshow_startup_delay_t *delay = (slideshow_startup_delay_t *)arg;
    uint32_t time_wait_seconds = 0;
    bool time_get_requested = false;
    if (delay == NULL) {
        s_slideshow_startup_delay_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "slideshow startup delay %u ms",
             (unsigned int)TDX_SLIDESHOW_STARTUP_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(TDX_SLIDESHOW_STARTUP_DELAY_MS));

    while (true) {
        uint32_t interval = 0;
        bool random = false;
        int64_t timestamp = 0;
        int64_t anchor_epoch = 0;
        bool enabled = read_slideshow_control_schedule(delay->base_path,
                                                       &interval,
                                                       &random,
                                                       &timestamp,
                                                       &anchor_epoch);
        if (!enabled) {
            ESP_LOGI(TAG, "slideshow startup skipped because control sw=0");
            break;
        }
        if (s_slideshow_task != NULL) {
            ESP_LOGI(TAG, "slideshow startup skipped because slideshow already running");
            break;
        }
        if (ServerNetworkStaEpdDisplay_IsBusy()) {
            ESP_LOGI(TAG, "slideshow startup postponed because EPD busy");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (!ServerNetworkStaTime_IsUsableForSlideshowRestore()) {
            if (!time_get_requested) {
                (void)ServerNetworkStaTime_RequestCh583Backup();                
                time_get_requested = true;
            }
            if (time_wait_seconds >= TDX_SLIDESHOW_STARTUP_TIME_FALLBACK_WAIT_SECONDS) {
                esp_err_t time_ret = ServerNetworkStaTime_SetSlideshowAnchorFallback(anchor_epoch);
                if (time_ret == ESP_OK) {
                    ESP_LOGW(TAG,
                             "slideshow startup fallback to anchor_epoch=%lld after waiting %lu seconds",
                             (long long)anchor_epoch,
                             (unsigned long)time_wait_seconds);
                } else {
                    ESP_LOGW(TAG,
                             "slideshow startup anchor fallback failed anchor=%lld ret=%s",
                             (long long)anchor_epoch,
                             esp_err_to_name(time_ret));
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    time_wait_seconds++;
                    continue;
                }
            } else {
                if (time_wait_seconds == 0 ||
                    (TDX_SLIDESHOW_STARTUP_TIME_WAIT_LOG_SECONDS > 0 &&
                     (time_wait_seconds % TDX_SLIDESHOW_STARTUP_TIME_WAIT_LOG_SECONDS) == 0U)) {
                    ESP_LOGI(TAG,
                             "slideshow startup waiting usable RTC time source wait=%lu fallback_after=%u",
                             (unsigned long)time_wait_seconds,
                             (unsigned int)TDX_SLIDESHOW_STARTUP_TIME_FALLBACK_WAIT_SECONDS);
                }
                time_wait_seconds++;
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
        }

        bool wifi_has_ip = false;
        bool sntp_synced = false;
        bool force_first_display =
            slideshow_should_force_first_display_for_stale_time(&wifi_has_ip, &sntp_synced);

        ESP_LOGI(TAG, "slideshow startup delay done, start saved slideshow interval=%lu random=%d force_first=%d wifi_ip=%d sntp=%d",
                 (unsigned long)interval,
                 random ? 1 : 0,
                 force_first_display ? 1 : 0,
                 wifi_has_ip ? 1 : 0,
                 sntp_synced ? 1 : 0);
        esp_err_t ret = start_saved_slideshow_with_mode(delay->base_path,
                                                        false,
                                                        force_first_display,
                                                        false);
        ESP_LOGI(TAG, "slideshow startup delayed start ret=%s", esp_err_to_name(ret));
        break;
    }

    free(delay);
    s_slideshow_startup_delay_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t ServerNetworkStaSlideshow_StartSavedDelayed(const char *base_path)
{
    if (base_path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_slideshow_startup_delay_task != NULL) {
        ESP_LOGW(TAG, "slideshow startup delay already scheduled");
        return ESP_OK;
    }

    slideshow_startup_delay_t *delay = (slideshow_startup_delay_t *)calloc(1, sizeof(*delay));
    if (delay == NULL) {
        return ESP_ERR_NO_MEM;
    }
    strlcpy(delay->base_path, base_path, sizeof(delay->base_path));

    BaseType_t task_ret = xTaskCreate(slideshow_startup_delay_task,
                                      "slide_start_delay",
                                      SLIDESHOW_TASK_STACK_SIZE / 2,
                                      delay,
                                      SLIDESHOW_TASK_PRIORITY,
                                      &s_slideshow_startup_delay_task);
    if (task_ret != pdPASS) {
        free(delay);
        s_slideshow_startup_delay_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
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
                       "],\"interval\":%lu,\"random\":%s,\"startIndex\":%u}",
                       (unsigned long)request->interval,
                       request->random ? "true" : "false",
                       (unsigned int)request->start_index);
    if (written < 0 || used + (size_t)written >= SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX) {
        free(json);
        return ESP_FAIL;
    }

    esp_err_t ret = write_text_file(path, json);
    free(json);
    return ret;
}

static esp_err_t parse_start_slideshow_request(const char *body, slideshow_request_t *request)
{
    memset(request, 0, sizeof(*request));
    if (!json_func_equals(body, "start_slideshow")) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (find_json_key(body, "fileNames") == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (!parse_file_names(body, request)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (find_json_key(body, "startIndex") == NULL) {
        ESP_LOGE(TAG,
                 "start_slideshow rejected: startIndex missing count=%u",
                 (unsigned int)request->file_count);
        return TDX_JSON_RESULT_SLIDESHOW_START_INDEX_MISSING;
    }
    uint32_t parsed_start_index = 0;
    if (!parse_json_u32(body, "startIndex", &parsed_start_index) ||
        parsed_start_index >= request->file_count) {
        ESP_LOGE(TAG,
                 "start_slideshow rejected: invalid startIndex=%lu count=%u",
                 (unsigned long)parsed_start_index,
                 (unsigned int)request->file_count);
        return TDX_JSON_RESULT_SLIDESHOW_START_INDEX_INVALID;
    }
    request->start_index = (size_t)parsed_start_index;
    if (!parse_json_u32(body, "interval", &request->interval) ||
        request->interval < TDX_SLIDESHOW_INTERVAL_MIN_SECONDS ||
        request->interval > TDX_SLIDESHOW_INTERVAL_MAX_SECONDS) {
        return ESP_ERR_INVALID_SIZE;
    }
    request->random = slideshow_force_random_config("start_slideshow",
                                                    parse_json_bool_default(body, "random", false));
    if (!parse_json_i64(body, "timestamp", &request->timestamp)) {
        return TDX_JSON_RESULT_SLIDESHOW_TIMESTAMP_INVALID;
    }
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
    if (ret == ESP_ERR_NOT_FOUND) {
        return send_start_slideshow_result(req, TDX_JSON_RESULT_FILE_NAMES_MISSING, "fileNames missing");
    }
    if (ret == ESP_ERR_INVALID_ARG) {
        return send_start_slideshow_result(req, TDX_JSON_RESULT_FILE_NAME_INVALID, "invalid fileNames");
    }
    if (ret == ESP_ERR_INVALID_SIZE) {
        return send_start_slideshow_result(req, TDX_JSON_RESULT_SLIDESHOW_INTERVAL_INVALID, "invalid interval");
    }
    if (ret == TDX_JSON_RESULT_SLIDESHOW_START_INDEX_MISSING) {
        return send_start_slideshow_result(req, ret, "startIndex missing");
    }
    if (ret == TDX_JSON_RESULT_SLIDESHOW_START_INDEX_INVALID) {
        return send_start_slideshow_result(req, ret, "invalid startIndex");
    }
    if (ret == TDX_JSON_RESULT_SLIDESHOW_TIMESTAMP_INVALID) {
        if (ServerNetworkStaTime_IsSntpSynced()) {
            (void)ServerNetworkStaTime_BackupCurrentToCh583("start_slideshow_bad_timestamp_sntp_now");
        }
        return send_start_slideshow_result(req, TDX_JSON_RESULT_SLIDESHOW_TIMESTAMP_INVALID, "invalid timestamp");
    }
    if (ret != ESP_OK) {
        return send_start_slideshow_result(req, TDX_JSON_RESULT_JSON_INVALID, "start slideshow failed");
    }

    ret = start_slideshow_apply_timestamp(&request);
    if (ret == TDX_JSON_RESULT_SLIDESHOW_TIMESTAMP_INVALID) {
        return send_start_slideshow_result(req, TDX_JSON_RESULT_SLIDESHOW_TIMESTAMP_INVALID, "invalid timestamp");
    }
    if (ret == TDX_JSON_RESULT_SLIDESHOW_TIME_DIFF_TOO_LARGE) {
        return send_start_slideshow_result(req,
                                           TDX_JSON_RESULT_SLIDESHOW_TIME_DIFF_TOO_LARGE,
                                           "timestamp differs from SNTP time");
    }
    if (ret == TDX_JSON_RESULT_SLIDESHOW_TIME_SET_FAILED) {
        return send_start_slideshow_result(req,
                                           TDX_JSON_RESULT_SLIDESHOW_TIME_SET_FAILED,
                                           "timestamp set rtc failed");
    }
    if (ret != ESP_OK) {
        return send_start_slideshow_result(req, TDX_JSON_RESULT_JSON_INVALID, "start slideshow failed");
    }

    char bin_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    if (ensure_bin_dir(base_path, bin_dir, sizeof(bin_dir)) != ESP_OK) {
        return send_start_slideshow_result(req, TDX_JSON_RESULT_STORAGE_NOT_READY, "sd card not ready");
    }
    if (check_slideshow_files_exist(bin_dir, &request) != ESP_OK) {
        return send_start_slideshow_result(req, TDX_JSON_RESULT_SLIDESHOW_FILE_NOT_FOUND, "file not found");
    }
    if (save_slideshow_config(bin_dir, &request) != ESP_OK) {
        return send_start_slideshow_result(req, TDX_JSON_RESULT_SLIDESHOW_CONFIG_SAVE_FAILED, "save config failed");
    }
    if (write_slideshow_rtc_control(bin_dir, &request) != ESP_OK) {
        return send_start_slideshow_result(req,
                                           TDX_JSON_RESULT_SLIDESHOW_CONTROL_SAVE_FAILED,
                                           "save slideshow control failed");
    }

    esp_err_t random_save_ret = app_nvs_write_str(TDX_SLIDESHOW_RANDOM_NVS_KEY,
                                                  request.random ? "true" : "false");
    g_slideshow_random_enable = request.random ? 1 : 0;
    ESP_LOGI(TAG,
             "start_slideshow saved list count=%u start_index=%u first_file=%s rtc_interval=%lu random=%d rtc_timestamp=%lld rtc_anchor=%lld random_save_ret=%s",
             (unsigned int)request.file_count,
             (unsigned int)request.start_index,
             request.file_names[request.start_index],
             (unsigned long)request.interval,
             request.random ? 1 : 0,
             (long long)request.timestamp,
             (long long)request.anchor_epoch,
             esp_err_to_name(random_save_ret));
    if (random_save_ret != ESP_OK) {
        return send_start_slideshow_result(req,
                                           TDX_JSON_RESULT_SLIDESHOW_CONFIG_SAVE_FAILED,
                                           "save random config failed");
    }

    esp_err_t mode_ret = EpdDisplayMode_SetBySlideshowSwitch(true);
    if (mode_ret != ESP_OK) {
        return send_start_slideshow_result(req,
                                           TDX_JSON_RESULT_SLIDESHOW_CONFIG_SAVE_FAILED,
                                           "save display mode failed");
    }

    esp_err_t start_ret = ServerNetworkStaSlideshow_StartSavedForNewCommand(base_path);
    if (start_ret != ESP_OK) {
        ESP_LOGW(TAG, "start_slideshow rtc runtime start failed ret=%s",
                 esp_err_to_name(start_ret));
        return send_start_slideshow_result(req,
                                           TDX_JSON_RESULT_SLIDESHOW_RUNTIME_FAILED,
                                           "start slideshow runtime failed");
    }
    ESP_LOGI(TAG,
             "start_slideshow saved list and started rtc slideshow interval=%lu random=%d start_index=%u timestamp=%lld anchor=%lld",
             (unsigned long)request.interval,
             request.random ? 1 : 0,
             (unsigned int)request.start_index,
             (long long)request.timestamp,
             (long long)request.anchor_epoch);

    return send_start_slideshow_result(req, TDX_JSON_RESULT_OK, NULL);
}
