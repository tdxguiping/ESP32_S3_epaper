#include "usb_console_ping.h"

#include <stdio.h>

#include "server_network_sta_wifi_work_time.h"
#include "usb_console_common.h"
#include "user_app.h"

esp_err_t UsbConsolePing_Handle(const usb_console_http_request_t *request,
                                usb_console_http_response_t *response)
{
    return UsbConsoleCommon_SubmitAsyncRequest(request, response, "ping", UsbConsolePing_Process);
}

esp_err_t UsbConsolePing_Process(const usb_console_http_request_t *request,
                                usb_console_http_response_t *response)
{
    (void)request;

    char ble_mac[13] = {0};
    ServerNetworkStaWifiWorkTime_OnNetworkData();
    get_ble_mac_no_colon(ble_mac, sizeof(ble_mac));

    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"ping_result\",\"result\":0,\"message\":\"ok\",\"Ble_MAC\":\"%s\"}",
                                     ble_mac);
}
