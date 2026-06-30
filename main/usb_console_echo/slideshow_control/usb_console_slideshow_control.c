#include "usb_console_slideshow_control.h"

#include <stdio.h>
#include <string.h>

#include "server_network_sta_slideshow.h"
#include "tdx_cfg.h"
#include "tdx_shared_spi.h"
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
                                         "{\"func\":\"set_slideshow_result\",\"result\":%d,\"message\":\"set slideshow failed\"}",
                                         TDX_JSON_RESULT_PARAM_INVALID);
    }
    bool interval_key_present = strstr(request->body, "\"interval\"") != NULL;
    bool interval_present = UsbConsoleCommon_JsonU32(request->body, "interval", &interval);
    if ((interval_key_present && !interval_present) || (sw == 1 && !interval_present) ||
        interval < TDX_SLIDESHOW_INTERVAL_MIN_SECONDS || interval > TDX_SLIDESHOW_INTERVAL_MAX_SECONDS) {
        return UsbConsoleCommon_SetJsonf(response, 200, "OK",
                                         "{\"func\":\"set_slideshow_result\",\"result\":%d,\"message\":\"invalid interval\"}",
                                         TDX_JSON_RESULT_SLIDESHOW_INTERVAL_INVALID);
    }
    if (strstr(request->body, "\"random\"") != NULL &&
        !UsbConsoleCommon_JsonBool(request->body, "random", &random)) {
        return UsbConsoleCommon_SetJsonf(response, 200, "OK",
                                         "{\"func\":\"set_slideshow_result\",\"result\":%d,\"message\":\"invalid random\"}",
                                         TDX_JSON_RESULT_PARAM_INVALID);
    }

    snprintf(control_path, sizeof(control_path), "%s/bin_img/%s", USB_CONSOLE_BASE_PATH, TDX_SLIDESHOW_CONTROL_FILE);
    esp_err_t lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        return UsbConsoleCommon_SetJsonf(response, 200, "OK",
                                         "{\"func\":\"set_slideshow_result\",\"result\":%d,\"message\":\"set slideshow failed\"}",
                                         TDX_JSON_RESULT_TIMEOUT);
    }
    FILE *fp = fopen(control_path, "wb");
    if (fp == NULL) {
        TdxSharedSpi_Unlock();
        return UsbConsoleCommon_SetJsonf(response, 200, "OK",
                                         "{\"func\":\"set_slideshow_result\",\"result\":%d,\"message\":\"set slideshow failed\"}",
                                         TDX_JSON_RESULT_SLIDESHOW_CONTROL_SAVE_FAILED);
    }
    int written = fprintf(fp, "{\"sw\":%d,\"interval\":%lu,\"random\":%s,\"run_mode\":%d}",
                          sw, (unsigned long)interval, random ? "true" : "false", TDX_SLIDESHOW_RUN_MODE);
    if (fclose(fp) != 0 || written < 0) {
        TdxSharedSpi_Unlock();
        return UsbConsoleCommon_SetJsonf(response, 200, "OK",
                                         "{\"func\":\"set_slideshow_result\",\"result\":%d,\"message\":\"set slideshow failed\"}",
                                         TDX_JSON_RESULT_SLIDESHOW_CONTROL_SAVE_FAILED);
    }
    TdxSharedSpi_Unlock();

    if (app_nvs_write_str(TDX_SLIDESHOW_RANDOM_NVS_KEY, random ? "true" : "false") != ESP_OK) {
        return UsbConsoleCommon_SetJsonf(response, 200, "OK",
                                         "{\"func\":\"set_slideshow_result\",\"result\":%d,\"message\":\"set slideshow failed\"}",
                                         TDX_JSON_RESULT_SLIDESHOW_CONTROL_SAVE_FAILED);
    }
    g_slideshow_random_enable = random ? 1 : 0;
    if (sw == 1) {
        esp_err_t start_ret = ServerNetworkStaSlideshow_StartSavedResetInterval(USB_CONSOLE_BASE_PATH);
        if (start_ret != ESP_OK) {
            return UsbConsoleCommon_SetJsonf(response,
                                             200,
                                             "OK",
                                             "{\"func\":\"set_slideshow_result\",\"result\":%d,\"message\":\"start slideshow runtime failed\"}",
                                             TDX_JSON_RESULT_SLIDESHOW_RUNTIME_FAILED);
        }
    } else {
        ServerNetworkStaSlideshow_Stop();
    }
    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"set_slideshow_result\",\"result\":%d}",
                                     TDX_JSON_RESULT_OK);
}
