#include "network_ota_upload.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "server_network_sta_wifi_work_time.h"
#include "tdx_cfg.h"

static const char *TAG = "net-ota-upload";
static int s_last_ota_result = TDX_JSON_RESULT_OK;

static void log_heap_watermark(const char *point)
{
    ESP_LOGI(TAG,
             "heap %s free=%u min=%u psram=%u internal=%u",
             point,
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

typedef struct {
    const char *data;
    size_t len;
} multipart_field_t;

typedef struct {
    char func[24];
    char version[SERVER_NETWORK_STA_OTA_VERSION_MAX];
    size_t firmware_size;
    bool reboot;
} ota_upload_meta_t;

static void PowerMode_SetOtaInProgress(bool in_progress)
{
    ServerNetworkStaWifiWorkTime_SetOtaInProgress(in_progress);
}

static const esp_app_desc_t *get_firmware_app_desc(const uint8_t *firmware, size_t firmware_len)
{
    size_t desc_offset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
    if (firmware == NULL || firmware_len < desc_offset + sizeof(esp_app_desc_t)) {
        return NULL;
    }
    return (const esp_app_desc_t *)(firmware + desc_offset);
}

static const char *ota_state_name(esp_ota_img_states_t state)
{
    switch (state) {
    case ESP_OTA_IMG_NEW:
        return "ESP_OTA_IMG_NEW";
    case ESP_OTA_IMG_PENDING_VERIFY:
        return "ESP_OTA_IMG_PENDING_VERIFY";
    case ESP_OTA_IMG_VALID:
        return "ESP_OTA_IMG_VALID";
    case ESP_OTA_IMG_INVALID:
        return "ESP_OTA_IMG_INVALID";
    case ESP_OTA_IMG_ABORTED:
        return "ESP_OTA_IMG_ABORTED";
    case ESP_OTA_IMG_UNDEFINED:
        return "ESP_OTA_IMG_UNDEFINED";
    default:
        return "UNKNOWN";
    }
}

esp_err_t NetworkOtaUpload_MarkCurrentAppValidIfPending(void)
{
    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition == NULL) {
        ESP_LOGE(TAG, "mark current app valid failed: running partition is null");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "mark current app valid check: running=%s addr=0x%lx",
             running_partition->label,
             (unsigned long)running_partition->address);
    if (strcmp(running_partition->label, "factory") == 0) {
        ESP_LOGI(TAG, "mark current app valid skipped: running from factory partition");
        return ESP_OK;
    }

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t err = esp_ota_get_state_partition(running_partition, &state);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mark current app valid failed: get state ret=%s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "mark current app valid state: %s", ota_state_name(state));
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        return ESP_OK;
    }

    err = esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "mark current app valid ret=%s", esp_err_to_name(err));
    return err;
}

bool NetworkOtaUpload_IsOtaRequest(httpd_req_t *req, const char *content_type)
{
    const char *uri = (req != NULL) ? req->uri : NULL;
    bool uri_match = (uri != NULL) &&
                     (strcmp(uri, "/ota") == 0 || strcmp(uri, "/ota_upload") == 0);
    bool multipart = (content_type != NULL) && (strstr(content_type, "multipart/form-data") != NULL);
    ESP_LOGI(TAG, "detect ota request: uri=%s multipart=%d match=%d",
             uri != NULL ? uri : "<null>", multipart ? 1 : 0, uri_match ? 1 : 0);
    return uri_match && multipart;
}

size_t NetworkOtaUpload_GetMaxBodySize(void)
{
    size_t max_body_size = SERVER_NETWORK_STA_OTA_UPLOAD_MAX_BODY_SIZE;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition != NULL) {
        size_t partition_body_limit = update_partition->size + SERVER_NETWORK_STA_OTA_MULTIPART_OVERHEAD_BYTES;
        if (partition_body_limit < max_body_size) {
            max_body_size = partition_body_limit;
        }
        ESP_LOGI(TAG, "ota max body size=%u cfg=%u partition=%s size=%u overhead=%u",
                 (unsigned int)max_body_size,
                 (unsigned int)SERVER_NETWORK_STA_OTA_UPLOAD_MAX_BODY_SIZE,
                 update_partition->label,
                 (unsigned int)update_partition->size,
                 (unsigned int)SERVER_NETWORK_STA_OTA_MULTIPART_OVERHEAD_BYTES);
    } else {
        ESP_LOGW(TAG, "ota max body size=%u cfg=%u partition=<null>",
                 (unsigned int)max_body_size,
                 (unsigned int)SERVER_NETWORK_STA_OTA_UPLOAD_MAX_BODY_SIZE);
    }
    return max_body_size;
}

static const char *find_bytes(const char *haystack, size_t haystack_len,
                              const char *needle, size_t needle_len)
{
    if (haystack == NULL || needle == NULL || needle_len == 0 || haystack_len < needle_len) {
        return NULL;
    }
    for (size_t i = 0; i <= haystack_len - needle_len; ++i) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return haystack + i;
        }
    }
    return NULL;
}

static bool extract_boundary(const char *content_type, char *boundary, size_t boundary_size)
{
    const char *key = "boundary=";
    const char *pos = content_type != NULL ? strstr(content_type, key) : NULL;
    if (pos == NULL || boundary == NULL || boundary_size == 0) {
        return false;
    }
    pos += strlen(key);
    if (*pos == '"') {
        pos++;
    }
    size_t len = 0;
    while (pos[len] != '\0' && pos[len] != '"' && pos[len] != ';' && len + 1 < boundary_size) {
        boundary[len] = pos[len];
        len++;
    }
    boundary[len] = '\0';
    ESP_LOGI(TAG, "boundary parsed: len=%u value=%s", (unsigned int)len, boundary);
    return len > 0;
}

static bool extract_multipart_field(const char *body,
                                    size_t body_len,
                                    const char *boundary,
                                    const char *field_name,
                                    multipart_field_t *field)
{
    if (body == NULL || boundary == NULL || field_name == NULL || field == NULL) {
        return false;
    }

    field->data = NULL;
    field->len = 0;

    char boundary_marker[SERVER_NETWORK_STA_OTA_BOUNDARY_MAX + 4];
    int boundary_marker_len = snprintf(boundary_marker, sizeof(boundary_marker), "--%s", boundary);
    if (boundary_marker_len <= 0 || boundary_marker_len >= (int)sizeof(boundary_marker)) {
        return false;
    }

    const char *body_end = body + body_len;
    const char *marker = find_bytes(body, body_len, boundary_marker, (size_t)boundary_marker_len);

    while (marker != NULL && marker < body_end) {
        const char *next = marker + boundary_marker_len;
        if (next + 2 <= body_end && next[0] == '-' && next[1] == '-') {
            break;
        }
        if (next + 2 > body_end || next[0] != '\r' || next[1] != '\n') {
            marker = find_bytes(next, body_end - next, boundary_marker, (size_t)boundary_marker_len);
            continue;
        }

        const char *headers_start = next + 2;
        const char *header_end = find_bytes(headers_start, body_end - headers_start, "\r\n\r\n", 4);
        if (header_end == NULL) {
            break;
        }

        char name_token[64];
        int name_token_len = snprintf(name_token, sizeof(name_token), "name=\"%s\"", field_name);
        const char *data_start = header_end + 4;
        const char *next_boundary = find_bytes(data_start, body_end - data_start,
                                               boundary_marker, (size_t)boundary_marker_len);
        if (next_boundary == NULL) {
            break;
        }

        const char *data_end = next_boundary;
        if (data_end >= data_start + 2 && data_end[-2] == '\r' && data_end[-1] == '\n') {
            data_end -= 2;
        }

        if (name_token_len > 0 &&
            find_bytes(headers_start, header_end - headers_start, name_token, (size_t)name_token_len) != NULL) {
            field->data = data_start;
            field->len = (size_t)(data_end - data_start);
            ESP_LOGI(TAG, "multipart field found: name=%s len=%u",
                     field_name, (unsigned int)field->len);
            return true;
        }

        marker = next_boundary;
    }

    ESP_LOGW(TAG, "multipart field missing: name=%s", field_name);
    return false;
}

static bool parse_meta_json(const char *json, size_t json_len, ota_upload_meta_t *meta)
{
    if (json == NULL || meta == NULL || json_len == 0) {
        return false;
    }

    ESP_LOGI(TAG, "parse meta json start: len=%u", (unsigned int)json_len);
    ESP_LOGI(TAG, "parse meta json raw: %.*s",
             (int)(json_len > 256 ? 256 : json_len), json);

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (root == NULL) {
        ESP_LOGE(TAG, "parse meta failed: invalid json");
        return false;
    }

    memset(meta, 0, sizeof(*meta));
    meta->reboot = true;

    cJSON *func = cJSON_GetObjectItem(root, "func");
    cJSON *version = cJSON_GetObjectItem(root, "version");
    cJSON *firmware_size = cJSON_GetObjectItem(root, "firmware_size");
    cJSON *reboot = cJSON_GetObjectItem(root, "reboot");

    if (cJSON_IsString(func)) {
        strlcpy(meta->func, func->valuestring, sizeof(meta->func));
    }
    if (cJSON_IsString(version)) {
        strlcpy(meta->version, version->valuestring, sizeof(meta->version));
    }
    if (cJSON_IsNumber(firmware_size) && firmware_size->valuedouble > 0) {
        meta->firmware_size = (size_t)firmware_size->valuedouble;
    }
    if (cJSON_IsBool(reboot)) {
        meta->reboot = cJSON_IsTrue(reboot);
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "meta parsed: func=%s version=%s firmware_size=%u reboot=%d",
             meta->func[0] ? meta->func : "<empty>",
             meta->version[0] ? meta->version : "<empty>",
             (unsigned int)meta->firmware_size,
             meta->reboot ? 1 : 0);
    ESP_LOGI(TAG, "meta validation result: accepted=%u",
             (strcmp(meta->func, "ota") == 0 || strcmp(meta->func, "network_ota") == 0) ? 1 : 0);
    return strcmp(meta->func, "ota") == 0 || strcmp(meta->func, "network_ota") == 0;
}

static void ota_stream_begin(httpd_req_t *req)
{
    if (req == NULL) {
        return;
    }

    httpd_resp_set_type(req, "application/x-ndjson");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "close");
}

static esp_err_t ota_stream_send_line(httpd_req_t *req, const char *json)
{
    if (req == NULL || json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = httpd_resp_send_chunk(req, json, HTTPD_RESP_USE_STRLEN);
    if (ret == ESP_OK) {
        ret = httpd_resp_send_chunk(req, "\n", 1);
    }
    ESP_LOGI(TAG, "send ota stream: ret=%s json=%s", esp_err_to_name(ret), json);
    return ret;
}

static esp_err_t ota_stream_finish(httpd_req_t *req)
{
    if (req == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = httpd_resp_send_chunk(req, NULL, 0);
    log_heap_watermark("ota_end");
    ESP_LOGI(TAG, "finish ota stream ret=%s", esp_err_to_name(ret));
    return ret;
}

static esp_err_t send_ota_eventf(httpd_req_t *req,
                                 const char *stage,
                                 int result,
                                 const char *message,
                                 esp_err_t err,
                                 const char *extra_fmt,
                                 ...)
{
    char extra[384] = {0};
    char response[768] = {0};

    if (extra_fmt != NULL && extra_fmt[0] != '\0') {
        va_list ap;
        va_start(ap, extra_fmt);
        vsnprintf(extra, sizeof(extra), extra_fmt, ap);
        va_end(ap);
    }

    snprintf(response, sizeof(response),
             "{\"func\":\"ota_event\",\"stage\":\"%s\",\"result\":%d,\"message\":\"%s\",\"esp_err\":%d%s}",
             stage != NULL ? stage : "unknown",
             result,
             message != NULL ? message : "unknown",
             (int)err,
             extra);
    return ota_stream_send_line(req, response);
}

static esp_err_t send_ota_resultf(httpd_req_t *req,
                                  int result,
                                  const char *message,
                                  esp_err_t err,
                                  const char *extra_fmt,
                                  ...)
{
    char extra[384] = {0};
    char response[768] = {0};

    if (extra_fmt != NULL && extra_fmt[0] != '\0') {
        va_list ap;
        va_start(ap, extra_fmt);
        vsnprintf(extra, sizeof(extra), extra_fmt, ap);
        va_end(ap);
    }

    snprintf(response, sizeof(response),
             "{\"func\":\"ota_result\",\"result\":%d,\"message\":\"%s\",\"esp_err\":%d%s}",
             result,
             message != NULL ? message : "unknown",
             (int)err,
             extra);
    return ota_stream_send_line(req, response);
}

static int ota_result_from_stage(const char *stage)
{
    if (stage == NULL) {
        return TDX_JSON_RESULT_INTERNAL_ERROR;
    }
    if (strcmp(stage, "upload_busy") == 0) {
        return TDX_JSON_RESULT_OTA_BUSY;
    }
    if (strcmp(stage, "empty_body") == 0) {
        return TDX_JSON_RESULT_FIELD_MISSING;
    }
    if (strcmp(stage, "body_too_large") == 0) {
        return TDX_JSON_RESULT_BODY_TOO_LARGE;
    }
    return TDX_JSON_RESULT_INTERNAL_ERROR;
}

esp_err_t NetworkOtaUpload_SendErrorAndFinish(httpd_req_t *req,
                                              const char *stage,
                                              const char *message,
                                              esp_err_t err)
{
    int result = ota_result_from_stage(stage);
    ota_stream_begin(req);
    send_ota_eventf(req, stage, result, message, err, NULL);
    send_ota_resultf(req, result, message, err,
                     ",\"failed_stage\":\"%s\"",
                     stage != NULL ? stage : "unknown");
    return ota_stream_finish(req);
}

static esp_err_t write_firmware_to_ota_partition(httpd_req_t *req,
                                                 const uint8_t *firmware,
                                                 size_t firmware_len,
                                                 const ota_upload_meta_t *meta)
{
    if (firmware == NULL || firmware_len == 0 || meta == NULL) {
        ESP_LOGE(TAG, "ota write reject: empty firmware");
        s_last_ota_result = TDX_JSON_RESULT_OTA_FIRMWARE_MISSING;
        send_ota_eventf(req, "validate_failed", s_last_ota_result, "empty_firmware", ESP_ERR_INVALID_ARG, NULL);
        return ESP_ERR_INVALID_ARG;
    }
    send_ota_eventf(req, "write_prepare", TDX_JSON_RESULT_OK, "write_prepare", ESP_OK,
                    ",\"firmware_size\":%u,\"meta_size\":%u",
                    (unsigned int)firmware_len,
                    (unsigned int)meta->firmware_size);
    ESP_LOGI(TAG, "ota write start: firmware_ptr=%p len=%u meta_func=%s meta_version=%s meta_size=%u reboot=%u",
             firmware,
             (unsigned int)firmware_len,
             meta->func[0] ? meta->func : "<empty>",
             meta->version[0] ? meta->version : "<empty>",
             (unsigned int)meta->firmware_size,
             meta->reboot ? 1 : 0);
    ESP_LOGI(TAG, "ota firmware head: %02X %02X %02X %02X %02X %02X %02X %02X",
             firmware_len > 0 ? (unsigned int)firmware[0] : 0,
             firmware_len > 1 ? (unsigned int)firmware[1] : 0,
             firmware_len > 2 ? (unsigned int)firmware[2] : 0,
             firmware_len > 3 ? (unsigned int)firmware[3] : 0,
             firmware_len > 4 ? (unsigned int)firmware[4] : 0,
             firmware_len > 5 ? (unsigned int)firmware[5] : 0,
             firmware_len > 6 ? (unsigned int)firmware[6] : 0,
             firmware_len > 7 ? (unsigned int)firmware[7] : 0);

    if (firmware_len < sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
        ESP_LOGE(TAG, "ota write reject: firmware too small len=%u", (unsigned int)firmware_len);
        s_last_ota_result = TDX_JSON_RESULT_OTA_FIRMWARE_SIZE_INVALID;
        send_ota_eventf(req, "validate_failed", s_last_ota_result, "firmware_too_small", ESP_ERR_INVALID_SIZE,
                        ",\"firmware_size\":%u",
                        (unsigned int)firmware_len);
        return ESP_ERR_INVALID_SIZE;
    }

    const esp_image_header_t *image_header = (const esp_image_header_t *)firmware;
    ESP_LOGI(TAG, "ota image header: magic=0x%02X segment_count=%u flash_mode=%u flash_size_freq=0x%02X entry=0x%08lX",
             image_header->magic,
             image_header->segment_count,
             image_header->spi_mode,
             image_header->spi_size,
             (unsigned long)image_header->entry_addr);
    if (image_header->magic != ESP_IMAGE_HEADER_MAGIC) {
        ESP_LOGE(TAG, "ota write reject: invalid image magic=0x%02X", image_header->magic);
        s_last_ota_result = TDX_JSON_RESULT_OTA_VERIFY_FAILED;
        send_ota_eventf(req, "validate_failed", s_last_ota_result, "invalid_image_magic", ESP_ERR_INVALID_VERSION,
                        ",\"magic\":%u",
                        (unsigned int)image_header->magic);
        return ESP_ERR_INVALID_VERSION;
    }
    if (meta->firmware_size != 0 && meta->firmware_size != firmware_len) {
        ESP_LOGE(TAG, "ota write reject: size mismatch meta=%u real=%u",
                 (unsigned int)meta->firmware_size, (unsigned int)firmware_len);
        s_last_ota_result = TDX_JSON_RESULT_OTA_FIRMWARE_SIZE_INVALID;
        send_ota_eventf(req, "validate_failed", s_last_ota_result, "size_mismatch", ESP_ERR_INVALID_SIZE,
                        ",\"meta_size\":%u,\"real_size\":%u",
                        (unsigned int)meta->firmware_size,
                        (unsigned int)firmware_len);
        return ESP_ERR_INVALID_SIZE;
    }

    const esp_app_desc_t *current_app = esp_app_get_description();
    const esp_app_desc_t *new_app = get_firmware_app_desc(firmware, firmware_len);
    if (new_app == NULL) {
        ESP_LOGE(TAG, "ota write reject: app desc missing");
        s_last_ota_result = TDX_JSON_RESULT_OTA_VERIFY_FAILED;
        send_ota_eventf(req, "validate_failed", s_last_ota_result, "app_desc_missing", ESP_ERR_INVALID_SIZE,
                        ",\"firmware_size\":%u",
                        (unsigned int)firmware_len);
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGI(TAG, "ota image version: current=%s new=%s meta=%s",
             current_app != NULL ? current_app->version : "<null>",
             new_app->version,
             meta->version[0] ? meta->version : "<empty>");
    send_ota_eventf(req, "version_checked", TDX_JSON_RESULT_OK, "version_checked", ESP_OK,
                    ",\"current_version\":\"%s\",\"new_version\":\"%s\",\"meta_version\":\"%s\"",
                    current_app != NULL ? current_app->version : "",
                    new_app->version,
                    meta->version[0] ? meta->version : "");

    if (meta->version[0] != '\0' && strcmp(meta->version, new_app->version) != 0) {
        ESP_LOGE(TAG, "ota write reject: version mismatch meta=%s image=%s", meta->version, new_app->version);
        s_last_ota_result = TDX_JSON_RESULT_OTA_VERSION_MISMATCH;
        send_ota_eventf(req, "validate_failed", s_last_ota_result, "version_mismatch", ESP_ERR_INVALID_VERSION,
                        ",\"meta_version\":\"%s\",\"image_version\":\"%s\"",
                        meta->version,
                        new_app->version);
        return ESP_ERR_INVALID_VERSION;
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "ota write failed: no update partition");
        s_last_ota_result = TDX_JSON_RESULT_OTA_PARTITION_TOO_SMALL;
        send_ota_eventf(req, "partition_failed", s_last_ota_result, "no_update_partition", ESP_ERR_NOT_FOUND, NULL);
        return ESP_ERR_NOT_FOUND;
    }
    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "ota partitions: running=%s next=%s next_addr=0x%lx next_size=%u",
             running_partition != NULL ? running_partition->label : "<null>",
             update_partition->label,
             (unsigned long)update_partition->address,
             (unsigned int)update_partition->size);
    send_ota_eventf(req, "write_begin", TDX_JSON_RESULT_OK, "write_begin", ESP_OK,
                    ",\"running\":\"%s\",\"target\":\"%s\",\"target_addr\":\"0x%lx\",\"target_size\":%u,\"firmware_size\":%u",
                    running_partition != NULL ? running_partition->label : "",
                    update_partition->label,
                    (unsigned long)update_partition->address,
                    (unsigned int)update_partition->size,
                    (unsigned int)firmware_len);
    if (firmware_len > update_partition->size) {
        ESP_LOGE(TAG, "ota write reject: firmware=%u partition=%u",
                 (unsigned int)firmware_len, (unsigned int)update_partition->size);
        s_last_ota_result = TDX_JSON_RESULT_OTA_PARTITION_TOO_SMALL;
        send_ota_eventf(req, "validate_failed", s_last_ota_result, "partition_too_small", ESP_ERR_INVALID_SIZE,
                        ",\"firmware_size\":%u,\"partition_size\":%u",
                        (unsigned int)firmware_len,
                        (unsigned int)update_partition->size);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_ota_handle_t update_handle = 0;
    ESP_LOGI(TAG, "ota begin: partition=%s address=0x%lx size=%u firmware=%u",
             update_partition->label,
             (unsigned long)update_partition->address,
             (unsigned int)update_partition->size,
             (unsigned int)firmware_len);
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        s_last_ota_result = TDX_JSON_RESULT_OTA_BEGIN_FAILED;
        send_ota_eventf(req, "ota_begin_failed", s_last_ota_result, esp_err_to_name(err), err, NULL);
        return err;
    }
    ESP_LOGI(TAG, "esp_ota_begin ok: handle=%lu", (unsigned long)update_handle);
    send_ota_eventf(req, "ota_begin_ok", TDX_JSON_RESULT_OK, "ota_begin_ok", ESP_OK,
                    ",\"target\":\"%s\"",
                    update_partition->label);

    size_t written = 0;
    while (written < firmware_len) {
        size_t chunk = firmware_len - written;
        if (chunk > 4096) {
            chunk = 4096;
        }
        err = esp_ota_write(update_handle, firmware + written, chunk);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s written=%u chunk=%u",
                     esp_err_to_name(err), (unsigned int)written, (unsigned int)chunk);
            esp_ota_abort(update_handle);
            s_last_ota_result = TDX_JSON_RESULT_OTA_WRITE_FAILED;
            send_ota_eventf(req, "write_failed", s_last_ota_result, esp_err_to_name(err), err,
                            ",\"written\":%u,\"total\":%u,\"chunk\":%u",
                            (unsigned int)written,
                            (unsigned int)firmware_len,
                            (unsigned int)chunk);
            return err;
        }
        written += chunk;
        if ((written % (256 * 1024)) == 0 || written == firmware_len) {
            ESP_LOGI(TAG, "ota write progress: %u/%u", (unsigned int)written, (unsigned int)firmware_len);
            unsigned int percent = firmware_len > 0 ? (unsigned int)((written * 100) / firmware_len) : 0;
            send_ota_eventf(req, "write_progress", TDX_JSON_RESULT_OK, "write_progress", ESP_OK,
                            ",\"written\":%u,\"total\":%u,\"percent\":%u",
                            (unsigned int)written,
                            (unsigned int)firmware_len,
                            percent);
        }
    }

    send_ota_eventf(req, "verify_begin", TDX_JSON_RESULT_OK, "verify_begin", ESP_OK,
                    ",\"written\":%u,\"total\":%u",
                    (unsigned int)written,
                    (unsigned int)firmware_len);
    err = esp_ota_end(update_handle);
    ESP_LOGI(TAG, "esp_ota_end returned: %s", esp_err_to_name(err));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        s_last_ota_result = TDX_JSON_RESULT_OTA_END_FAILED;
        send_ota_eventf(req, "verify_failed", s_last_ota_result, esp_err_to_name(err), err, NULL);
        return err;
    }
    send_ota_eventf(req, "verify_ok", TDX_JSON_RESULT_OK, "verify_ok", ESP_OK, NULL);

    err = esp_ota_set_boot_partition(update_partition);
    ESP_LOGI(TAG, "esp_ota_set_boot_partition returned: %s", esp_err_to_name(err));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        s_last_ota_result = TDX_JSON_RESULT_OTA_SET_BOOT_FAILED;
        send_ota_eventf(req, "set_boot_failed", s_last_ota_result, esp_err_to_name(err), err,
                        ",\"target\":\"%s\"",
                        update_partition->label);
        return err;
    }
    send_ota_eventf(req, "set_boot_ok", TDX_JSON_RESULT_OK, "set_boot_ok", ESP_OK,
                    ",\"next\":\"%s\"",
                    update_partition->label);

    ESP_LOGI(TAG, "ota write success: next boot partition=%s", update_partition->label);
    send_ota_eventf(req, "write_done", TDX_JSON_RESULT_OK, "write_done", ESP_OK,
                    ",\"next\":\"%s\",\"firmware_size\":%u",
                    update_partition->label,
                    (unsigned int)firmware_len);
    return ESP_OK;
}

esp_err_t NetworkOtaUpload_ProcessReceivedBody(httpd_req_t *req,
                                               const char *body,
                                               size_t body_len,
                                               const char *content_type)
{
    char boundary[SERVER_NETWORK_STA_OTA_BOUNDARY_MAX] = {0};
    multipart_field_t meta_field = {0};
    multipart_field_t firmware_field = {0};
    ota_upload_meta_t meta = {0};

    log_heap_watermark("ota_start");
    s_last_ota_result = TDX_JSON_RESULT_OK;
    ota_stream_begin(req);
    send_ota_eventf(req, "body_received", TDX_JSON_RESULT_OK, "body_received", ESP_OK,
                    ",\"body_len\":%u",
                    (unsigned int)body_len);
    ESP_LOGI(TAG, "process ota body: len=%u content_type=%s",
             (unsigned int)body_len,
             content_type != NULL ? content_type : "<null>");
    ESP_LOGI(TAG, "process ota body: body_ptr=%p", body);
    send_ota_eventf(req, "parse_begin", TDX_JSON_RESULT_OK, "parse_begin", ESP_OK, NULL);

    if (!extract_boundary(content_type, boundary, sizeof(boundary))) {
        ESP_LOGE(TAG, "process ota body failed: no boundary");
        send_ota_eventf(req, "missing_boundary", TDX_JSON_RESULT_OTA_BOUNDARY_MISSING, "missing_boundary", ESP_ERR_INVALID_ARG, NULL);
        send_ota_resultf(req, TDX_JSON_RESULT_OTA_BOUNDARY_MISSING, "missing_boundary", ESP_ERR_INVALID_ARG,
                         ",\"failed_stage\":\"missing_boundary\"");
        return ota_stream_finish(req);
    }
    ESP_LOGI(TAG, "process ota body: boundary=%s", boundary);
    send_ota_eventf(req, "boundary_ok", TDX_JSON_RESULT_OK, "boundary_ok", ESP_OK,
                    ",\"boundary_len\":%u",
                    (unsigned int)strlen(boundary));

    if (!extract_multipart_field(body, body_len, boundary, "meta", &meta_field)) {
        ESP_LOGE(TAG, "process ota body failed: missing meta field");
        send_ota_eventf(req, "missing_meta", TDX_JSON_RESULT_OTA_META_MISSING, "missing_meta", ESP_ERR_INVALID_ARG, NULL);
        send_ota_resultf(req, TDX_JSON_RESULT_OTA_META_MISSING, "missing_meta", ESP_ERR_INVALID_ARG,
                         ",\"failed_stage\":\"missing_meta\"");
        return ota_stream_finish(req);
    }
    send_ota_eventf(req, "meta_received", TDX_JSON_RESULT_OK, "meta_received", ESP_OK,
                    ",\"meta_len\":%u",
                    (unsigned int)meta_field.len);
    if (!parse_meta_json(meta_field.data, meta_field.len, &meta)) {
        ESP_LOGE(TAG, "process ota body failed: invalid meta");
        send_ota_eventf(req, "invalid_meta", TDX_JSON_RESULT_OTA_META_INVALID, "invalid_meta", ESP_ERR_INVALID_ARG, NULL);
        send_ota_resultf(req, TDX_JSON_RESULT_OTA_META_INVALID, "invalid_meta", ESP_ERR_INVALID_ARG,
                         ",\"failed_stage\":\"invalid_meta\"");
        return ota_stream_finish(req);
    }
    send_ota_eventf(req, "meta_ok", TDX_JSON_RESULT_OK, "meta_ok", ESP_OK,
                    ",\"version\":\"%s\",\"firmware_size\":%u,\"reboot\":%u",
                    meta.version[0] ? meta.version : "",
                    (unsigned int)meta.firmware_size,
                    meta.reboot ? 1 : 0);
    if (!extract_multipart_field(body, body_len, boundary, "firmware", &firmware_field) &&
        !extract_multipart_field(body, body_len, boundary, "bin", &firmware_field)) {
        ESP_LOGE(TAG, "process ota body failed: missing firmware field");
        send_ota_eventf(req, "missing_firmware", TDX_JSON_RESULT_OTA_FIRMWARE_MISSING, "missing_firmware", ESP_ERR_INVALID_ARG, NULL);
        send_ota_resultf(req, TDX_JSON_RESULT_OTA_FIRMWARE_MISSING, "missing_firmware", ESP_ERR_INVALID_ARG,
                         ",\"failed_stage\":\"missing_firmware\"");
        return ota_stream_finish(req);
    }
    ESP_LOGI(TAG, "process ota body: fields ready meta_len=%u firmware_len=%u",
             (unsigned int)meta_field.len,
             (unsigned int)firmware_field.len);
    send_ota_eventf(req, "firmware_received", TDX_JSON_RESULT_OK, "firmware_received", ESP_OK,
                    ",\"firmware_size\":%u",
                    (unsigned int)firmware_field.len);

    PowerMode_SetOtaInProgress(true);
    send_ota_eventf(req, "power_hold", TDX_JSON_RESULT_OK, "ota_power_hold_enabled", ESP_OK, NULL);
    esp_err_t err = write_firmware_to_ota_partition(req,
                                                    (const uint8_t *)firmware_field.data,
                                                    firmware_field.len,
                                                    &meta);
    if (err != ESP_OK) {
        PowerMode_SetOtaInProgress(false);
        int result = s_last_ota_result != TDX_JSON_RESULT_OK ? s_last_ota_result : TDX_JSON_RESULT_INTERNAL_ERROR;
        send_ota_eventf(req, "power_hold_release", TDX_JSON_RESULT_OK, "ota_power_hold_disabled", ESP_OK, NULL);
        send_ota_resultf(req, result, esp_err_to_name(err), err,
                         ",\"failed_stage\":\"write_or_verify\"");
        return ota_stream_finish(req);
    }

    send_ota_resultf(req, TDX_JSON_RESULT_OK, "ok", ESP_OK,
                     ",\"reboot\":%u,\"firmware_size\":%u",
                     meta.reboot ? 1 : 0,
                     (unsigned int)firmware_field.len);
    ESP_LOGI(TAG, "process ota body: success result sent reboot=%u", meta.reboot ? 1 : 0);
    if (meta.reboot) {
        send_ota_eventf(req, "rebooting", TDX_JSON_RESULT_OK, "rebooting", ESP_OK,
                        ",\"delay_ms\":1000");
        ota_stream_finish(req);
        ESP_LOGI(TAG, "ota reboot scheduled after response");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        ESP_LOGI(TAG, "ota reboot skipped by meta");
        send_ota_eventf(req, "reboot_skipped", TDX_JSON_RESULT_OK, "reboot_skipped", ESP_OK, NULL);
        PowerMode_SetOtaInProgress(false);
        send_ota_eventf(req, "power_hold_release", TDX_JSON_RESULT_OK, "ota_power_hold_disabled", ESP_OK, NULL);
        return ota_stream_finish(req);
    }
    return ESP_OK;
}


// body_received        收完整 OTA body
// parse_begin          开始解析
// boundary_ok          multipart boundary 正常
// meta_received        收到 meta
// meta_ok              meta 解析成功
// firmware_received    收到 firmware
// power_hold           OTA 期间锁定功耗模式
// write_prepare        准备写入
// version_checked      版本校验通过
// write_begin          开始写 ota_1
// ota_begin_ok         esp_ota_begin 成功
// write_progress       写入进度 5% ~ 100%
// verify_begin         开始校验
// verify_ok            esp_ota_end 成功
// set_boot_ok          设置下次启动分区成功
// write_done           写入完成
// ota_result           最终成功
// rebooting            准备重启


// body_received       已收到升级包
// meta_ok             升级信息解析成功
// firmware_received   固件数据解析成功
// write_begin         开始写入
// write_progress      更新进度条
// verify_begin        开始校验
// verify_ok           校验成功
// set_boot_ok         设置启动分区成功
// write_done          写入完成
// ota_result result=0 最终成功
// rebooting           设备即将重启

#if 0
增加一种 OTA 模式 （原本的 OTA 不要修改）
1，所有与  OTA相关的数据， （JSON ,及 二进制软件 数据）全部在 函数  receive_data_redirect_handler 里接收
2，尽量写成模块化
3，新加一个文件，将增加的这种 OTA 相关的 函数等等，全放在这个文件之中

============================ 无 version 模式 ===========================
$esp = "http://192.168.1.104"
$bin = "H:\AI2\ESP32-S3-PhotoPainter-main\01_Example\xiaozhi-esp32\build\xiaozhi.bin"

$size = (Get-Item $bin).Length

$meta = '{"func":"ota","firmware_size":' + $size + ',"reboot":true}'
$meta | Set-Content -Path ".\ota_meta.json" -NoNewline -Encoding ascii

curl.exe -v "$esp/ota" `
  -F "meta=<.\ota_meta.json;type=application/json" `
  -F "firmware=@$bin;type=application/octet-stream"

=============================extend version 模式 =========================
$esp = "http://192.168.1.104"
$bin = "H:\AI2\ESP32-S3-PhotoPainter-main\01_Example\xiaozhi-esp32\build\xiaozhi.bin"

$size = (Get-Item $bin).Length
$version = "2.0.2"

$meta = '{"func":"ota","version":"' + $version + '","firmware_size":' + $size + ',"reboot":true}'
$meta | Set-Content -Path ".\ota_meta.json" -NoNewline -Encoding ascii

Get-Content ".\ota_meta.json"

curl.exe -v "$esp/ota" `
  -F "meta=<.\ota_meta.json;type=application/json" `
  -F "firmware=@$bin;type=application/octet-stream"

===============================
#endif
