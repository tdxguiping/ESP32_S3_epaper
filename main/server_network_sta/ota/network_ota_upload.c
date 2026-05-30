#include "network_ota_upload.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "tdx_cfg.h"

static const char *TAG = "net_ota_upload";

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

static const char *find_bytes(const char *haystack, size_t haystack_len, const char *needle, size_t needle_len)
{
    if (haystack == NULL || needle == NULL || needle_len == 0 || haystack_len < needle_len) {
        return NULL;
    }

    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
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
    ESP_LOGI(TAG, "boundary parsed len=%u value=%s", (unsigned int)len, boundary);
    return len > 0;
}

static bool extract_multipart_field(const char *body, size_t body_len,
                                    const char *boundary, const char *field_name,
                                    multipart_field_t *field)
{
    if (body == NULL || boundary == NULL || field_name == NULL || field == NULL) {
        return false;
    }

    field->data = NULL;
    field->len = 0;

    char marker[SERVER_NETWORK_STA_OTA_BOUNDARY_MAX + 4];
    int marker_len = snprintf(marker, sizeof(marker), "--%s", boundary);
    const char *body_end = body + body_len;
    const char *part = find_bytes(body, body_len, marker, (size_t)marker_len);

    while (part != NULL && part < body_end) {
        const char *cursor = part + marker_len;
        if (cursor + 2 <= body_end && cursor[0] == '-' && cursor[1] == '-') {
            break;
        }
        if (cursor + 2 > body_end || cursor[0] != '\r' || cursor[1] != '\n') {
            part = find_bytes(cursor, body_end - cursor, marker, (size_t)marker_len);
            continue;
        }

        const char *headers_start = cursor + 2;
        const char *headers_end = find_bytes(headers_start, body_end - headers_start, "\r\n\r\n", 4);
        if (headers_end == NULL) {
            break;
        }

        const char *data_start = headers_end + 4;
        const char *next_boundary = find_bytes(data_start, body_end - data_start, marker, (size_t)marker_len);
        if (next_boundary == NULL) {
            break;
        }

        const char *data_end = next_boundary;
        if (data_end >= data_start + 2 && data_end[-2] == '\r' && data_end[-1] == '\n') {
            data_end -= 2;
        }

        char name_token[64];
        snprintf(name_token, sizeof(name_token), "name=\"%s\"", field_name);
        if (find_bytes(headers_start, headers_end - headers_start, name_token, strlen(name_token)) != NULL) {
            field->data = data_start;
            field->len = data_end - data_start;
            ESP_LOGI(TAG, "multipart field found name=%s len=%u", field_name, (unsigned int)field->len);
            return true;
        }

        part = next_boundary;
    }

    ESP_LOGW(TAG, "multipart field missing name=%s", field_name);
    return false;
}

static const char *find_json_key(const char *json, size_t json_len, const char *key)
{
    char token[48];
    snprintf(token, sizeof(token), "\"%s\"", key);
    return find_bytes(json, json_len, token, strlen(token));
}

static bool parse_json_string(const char *json, size_t json_len, const char *key, char *out, size_t out_size)
{
    const char *pos = find_json_key(json, json_len, key);
    const char *json_end = json + json_len;
    if (pos == NULL || out == NULL || out_size == 0) {
        return false;
    }

    pos += strlen(key) + 2;
    while (pos < json_end && isspace((unsigned char)*pos)) {
        pos++;
    }
    if (pos >= json_end || *pos != ':') {
        return false;
    }
    pos++;
    while (pos < json_end && isspace((unsigned char)*pos)) {
        pos++;
    }
    if (pos >= json_end || *pos != '"') {
        return false;
    }
    pos++;

    size_t len = 0;
    while (pos + len < json_end && pos[len] != '"' && len + 1 < out_size) {
        out[len] = pos[len];
        len++;
    }
    out[len] = '\0';
    return len > 0;
}

static bool parse_json_size(const char *json, size_t json_len, const char *key, size_t *out)
{
    const char *pos = find_json_key(json, json_len, key);
    const char *json_end = json + json_len;
    if (pos == NULL || out == NULL) {
        return false;
    }

    pos += strlen(key) + 2;
    while (pos < json_end && isspace((unsigned char)*pos)) {
        pos++;
    }
    if (pos >= json_end || *pos != ':') {
        return false;
    }
    pos++;
    while (pos < json_end && isspace((unsigned char)*pos)) {
        pos++;
    }

    size_t value = 0;
    bool got_digit = false;
    while (pos < json_end && *pos >= '0' && *pos <= '9') {
        got_digit = true;
        value = value * 10 + (size_t)(*pos - '0');
        pos++;
    }

    if (!got_digit) {
        return false;
    }
    *out = value;
    return true;
}

static bool parse_json_bool(const char *json, size_t json_len, const char *key, bool *out)
{
    const char *pos = find_json_key(json, json_len, key);
    const char *json_end = json + json_len;
    if (pos == NULL || out == NULL) {
        return false;
    }

    pos += strlen(key) + 2;
    while (pos < json_end && isspace((unsigned char)*pos)) {
        pos++;
    }
    if (pos >= json_end || *pos != ':') {
        return false;
    }
    pos++;
    while (pos < json_end && isspace((unsigned char)*pos)) {
        pos++;
    }

    if (json_end - pos >= 4 && memcmp(pos, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (json_end - pos >= 5 && memcmp(pos, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool parse_meta_json(const char *json, size_t json_len, ota_upload_meta_t *meta)
{
    if (json == NULL || json_len == 0 || meta == NULL) {
        return false;
    }

    memset(meta, 0, sizeof(*meta));
    meta->reboot = true;
    parse_json_string(json, json_len, "func", meta->func, sizeof(meta->func));
    parse_json_string(json, json_len, "version", meta->version, sizeof(meta->version));
    parse_json_size(json, json_len, "firmware_size", &meta->firmware_size);
    parse_json_bool(json, json_len, "reboot", &meta->reboot);

    ESP_LOGI(TAG, "parse meta func=%s version=%s size=%u reboot=%d",
             meta->func[0] ? meta->func : "<none>",
             meta->version[0] ? meta->version : "<none>",
             (unsigned int)meta->firmware_size,
             meta->reboot ? 1 : 0);
    return strcmp(meta->func, "ota") == 0 && meta->firmware_size > 0;
}

static esp_err_t send_ota_json_response(httpd_req_t *req, const char *stage,
                                        const char *message, esp_err_t err)
{
    char json[192];
    snprintf(json, sizeof(json),
             "{\"func\":\"ota_result\",\"result\":%d,\"stage\":\"%s\",\"message\":\"%s\",\"err\":%d}",
             err == ESP_OK ? 0 : 1,
             stage != NULL ? stage : "",
             message != NULL ? message : "",
             (int)err);
    ESP_LOGI(TAG, "ota response: %s", json);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t write_firmware_to_ota_partition(const char *firmware,
                                                 size_t firmware_len,
                                                 const ota_upload_meta_t *meta)
{
    if (firmware == NULL || firmware_len == 0 || meta == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (firmware_len < sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
        ESP_LOGE(TAG, "firmware too small len=%u", (unsigned int)firmware_len);
        return ESP_ERR_INVALID_SIZE;
    }
    if ((uint8_t)firmware[0] != ESP_IMAGE_HEADER_MAGIC) {
        ESP_LOGE(TAG, "invalid firmware magic first=0x%02x", (unsigned int)(uint8_t)firmware[0]);
        return ESP_ERR_INVALID_VERSION;
    }
    if (meta->firmware_size != firmware_len) {
        ESP_LOGE(TAG, "firmware size mismatch meta=%u actual=%u",
                 (unsigned int)meta->firmware_size, (unsigned int)firmware_len);
        return ESP_ERR_INVALID_SIZE;
    }

    const esp_app_desc_t *app_desc = (const esp_app_desc_t *)(firmware + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
    ESP_LOGI(TAG, "firmware app version=%s project=%s", app_desc->version, app_desc->project_name);

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "no OTA update partition found");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "target ota partition label=%s addr=0x%lx size=0x%lx",
             update_partition->label, update_partition->address, update_partition->size);
    if (firmware_len > update_partition->size) {
        ESP_LOGE(TAG, "partition too small firmware=%u partition=%u",
                 (unsigned int)firmware_len, (unsigned int)update_partition->size);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, firmware_len, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed ret=%s", esp_err_to_name(err));
        return err;
    }

    size_t written = 0;
    while (written < firmware_len) {
        size_t chunk = firmware_len - written;
        if (chunk > 4096) {
            chunk = 4096;
        }
        err = esp_ota_write(ota_handle, firmware + written, chunk);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed offset=%u ret=%s",
                     (unsigned int)written, esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            return err;
        }
        written += chunk;
        if ((written % (256 * 1024)) == 0 || written == firmware_len) {
            ESP_LOGI(TAG, "ota write progress written=%u total=%u",
                     (unsigned int)written, (unsigned int)firmware_len);
        }
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed ret=%s", esp_err_to_name(err));
        return err;
    }

    err = esp_ota_set_boot_partition(update_partition);
    ESP_LOGI(TAG, "esp_ota_set_boot_partition ret=%s", esp_err_to_name(err));
    return err;
}

esp_err_t NetworkOtaUpload_MarkCurrentAppValidIfPending(void)
{
    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition == NULL) {
        ESP_LOGE(TAG, "mark current app valid failed: running partition is null");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "mark current app valid check running=%s addr=0x%lx",
             running_partition->label, running_partition->address);

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t err = esp_ota_get_state_partition(running_partition, &state);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "get running ota state ret=%s", esp_err_to_name(err));
        return ESP_OK;
    }

    ESP_LOGI(TAG, "running ota state=%s", ota_state_name(state));
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        return ESP_OK;
    }

    // Confirm the just-booted OTA image so ESP-IDF allows the next OTA update.
    // 确认刚启动的 OTA 固件有效，这样 ESP-IDF 才允许下一次 OTA 更新。
    err = esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "mark current app valid ret=%s", esp_err_to_name(err));
    return err;
}

bool NetworkOtaUpload_IsOtaRequest(httpd_req_t *req, const char *content_type)
{
    const char *uri = req != NULL ? req->uri : NULL;
    bool uri_match = uri != NULL && (strcmp(uri, "/ota") == 0 || strcmp(uri, "/ota_upload") == 0);
    bool multipart = content_type != NULL && strstr(content_type, "multipart/form-data") != NULL;
    ESP_LOGI(TAG, "detect ota request uri=%s multipart=%d match=%d",
             uri != NULL ? uri : "<null>", multipart ? 1 : 0, uri_match ? 1 : 0);
    return uri_match && multipart;
}

size_t NetworkOtaUpload_GetMaxBodySize(void)
{
    ESP_LOGI(TAG, "ota max body size=%u", (unsigned int)SERVER_NETWORK_STA_OTA_UPLOAD_MAX_BODY_SIZE);
    return SERVER_NETWORK_STA_OTA_UPLOAD_MAX_BODY_SIZE;
}

esp_err_t NetworkOtaUpload_SendErrorAndFinish(httpd_req_t *req,
                                              const char *stage,
                                              const char *message,
                                              esp_err_t err)
{
    return send_ota_json_response(req, stage, message, err);
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

    ESP_LOGI(TAG, "ota process body_len=%u content_type=%s",
             (unsigned int)body_len, content_type != NULL ? content_type : "<null>");

    if (!extract_boundary(content_type, boundary, sizeof(boundary))) {
        return send_ota_json_response(req, "parse", "missing_boundary", ESP_ERR_INVALID_ARG);
    }
    if (!extract_multipart_field(body, body_len, boundary, "meta", &meta_field)) {
        return send_ota_json_response(req, "parse", "missing_meta", ESP_ERR_INVALID_ARG);
    }
    if (!parse_meta_json(meta_field.data, meta_field.len, &meta)) {
        return send_ota_json_response(req, "parse", "invalid_meta", ESP_ERR_INVALID_ARG);
    }
    if (!extract_multipart_field(body, body_len, boundary, "firmware", &firmware_field)) {
        ESP_LOGW(TAG, "firmware field missing, try bin field");
        if (!extract_multipart_field(body, body_len, boundary, "bin", &firmware_field)) {
            return send_ota_json_response(req, "parse", "missing_firmware", ESP_ERR_INVALID_ARG);
        }
    }

    esp_err_t err = write_firmware_to_ota_partition(firmware_field.data, firmware_field.len, &meta);
    if (err != ESP_OK) {
        return send_ota_json_response(req, "write", "ota_write_failed", err);
    }

    esp_err_t resp_ret = send_ota_json_response(req, "done", "ota_success", ESP_OK);
    if (meta.reboot) {
        ESP_LOGI(TAG, "ota success, reboot requested");
        esp_restart();
    }
    return resp_ret;
}
