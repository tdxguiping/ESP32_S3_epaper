#include "server_network_sta_slideshow.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "nvs.h"
#include "file_serving_example_common.h"
#include "epd_display_app.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tdx_cfg.h"

static const char *TAG = "server_sta_slide";
#define SLIDESHOW_TASK_STACK_SIZE (12 * 1024)
#define SLIDESHOW_TASK_PRIORITY 4
#define SLIDESHOW_PROGRESS_MAGIC 0x534C4450UL
#define SLIDESHOW_PROGRESS_VERSION 1U
#define SLIDESHOW_PROGRESS_SAVE_RETRIES 3

typedef struct {
    char file_names[TDX_SLIDESHOW_MAX_FILES][TDX_SLIDESHOW_FILE_NAME_MAX_LEN];
    size_t file_count;
    uint32_t interval;
    bool random;
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
    char base_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX];
    slideshow_request_t request;
    slideshow_progress_t progress;
} slideshow_runtime_t;

static TaskHandle_t s_slideshow_task = NULL;
static volatile bool s_slideshow_stop = false;

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

static esp_err_t display_slideshow_file_and_wait(const char *base_path, const char *file_name)
{
    char path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + TDX_SLIDESHOW_FILE_NAME_MAX_LEN + 24];
    struct stat st = {0};

    if (base_path == NULL || !file_name_is_safe(file_name)) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(path, sizeof(path), "%s/bin_img/%s.bin", base_path, file_name);
    if (stat(path, &st) != 0 || st.st_size <= 0) {
        ESP_LOGE(TAG, "slideshow file missing path=%s", path);
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t *buf = (uint8_t *)heap_caps_malloc((size_t)st.st_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        buf = (uint8_t *)heap_caps_malloc((size_t)st.st_size, MALLOC_CAP_8BIT);
    }
    if (buf == NULL) {
        ESP_LOGE(TAG, "slideshow file alloc failed path=%s size=%u",
                 path, (unsigned int)st.st_size);
        return ESP_ERR_NO_MEM;
    }

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        heap_caps_free(buf);
        ESP_LOGE(TAG, "slideshow file open failed path=%s errno=%d", path, errno);
        return ESP_FAIL;
    }

    size_t read_len = fread(buf, 1, (size_t)st.st_size, fp);
    fclose(fp);
    if (read_len != (size_t)st.st_size) {
        heap_caps_free(buf);
        ESP_LOGE(TAG, "slideshow file read failed path=%s expect=%u actual=%u",
                 path, (unsigned int)st.st_size, (unsigned int)read_len);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "slideshow display start file=%s size=%u",
             file_name, (unsigned int)read_len);
    esp_err_t ret = ServerNetworkStaEpdDisplay_QueueToScreenAndWait(buf, read_len, 1);
    heap_caps_free(buf);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "slideshow display done file=%s", file_name);
    } else {
        ESP_LOGW(TAG, "slideshow display failed file=%s ret=%s, progress unchanged",
                 file_name, esp_err_to_name(ret));
    }
    return ret;
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

static void slideshow_build_order(slideshow_progress_t *progress, size_t file_count, bool random)
{
    progress->order_count = (uint8_t)file_count;
    for (size_t i = 0; i < file_count; ++i) {
        progress->order[i] = (uint8_t)i;
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

static bool find_file_index(const slideshow_request_t *request,
                            const char *file_name,
                            size_t *index_out)
{
    if (request == NULL || file_name == NULL || index_out == NULL) {
        return false;
    }
    for (size_t i = 0; i < request->file_count; ++i) {
        if (strcmp(request->file_names[i], file_name) == 0) {
            *index_out = i;
            return true;
        }
    }
    return false;
}

static void slideshow_init_progress(const slideshow_request_t *request,
                                    size_t first_index,
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
    slideshow_build_order(progress, request->file_count, request->random);

    if (first_index >= request->file_count) {
        first_index = 0;
    }
    for (size_t i = 0; i < request->file_count; ++i) {
        if (progress->order[i] == first_index) {
            uint8_t tmp = progress->order[0];
            progress->order[0] = progress->order[i];
            progress->order[i] = tmp;
            break;
        }
    }
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
                                                   slideshow_progress_t *progress)
{
    esp_err_t progress_read_ret = ESP_ERR_NVS_NOT_FOUND;
    if (!reset) {
        progress_read_ret = app_nvs_read_blob(TDX_SLIDESHOW_NVS_PROGRESS_KEY,
                                              progress,
                                              sizeof(*progress));
        if (progress_read_ret == ESP_OK && slideshow_progress_valid(request, progress)) {
            ESP_LOGI(TAG, "slideshow resume pending=%s position=%u/%u",
                     progress->pending_file,
                     (unsigned int)progress->position,
                     (unsigned int)progress->order_count);
            return ESP_OK;
        }
    }

    size_t first_index = 0;
    if (!reset && progress_read_ret == ESP_ERR_NVS_NOT_FOUND) {
        char legacy_file[TDX_SLIDESHOW_FILE_NAME_MAX_LEN] = {0};
        size_t legacy_index = 0;
        if (app_nvs_read_str(TDX_SLIDESHOW_NVS_LAST_FILE_KEY,
                             legacy_file,
                             sizeof(legacy_file),
                             "") == ESP_OK &&
            find_file_index(request, legacy_file, &legacy_index)) {
            first_index = legacy_index;
            ESP_LOGI(TAG, "slideshow migrate legacy pending=%s", legacy_file);
        }
    }

    slideshow_init_progress(request, first_index, progress);
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
        slideshow_build_order(next, request->file_count, request->random);
    }
    strlcpy(next->pending_file,
            request->file_names[next->order[next->position]],
            sizeof(next->pending_file));
}

static void wait_slideshow_interval_seconds(uint32_t interval)
{
    if (interval < TDX_SLIDESHOW_INTERVAL_MIN_SECONDS) {
        interval = TDX_SLIDESHOW_INTERVAL_MIN_SECONDS;
    }

    TickType_t last_wake_tick = xTaskGetTickCount();
    const TickType_t one_second_ticks = pdMS_TO_TICKS(1000);
    for (uint32_t elapsed_s = 0; elapsed_s < interval && !s_slideshow_stop; elapsed_s++) {
        vTaskDelayUntil(&last_wake_tick, one_second_ticks);
    }
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
    if (stat(config_path, &st) != 0 || st.st_size <= 0) {
        ESP_LOGI(TAG, "slideshow config missing path=%s", config_path);
        return ESP_ERR_NOT_FOUND;
    }

    char *json = (char *)malloc((size_t)st.st_size + 1);
    if (json == NULL) {
        ESP_LOGE(TAG, "slideshow config alloc failed size=%u", (unsigned int)st.st_size);
        return ESP_ERR_NO_MEM;
    }

    FILE *fp = fopen(config_path, "rb");
    if (fp == NULL) {
        free(json);
        ESP_LOGE(TAG, "slideshow config open failed path=%s errno=%d", config_path, errno);
        return ESP_FAIL;
    }

    size_t read_len = fread(json, 1, (size_t)st.st_size, fp);
    fclose(fp);
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
    request->random = parse_json_bool_default(json, "random", false);
    free(json);
    return ESP_OK;
}

static bool read_slideshow_control_on(const char *base_path, uint32_t *interval, bool *random)
{
    char control_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    char buf[256] = {0};
    int sw = 0;

    if (base_path == NULL) {
        return false;
    }

    snprintf(control_path, sizeof(control_path), "%s/bin_img/%s", base_path, TDX_SLIDESHOW_CONTROL_FILE);
    FILE *fp = fopen(control_path, "rb");
    if (fp == NULL) {
        ESP_LOGI(TAG, "slideshow control missing path=%s", control_path);
        return false;
    }

    size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[len] = '\0';

    if (!parse_json_int(buf, "sw", &sw)) {
        ESP_LOGI(TAG, "slideshow control has no sw path=%s json=%s", control_path, buf);
        return false;
    }
    if (interval != NULL) {
        parse_json_u32(buf, "interval", interval);
    }
    if (random != NULL) {
        *random = parse_json_bool_default(buf, "random", *random);
    }
    ESP_LOGI(TAG, "slideshow control read sw=%d interval=%lu random=%d json=%s",
             sw,
             interval != NULL ? (unsigned long)*interval : 0UL,
             random != NULL && *random ? 1 : 0,
             buf);
    return sw == 1;
}

bool ServerNetworkStaSlideshow_IsSavedEnabled(const char *base_path,
                                              uint32_t *interval,
                                              bool *random)
{
    return read_slideshow_control_on(base_path, interval, random);
}

static void slideshow_task(void *arg)
{
    slideshow_runtime_t *runtime = (slideshow_runtime_t *)arg;
    if (runtime == NULL) {
        s_slideshow_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "slideshow task start count=%u interval=%lu random=%d index=%u",
             (unsigned int)runtime->request.file_count,
             (unsigned long)runtime->request.interval,
             runtime->request.random ? 1 : 0,
             (unsigned int)runtime->progress.order[runtime->progress.position]);

    while (!s_slideshow_stop && runtime->request.file_count > 0) {
        const char *file_name = runtime->progress.pending_file;
        esp_err_t display_ret = display_slideshow_file_and_wait(runtime->base_path, file_name);
        if (display_ret == ESP_OK) {
            slideshow_progress_t next;
            prepare_next_slideshow_progress(&runtime->request, &runtime->progress, &next);
            esp_err_t save_ret = save_slideshow_progress(&next);
            if (save_ret == ESP_OK) {
                memcpy(&runtime->progress, &next, sizeof(runtime->progress));
            } else {
                ESP_LOGE(TAG, "slideshow progress unchanged after display file=%s ret=%s",
                         file_name, esp_err_to_name(save_ret));
            }
        }

        wait_slideshow_interval_seconds(runtime->request.interval);
    }

    ESP_LOGI(TAG, "slideshow task stop");
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
                                         const slideshow_progress_t *progress)
{
    if (base_path == NULL || request == NULL || progress == NULL ||
        request->file_count == 0 || !slideshow_progress_valid(request, progress)) {
        return ESP_ERR_INVALID_ARG;
    }

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
    ret = display_slideshow_file_and_wait(base_path, request->file_names[0]);
    free(request);
    return ret;
}

esp_err_t ServerNetworkStaSlideshow_StartSaved(const char *base_path)
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
    if (!read_slideshow_control_on(base_path, &interval, &random)) {
        ServerNetworkStaSlideshow_Stop();
        free(request);
        return ESP_ERR_INVALID_STATE;
    }
    if (interval >= TDX_SLIDESHOW_INTERVAL_MIN_SECONDS &&
        interval <= TDX_SLIDESHOW_INTERVAL_MAX_SECONDS) {
        request->interval = interval;
    }
    request->random = random;
    slideshow_progress_t progress;
    ret = load_or_create_slideshow_progress(request, false, &progress);
    if (ret == ESP_OK) {
        ret = start_slideshow_runtime(base_path, request, &progress);
    }
    free(request);
    return ret;
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
                       "],\"interval\":%lu,\"random\":%s}",
                       (unsigned long)request->interval, request->random ? "true" : "false");
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
    snprintf(json, sizeof(json), "{\"sw\":1,\"interval\":%lu,\"random\":%s,\"run_mode\":%d}",
             (unsigned long)request->interval,
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
    if (find_json_key(body, "fileNames") == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (!parse_file_names(body, request)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!parse_json_u32(body, "interval", &request->interval) ||
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
    if (ret == ESP_ERR_NOT_FOUND) {
        return send_start_slideshow_result(req, TDX_JSON_RESULT_FILE_NAMES_MISSING, "fileNames missing");
    }
    if (ret == ESP_ERR_INVALID_ARG) {
        return send_start_slideshow_result(req, TDX_JSON_RESULT_FILE_NAME_INVALID, "invalid fileNames");
    }
    if (ret == ESP_ERR_INVALID_SIZE) {
        return send_start_slideshow_result(req, TDX_JSON_RESULT_SLIDESHOW_INTERVAL_INVALID, "invalid interval");
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
    if (save_slideshow_config(bin_dir, &request) != ESP_OK ||
        save_slideshow_control(bin_dir, &request) != ESP_OK) {
        return send_start_slideshow_result(req, TDX_JSON_RESULT_SLIDESHOW_CONFIG_SAVE_FAILED, "save config failed");
    }
    esp_err_t random_save_ret = app_nvs_write_str(TDX_SLIDESHOW_RANDOM_NVS_KEY,
                                                  request.random ? "true" : "false");
    g_slideshow_random_enable = request.random ? 1 : 0;
    ESP_LOGI(TAG, "start_slideshow save random=%d ret=%s",
             g_slideshow_random_enable, esp_err_to_name(random_save_ret));
    if (random_save_ret != ESP_OK) {
        return send_start_slideshow_result(req, TDX_JSON_RESULT_SLIDESHOW_CONFIG_SAVE_FAILED,
                                           "save random config failed");
    }

    ESP_LOGI(TAG, "start_slideshow ready count=%u interval=%lu random=%d run_mode=%d",
             (unsigned int)request.file_count,
             (unsigned long)request.interval,
             request.random ? 1 : 0,
             TDX_SLIDESHOW_RUN_MODE);

    slideshow_progress_t progress;
    esp_err_t start_ret = load_or_create_slideshow_progress(&request, true, &progress);
    if (start_ret == ESP_OK) {
        start_ret = start_slideshow_runtime(base_path, &request, &progress);
    }
    if (start_ret != ESP_OK) {
        ESP_LOGW(TAG, "start_slideshow runtime start failed ret=%s", esp_err_to_name(start_ret));
        return send_start_slideshow_result(req,
                                           start_ret == ESP_ERR_NO_MEM ?
                                               TDX_JSON_RESULT_SLIDESHOW_START_FAILED :
                                               TDX_JSON_RESULT_SLIDESHOW_RUNTIME_FAILED,
                                           "start slideshow runtime failed");
    }

    return send_start_slideshow_result(req, TDX_JSON_RESULT_OK, NULL);
}
