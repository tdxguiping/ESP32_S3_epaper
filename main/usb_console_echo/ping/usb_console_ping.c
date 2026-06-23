#include "usb_console_ping.h"

#include <stdio.h>

#include "server_network_sta_wifi_work_time.h"
#include "tdx_cfg.h"
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

    int result = ble_mac[0] == '\0' ? TDX_JSON_RESULT_BLE_MAC_EMPTY : TDX_JSON_RESULT_OK;
    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"ping_result\",\"result\":%d,\"message\":\"%s\",\"Ble_MAC\":\"%s\"}",
                                     result,
                                     result == TDX_JSON_RESULT_OK ? "ok" : "Ble_MAC not ready",
                                     ble_mac);
}
