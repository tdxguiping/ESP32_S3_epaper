#include "usb_console_router.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_err.h"
#include "esp_log.h"
#include "tdx_cfg.h"
#include "usb_console_cast.h"
#include "usb_console_cast2pic.h"
#include "usb_console_delete.h"
#include "usb_console_epd_type.h"
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

static bool usb_request_is_set_epd_type(const usb_console_http_request_t *request)
{
    if (request == NULL) {
        return false;
    }
    if (strcasecmp(request->method, "POST") == 0) {
        return true;
    }
    return request->body != NULL &&
           request->body_len > 0 &&
           strstr(request->body, "\"set_epd_type\"") != NULL;
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
    } else if (path_is(request->path, USB_CONSOLE_EPD_TYPE_LIST_URI)) {
        response->status = 0;
        ret = UsbConsoleEpdType_SendList();
    } else if (path_is(request->path, USB_CONSOLE_EPD_TYPE_URI)) {
        bool is_set_epd_type = usb_request_is_set_epd_type(request);
        ESP_LOGI(TAG, "route epd_type method=%s body_len=%u is_set=%d",
                 request->method,
                 (unsigned int)request->body_len,
                 is_set_epd_type ? 1 : 0);
        if (is_set_epd_type) {
            ret = UsbConsoleEpdType_HandleSet(request, response);
        } else {
            response->status = 0;
            ret = UsbConsoleEpdType_SendCurrent();
        }
    } else if (path_is(request->path, USB_CONSOLE_EPD_TEST_URI)) {
        ret = UsbConsoleEpdType_HandleTest(request, response);
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
                               "{\"func\":\"usb_unknown_result\",\"result\":1104,\"message\":\"route not found\",\"error\":\"route_not_found\"}");
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "USB route failed ret=%s", esp_err_to_name(ret));
        UsbConsoleHttp_SetJson(response,
                               500,
                               "Internal Server Error",
                               "{\"func\":\"usb_route_result\",\"result\":1105,\"message\":\"handler failed\",\"error\":\"handler_failed\"}");
    }

    // Keep the 8 KB response buffer off the UsbConsoleEcho stack to avoid stack overflow after sending.
    // Chinese note: response buffer is large, so free it after USB response has been sent.
    if (response->status != 0) {
        send_ret = UsbConsoleHttp_SendResponse(response);
    }
    free(response);
    return send_ret;
}
