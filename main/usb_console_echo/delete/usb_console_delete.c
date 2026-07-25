#include "usb_console_delete.h"

#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>

#include "esp_log.h"
#include "led_status.h"
#include "tdx_cfg.h"
#include "tdx_shared_spi.h"
#include "usb_console_common.h"

static const char *TAG = "usb_console_delete";

typedef enum {
    USB_DELETE_PARSE_OK = 0,
    USB_DELETE_PARSE_MISSING_FILE_NAMES,
    USB_DELETE_PARSE_INVALID_FILE_NAME,
    USB_DELETE_PARSE_TOO_MANY_FILES,
    USB_DELETE_PARSE_INVALID_JSON,
} usb_delete_parse_result_t;

typedef struct {
    char file_names[TDX_DELETE_MAX_FILES][TDX_SLIDESHOW_FILE_NAME_MAX_LEN];
    size_t file_count;
} usb_delete_request_t;

typedef struct {
    int deleted;
    int missing;
    int failed;
} usb_delete_summary_t;

static void delete_one_path(const char *path, usb_delete_summary_t *summary)
{
    if (unlink(path) == 0) {
        summary->deleted++;
    } else if (errno == ENOENT) {
        summary->missing++;
    } else {
        summary->failed++;
        ESP_LOGE(TAG, "delete failed path=%s errno=%d", path, errno);
    }
}

static void delete_file_pair(const char *file_name, usb_delete_summary_t *summary)
{
    char bin_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + TDX_SLIDESHOW_FILE_NAME_MAX_LEN + 24];
    char jpg_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + TDX_SLIDESHOW_FILE_NAME_MAX_LEN + 24];

    if (TdxSharedSpi_Lock(portMAX_DELAY) != ESP_OK) {
        summary->failed++;
        return;
    }
    snprintf(bin_path, sizeof(bin_path), "%s/bin_img/%s.bin", USB_CONSOLE_BASE_PATH, file_name);
    delete_one_path(bin_path, summary);

    snprintf(jpg_path, sizeof(jpg_path), "%s/jpg_img/%s.jpg", USB_CONSOLE_BASE_PATH, file_name);
    delete_one_path(jpg_path, summary);

    TdxSharedSpi_Unlock();
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

static usb_delete_parse_result_t parse_file_names(const char *body, usb_delete_request_t *request)
{
    if (request != NULL) {
        memset(request, 0, sizeof(*request));
    }

    const char *pos = find_json_key(body, "fileNames");
    if (pos == NULL || request == NULL) {
        return USB_DELETE_PARSE_MISSING_FILE_NAMES;
    }
    pos += strlen("fileNames") + 2;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    if (*pos != ':') {
        return USB_DELETE_PARSE_INVALID_JSON;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    if (*pos != '[') {
        return USB_DELETE_PARSE_INVALID_JSON;
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
            return USB_DELETE_PARSE_INVALID_JSON;
        }
        pos++;
        char file_name[TDX_SLIDESHOW_FILE_NAME_MAX_LEN] = {0};
        size_t len = 0;
        while (*pos != '\0' && *pos != '"' && len + 1 < sizeof(file_name)) {
            file_name[len++] = *pos++;
        }
        if (*pos != '"') {
            return USB_DELETE_PARSE_INVALID_FILE_NAME;
        }
        pos++;
        if (!UsbConsoleCommon_FileNameIsSafe(file_name)) {
            return USB_DELETE_PARSE_INVALID_FILE_NAME;
        }
        if (request->file_count >= TDX_DELETE_MAX_FILES) {
            return USB_DELETE_PARSE_TOO_MANY_FILES;
        }
        strlcpy(request->file_names[request->file_count], file_name,
                sizeof(request->file_names[request->file_count]));
        request->file_count++;
    }

    if (!closed) {
        return USB_DELETE_PARSE_INVALID_JSON;
    }

    return request->file_count > 0 ? USB_DELETE_PARSE_OK : USB_DELETE_PARSE_MISSING_FILE_NAMES;
}

esp_err_t UsbConsoleDelete_Handle(const usb_console_http_request_t *request,
                                  usb_console_http_response_t *response)
{
    return UsbConsoleCommon_SubmitAsyncRequest(request, response, "delete", UsbConsoleDelete_Process);
}

esp_err_t UsbConsoleDelete_Process(const usb_console_http_request_t *request,
                                  usb_console_http_response_t *response)
{
    usb_delete_summary_t summary = {0};
    usb_delete_request_t delete_request;

    if (request == NULL || response == NULL ||
        !UsbConsoleCommon_JsonFuncEquals(request->body, "delete")) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    usb_delete_parse_result_t parse_ret = parse_file_names(request->body, &delete_request);
    if (parse_ret != USB_DELETE_PARSE_OK) {
        int result = parse_ret == USB_DELETE_PARSE_MISSING_FILE_NAMES ? TDX_JSON_RESULT_FILE_NAMES_MISSING :
                     parse_ret == USB_DELETE_PARSE_INVALID_FILE_NAME ? TDX_JSON_RESULT_FILE_NAME_INVALID :
                     parse_ret == USB_DELETE_PARSE_INVALID_JSON ? TDX_JSON_RESULT_JSON_INVALID :
                     TDX_JSON_RESULT_FILE_NAMES_TOO_MANY;
        const char *message = parse_ret == USB_DELETE_PARSE_INVALID_FILE_NAME ? "invalid fileName" :
                              parse_ret == USB_DELETE_PARSE_INVALID_JSON ? "invalid JSON" :
                              parse_ret == USB_DELETE_PARSE_TOO_MANY_FILES ? "too many fileNames" :
                              "fileNames missing";
        ESP_LOGW(TAG, "delete rejected result=%d parse=%d accepted_count=%u max=%d",
                 result, (int)parse_ret, (unsigned int)delete_request.file_count, TDX_DELETE_MAX_FILES);
        if (result == TDX_JSON_RESULT_FILE_NAMES_TOO_MANY) {
            return UsbConsoleCommon_SetJsonf(response,
                                             200,
                                             "OK",
                                             "{\"func\":\"delete_result\",\"result\":%d,\"message\":\"too many fileNames\",\"maxFiles\":%d}",
                                             result,
                                             TDX_DELETE_MAX_FILES);
        }
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"delete_result\",\"result\":%d,\"message\":\"%s\"}",
                                         result,
                                         message);
    }

    for (size_t i = 0; i < delete_request.file_count; i++) {
        delete_file_pair(delete_request.file_names[i], &summary);
    }
    if (summary.failed > 0 || summary.deleted <= 0) {
        ESP_LOGW(TAG, "delete failed request_count=%u deleted=%d missing=%d failed=%d",
                 (unsigned int)delete_request.file_count,
                 summary.deleted,
                 summary.missing,
                 summary.failed);
        UserLedStatus_ShowOperationFail();
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"delete_result\",\"result\":%d,\"message\":\"delete failed\"}",
                                         TDX_JSON_RESULT_DELETE_FAILED);
    }
    ESP_LOGI(TAG, "delete success request_count=%u deleted=%d missing=%d failed=%d",
             (unsigned int)delete_request.file_count,
             summary.deleted,
             summary.missing,
             summary.failed);
    UserLedStatus_ShowSuccess();
    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"delete_result\",\"result\":%d}",
                                     TDX_JSON_RESULT_OK);
}
