#include "usb_console_slideshow_control.h"

#include "server_network_sta_slideshow_control.h"
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
    if (request == NULL || response == NULL ||
        !UsbConsoleCommon_JsonFuncEquals(request->body, "set_slideshow")) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    server_network_sta_slideshow_control_result_t result;
    esp_err_t ret = ServerNetworkStaSlideshowControl_ApplyJson(request->body,
                                                               USB_CONSOLE_BASE_PATH,
                                                               &result);
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (ret != ESP_OK) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"set_slideshow_result\",\"result\":%d,\"message\":\"set slideshow failed\"}",
                                         TDX_JSON_RESULT_INTERNAL_ERROR);
    }
    if (result.result != TDX_JSON_RESULT_OK) {
        if (result.result == TDX_JSON_RESULT_SLIDESHOW_TIME_DIFF_TOO_LARGE) {
            return UsbConsoleCommon_SetJsonf(response,
                                             200,
                                             "OK",
                                             "{\"func\":\"set_slideshow_result\",\"result\":%d,"
                                             "\"message\":\"%s\",\"timestamp\":%lld,"
                                             "\"now_epoch\":%lld,\"time_diff\":%lld,"
                                             "\"time_source\":\"%s\"}",
                                             result.result,
                                             result.message[0] != '\0' ? result.message : "timestamp differs from SNTP time",
                                             (long long)result.timestamp,
                                             (long long)result.now_epoch,
                                             (long long)result.time_diff,
                                             result.time_source);
        }
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"set_slideshow_result\",\"result\":%d,\"message\":\"%s\"}",
                                         result.result,
                                         result.message[0] != '\0' ? result.message : "set slideshow failed");
    }
    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"set_slideshow_result\",\"result\":%d,"
                                     "\"sw\":%d,\"interval\":%lu,\"random\":%s,"
                                     "\"timestamp\":%lld,"
                                     "\"time_source\":\"%s\","
                                     "\"time_diff\":%lld,"
                                     "\"anchor_epoch\":%lld,\"now_epoch\":%lld,"
                                     "\"next_epoch\":%lld,\"remain\":%lu}",
                                     TDX_JSON_RESULT_OK,
                                     result.sw,
                                     (unsigned long)result.interval,
                                     result.random ? "true" : "false",
                                     (long long)result.timestamp,
                                     result.time_source,
                                     (long long)result.time_diff,
                                     (long long)result.anchor_epoch,
                                     (long long)result.now_epoch,
                                     (long long)result.next_epoch,
                                     (unsigned long)result.remain);
}
