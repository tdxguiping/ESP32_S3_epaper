#include "usb_console_slideshow_control.h"

#include <stdio.h>
#include <string.h>

#include "server_network_sta_slideshow.h"
#include "tdx_cfg.h"
#include "usb_console_common.h"

esp_err_t UsbConsoleSlideshowControl_Handle(const usb_console_http_request_t *request,
                                            usb_console_http_response_t *response)
{
    return UsbConsoleCommon_SubmitAsyncRequest(request, response, "slideshow_control", UsbConsoleSlideshowControl_Process);
}

esp_err_t UsbConsoleSlideshowControl_Process(const usb_console_http_request_t *request,
                                            usb_console_http_response_t *response)
{
    int sw = 0;
    uint32_t interval = TDX_SLIDESHOW_INTERVAL_MIN_SECONDS;
    bool random = false;
    char control_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];

    if (request == NULL || response == NULL ||
        !UsbConsoleCommon_JsonFuncEquals(request->body, "set_slideshow")) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!UsbConsoleCommon_JsonInt(request->body, "sw", &sw) || (sw != 0 && sw != 1)) {
        return UsbConsoleCommon_SetJsonf(response, 200, "OK",
                                         "{\"func\":\"set_slideshow_result\",\"result\":1,\"message\":\"set slideshow failed\"}");
    }
    (void)UsbConsoleCommon_JsonU32(request->body, "interval", &interval);
    (void)UsbConsoleCommon_JsonBool(request->body, "random", &random);
    if (interval < TDX_SLIDESHOW_INTERVAL_MIN_SECONDS || interval > TDX_SLIDESHOW_INTERVAL_MAX_SECONDS) {
        interval = TDX_SLIDESHOW_INTERVAL_MIN_SECONDS;
    }

    snprintf(control_path, sizeof(control_path), "%s/bin_img/%s", USB_CONSOLE_BASE_PATH, TDX_SLIDESHOW_CONTROL_FILE);
    FILE *fp = fopen(control_path, "wb");
    if (fp == NULL) {
        return UsbConsoleCommon_SetJsonf(response, 200, "OK",
                                         "{\"func\":\"set_slideshow_result\",\"result\":1,\"message\":\"set slideshow failed\"}");
    }
    fprintf(fp, "{\"sw\":%d,\"interval\":%lu,\"random\":%s,\"run_mode\":%d}",
            sw, (unsigned long)interval, random ? "true" : "false", TDX_SLIDESHOW_RUN_MODE);
    fclose(fp);

    (void)app_nvs_write_str(TDX_SLIDESHOW_RANDOM_NVS_KEY, random ? "true" : "false");
    g_slideshow_random_enable = random ? 1 : 0;
    if (sw == 1) {
        (void)ServerNetworkStaSlideshow_StartSaved(USB_CONSOLE_BASE_PATH);
    } else {
        ServerNetworkStaSlideshow_Stop();
    }
    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"set_slideshow_result\",\"result\":0}");
}
