#include "server_network_sta_data.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "epd_display_app.h"
#include "epd_sd_power_test.h"
#include "file_serving_example_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "image_business_worker.h"
#include "led_status.h"
#include "network_ota_upload.h"
#include "network_ota_boot.h"
#include "server_network_sta_cast2pic.h"
#include "server_network_sta_cast.h"
#include "server_network_sta_daily_image.h"
#include "server_network_sta_delete.h"
#include "server_network_sta_saved_images.h"
#include "server_network_sta_slideshow.h"
#include "server_network_sta_slideshow_control.h"
#include "server_network_sta_snapshot.h"
#include "server_network_sta_upload.h"
#include "server_network_sta_upload_gate.h"
#include "server_network_sta_wifi_work_time.h"
#include "tdx_cfg.h"
#include "tdx_shared_spi.h"

static const char *TAG = "server_sta_data";
static char s_base_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX] = {0};
static SemaphoreHandle_t s_upload_mutex;

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

static char *alloc_request_body_buffer(size_t size)
{
    /* English: Prefer PSRAM for full upload bodies so internal RAM remains available for WiFi and HTTP server tasks. */
    /* 涓枃锛氬畬鏁翠笂浼犲寘浼樺厛鏀惧埌 PSRAM锛屼繚鐣欏唴閮?RAM 缁?WiFi 鍜?HTTP Server 浠诲姟浣跨敤銆?*/
    char *body = (char *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (body != NULL) {
        return body;
    }

    /* Do not allocate large upload bodies from internal RAM when PSRAM allocation fails. */
    /* PSRAM 鐢宠澶辫触鏃讹紝澶т笂浼犲寘涓嶅啀閫€鍥炲唴閮?RAM锛岄伩鍏嶆尋鐖?WiFi/httpd 鎵€闇€鍐呭瓨銆?*/
    if (size > USER_INTERNAL_RAM_FALLBACK_MAX_SIZE) {
        ESP_LOGE(TAG, "body alloc PSRAM failed len=%u",
                (unsigned int)size);
        return NULL;
    }

    /* Small requests may still fall back to internal 8-bit heap. */
    /* 灏忚姹備粛鍏佽閫€鍥炲唴閮?8-bit 鍫嗗唴瀛樸€?*/
    body = (char *)heap_caps_malloc(size, MALLOC_CAP_8BIT);
    if (body != NULL) {
        ESP_LOGW(TAG, "body alloc fallback internal len=%u", (unsigned int)size);
    }

    return body;
}

static const char *memmem_local(const char *haystack, size_t haystack_len, const char *needle, size_t needle_len)
{
    if (needle_len == 0 || haystack == NULL || needle == NULL || haystack_len < needle_len) {
        return NULL;
    }

    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return haystack + i;
        }
    }
    return NULL;
}

static void get_request_header_value(httpd_req_t *req, const char *key, char *value, size_t value_size)
{
    if (value == NULL || value_size == 0) {
        return;
    }
    value[0] = '\0';

    size_t hdr_len = httpd_req_get_hdr_value_len(req, key);
    if (hdr_len == 0 || hdr_len + 1 > value_size) {
        return;
    }
    if (httpd_req_get_hdr_value_str(req, key, value, value_size) != ESP_OK) {
        value[0] = '\0';
    }
}

static void log_request_headers(httpd_req_t *req)
{
    char content_type[SERVER_NETWORK_STA_HTTP_HEADER_VALUE_MAX] = {0};
    char content_length[32] = {0};

    get_request_header_value(req, "Content-Type", content_type, sizeof(content_type));
    get_request_header_value(req, "Content-Length", content_length, sizeof(content_length));

    ESP_LOGI(TAG, "HTTP data header len=%s type=%s",
             content_length[0] ? content_length : "<none>",
             content_type[0] ? content_type : "<none>");
}

static bool read_request_body_to_buffer(httpd_req_t *req, char *body, size_t body_size, size_t body_len)
{
    if (body == NULL || body_size <= body_len) {
        return false;
    }

    size_t received_total = 0;
    while (received_total < body_len) {
        int received = httpd_req_recv(req, body + received_total, body_len - received_total);
        if (received <= 0) {
            ESP_LOGE(TAG, "HTTP data recv failed ret=%d got=%u remain=%u",
                     received, (unsigned int)received_total, (unsigned int)(body_len - received_total));
            return false;
        }
        received_total += received;
        ServerNetworkStaWifiWorkTime_OnHttpNetworkActivity();
    }
    body[body_len] = '\0';
    return true;
}

static esp_err_t send_json_response(httpd_req_t *req, const char *json)
{
    ESP_LOGI(TAG, "network small JSON response: %s", json != NULL ? json : "{}");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json != NULL ? json : "{}");
}

static esp_err_t send_dataup_error_response(httpd_req_t *req,
                                            const char *http_status,
                                            int result,
                                            const char *message,
                                            const char *error)
{
    char json[192];
    snprintf(json, sizeof(json),
             "{\"func\":\"dataup_result\",\"result\":%d,\"message\":\"%s\",\"error\":\"%s\"}",
             result,
             message != NULL ? message : "dataUP failed",
             error != NULL ? error : "dataup_failed");
    if (http_status != NULL) {
        httpd_resp_set_status(req, http_status);
    }
    return send_json_response(req, json);
}

static esp_err_t send_invalid_json_response(httpd_req_t *req, const char *func)
{
    char json[160] = {0};
    snprintf(json, sizeof(json),
             "{\"func\":\"%s\",\"result\":%d,\"message\":\"invalid_Json\",\"stage\":\"receive_data_redirect_handler\"}",
             func != NULL ? func : "get_saved_images",
             TDX_JSON_RESULT_JSON_INVALID);
    return send_json_response(req, json);
}

static bool body_looks_like_json(const char *body, size_t body_len)
{
    if (body == NULL || body_len == 0) {
        return false;
    }

    for (size_t i = 0; i < body_len; i++) {
        if (body[i] == ' ' || body[i] == '\r' || body[i] == '\n' || body[i] == '\t') {
            continue;
        }
        return body[i] == '{' || body[i] == '[';
    }
    return false;
}

static esp_err_t send_unsupported_func_response(httpd_req_t *req)
{
    char json[128];
    snprintf(json, sizeof(json),
             "{\"func\":\"unknown_result\",\"result\":%d,\"message\":\"unsupported func\"}",
             TDX_JSON_RESULT_FUNC_UNSUPPORTED);
    return send_json_response(req, json);
}

static void log_network_small_json_body(const char *body, size_t body_len)
{
    const size_t chunk_size = 240;
    if (body == NULL) {
        ESP_LOGI(TAG, "network small JSON received len=%u body=<null>", (unsigned int)body_len);
        return;
    }

    size_t log_len = body_len < chunk_size ? body_len : chunk_size;
    ESP_LOGI(TAG,
             "network small JSON received body_len=%u log_len=%u truncated=%d: %.*s",
             (unsigned int)body_len,
             (unsigned int)log_len,
             body_len > chunk_size ? 1 : 0,
             (int)log_len,
             body);
}

static esp_err_t process_small_json_request(httpd_req_t *req, const char *body, size_t body_len)
{
    log_network_small_json_body(body, body_len);

    if (!body_looks_like_json(body, body_len)) {
        ESP_LOGW(TAG, "small JSON invalid body");
        return send_invalid_json_response(req, "get_saved_images");
    }

    esp_err_t daily_ret = ServerNetworkStaDailyImage_ProcessJson(req,
                                                                 body,
                                                                 body_len,
                                                                 s_base_path);
    if (daily_ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "small JSON func=daily_download_file ret=%s",
                 esp_err_to_name(daily_ret));
        return daily_ret;
    }

    esp_err_t snapshot_ret = ServerNetworkStaSnapshot_ProcessJson(req, body, body_len, s_base_path);
    if (snapshot_ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "small JSON func=get_snapshot ret=%s", esp_err_to_name(snapshot_ret));
        return snapshot_ret;
    }

    esp_err_t saved_ret = ServerNetworkStaSavedImages_ProcessJson(req, body, body_len, s_base_path);
    if (saved_ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "small JSON func=get_saved_images ret=%s", esp_err_to_name(saved_ret));
        return saved_ret;
    }

    esp_err_t slideshow_ret = ServerNetworkStaSlideshow_ProcessJson(req, body, body_len, s_base_path);
    if (slideshow_ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "small JSON func=start_slideshow ret=%s", esp_err_to_name(slideshow_ret));
        return slideshow_ret;
    }

    esp_err_t slideshow_control_ret = ServerNetworkStaSlideshowControl_ProcessJson(req, body, body_len, s_base_path);
    if (slideshow_control_ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "small JSON func=set_slideshow ret=%s", esp_err_to_name(slideshow_control_ret));
        return slideshow_control_ret;
    }

    esp_err_t delete_ret = ServerNetworkStaDelete_ProcessJson(req, body, body_len, s_base_path);
    if (delete_ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "small JSON func=delete ret=%s", esp_err_to_name(delete_ret));
        return delete_ret;
    }

    esp_err_t wifi_work_time_ret = ServerNetworkStaWifiWorkTime_ProcessJson(req, body, body_len);
    if (wifi_work_time_ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "small JSON func=set_wifi_work_time ret=%s", esp_err_to_name(wifi_work_time_ret));
        return wifi_work_time_ret;
    }

    ESP_LOGW(TAG, "small JSON unsupported command");
    return send_unsupported_func_response(req);
}

static bool header_value_contains(const char *headers, size_t headers_len, const char *name, const char *value)
{
    const char *name_pos = memmem_local(headers, headers_len, name, strlen(name));
    if (name_pos == NULL) {
        return false;
    }

    const char *line_end = memmem_local(name_pos, (headers + headers_len) - name_pos, "\r\n", 2);
    if (line_end == NULL) {
        line_end = headers + headers_len;
    }

    return memmem_local(name_pos, line_end - name_pos, value, strlen(value)) != NULL;
}

static bool get_disposition_value(const char *headers, size_t headers_len,
                                  const char *key, char *out, size_t out_size)
{
    char pattern[SERVER_NETWORK_STA_DATAUP_FIELD_NAME_MAX];
    int pattern_len = snprintf(pattern, sizeof(pattern), "%s=\"", key);
    const char *start = memmem_local(headers, headers_len, pattern, (size_t)pattern_len);
    if (start == NULL || out == NULL || out_size == 0) {
        return false;
    }

    start += pattern_len;
    const char *end = memchr(start, '"', (headers + headers_len) - start);
    if (end == NULL) {
        return false;
    }

    size_t copy_len = end - start;
    if (copy_len >= out_size) {
        copy_len = out_size - 1;
    }
    memcpy(out, start, copy_len);
    out[copy_len] = '\0';
    return true;
}

static esp_err_t ensure_dir(const char *path)
{
    struct stat st = {0};
    if (!example_storage_supports_directories()) {
        return ESP_OK;
    }
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? ESP_OK : ESP_FAIL;
    }
    if (mkdir(path, 0775) == 0 || errno == EEXIST) {
        return ESP_OK;
    }
    if (errno == ENOTSUP || errno == EOPNOTSUPP) {
        ESP_LOGW(TAG, "mkdir %s not supported, use flat storage path", path);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "mkdir %s failed, continue with base path", path);
    return ESP_FAIL;
}

static bool upload_file_name_is_safe(const char *file_name, const char *expected_ext)
{
    if (file_name == NULL || file_name[0] == '\0') {
        return true;
    }
    if (strcmp(file_name, ".") == 0 || strcmp(file_name, "..") == 0 ||
        strchr(file_name, '/') != NULL || strchr(file_name, '\\') != NULL) {
        return false;
    }

    size_t name_len = strlen(file_name);
    size_t base_len = name_len;
    size_t ext_len = expected_ext != NULL ? strlen(expected_ext) : 0U;
    if (ext_len > 0U && name_len > ext_len &&
        strcmp(file_name + name_len - ext_len, expected_ext) == 0) {
        base_len -= ext_len;
    }
    return base_len > 0U && base_len <= TDX_IMAGE_BASE_NAME_MAX_BYTES;
}

static esp_err_t save_upload_part(const char *field_name, const char *file_name,
                                  const char *data, size_t data_len)
{
    char dir_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    char path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX + 24];
    const char *ext = NULL;

    if (strcmp(field_name, "bin") == 0) {
        ext = ".bin";
        snprintf(dir_path, sizeof(dir_path), "%s/bin_img", s_base_path);
    } else if (strcmp(field_name, "image") == 0) {
        ext = ".jpg";
        snprintf(dir_path, sizeof(dir_path), "%s/jpg_img", s_base_path);
    } else {
#if USER_HTTP_MULTIPART_DETAIL_LOG_ENABLE
        ESP_LOGI(TAG, "multipart skip field=%s len=%u",
                 field_name != NULL ? field_name : "<null>", (unsigned int)data_len);
#endif
        return ESP_OK;
    }

    if (!upload_file_name_is_safe(file_name, ext)) {
        ESP_LOGW(TAG,
                 "multipart file name rejected field=%s len=%u max_base=%u",
                 field_name,
                 (unsigned int)(file_name != NULL ? strlen(file_name) : 0U),
                 (unsigned int)TDX_IMAGE_BASE_NAME_MAX_BYTES);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }

    if (ensure_dir(dir_path) == ESP_OK) {
        snprintf(path, sizeof(path), "%s/%s", dir_path, file_name && file_name[0] ? file_name : field_name);
    } else if (file_name != NULL && file_name[0] != '\0') {
        snprintf(path, sizeof(path), "%s/%s", s_base_path, file_name);
    } else {
        snprintf(path, sizeof(path), "%s/%s%s", s_base_path, field_name, ext);
    }

    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        TdxSharedSpi_Unlock();
        ESP_LOGE(TAG, "multipart open failed path=%s", path);
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, data_len, fp);
    fclose(fp);
    TdxSharedSpi_Unlock();
    ESP_LOGI(TAG, "multipart saved path=%s len=%u written=%u",
             path, (unsigned int)data_len, (unsigned int)written);
    return written == data_len ? ESP_OK : ESP_FAIL;
}

static esp_err_t process_multipart_upload_request(httpd_req_t *req, const char *body,
                                                  size_t body_len, const char *content_type)
{
    (void)req;
    char *boundary = strstr(content_type, "boundary=");
    if (boundary == NULL) {
        ESP_LOGW(TAG, "multipart missing boundary type=%s",
                 content_type != NULL ? content_type : "<null>");
        return ESP_FAIL;
    }
    boundary += strlen("boundary=");

    char marker[96];
    int marker_len = snprintf(marker, sizeof(marker), "--%s", boundary);
    const char *cursor = body;
    const char *end = body + body_len;

#if USER_HTTP_MULTIPART_DETAIL_LOG_ENABLE
    ESP_LOGI(TAG, "multipart fallback len=%u boundary=%s",
             (unsigned int)body_len, boundary);
#endif

    while (cursor < end) {
        const char *part = memmem_local(cursor, end - cursor, marker, (size_t)marker_len);
        if (part == NULL) {
            break;
        }
        part += marker_len;
        if (part + 2 <= end && part[0] == '-' && part[1] == '-') {
            break;
        }
        if (part + 2 <= end && part[0] == '\r' && part[1] == '\n') {
            part += 2;
        }

        const char *headers_end = memmem_local(part, end - part, "\r\n\r\n", 4);
        if (headers_end == NULL) {
            break;
        }

        const char *data_start = headers_end + 4;
        const char *next = memmem_local(data_start, end - data_start, marker, (size_t)marker_len);
        if (next == NULL) {
            break;
        }

        const char *data_end = next;
        if (data_end >= data_start + 2 && data_end[-2] == '\r' && data_end[-1] == '\n') {
            data_end -= 2;
        }

        char field_name[SERVER_NETWORK_STA_DATAUP_FIELD_NAME_MAX] = {0};
        char file_name[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX] = {0};
        size_t headers_len = headers_end - part;
        if (header_value_contains(part, headers_len, "Content-Disposition", "form-data") &&
            get_disposition_value(part, headers_len, "name", field_name, sizeof(field_name))) {
            (void)get_disposition_value(part, headers_len, "filename", file_name, sizeof(file_name));
#if USER_HTTP_MULTIPART_DETAIL_LOG_ENABLE
            ESP_LOGI(TAG, "multipart part field=%s file=%s len=%u",
                     field_name, file_name[0] ? file_name : "<none>", (unsigned int)(data_end - data_start));
#endif
            ESP_RETURN_ON_ERROR(save_upload_part(field_name, file_name, data_start, data_end - data_start),
                                TAG, "save multipart field failed");
        }

        cursor = next;
    }

    return ESP_OK;
}

static esp_err_t receive_data_redirect_handler_impl(httpd_req_t *req)
{
    size_t remaining = req->content_len;
    const char *uri = req->uri;
    char content_type[SERVER_NETWORK_STA_HTTP_HEADER_VALUE_MAX] = {0};
    bool upload_mutex_locked = false;

    get_request_header_value(req, "Content-Type", content_type, sizeof(content_type));
    ServerNetworkStaWifiWorkTime_OnHttpNetworkActivity();
    log_request_headers(req);
    ESP_LOGI(TAG, "HTTP data enter uri=%s len=%u type=%s",
             uri != NULL ? uri : "<null>", (unsigned int)remaining,
             content_type[0] ? content_type : "<none>");

    bool is_network_ota = NetworkOtaUpload_IsOtaRequest(req, content_type);
    bool is_multipart = (strstr(content_type, "multipart/form-data") != NULL);
    if (is_multipart && NetworkOtaUpload_IsRestartPending()) {
        ESP_LOGW(TAG, "HTTP multipart rejected while OTA restart is pending uri=%s",
                 uri != NULL ? uri : "<null>");
        if (is_network_ota) {
            return NetworkOtaUpload_SendErrorAndFinish(req,
                                                       "upload_busy",
                                                       "restart_pending",
                                                       ESP_ERR_INVALID_STATE);
        }
        return send_json_response(req,
                                  "{\"func\":\"dataup_result\",\"result\":1007,\"message\":\"restart_pending\",\"error\":\"restart_pending\"}");
    }

    bool epd_busy = ServerNetworkStaEpdDisplay_IsBusy();
    uint32_t cast_owner_mask =
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_CAST) |
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_CAST2PIC);
    bool cast_busy = ImageBusinessWorker_IsAnyOwnerBusy(cast_owner_mask);
    bool upload_reserved = ServerNetworkStaUploadGate_IsReserved();
    if (!is_network_ota && is_multipart &&
        (epd_busy || cast_busy || upload_reserved)) {
        const char *message = epd_busy ? "epd_busy" :
                               cast_busy ? "cast_busy" :
                               "upload_busy";
        char busy_json[112];
        snprintf(busy_json,
                 sizeof(busy_json),
                 "{\"func\":\"dataup_result\",\"result\":%d,\"message\":\"%s\",\"error\":\"%s\"}",
                 TDX_JSON_RESULT_BUSY,
                 message,
                 message);
        ESP_LOGW(TAG, "HTTP data multipart rejected uri=%s len=%u reason=%s epd=%d cast=%d upload=%d",
                 uri != NULL ? uri : "<null>",
                 (unsigned int)remaining,
                 message,
                 epd_busy ? 1 : 0,
                 cast_busy ? 1 : 0,
                 upload_reserved ? 1 : 0);
        httpd_resp_set_hdr(req, "Connection", "close");
        return send_json_response(req, busy_json);
    }

    if (is_multipart && s_upload_mutex != NULL) {
        if (xSemaphoreTake(s_upload_mutex, 0) != pdTRUE) {
            ESP_LOGW(TAG, "HTTP upload busy uri=%s len=%u",
                     uri != NULL ? uri : "<null>", (unsigned int)remaining);
            if (is_network_ota) {
                esp_err_t ret = NetworkOtaUpload_SendErrorAndFinish(req,
                                                                    "upload_busy",
                                                                    "upload_busy",
                                                                    ESP_ERR_TIMEOUT);
                return ret;
            }
            return send_json_response(req,
                                      "{\"func\":\"dataup_result\",\"result\":1007,\"message\":\"upload_busy\",\"error\":\"upload_busy\"}");
        }
        upload_mutex_locked = true;
#if USER_HTTP_MULTIPART_DETAIL_LOG_ENABLE
        ESP_LOGI(TAG, "HTTP upload slot uri=%s",
                 uri != NULL ? uri : "<null>");
#endif
    }
    if (is_network_ota) {
        ServerNetworkStaWifiWorkTime_SetOtaReceiveInProgress(true);
    }

    if (remaining == 0) {
        if (upload_mutex_locked) {
            xSemaphoreGive(s_upload_mutex);
        }
        ESP_LOGW(TAG, "HTTP data empty uri=%s", uri != NULL ? uri : "<null>");
        if (is_network_ota) {
            esp_err_t ret = NetworkOtaUpload_SendErrorAndFinish(req,
                                                                "empty_body",
                                                                "empty_body",
                                                                ESP_ERR_INVALID_SIZE);
            ServerNetworkStaWifiWorkTime_SetOtaReceiveInProgress(false);
            return ret;
        }
        return send_invalid_json_response(req, "get_saved_images");
    }

    size_t request_body_max = is_network_ota ? NetworkOtaUpload_GetMaxBodySize() : SERVER_NETWORK_STA_DATAUP_MAX_BODY_SIZE;
    if (remaining > request_body_max) {
        if (upload_mutex_locked) {
            xSemaphoreGive(s_upload_mutex);
        }
        ESP_LOGW(TAG, "HTTP data too large uri=%s len=%u max=%u",
                 uri != NULL ? uri : "<null>", (unsigned int)remaining,
                 (unsigned int)request_body_max);
        if (is_network_ota) {
            esp_err_t ret = NetworkOtaUpload_SendErrorAndFinish(req,
                                                                "body_too_large",
                                                                "body_too_large",
                                                                ESP_ERR_INVALID_SIZE);
            ServerNetworkStaWifiWorkTime_SetOtaReceiveInProgress(false);
            return ret;
        }
        return send_dataup_error_response(req,
                                          "413 Content Too Large",
                                          TDX_JSON_RESULT_BODY_TOO_LARGE,
                                          "body too large",
                                          "body_too_large");
    }

    bool is_small_json = (!is_multipart && remaining <= SERVER_NETWORK_STA_SMALL_JSON_BODY_MAX);
    bool network_led_active = is_network_ota || is_multipart ||
                              remaining > SERVER_NETWORK_STA_SMALL_JSON_BODY_MAX;
    if (is_network_ota) {
        UserLedStatus_OtaBegin();
    }
    if (network_led_active) {
        UserLedStatus_ActivityBegin(USER_LED_ACTIVITY_NETWORK);
    }
    char *body = alloc_request_body_buffer(remaining + 1);
    if (body == NULL) {
        if (upload_mutex_locked) {
            xSemaphoreGive(s_upload_mutex);
        }
        if (network_led_active) {
            UserLedStatus_ActivityEnd(USER_LED_ACTIVITY_NETWORK);
        }
        if (is_network_ota) {
            UserLedStatus_OtaEnd(false);
        }
        ESP_LOGE(TAG, "body alloc failed len=%u", (unsigned int)remaining);
        esp_err_t ret = send_dataup_error_response(req,
                                                   HTTPD_500,
                                                   TDX_JSON_RESULT_NO_MEMORY,
                                                   "no memory",
                                                   "no_memory");
        if (is_network_ota) {
            ServerNetworkStaWifiWorkTime_SetOtaReceiveInProgress(false);
        }
        return ret;
    }
    if (is_network_ota) {
        log_heap_watermark("body_alloc");
    }

    if (!read_request_body_to_buffer(req, body, remaining + 1, remaining)) {
        heap_caps_free(body);
        if (is_network_ota) {
            log_heap_watermark("body_free");
        }
        if (upload_mutex_locked) {
            xSemaphoreGive(s_upload_mutex);
        }
        if (network_led_active) {
            UserLedStatus_ActivityEnd(USER_LED_ACTIVITY_NETWORK);
        }
        if (is_network_ota) {
            UserLedStatus_OtaEnd(false);
            ServerNetworkStaWifiWorkTime_SetOtaReceiveInProgress(false);
        }
        return ESP_FAIL;
    }

    esp_err_t resp_ret = ESP_FAIL;
    bool body_taken = false;
    if (is_network_ota) {
        ESP_LOGI(TAG, "HTTP data dispatch=ota uri=%s len=%u",
                 uri != NULL ? uri : "<null>", (unsigned int)remaining);
        resp_ret = NetworkOtaUpload_ProcessReceivedBody(req, body, remaining, content_type);
    } else if (is_small_json) {
        ESP_LOGI(TAG, "HTTP data dispatch=json uri=%s len=%u",
                 uri != NULL ? uri : "<null>", (unsigned int)remaining);
        resp_ret = process_small_json_request(req, body, remaining);
    } else if (is_multipart) {
        ESP_LOGI(TAG, "HTTP data dispatch=multipart len=%u", (unsigned int)remaining);
        resp_ret = ServerNetworkStaCast2Pic_Process(req, body, remaining, content_type, s_base_path, &body_taken);
        if (resp_ret == ESP_ERR_NOT_SUPPORTED) {
            resp_ret = ServerNetworkStaCast_Process(req, body, remaining, content_type, s_base_path, &body_taken);
        }
        if (resp_ret == ESP_ERR_NOT_SUPPORTED) {
            resp_ret = ServerNetworkStaUpload_Process(req, body, remaining, content_type, s_base_path, &body_taken);
        }
        if (resp_ret == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGI(TAG, "HTTP data multipart fallback uri=%s",
                     uri != NULL ? uri : "<null>");
            resp_ret = process_multipart_upload_request(req, body, remaining, content_type);
            if (resp_ret == ESP_OK) {
                resp_ret = send_json_response(req,
                                              "{\"func\":\"dataup_result\",\"result\":0,\"message\":\"upload_success\",\"stage\":\"dataUP\"}");
            } else {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
            }
        }
    } else {
        ESP_LOGW(TAG, "HTTP data invalid body uri=%s len=%u",
                 uri != NULL ? uri : "<null>", (unsigned int)remaining);
        resp_ret = send_invalid_json_response(req, "get_saved_images");
    }

    if (!body_taken) {
        heap_caps_free(body);
    }
    if (is_network_ota) {
        log_heap_watermark("body_free");
    }

    if (upload_mutex_locked) {
        xSemaphoreGive(s_upload_mutex);
    }

    if (network_led_active) {
        UserLedStatus_ActivityEnd(USER_LED_ACTIVITY_NETWORK);
    }
    if (is_network_ota) {
        /*
         * A successful OTA now returns from the HTTP handler before its dedicated
         * restart task runs. Preserve the success indication during that short
         * window; failed OTA requests continue to use the existing failure state.
         */
        if (!NetworkOtaUpload_IsRestartPending()) {
            UserLedStatus_OtaEnd(false);
        }
        ServerNetworkStaWifiWorkTime_SetOtaReceiveInProgress(false);
    }

    ESP_LOGI(TAG, "HTTP data handler done uri=%s len=%u ret=%s",
             uri != NULL ? uri : "<null>",
             (unsigned int)remaining,
             esp_err_to_name(resp_ret));
    return resp_ret;
}

esp_err_t receive_data_redirect_handler(httpd_req_t *req)
{
    char content_type[SERVER_NETWORK_STA_HTTP_HEADER_VALUE_MAX] = {0};
    get_request_header_value(req, "Content-Type", content_type, sizeof(content_type));
    bool is_multipart = strstr(content_type, "multipart/form-data") != NULL;
    bool is_network_ota = NetworkOtaUpload_IsOtaRequest(req, content_type);

    if (is_multipart && !is_network_ota) {
        esp_err_t power_ret = EpdSdPowerTest_NetworkTryBegin();
        if (power_ret != ESP_OK) {
            ServerNetworkStaWifiWorkTime_OnHttpNetworkActivity();
            ESP_LOGW(TAG, "HTTP multipart rejected before body reason=sd_power_busy len=%u",
                     (unsigned int)req->content_len);
            return send_json_response(
                req,
                "{\"func\":\"dataup_result\",\"result\":1007,\"message\":\"sd_power_busy\",\"error\":\"sd_power_busy\"}");
        }
        esp_err_t ret = receive_data_redirect_handler_impl(req);
        EpdSdPowerTest_NetworkEnd();
        return ret;
    }

    EpdSdPowerTest_NetworkBegin();
    esp_err_t ret = receive_data_redirect_handler_impl(req);
    EpdSdPowerTest_NetworkEnd();
    return ret;
}

esp_err_t server_network_sta_net_data_register_handlers(httpd_handle_t server, const char *base_path)
{
    strlcpy(s_base_path, base_path, sizeof(s_base_path));
    if (s_upload_mutex == NULL) {
        s_upload_mutex = xSemaphoreCreateMutex();
        if (s_upload_mutex == NULL) {
            ESP_LOGE(TAG, "net handlers: upload mutex failed");
            return ESP_ERR_NO_MEM;
        }
    }

    // Register /dataUP with the migrated receive_data_redirect_handler so old web requests use one receive path.
    // 涓枃锛氬皢 /dataUP 娉ㄥ唽鍒扮Щ妞嶇殑鎺ユ敹鍑芥暟锛岃鏃х綉椤佃姹傜粺涓€璧拌繖涓€鏉℃敹鍖呰矾寰勩€?
    httpd_uri_t dataup = {
        .uri = "/dataUP",
        .method = HTTP_POST,
        .handler = receive_data_redirect_handler,
        .user_ctx = NULL,
    };

    esp_err_t ret = httpd_register_uri_handler(server, &dataup);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register /dataUP failed ret=%s", esp_err_to_name(ret));
        return ret;
    }

    // Register OTA upload endpoints on the same receive dispatcher so firmware and image uploads share diagnostics.
    // 涓枃锛歄TA 涓婁紶鎺ュ彛涔熸敞鍐屽埌鍚屼竴鎺ユ敹鍑芥暟锛屼究浜庡浐浠朵笂浼犲拰鍥剧墖涓婁紶鍏辩敤璇婃柇鏃ュ織銆?
    httpd_uri_t ota = {
        .uri = "/ota",
        .method = HTTP_POST,
        .handler = receive_data_redirect_handler,
        .user_ctx = NULL,
    };
    ret = httpd_register_uri_handler(server, &ota);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register /ota failed ret=%s", esp_err_to_name(ret));
        return ret;
    }

    // Keep the old project's /ota_upload alias for tools that still post firmware to that URI.
    // 涓枃锛氫繚鐣欐棫椤圭洰鐨?/ota_upload 鍒悕锛屽吋瀹逛粛鐒跺悜璇?URI 涓婁紶鍥轰欢鐨勫伐鍏枫€?
    httpd_uri_t ota_upload = {
        .uri = "/ota_upload",
        .method = HTTP_POST,
        .handler = receive_data_redirect_handler,
        .user_ctx = NULL,
    };
    ret = httpd_register_uri_handler(server, &ota_upload);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register /ota_upload failed ret=%s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "HTTP POST ready /dataUP /ota /ota_upload");
    /*
     * Confirm a pending OTA image only after every upload endpoint is available.
     * A validation failure must not remove the recovery endpoints from service.
     */
    esp_err_t ota_valid_ret = NetworkOtaBoot_ConfirmCurrentImage();
    if (ota_valid_ret != ESP_OK) {
        ESP_LOGW(TAG, "HTTP POST remains ready after OTA app validation failure");
    }
    return ESP_OK;
}
