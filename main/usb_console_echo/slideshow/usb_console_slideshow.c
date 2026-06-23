#include "usb_console_slideshow.h"

#include <stdio.h>
#include <string.h>

#include "server_network_sta_slideshow.h"
#include "tdx_cfg.h"
#include "usb_console_common.h"

static int validate_file_names(const char *body)
{
    const char *pos = body != NULL ? strstr(body, "\"fileNames\"") : NULL;
    if (pos == NULL || (pos = strchr(pos, '[')) == NULL) {
        return TDX_JSON_RESULT_FILE_NAMES_MISSING;
    }
    pos++;
    size_t count = 0;
    while (*pos != '\0') {
        while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == ',') {
            pos++;
        }
        if (*pos == ']') {
            return count > 0 ? TDX_JSON_RESULT_OK : TDX_JSON_RESULT_FILE_NAMES_MISSING;
        }
        if (*pos++ != '"') {
            return TDX_JSON_RESULT_JSON_INVALID;
        }
        char name[TDX_SLIDESHOW_FILE_NAME_MAX_LEN] = {0};
        size_t len = 0;
        while (*pos != '\0' && *pos != '"' && len + 1 < sizeof(name)) {
            name[len++] = *pos++;
        }
        if (*pos++ != '"' || !UsbConsoleCommon_FileNameIsSafe(name) ||
            len >= TDX_SLIDESHOW_FILE_NAME_MAX_LEN || ++count > TDX_SLIDESHOW_MAX_FILES) {
            return TDX_JSON_RESULT_FILE_NAME_INVALID;
        }
    }
    return TDX_JSON_RESULT_JSON_INVALID;
}

static bool write_slideshow_config(const char *body)
{
    char config_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    snprintf(config_path, sizeof(config_path), "%s/bin_img/%s", USB_CONSOLE_BASE_PATH, TDX_SLIDESHOW_CONFIG_FILE);
    FILE *fp = fopen(config_path, "wb");
    if (fp == NULL) {
        return false;
    }
    size_t len = strlen(body);
    size_t written = fwrite(body, 1, len, fp);
    return fclose(fp) == 0 && written == len;
}

esp_err_t UsbConsoleSlideshow_Handle(const usb_console_http_request_t *request,
                                     usb_console_http_response_t *response)
{
    return UsbConsoleCommon_SubmitAsyncRequest(request, response, "slideshow", UsbConsoleSlideshow_Process);
}

esp_err_t UsbConsoleSlideshow_Process(const usb_console_http_request_t *request,
                                     usb_console_http_response_t *response)
{
    if (request == NULL || response == NULL ||
        !UsbConsoleCommon_JsonFuncEquals(request->body, "start_slideshow")) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    int file_result = validate_file_names(request->body);
    if (file_result != TDX_JSON_RESULT_OK) {
        return UsbConsoleCommon_SetJsonf(response, 200, "OK",
                                         "{\"func\":\"start_slideshow_result\",\"result\":%d,\"message\":\"invalid fileNames\"}",
                                         file_result);
    }
    uint32_t interval = 0;
    if (!UsbConsoleCommon_JsonU32(request->body, "interval", &interval) ||
        interval < TDX_SLIDESHOW_INTERVAL_MIN_SECONDS ||
        interval > TDX_SLIDESHOW_INTERVAL_MAX_SECONDS) {
        return UsbConsoleCommon_SetJsonf(response, 200, "OK",
                                         "{\"func\":\"start_slideshow_result\",\"result\":%d,\"message\":\"invalid interval\"}",
                                         TDX_JSON_RESULT_SLIDESHOW_INTERVAL_INVALID);
    }
    if (!write_slideshow_config(request->body)) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"start_slideshow_result\",\"result\":%d,\"message\":\"start slideshow failed\"}",
                                         TDX_JSON_RESULT_SLIDESHOW_CONFIG_SAVE_FAILED);
    }
    esp_err_t ret = ServerNetworkStaSlideshow_StartSaved(USB_CONSOLE_BASE_PATH);
    if (ret != ESP_OK) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"start_slideshow_result\",\"result\":%d,\"message\":\"start slideshow runtime failed\"}",
                                         ret == ESP_ERR_NO_MEM ?
                                             TDX_JSON_RESULT_SLIDESHOW_START_FAILED :
                                             TDX_JSON_RESULT_SLIDESHOW_RUNTIME_FAILED);
    }
    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"start_slideshow_result\",\"result\":%d}",
                                     TDX_JSON_RESULT_OK);
}
