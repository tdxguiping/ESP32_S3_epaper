#include "server_network_sta_ping.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "server_network_sta_wifi_work_time.h"
#include "tdx_cfg.h"
#include "user_app.h"

static const char *TAG = "server_sta_ping";

esp_err_t ServerNetworkStaPing_ProcessGet(httpd_req_t *req)
{
    if (req == NULL || strcmp(req->uri, SERVER_NETWORK_STA_PING_URI) != 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Handle ping before SD-card and file lookups so heartbeat stays fast and independent.
    /* 中文：优先处理 ping，不依赖 SD 卡和文件查找，保证心跳响应快速独立。 */
    ServerNetworkStaWifiWorkTime_OnNetworkData();

    char ble_mac[13] = {0};
    char json[128] = {0};

    get_ble_mac_no_colon(ble_mac, sizeof(ble_mac));
    if (ble_mac[0] == '\0') {
        ESP_LOGW(TAG, "ping Ble_MAC empty, CH583 BLE_MAC not received yet");
    }
    ESP_LOGI(TAG, "ping request uri=%s method=GET Ble_MAC=%s", req->uri, ble_mac);
    snprintf(json, sizeof(json),
             "{\"func\":\"ping_result\",\"result\":0,\"message\":\"ok\",\"Ble_MAC\":\"%s\"}",
             ble_mac);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}
