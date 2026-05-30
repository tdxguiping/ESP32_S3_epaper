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
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "led_status.h"
#include "network_ota_upload.h"
#include "server_network_sta_cast.h"
#include "server_network_sta_delete.h"
#include "server_network_sta_saved_images.h"
#include "server_network_sta_slideshow.h"
#include "server_network_sta_slideshow_control.h"
#include "server_network_sta_upload.h"
#include "server_network_sta_wifi_work_time.h"
#include "tdx_cfg.h"

static const char *TAG = "server_sta_data";
static char s_base_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX] = {0};
static SemaphoreHandle_t s_upload_mutex;

static void log_upload_heap_state(const char *stage)
{
    /* English: Print heap state before and after large network body allocation. */
    /* 中文：在大网络包申请前后打印堆状态，方便判断是否缺少连续内存。 */
    ESP_LOGI(TAG,
             "heap %s free_8bit=%u largest_8bit=%u free_internal=%u largest_internal=%u free_spiram=%u largest_spiram=%u",
             stage != NULL ? stage : "<null>",
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

static char *alloc_request_body_buffer(size_t size)
{
    /* English: Prefer PSRAM for full upload bodies so internal RAM remains available for WiFi and HTTP server tasks. */
    /* 中文：完整上传包优先放到 PSRAM，保留内部 RAM 给 WiFi 和 HTTP Server 任务使用。 */
    char *body = (char *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (body != NULL) {
        ESP_LOGI(TAG, "receive_data_redirect_handler: body allocated from PSRAM ptr=%p size=%u",
                 body, (unsigned int)size);
        return body;
    }

    /* English: Fall back to any 8-bit capable heap when PSRAM is disabled or unavailable. */
    /* 中文：PSRAM 未开启或不可用时，退回到任意 8-bit 可访问堆内存。 */
    body = (char *)heap_caps_malloc(size, MALLOC_CAP_8BIT);
    if (body != NULL) {
        ESP_LOGW(TAG, "receive_data_redirect_handler: body allocated from 8BIT heap ptr=%p size=%u",
                 body, (unsigned int)size);
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
    char connection[32] = {0};
    char host[64] = {0};

    get_request_header_value(req, "Content-Type", content_type, sizeof(content_type));
    get_request_header_value(req, "Content-Length", content_length, sizeof(content_length));
    get_request_header_value(req, "Connection", connection, sizeof(connection));
    get_request_header_value(req, "Host", host, sizeof(host));

    ESP_LOGI(TAG, "headers Content-Type=%s", content_type[0] ? content_type : "<none>");
    ESP_LOGI(TAG, "headers Content-Length=%s", content_length[0] ? content_length : "<none>");
    ESP_LOGI(TAG, "headers Connection=%s", connection[0] ? connection : "<none>");
    ESP_LOGI(TAG, "headers Host=%s", host[0] ? host : "<none>");
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
            ESP_LOGE(TAG, "receive_data_redirect_handler: recv failed ret=%d received=%u remaining=%u",
                     received, (unsigned int)received_total, (unsigned int)(body_len - received_total));
            return false;
        }
        received_total += received;
        ESP_LOGI(TAG, "receive_data_redirect_handler: recv progress received=%u remaining=%u",
                 (unsigned int)received_total, (unsigned int)(body_len - received_total));
    }
    body[body_len] = '\0';
    return true;
}

static esp_err_t send_json_response(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json != NULL ? json : "{}");
}

static esp_err_t send_invalid_json_response(httpd_req_t *req, const char *func)
{
    char json[160] = {0};
    snprintf(json, sizeof(json),
             "{\"func\":\"%s\",\"result\":1,\"message\":\"invalid_Json\",\"stage\":\"receive_data_redirect_handler\"}",
             func != NULL ? func : "get_saved_images");
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

static esp_err_t send_ping_result_response(httpd_req_t *req)
{
    ESP_LOGI(TAG, "send_ping_result_response: receive dispatcher alive");
    return send_json_response(req, "{\"func\":\"ping_result\",\"result\":0,\"message\":\"Server Network STA receive ok\"}");
}

static esp_err_t process_small_json_request(httpd_req_t *req, const char *body, size_t body_len)
{
    ESP_LOGI(TAG, "process_small_json_request: body_len=%u body=%s",
             (unsigned int)body_len, body != NULL ? body : "<null>");

    if (!body_looks_like_json(body, body_len)) {
        ESP_LOGW(TAG, "process_small_json_request: invalid non-json body");
        return send_invalid_json_response(req, "get_saved_images");
    }

    esp_err_t saved_ret = ServerNetworkStaSavedImages_ProcessJson(req, body, body_len, s_base_path);
    if (saved_ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "process_small_json_request: get_saved_images ret=%s", esp_err_to_name(saved_ret));
        return saved_ret;
    }

    esp_err_t slideshow_ret = ServerNetworkStaSlideshow_ProcessJson(req, body, body_len, s_base_path);
    if (slideshow_ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "process_small_json_request: start_slideshow ret=%s", esp_err_to_name(slideshow_ret));
        return slideshow_ret;
    }

    esp_err_t slideshow_control_ret = ServerNetworkStaSlideshowControl_ProcessJson(req, body, body_len, s_base_path);
    if (slideshow_control_ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "process_small_json_request: set_slideshow ret=%s", esp_err_to_name(slideshow_control_ret));
        return slideshow_control_ret;
    }

    esp_err_t delete_ret = ServerNetworkStaDelete_ProcessJson(req, body, body_len, s_base_path);
    if (delete_ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "process_small_json_request: delete ret=%s", esp_err_to_name(delete_ret));
        return delete_ret;
    }

    esp_err_t wifi_work_time_ret = ServerNetworkStaWifiWorkTime_ProcessJson(req, body, body_len);
    if (wifi_work_time_ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "process_small_json_request: set_wifi_work_time ret=%s", esp_err_to_name(wifi_work_time_ret));
        return wifi_work_time_ret;
    }

    ESP_LOGW(TAG, "process_small_json_request: unsupported json command");
    return send_ping_result_response(req);
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
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? ESP_OK : ESP_FAIL;
    }
    if (mkdir(path, 0775) == 0 || errno == EEXIST) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "mkdir %s failed, continue with base path", path);
    return ESP_FAIL;
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
        ESP_LOGI(TAG, "process_multipart_upload_request: skip field=%s len=%u",
                 field_name != NULL ? field_name : "<null>", (unsigned int)data_len);
        return ESP_OK;
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
        ESP_LOGE(TAG, "process_multipart_upload_request: open upload output failed path=%s", path);
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, data_len, fp);
    fclose(fp);
    ESP_LOGI(TAG, "process_multipart_upload_request: saved path=%s len=%u written=%u",
             path, (unsigned int)data_len, (unsigned int)written);
    return written == data_len ? ESP_OK : ESP_FAIL;
}

static esp_err_t process_multipart_upload_request(httpd_req_t *req, const char *body,
                                                  size_t body_len, const char *content_type)
{
    (void)req;
    char *boundary = strstr(content_type, "boundary=");
    if (boundary == NULL) {
        ESP_LOGW(TAG, "process_multipart_upload_request: missing boundary content_type=%s",
                 content_type != NULL ? content_type : "<null>");
        return ESP_FAIL;
    }
    boundary += strlen("boundary=");

    char marker[96];
    int marker_len = snprintf(marker, sizeof(marker), "--%s", boundary);
    const char *cursor = body;
    const char *end = body + body_len;

    ESP_LOGI(TAG, "process_multipart_upload_request: body_len=%u boundary=%s",
             (unsigned int)body_len, boundary);

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
            ESP_LOGI(TAG, "process_multipart_upload_request: part field=%s file=%s len=%u",
                     field_name, file_name[0] ? file_name : "<none>", (unsigned int)(data_end - data_start));
            ESP_RETURN_ON_ERROR(save_upload_part(field_name, file_name, data_start, data_end - data_start),
                                TAG, "save multipart field failed");
        }

        cursor = next;
    }

    return ESP_OK;
}

esp_err_t receive_data_redirect_handler(httpd_req_t *req)
{
    size_t remaining = req->content_len;
    const char *uri = req->uri;
    char content_type[SERVER_NETWORK_STA_HTTP_HEADER_VALUE_MAX] = {0};
    bool upload_mutex_locked = false;

    get_request_header_value(req, "Content-Type", content_type, sizeof(content_type));
    ServerNetworkStaWifiWorkTime_OnNetworkData();
    log_request_headers(req);
    ESP_LOGI(TAG, "receive_data_redirect_handler: enter uri=%s len=%u content_type=%s",
             uri != NULL ? uri : "<null>", (unsigned int)remaining,
             content_type[0] ? content_type : "<none>");

    bool is_network_ota = NetworkOtaUpload_IsOtaRequest(req, content_type);
    if (strstr(content_type, "multipart/form-data") != NULL && s_upload_mutex != NULL) {
        if (xSemaphoreTake(s_upload_mutex, 0) != pdTRUE) {
            ESP_LOGW(TAG, "receive_data_redirect_handler: reject extra upload uri=%s len=%u",
                     uri != NULL ? uri : "<null>", (unsigned int)remaining);
            if (is_network_ota) {
                return NetworkOtaUpload_SendErrorAndFinish(req, "upload_busy", "upload_busy", ESP_ERR_TIMEOUT);
            }
            return send_json_response(req,
                                      "{\"result\":1,\"message\":\"upload_busy\",\"error\":\"upload_busy\"}");
        }
        upload_mutex_locked = true;
        ESP_LOGI(TAG, "receive_data_redirect_handler: upload slot acquired uri=%s",
                 uri != NULL ? uri : "<null>");
    }

    if (remaining == 0) {
        if (upload_mutex_locked) {
            xSemaphoreGive(s_upload_mutex);
        }
        ESP_LOGW(TAG, "receive_data_redirect_handler: empty body uri=%s", uri != NULL ? uri : "<null>");
        if (is_network_ota) {
            return NetworkOtaUpload_SendErrorAndFinish(req, "empty_body", "empty_body", ESP_ERR_INVALID_SIZE);
        }
        return send_invalid_json_response(req, "get_saved_images");
    }

    size_t request_body_max = is_network_ota ? NetworkOtaUpload_GetMaxBodySize() : SERVER_NETWORK_STA_DATAUP_MAX_BODY_SIZE;
    if (remaining > request_body_max) {
        if (upload_mutex_locked) {
            xSemaphoreGive(s_upload_mutex);
        }
        ESP_LOGW(TAG, "receive_data_redirect_handler: body too large uri=%s len=%u max=%u",
                 uri != NULL ? uri : "<null>", (unsigned int)remaining,
                 (unsigned int)request_body_max);
        if (is_network_ota) {
            return NetworkOtaUpload_SendErrorAndFinish(req, "body_too_large", "body_too_large", ESP_ERR_INVALID_SIZE);
        }
        httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "Request body too large");
        return ESP_OK;
    }

    bool is_multipart = (strstr(content_type, "multipart/form-data") != NULL);
    bool is_small_json = (!is_multipart && remaining <= SERVER_NETWORK_STA_SMALL_JSON_BODY_MAX);
    if (is_multipart) {
        UserLedStatus_Set(USER_LED_STATE_TRANSFER);
    }
    log_upload_heap_state("before_body_alloc");
    char *body = alloc_request_body_buffer(remaining + 1);
    if (body == NULL) {
        if (upload_mutex_locked) {
            xSemaphoreGive(s_upload_mutex);
        }
        log_upload_heap_state("body_alloc_failed");
        ESP_LOGE(TAG, "receive_data_redirect_handler: body alloc failed uri=%s len=%u need=%u",
                 uri != NULL ? uri : "<null>", (unsigned int)remaining, (unsigned int)(remaining + 1));
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "receive_data_redirect_handler: body alloc ok uri=%s len=%u ptr=%p",
             uri != NULL ? uri : "<null>", (unsigned int)remaining, body);
    if (!read_request_body_to_buffer(req, body, remaining + 1, remaining)) {
        heap_caps_free(body);
        if (upload_mutex_locked) {
            xSemaphoreGive(s_upload_mutex);
        }
        return ESP_FAIL;
    }

    esp_err_t resp_ret = ESP_FAIL;
    if (is_network_ota) {
        ESP_LOGI(TAG, "receive_data_redirect_handler: dispatch ota uri=%s len=%u",
                 uri != NULL ? uri : "<null>", (unsigned int)remaining);
        resp_ret = NetworkOtaUpload_ProcessReceivedBody(req, body, remaining, content_type);
    } else if (is_small_json) {
        ESP_LOGI(TAG, "receive_data_redirect_handler: dispatch small json uri=%s len=%u",
                 uri != NULL ? uri : "<null>", (unsigned int)remaining);
        resp_ret = process_small_json_request(req, body, remaining);
    } else if (is_multipart) {
        ESP_LOGI(TAG, "receive_data_redirect_handler: dispatch multipart uri=%s len=%u",
                 uri != NULL ? uri : "<null>", (unsigned int)remaining);
        resp_ret = ServerNetworkStaCast_Process(req, body, remaining, content_type, s_base_path);
        if (resp_ret == ESP_ERR_NOT_SUPPORTED) {
            resp_ret = ServerNetworkStaUpload_Process(req, body, remaining, content_type, s_base_path);
        }
        if (resp_ret == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGI(TAG, "receive_data_redirect_handler: fallback legacy multipart save uri=%s",
                     uri != NULL ? uri : "<null>");
            resp_ret = process_multipart_upload_request(req, body, remaining, content_type);
            if (resp_ret == ESP_OK) {
                resp_ret = send_json_response(req,
                                              "{\"result\":0,\"message\":\"upload_success\",\"stage\":\"dataUP\"}");
            } else {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
            }
        }
    } else {
        ESP_LOGW(TAG, "receive_data_redirect_handler: invalid non-multipart large body uri=%s len=%u",
                 uri != NULL ? uri : "<null>", (unsigned int)remaining);
        resp_ret = send_invalid_json_response(req, "get_saved_images");
    }

    ESP_LOGI(TAG, "receive_data_redirect_handler: free body uri=%s ptr=%p len=%u ret=%s",
             uri != NULL ? uri : "<null>", body, (unsigned int)remaining, esp_err_to_name(resp_ret));
    heap_caps_free(body);
    log_upload_heap_state("after_body_free");

    if (upload_mutex_locked) {
        xSemaphoreGive(s_upload_mutex);
        ESP_LOGI(TAG, "receive_data_redirect_handler: upload slot released uri=%s ret=%s",
                 uri != NULL ? uri : "<null>", esp_err_to_name(resp_ret));
    }

    if (is_multipart) {
        UserLedStatus_Set(resp_ret == ESP_OK ? USER_LED_STATE_SUCCESS : USER_LED_STATE_OPERATION_FAIL);
    }

    return resp_ret;
}

esp_err_t server_network_sta_net_data_register_handlers(httpd_handle_t server, const char *base_path)
{
    strlcpy(s_base_path, base_path, sizeof(s_base_path));
    (void)NetworkOtaUpload_MarkCurrentAppValidIfPending();
    if (s_upload_mutex == NULL) {
        s_upload_mutex = xSemaphoreCreateMutex();
        if (s_upload_mutex == NULL) {
            ESP_LOGE(TAG, "server_network_sta_net_data_register_handlers: create upload mutex failed");
            return ESP_ERR_NO_MEM;
        }
    }

    // Register /dataUP with the migrated receive_data_redirect_handler so old web requests use one receive path.
    // 将 /dataUP 注册到移植的 receive_data_redirect_handler，让旧网页请求统一走这一条收包路径。
    httpd_uri_t dataup = {
        .uri = "/dataUP",
        .method = HTTP_POST,
        .handler = receive_data_redirect_handler,
        .user_ctx = NULL,
    };

    esp_err_t ret = httpd_register_uri_handler(server, &dataup);
    ESP_LOGI(TAG, "server_network_sta_net_data_register_handlers: register /dataUP ret=%s base_path=%s",
             esp_err_to_name(ret), s_base_path);
    if (ret != ESP_OK) {
        return ret;
    }

    // Register OTA upload endpoints on the same receive dispatcher so firmware and image uploads share diagnostics.
    // 将 OTA 上传接口也注册到同一个接收分发函数，便于固件上传和图片上传共用调试日志。
    httpd_uri_t ota = {
        .uri = "/ota",
        .method = HTTP_POST,
        .handler = receive_data_redirect_handler,
        .user_ctx = NULL,
    };
    ret = httpd_register_uri_handler(server, &ota);
    ESP_LOGI(TAG, "server_network_sta_net_data_register_handlers: register /ota ret=%s", esp_err_to_name(ret));
    if (ret != ESP_OK) {
        return ret;
    }

    // Keep the old project's /ota_upload alias for tools that still post firmware to that URI.
    // 保留旧项目的 /ota_upload 别名，兼容仍然向该 URI 上传固件的工具。
    httpd_uri_t ota_upload = {
        .uri = "/ota_upload",
        .method = HTTP_POST,
        .handler = receive_data_redirect_handler,
        .user_ctx = NULL,
    };
    ret = httpd_register_uri_handler(server, &ota_upload);
    ESP_LOGI(TAG, "server_network_sta_net_data_register_handlers: register /ota_upload ret=%s", esp_err_to_name(ret));
    return ret;
}
