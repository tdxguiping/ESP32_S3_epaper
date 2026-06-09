#include "usb_console_slideshow.h"

#include <stdio.h>
#include <string.h>

#include "server_network_sta_slideshow.h"
#include "tdx_cfg.h"
#include "usb_console_common.h"

static bool write_slideshow_config(const char *body)
{
    char config_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    snprintf(config_path, sizeof(config_path), "%s/bin_img/%s", USB_CONSOLE_BASE_PATH, TDX_SLIDESHOW_CONFIG_FILE);
    FILE *fp = fopen(config_path, "wb");
    if (fp == NULL) {
        return false;
    }
    fwrite(body, 1, strlen(body), fp);
    fclose(fp);
    return true;
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
    if (!write_slideshow_config(request->body)) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"start_slideshow_result\",\"result\":1,\"message\":\"start slideshow failed\"}");
    }
    esp_err_t ret = ServerNetworkStaSlideshow_StartSaved(USB_CONSOLE_BASE_PATH);
    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"start_slideshow_result\",\"result\":%d}",
                                     ret == ESP_OK ? 0 : 1);
}
