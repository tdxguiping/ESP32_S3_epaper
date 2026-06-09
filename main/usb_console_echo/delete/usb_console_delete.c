#include "usb_console_delete.h"

#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>

#include "esp_log.h"
#include "tdx_cfg.h"
#include "usb_console_common.h"

static const char *TAG = "usb_console_delete";

static bool delete_file_pair(const char *file_name)
{
    char path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + TDX_SLIDESHOW_FILE_NAME_MAX_LEN + 24];
    bool removed = false;

    snprintf(path, sizeof(path), "%s/bin_img/%s.bin", USB_CONSOLE_BASE_PATH, file_name);
    removed = (unlink(path) == 0) || removed;
    snprintf(path, sizeof(path), "%s/jpg_img/%s.jpg", USB_CONSOLE_BASE_PATH, file_name);
    removed = (unlink(path) == 0) || removed;
    return removed;
}

static bool parse_file_names_and_delete(const char *body, int *removed_count)
{
    const char *pos = strstr(body, "\"fileNames\"");
    if (pos == NULL || removed_count == NULL) {
        return false;
    }
    pos = strchr(pos, '[');
    if (pos == NULL) {
        return false;
    }
    pos++;

    *removed_count = 0;
    while (*pos != '\0' && *pos != ']') {
        while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == ',') {
            pos++;
        }
        if (*pos != '"') {
            break;
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
        if (!UsbConsoleCommon_FileNameIsSafe(file_name)) {
            return false;
        }
        if (delete_file_pair(file_name)) {
            (*removed_count)++;
        }
    }
    return true;
}

esp_err_t UsbConsoleDelete_Handle(const usb_console_http_request_t *request,
                                  usb_console_http_response_t *response)
{
    return UsbConsoleCommon_SubmitAsyncRequest(request, response, "delete", UsbConsoleDelete_Process);
}

esp_err_t UsbConsoleDelete_Process(const usb_console_http_request_t *request,
                                  usb_console_http_response_t *response)
{
    int removed_count = 0;

    if (request == NULL || response == NULL ||
        !UsbConsoleCommon_JsonFuncEquals(request->body, "delete")) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!parse_file_names_and_delete(request->body, &removed_count) || removed_count <= 0) {
        ESP_LOGW(TAG, "delete failed removed=%d", removed_count);
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"delete_result\",\"result\":1,\"message\":\"delete failed\"}");
    }
    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"delete_result\",\"result\":0}");
}
