#include "usb_console_router.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "tdx_cfg.h"
#include "usb_console_cast.h"
#include "usb_console_cast2pic.h"
#include "usb_console_delete.h"
#include "usb_console_net_data.h"
#include "usb_console_ping.h"
#include "usb_console_saved_images.h"
#include "usb_console_slideshow.h"
#include "usb_console_slideshow_control.h"
#include "usb_console_snapshot.h"
#include "usb_console_upload.h"
#include "usb_console_wifi_work_time.h"
#include "usb_console_wifi.h"

static const char *TAG = "usb_console_router";

static bool path_is(const char *path, const char *prefix)
{
    size_t prefix_len = strlen(prefix);
    return strncmp(path, prefix, prefix_len) == 0 &&
           (path[prefix_len] == '\0' || path[prefix_len] == '?' || path[prefix_len] == '/');
}

esp_err_t UsbConsoleRouter_Handle(const usb_console_http_request_t *request)
{
    usb_console_http_response_t *response = NULL;
    esp_err_t ret = ESP_OK;
    esp_err_t send_ret = ESP_OK;

    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    response = (usb_console_http_response_t *)calloc(1, sizeof(*response));
    if (response == NULL) {
        return ESP_ERR_NO_MEM;
    }

#if USB_CONSOLE_VERBOSE_LOG_ENABLE
    ESP_LOGD(TAG, "route USB request method=%s path=%s", request->method, request->path);
#endif

    if (path_is(request->path, "/ping")) {
        ret = UsbConsolePing_Handle(request, response);
    } else if (path_is(request->path, "/wifi")) {
        ret = UsbConsoleWifi_Handle(request, response);
    } else if (path_is(request->path, "/dataUP") || path_is(request->path, "/net_data")) {
        ret = UsbConsoleNetData_Handle(request, response);
    } else if (path_is(request->path, "/cast")) {
        ret = UsbConsoleCast_Handle(request, response);
    } else if (path_is(request->path, "/cast2pic")) {
        ret = UsbConsoleCast2Pic_Handle(request, response);
    } else if (path_is(request->path, "/delete")) {
        ret = UsbConsoleDelete_Handle(request, response);
    } else if (path_is(request->path, "/saved_images") || path_is(request->path, "/thumb")) {
        ret = UsbConsoleSavedImages_Handle(request, response);
    } else if (path_is(request->path, "/slideshow")) {
        ret = UsbConsoleSlideshow_Handle(request, response);
    } else if (path_is(request->path, "/slideshow_control")) {
        ret = UsbConsoleSlideshowControl_Handle(request, response);
    } else if (path_is(request->path, "/snapshot")) {
        ret = UsbConsoleSnapshot_Handle(request, response);
    } else if (path_is(request->path, "/upload")) {
        ret = UsbConsoleUpload_Handle(request, response);
    } else if (path_is(request->path, "/wifi_work_time")) {
        ret = UsbConsoleWifiWorkTime_Handle(request, response);
    } else {
#if USB_CONSOLE_VERBOSE_LOG_ENABLE
        ESP_LOGD(TAG, "unknown USB route path=%s", request->path);
#endif
        UsbConsoleHttp_SetJson(response,
                               404,
                               "Not Found",
                               "{\"func\":\"usb_unknown_result\",\"result\":1,\"message\":\"route not found\"}");
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "USB route failed ret=%s", esp_err_to_name(ret));
        UsbConsoleHttp_SetJson(response,
                               500,
                               "Internal Server Error",
                               "{\"func\":\"usb_route_result\",\"result\":1,\"message\":\"handler failed\"}");
    }

    // Keep the 8 KB response buffer off the UsbConsoleEcho stack to avoid stack overflow after sending.
    // Chinese note: response buffer is large, so free it after USB response has been sent.
    if (response->status != 0) {
        send_ret = UsbConsoleHttp_SendResponse(response);
    }
    free(response);
    return send_ret;
}
