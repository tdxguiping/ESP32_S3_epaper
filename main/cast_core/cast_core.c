#include "cast_core.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "epd_display_app.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "file_serving_example_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "server_network_sta_slideshow.h"
#include "tdx_cfg.h"

typedef struct {
    char file_name[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX];
    char base_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX];
    const char *bin_data;
    size_t bin_len;
    const char *image_data;
    size_t image_len;
    bool record_last_cast;
    SemaphoreHandle_t done;
    esp_err_t *ret_out;
    int *result_out;
    char *error_out;
    size_t error_out_size;
} cast_save_job_t;

static const char *TAG = "cast_core";
static QueueHandle_t s_cast_save_queue;
static TaskHandle_t s_cast_save_task;

static uint32_t elapsed_ms_since(int64_t start_us)
{
    return (uint32_t)((esp_timer_get_time() - start_us) / 1000);
}

static void set_result(tdx_cast_core_result_t *result, int code, const char *message, const char *error)
{
    if (result == NULL) {
        return;
    }
    result->result = code;
    snprintf(result->message, sizeof(result->message), "%s", message != NULL ? message : "");
    snprintf(result->error, sizeof(result->error), "%s", error != NULL ? error : "");
}

void TdxCastCore_ResultOk(tdx_cast_core_result_t *result, const char *file_name, const char *message)
{
    if (result == NULL) {
        return;
    }
    set_result(result, TDX_JSON_RESULT_OK, message != NULL ? message : "ok", NULL);
    snprintf(result->file_name, sizeof(result->file_name), "%s", file_name != NULL ? file_name : "");
}

static bool part_text_len(const usb_console_multipart_part_t *part, size_t *len)
{
    if (len == NULL) {
        return false;
    }
    *len = (part != NULL && part->present) ? part->len : 0;
    return true;
}

static esp_err_t ensure_dir(const char *path)
{
    struct stat st = {0};

    if (path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!example_storage_supports_directories()) {
        return ESP_OK;
    }
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? ESP_OK : ESP_FAIL;
    }
    errno = 0;
    if (mkdir(path, 0775) == 0 || errno == EEXIST) {
        return ESP_OK;
    }
    if (errno == ENOTSUP || errno == EOPNOTSUPP) {
        ESP_LOGW(TAG, "mkdir not supported, skip path=%s errno=%d", path, errno);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "mkdir failed path=%s errno=%d", path, errno);
    return ESP_FAIL;
}

static esp_err_t write_file_exact(const char *path, const char *data, size_t len)
{
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "open failed path=%s errno=%d", path, errno);
        return ESP_FAIL;
    }
    void *io_buf = NULL;
#if USB_CONSOLE_FILE_SAVE_STREAM_BUF_SIZE > 0
    io_buf = malloc(USB_CONSOLE_FILE_SAVE_STREAM_BUF_SIZE);
    if (io_buf != NULL) {
        (void)setvbuf(fp, io_buf, _IOFBF, USB_CONSOLE_FILE_SAVE_STREAM_BUF_SIZE);
    }
#endif
    size_t written = fwrite(data, 1, len, fp);
    fclose(fp);
    free(io_buf);
    return written == len ? ESP_OK : ESP_FAIL;
}

static esp_err_t check_save_space(const char *base_path, size_t bin_len, size_t image_len)
{
    uint64_t free_bytes64 = 0;
    esp_err_t info_ret = example_storage_get_free_bytes(base_path, &free_bytes64);
    if (info_ret != ESP_OK) {
        ESP_LOGW(TAG, "storage info failed base=%s ret=%s, continue without space check",
                 base_path, esp_err_to_name(info_ret));
        return ESP_OK;
    }

    size_t free_bytes = (size_t)free_bytes64;
    size_t required_bytes = bin_len + image_len + SERVER_NETWORK_STA_CAST_SAVE_RESERVE_BYTES;
    if (free_bytes < required_bytes) {
        ESP_LOGE(TAG, "not enough storage free=%u required=%u",
                 (unsigned int)free_bytes, (unsigned int)required_bytes);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t save_one_file(const char *dir,
                               const char *file_name,
                               const char *ext,
                               const char *data,
                               size_t len)
{
    int64_t start_us = esp_timer_get_time();
    char path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX + 24];
    char tmp_path[sizeof(path) + 4];
    struct stat st = {0};

    snprintf(path, sizeof(path), "%s/%s%s", dir, file_name, ext);
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    unlink(tmp_path);

    esp_err_t ret = write_file_exact(tmp_path, data, len);
    if (ret != ESP_OK) {
        unlink(tmp_path);
        return ret;
    }
    if (stat(tmp_path, &st) != 0 || (size_t)st.st_size != len) {
        ESP_LOGE(TAG, "temp size mismatch path=%s actual=%u expected=%u",
                 tmp_path, (unsigned int)st.st_size, (unsigned int)len);
        unlink(tmp_path);
        return ESP_FAIL;
    }
    unlink(path);
    if (rename(tmp_path, path) != 0) {
        ESP_LOGE(TAG, "rename failed tmp=%s final=%s errno=%d", tmp_path, path, errno);
        unlink(tmp_path);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "save file path=%s size=%u elapsed_ms=%lu",
             path, (unsigned int)len, (unsigned long)elapsed_ms_since(start_us));
    return ESP_OK;
}

static esp_err_t record_last_cast(const char *base_path, const char *file_name)
{
    int64_t start_us = esp_timer_get_time();
    char record_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    char json[256];

    snprintf(record_path, sizeof(record_path), "%s/bin_img/%s", base_path, SERVER_NETWORK_STA_LAST_CAST_FILE);
    int len = snprintf(json, sizeof(json),
                       "{\"fileName\":\"%s\",\"bin\":\"%s/bin_img/%s.bin\",\"image\":\"%s/jpg_img/%s.jpg\"}",
                       file_name, base_path, file_name, base_path, file_name);
    if (len < 0 || (size_t)len >= sizeof(json)) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t ret = write_file_exact(record_path, json, (size_t)len);
    ESP_LOGI(TAG, "record last cast file=%s ret=%s elapsed_ms=%lu",
             file_name, esp_err_to_name(ret), (unsigned long)elapsed_ms_since(start_us));
    return ret;
}

static void save_task_set_error(cast_save_job_t *job, esp_err_t ret, int result_code, const char *error)
{
    if (job->ret_out != NULL) {
        *job->ret_out = ret;
    }
    if (job->result_out != NULL) {
        *job->result_out = result_code;
    }
    if (job->error_out != NULL && job->error_out_size > 0) {
        snprintf(job->error_out, job->error_out_size, "%s", error != NULL ? error : "");
    }
}

static void CastSaveTask(void *arg)
{
    (void)arg;
    cast_save_job_t job = {0};

    for (;;) {
        if (xQueueReceive(s_cast_save_queue, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        int64_t start_us = esp_timer_get_time();
        char bin_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
        char jpg_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
        snprintf(bin_dir, sizeof(bin_dir), "%s/bin_img", job.base_path);
        snprintf(jpg_dir, sizeof(jpg_dir), "%s/jpg_img", job.base_path);

        ESP_LOGI(TAG, "save task start file=%s bin=%u image=%u",
                 job.file_name, (unsigned int)job.bin_len, (unsigned int)job.image_len);

        if (ensure_dir(bin_dir) != ESP_OK || ensure_dir(jpg_dir) != ESP_OK) {
            save_task_set_error(&job, ESP_ERR_NOT_FOUND, TDX_JSON_RESULT_STORAGE_NOT_READY, "sd_not_ready");
        } else if (check_save_space(job.base_path, job.bin_len, job.image_len) != ESP_OK) {
            save_task_set_error(&job, ESP_ERR_NO_MEM, TDX_JSON_RESULT_STORAGE_NO_SPACE, "storage_not_enough");
        } else if (save_one_file(bin_dir, job.file_name, ".bin", job.bin_data, job.bin_len) != ESP_OK) {
            save_task_set_error(&job, ESP_FAIL, TDX_JSON_RESULT_SAVE_BIN_FAILED, "save_bin_failed");
        } else if (save_one_file(jpg_dir, job.file_name, ".jpg", job.image_data, job.image_len) != ESP_OK) {
            save_task_set_error(&job, ESP_FAIL, TDX_JSON_RESULT_SAVE_IMAGE_FAILED, "save_image_failed");
        } else if (job.record_last_cast && record_last_cast(job.base_path, job.file_name) != ESP_OK) {
            save_task_set_error(&job, ESP_FAIL, TDX_JSON_RESULT_LAST_CAST_SAVE_FAILED, "last_cast_failed");
        } else {
            save_task_set_error(&job, ESP_OK, TDX_JSON_RESULT_OK, "");
        }

        ESP_LOGI(TAG, "save task done file=%s ret=%s total_ms=%lu",
                 job.file_name,
                 job.ret_out != NULL ? esp_err_to_name(*job.ret_out) : "unknown",
                 (unsigned long)elapsed_ms_since(start_us));
        if (job.done != NULL) {
            xSemaphoreGive(job.done);
        }
    }
}

static esp_err_t cast_save_task_init(void)
{
    if (s_cast_save_task != NULL) {
        return ESP_OK;
    }
    if (s_cast_save_queue == NULL) {
        s_cast_save_queue = xQueueCreate(CAST_SAVE_TASK_QUEUE_LENGTH, sizeof(cast_save_job_t));
        if (s_cast_save_queue == NULL) {
            ESP_LOGE(TAG, "create save queue failed");
            return ESP_ERR_NO_MEM;
        }
    }
    BaseType_t ret = xTaskCreate(CastSaveTask,
                                 "cast_save",
                                 CAST_SAVE_TASK_STACK_SIZE,
                                 NULL,
                                 CAST_SAVE_TASK_PRIORITY,
                                 &s_cast_save_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "create save task failed");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "save task started stack=%u priority=%u queue=%u",
             (unsigned int)CAST_SAVE_TASK_STACK_SIZE,
             (unsigned int)CAST_SAVE_TASK_PRIORITY,
             (unsigned int)CAST_SAVE_TASK_QUEUE_LENGTH);
    return ESP_OK;
}

static esp_err_t submit_save_item_and_wait(const tdx_image_transfer_item_t *item,
                                      const char *base_path,
                                      tdx_cast_core_result_t *result)
{
    esp_err_t save_ret = ESP_FAIL;
    int save_result = TDX_JSON_RESULT_SAVE_BIN_FAILED;
    char save_error[64] = {0};
    SemaphoreHandle_t done = NULL;

    esp_err_t init_ret = cast_save_task_init();
    if (init_ret != ESP_OK) {
        set_result(result, TDX_JSON_RESULT_QUEUE_FAILED, "cast failed", "save_queue_failed");
        return init_ret;
    }

    done = xSemaphoreCreateBinary();
    if (done == NULL) {
        set_result(result, TDX_JSON_RESULT_NO_MEMORY, "cast failed", "save_wait_alloc_failed");
        return ESP_ERR_NO_MEM;
    }

    cast_save_job_t job = {
        .bin_data = item->bin_part.data,
        .bin_len = item->bin_part.len,
        .image_data = item->image_part.data,
        .image_len = item->image_part.len,
        .record_last_cast = item->record_last_cast,
        .done = done,
        .ret_out = &save_ret,
        .result_out = &save_result,
        .error_out = save_error,
        .error_out_size = sizeof(save_error),
    };
    snprintf(job.file_name, sizeof(job.file_name), "%s", item->save_name);
    snprintf(job.base_path, sizeof(job.base_path), "%s", base_path);

    if (xQueueSend(s_cast_save_queue, &job, 0) != pdTRUE) {
        vSemaphoreDelete(done);
        set_result(result, TDX_JSON_RESULT_QUEUE_FAILED, "cast failed", "save_queue_full");
        return ESP_ERR_NO_MEM;
    }

    if (xSemaphoreTake(done, portMAX_DELAY) != pdTRUE) {
        vSemaphoreDelete(done);
        set_result(result, TDX_JSON_RESULT_TIMEOUT, "cast failed", "save_wait_failed");
        return ESP_ERR_TIMEOUT;
    }
    vSemaphoreDelete(done);

    if (save_ret != ESP_OK) {
        set_result(result, save_result, "cast failed", save_error);
        return save_ret;
    }
    return ESP_OK;
}

esp_err_t TdxImageTransfer_ProcessItems(const tdx_image_transfer_item_t *items,
                                        size_t item_count,
                                        const char *base_path,
                                        const char *log_prefix,
                                        tdx_cast_core_result_t *result)
{
    int64_t total_start_us = esp_timer_get_time();
    if (items == NULL || item_count == 0 || base_path == NULL || result == NULL) {
        set_result(result, TDX_JSON_RESULT_UPLOAD_INVALID, "image transfer failed", "invalid_arg");
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < item_count; i++) {
        const tdx_image_transfer_item_t *item = &items[i];
        if (!item->show) {
            continue;
        }
        int64_t stage_start_us = esp_timer_get_time();
        uint8_t epd_target = item->epd_target == 0 ? 1 : item->epd_target;
        esp_err_t display_ret = ServerNetworkStaEpdDisplay_QueueToScreen((const uint8_t *)item->bin_part.data,
                                                                         item->bin_part.len,
                                                                         epd_target);
        ESP_LOGI(TAG, "%s display item=%u target=%u ret=%s elapsed_ms=%lu total_ms=%lu",
                 log_prefix != NULL ? log_prefix : "image",
                 (unsigned int)i,
                 (unsigned int)epd_target,
                 esp_err_to_name(display_ret),
                 (unsigned long)elapsed_ms_since(stage_start_us),
                 (unsigned long)elapsed_ms_since(total_start_us));
        if (display_ret != ESP_OK) {
            set_result(result, TDX_JSON_RESULT_DISPLAY_QUEUE_FAILED, "image transfer failed", "display_queue_failed");
            return display_ret;
        }
    }

    for (size_t i = 0; i < item_count; i++) {
        const tdx_image_transfer_item_t *item = &items[i];
        if (!item->save) {
            continue;
        }
        int64_t stage_start_us = esp_timer_get_time();
        esp_err_t save_ret = submit_save_item_and_wait(item, base_path, result);
        ESP_LOGI(TAG, "%s save item=%u name=%s ret=%s elapsed_ms=%lu total_ms=%lu",
                 log_prefix != NULL ? log_prefix : "image",
                 (unsigned int)i,
                 item->save_name,
                 esp_err_to_name(save_ret),
                 (unsigned long)elapsed_ms_since(stage_start_us),
                 (unsigned long)elapsed_ms_since(total_start_us));
        if (save_ret != ESP_OK) {
            return save_ret;
        }
    }

    TdxCastCore_ResultOk(result, items[0].save_name, "ok");
    return ESP_OK;
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

static void read_slideshow_control_values(const char *control_path, uint32_t *interval, bool *random)
{
    FILE *fp = fopen(control_path, "rb");
    if (fp == NULL) {
        return;
    }
    char buf[192] = {0};
    size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[len] = '\0';
    parse_json_u32(buf, "interval", interval);
    parse_json_bool(buf, "random", random);
}

static esp_err_t stop_slideshow_for_cast(const char *base_path)
{
    char control_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    char json[160];
    uint32_t interval = TDX_SLIDESHOW_INTERVAL_MIN_SECONDS;
    bool random = false;

    snprintf(control_path, sizeof(control_path), "%s/bin_img/%s", base_path, TDX_SLIDESHOW_CONTROL_FILE);
    read_slideshow_control_values(control_path, &interval, &random);
    int json_len = snprintf(json, sizeof(json),
                            "{\"sw\":0,\"interval\":%lu,\"random\":%s,\"run_mode\":%d}",
                            (unsigned long)interval,
                            random ? "true" : "false",
                            TDX_SLIDESHOW_RUN_MODE);
    if (json_len < 0 || (size_t)json_len >= sizeof(json)) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t ret = write_file_exact(control_path, json, strlen(json));
    if (ret == ESP_OK) {
        ServerNetworkStaSlideshow_Stop();
    }
    ESP_LOGI(TAG, "stop slideshow ret=%s", esp_err_to_name(ret));
    return ret;
}

esp_err_t TdxImageTransfer_ParseSingle(const char *body,
                                       size_t body_len,
                                       const char *content_type,
                                       const char *expected_func,
                                       bool default_show,
                                       bool require_save,
                                       tdx_cast_core_request_t *cast,
                                       tdx_cast_core_result_t *result)
{
    char boundary[SERVER_NETWORK_STA_OTA_BOUNDARY_MAX] = {0};
    char func[16] = {0};
    const char *part_names[] = {
        "func",
        "fileName",
        "bin_size",
        "image_size",
        "save",
        "show",
        "bin",
        "image",
    };
    usb_console_multipart_part_t parts[sizeof(part_names) / sizeof(part_names[0])] = {0};
    usb_console_multipart_part_t *func_part = &parts[0];
    usb_console_multipart_part_t *file_name_part = &parts[1];
    usb_console_multipart_part_t *bin_size_part = &parts[2];
    usb_console_multipart_part_t *image_size_part = &parts[3];
    usb_console_multipart_part_t *save_part = &parts[4];
    usb_console_multipart_part_t *show_part = &parts[5];
    usb_console_multipart_part_t *bin_part = &parts[6];
    usb_console_multipart_part_t *image_part = &parts[7];

    if (body == NULL || content_type == NULL || expected_func == NULL || cast == NULL) {
        set_result(result, TDX_JSON_RESULT_UPLOAD_INVALID, "image transfer failed", "invalid_arg");
        return ESP_ERR_INVALID_ARG;
    }
    memset(cast, 0, sizeof(*cast));

    if (!UsbConsoleCommon_ExtractBoundary(content_type, boundary, sizeof(boundary))) {
        set_result(result, TDX_JSON_RESULT_UPLOAD_BOUNDARY_MISSING, "missing boundary", "missing_boundary");
        return ESP_ERR_INVALID_ARG;
    }
    if (!UsbConsoleCommon_MultipartParts(body, body_len, boundary, part_names, parts, sizeof(parts) / sizeof(parts[0])) ||
        !func_part->present) {
        set_result(result, TDX_JSON_RESULT_UPLOAD_FUNC_MISSING, "missing func", "missing_func");
        return ESP_ERR_INVALID_ARG;
    }

    UsbConsoleCommon_CopyPartText(func_part, func, sizeof(func));
    if (strcmp(func, expected_func) != 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    UsbConsoleCommon_CopyPartText(file_name_part, cast->file_name, sizeof(cast->file_name));
    cast->save = UsbConsoleCommon_ParsePartBool(save_part, true);
    cast->show = UsbConsoleCommon_ParsePartBool(show_part, default_show);
    cast->bin_part = *bin_part;
    cast->image_part = *image_part;

    if (!UsbConsoleCommon_FileNameIsSafe(cast->file_name)) {
        set_result(result, TDX_JSON_RESULT_UPLOAD_FILE_NAME_INVALID, "image transfer failed", "invalid_fileName");
        return ESP_ERR_INVALID_ARG;
    }
    if (!UsbConsoleCommon_ParsePartSize(bin_size_part, &cast->bin_size)) {
        set_result(result, TDX_JSON_RESULT_UPLOAD_SIZE_MISMATCH, "image transfer failed", "invalid_bin_size");
        return ESP_ERR_INVALID_ARG;
    }
    if (!UsbConsoleCommon_ParsePartSize(image_size_part, &cast->image_size)) {
        set_result(result, TDX_JSON_RESULT_UPLOAD_SIZE_MISMATCH, "image transfer failed", "invalid_image_size");
        return ESP_ERR_INVALID_ARG;
    }
    if (!cast->bin_part.present || cast->bin_part.data == NULL) {
        set_result(result, TDX_JSON_RESULT_UPLOAD_BIN_MISSING, "image transfer failed", "missing_bin");
        return ESP_ERR_INVALID_ARG;
    }
    if (!cast->image_part.present || cast->image_part.data == NULL) {
        set_result(result, TDX_JSON_RESULT_UPLOAD_IMAGE_MISSING, "image transfer failed", "missing_image");
        return ESP_ERR_INVALID_ARG;
    }
    if (cast->bin_part.len != cast->bin_size) {
        set_result(result, TDX_JSON_RESULT_UPLOAD_SIZE_MISMATCH, "image transfer failed", "bin_size_mismatch");
        return ESP_ERR_INVALID_SIZE;
    }
    if (cast->image_part.len != cast->image_size) {
        set_result(result, TDX_JSON_RESULT_UPLOAD_SIZE_MISMATCH, "image transfer failed", "image_size_mismatch");
        return ESP_ERR_INVALID_SIZE;
    }
    if (require_save && !cast->save) {
        set_result(result, TDX_JSON_RESULT_SAVE_REQUIRED_FOR_LAST_CAST, "image transfer failed", "save_required_for_last_cast");
        return ESP_ERR_INVALID_STATE;
    }

    size_t json_size = 0;
    size_t part_len = 0;
    (void)part_text_len(func_part, &part_len);
    json_size += part_len;
    (void)part_text_len(file_name_part, &part_len);
    json_size += part_len;
    (void)part_text_len(bin_size_part, &part_len);
    json_size += part_len;
    (void)part_text_len(image_size_part, &part_len);
    json_size += part_len;
    (void)part_text_len(save_part, &part_len);
    json_size += part_len;
    (void)part_text_len(show_part, &part_len);
    json_size += part_len;

    ESP_LOGI(TAG, "%s meta file=%s save=%d show=%d body=%u bin=%u/%u image=%u/%u json=%u",
             expected_func,
             cast->file_name,
             cast->save ? 1 : 0,
             cast->show ? 1 : 0,
             (unsigned int)body_len,
             (unsigned int)cast->bin_part.len,
             (unsigned int)cast->bin_size,
             (unsigned int)cast->image_part.len,
             (unsigned int)cast->image_size,
             (unsigned int)json_size);
    return ESP_OK;
}

esp_err_t TdxCastCore_ParseAndValidate(const char *body,
                                       size_t body_len,
                                       const char *content_type,
                                       tdx_cast_core_request_t *cast,
                                       tdx_cast_core_result_t *result)
{
    return TdxImageTransfer_ParseSingle(body, body_len, content_type, "cast", true, true, cast, result);
}

esp_err_t TdxCastCore_ProcessValidated(const tdx_cast_core_request_t *cast,
                                       const char *base_path,
                                       const char *log_prefix,
                                       tdx_cast_core_result_t *result)
{
    int64_t total_start_us = esp_timer_get_time();
    if (cast == NULL || base_path == NULL || result == NULL) {
        set_result(result, TDX_JSON_RESULT_UPLOAD_INVALID, "cast failed", "invalid_arg");
        return ESP_ERR_INVALID_ARG;
    }

    tdx_image_transfer_item_t item = {
        .save = cast->save,
        .show = cast->show,
        .record_last_cast = true,
        .epd_target = 1,
        .bin_part = cast->bin_part,
        .image_part = cast->image_part,
    };
    snprintf(item.save_name, sizeof(item.save_name), "%s", cast->file_name);

    esp_err_t ret = TdxImageTransfer_ProcessItems(&item, 1, base_path, log_prefix, result);
    if (cast->show && ret == ESP_OK) {
        (void)stop_slideshow_for_cast(base_path);
    }
    if (ret != ESP_OK) {
        return ret;
    }

    TdxCastCore_ResultOk(result, cast->file_name, "ok");
    ESP_LOGI(TAG, "%s result ready total_ms=%lu",
             log_prefix != NULL ? log_prefix : "cast",
             (unsigned long)elapsed_ms_since(total_start_us));
    return ESP_OK;
}
