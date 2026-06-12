#include "usb_console_wifi_work_time.h"

#include "server_network_sta_wifi_work_time.h"
#include "usb_console_common.h"

esp_err_t UsbConsoleWifiWorkTime_Handle(const usb_console_http_request_t *request,
                                        usb_console_http_response_t *response)
{
    return UsbConsoleCommon_SubmitAsyncRequest(request, response, "wifi_work_time", UsbConsoleWifiWorkTime_Process);
}

esp_err_t UsbConsoleWifiWorkTime_Process(const usb_console_http_request_t *request,
                                        usb_console_http_response_t *response)
{
    uint32_t seconds = 0;

    if (request == NULL || response == NULL ||
        !UsbConsoleCommon_JsonFuncEquals(request->body, "set_wifi_work_time")) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!UsbConsoleCommon_JsonU32(request->body, "seconds", &seconds)) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"set_wifi_work_time_result\",\"result\":1,\"message\":\"set wifi work time failed\"}");
    }
    if (seconds < SERVER_NETWORK_STA_WIFI_WORK_TIME_MIN_SECONDS ||
        seconds > SERVER_NETWORK_STA_WIFI_WORK_TIME_MAX_SECONDS) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"set_wifi_work_time_result\",\"result\":1,\"message\":\"set wifi work time failed\"}");
    }

    esp_err_t ret = ServerNetworkStaWifiWorkTime_SetAndSave(seconds);
    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"set_wifi_work_time_result\",\"result\":%d}",
                                     ret == ESP_OK ? 0 : 1);
}
