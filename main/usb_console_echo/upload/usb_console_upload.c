#include "usb_console_upload.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "cast_core.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "tdx_cfg.h"
#include "tdx_shared_spi.h"
#include "usb_console_common.h"

static const char *TAG = "usb_console_upload";

static uint32_t elapsed_ms_since(int64_t start_us)
{
    return (uint32_t)((esp_timer_get_time() - start_us) / 1000);
}

static bool content_type_is_octet_stream(const char *content_type)
{
    return content_type != NULL && strncasecmp(content_type, "application/octet-stream", strlen("application/octet-stream")) == 0;
}

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static bool url_decode_path_value(const char *src, char *out, size_t out_size)
{
    size_t used = 0;

    if (src == NULL || out == NULL || out_size == 0) {
        return false;
    }

    while (*src != '\0' && *src != '&' && used + 1 < out_size) {
        if (*src == '%' && src[1] != '\0' && src[2] != '\0') {
            int hi = hex_value(src[1]);
            int lo = hex_value(src[2]);
            if (hi < 0 || lo < 0) {
                return false;
            }
            out[used++] = (char)((hi << 4) | lo);
            src += 3;
        } else if (*src == '+') {
            out[used++] = ' ';
            src++;
        } else {
            out[used++] = *src++;
        }
    }

    out[used] = '\0';
    return *src == '\0' || *src == '&';
}

static bool query_path_value(const char *uri, char *out, size_t out_size)
{
    const char *query = strchr(uri, '?');

    if (uri == NULL || out == NULL || out_size == 0 || query == NULL) {
        return false;
    }

    query++;
    while (*query != '\0') {
        if (strncmp(query, "path=", 5) == 0) {
            return url_decode_path_value(query + 5, out, out_size);
        }
        query = strchr(query, '&');
        if (query == NULL) {
            break;
        }
        query++;
    }

    return false;
}

static bool raw_upload_path_is_safe(const char *path)
{
    size_t base_len = strlen(USB_CONSOLE_BASE_PATH);

    if (path == NULL || path[0] == '\0') {
        return false;
    }
    if (strncmp(path, USB_CONSOLE_BASE_PATH, base_len) != 0 ||
        (path[base_len] != '\0' && path[base_len] != '/')) {
        return false;
    }
    if (path[base_len] == '\0' || path[strlen(path) - 1] == '/') {
        return false;
    }
    if (strstr(path, "..") != NULL || strchr(path, '\\') != NULL) {
        return false;
    }
    return true;
}

static esp_err_t ensure_parent_dirs(const char *path)
{
    char temp[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX + 64];
    char *cursor = NULL;

    if (path == NULL || strlen(path) >= sizeof(temp)) {
        return ESP_ERR_INVALID_ARG;
    }

    strcpy(temp, path);
    cursor = temp + strlen(USB_CONSOLE_BASE_PATH) + 1;
    while ((cursor = strchr(cursor, '/')) != NULL) {
        *cursor = '\0';
        if (mkdir(temp, 0775) != 0 && errno != EEXIST) {
            return ESP_FAIL;
        }
        *cursor = '/';
        cursor++;
    }

    return ESP_OK;
}

static esp_err_t save_raw_body_to_path(const char *path, const char *body, size_t body_len)
{
    int64_t start_us = esp_timer_get_time();
    char tmp_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX + 72];
    FILE *fp = NULL;
    void *io_buf = NULL;

    if (!raw_upload_path_is_safe(path) || body == NULL || strlen(path) + 4 >= sizeof(tmp_path)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }
    esp_err_t dir_ret = ensure_parent_dirs(path);
    if (dir_ret != ESP_OK) {
        TdxSharedSpi_Unlock();
        ESP_LOGE(TAG, "make raw upload parent dirs failed");
        return dir_ret;
    }
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    fp = fopen(tmp_path, "wb");
    if (fp == NULL) {
        TdxSharedSpi_Unlock();
        ESP_LOGE(TAG, "raw upload open failed path=%s errno=%d", tmp_path, errno);
        return ESP_FAIL;
    }
#if USB_CONSOLE_FILE_SAVE_STREAM_BUF_SIZE > 0
    // Use the same file buffer policy as multipart upload so SD writes are comparable.
    // 使用和 multipart 上传相同的文件缓冲策略，便于比较 SD 写入耗时。
    io_buf = malloc(USB_CONSOLE_FILE_SAVE_STREAM_BUF_SIZE);
    if (io_buf != NULL) {
        (void)setvbuf(fp, io_buf, _IOFBF, USB_CONSOLE_FILE_SAVE_STREAM_BUF_SIZE);
    }
#endif

    size_t written = fwrite(body, 1, body_len, fp);
    fclose(fp);
    free(io_buf);

    if (written != body_len) {
        unlink(tmp_path);
        TdxSharedSpi_Unlock();
        ESP_LOGE(TAG, "raw upload write mismatch path=%s written=%u expected=%u",
                 tmp_path,
                 (unsigned int)written,
                 (unsigned int)body_len);
        return ESP_FAIL;
    }

    unlink(path);
    if (rename(tmp_path, path) != 0) {
        unlink(tmp_path);
        TdxSharedSpi_Unlock();
        ESP_LOGE(TAG, "raw upload rename failed path=%s errno=%d", path, errno);
        return ESP_FAIL;
    }
    TdxSharedSpi_Unlock();

    ESP_LOGI(TAG,
             "raw upload saved path=%s size=%u elapsed_ms=%lu",
             path,
             (unsigned int)body_len,
             (unsigned long)elapsed_ms_since(start_us));
    return ESP_OK;
}

static esp_err_t process_raw_upload(const usb_console_http_request_t *request,
                                    usb_console_http_response_t *response)
{
    char path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX + 64] = {0};

    if (!query_path_value(request->path, path, sizeof(path))) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"upload_raw_result\",\"result\":%d,\"message\":\"missing path\"}",
                                         TDX_JSON_RESULT_UPLOAD_RAW_PATH_MISSING);
    }
    if (!raw_upload_path_is_safe(path)) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"upload_raw_result\",\"result\":%d,\"message\":\"invalid path\"}",
                                         TDX_JSON_RESULT_UPLOAD_RAW_PATH_INVALID);
    }

    ESP_LOGI(TAG,
             "raw upload request path=%s body_len=%u content_type=%s",
             path,
             (unsigned int)request->body_len,
             request->content_type);

    esp_err_t save_ret = save_raw_body_to_path(path, request->body, request->body_len);
    if (save_ret != ESP_OK) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"upload_raw_result\",\"result\":%d,\"message\":\"save failed\",\"error\":\"%s\"}",
                                         TDX_JSON_RESULT_UPLOAD_RAW_SAVE_FAILED,
                                         esp_err_to_name(save_ret));
    }

    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"upload_raw_result\",\"result\":%d,\"message\":\"ok\",\"path\":\"%s\",\"size\":%u}",
                                     TDX_JSON_RESULT_OK,
                                     path,
                                     (unsigned int)request->body_len);
}

esp_err_t UsbConsoleUpload_Handle(const usb_console_http_request_t *request,
                                  usb_console_http_response_t *response)
{
    return UsbConsoleCommon_SubmitAsyncRequest(request, response, "upload", UsbConsoleUpload_Process);
}

esp_err_t UsbConsoleUpload_Process(const usb_console_http_request_t *request,
                                  usb_console_http_response_t *response)
{
    tdx_cast_core_request_t upload = {0};
    tdx_cast_core_result_t result = {0};

    if (request == NULL || response == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (content_type_is_octet_stream(request->content_type) || strchr(request->path, '?') != NULL) {
        return process_raw_upload(request, response);
    }

    esp_err_t parse_ret = TdxImageTransfer_ParseSingle(request->body,
                                                       request->body_len,
                                                       request->content_type,
                                                       "upload",
                                                       false,
                                                       false,
                                                       &upload,
                                                       &result);
    if (parse_ret == ESP_ERR_NOT_SUPPORTED) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (parse_ret == ESP_OK) {
        tdx_image_transfer_item_t item = {
            .save = upload.save,
            .show = upload.show,
            .record_last_cast = false,
            .epd_target = 1,
            .bin_part = upload.bin_part,
            .image_part = upload.image_part,
        };
        snprintf(item.save_name, sizeof(item.save_name), "%s", upload.file_name);
        (void)TdxImageTransfer_ProcessItems(&item, 1, USB_CONSOLE_BASE_PATH, "usb upload", &result);
    }

    if (result.result == TDX_JSON_RESULT_OK) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"upload_result\",\"result\":%d,\"message\":\"ok\",\"fileName\":\"%s\"}",
                                         TDX_JSON_RESULT_OK,
                                         result.file_name[0] ? result.file_name : upload.file_name);
    }

    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"upload_result\",\"result\":%d,\"message\":\"%s\",\"error\":\"%s\"}",
                                     result.result,
                                     result.message[0] ? result.message : "upload failed",
                                     result.error[0] ? result.error : "");
}
