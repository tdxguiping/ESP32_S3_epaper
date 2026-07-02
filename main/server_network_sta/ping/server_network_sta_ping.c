#include "server_network_sta_ping.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "epd_display_app.h"
#include "server_network_sta_wifi_work_time.h"
#include "tdx_cfg.h"
#include "user_app.h"

static const char *TAG = "server_sta_ping";

static bool ping_uri_matches(const char *uri)
{
    const size_t ping_len = strlen(SERVER_NETWORK_STA_PING_URI);
    if (uri == NULL || strncmp(uri, SERVER_NETWORK_STA_PING_URI, ping_len) != 0) {
        return false;
    }
    return uri[ping_len] == '\0' || uri[ping_len] == '?' || uri[ping_len] == '#';
}

esp_err_t ServerNetworkStaPing_ProcessGet(httpd_req_t *req)
{
    if (req == NULL || !ping_uri_matches(req->uri)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Handle ping before SD-card and file lookups so heartbeat stays fast and independent.
    /* 中文：优先处理 ping，不依赖 SD 卡和文件查找，保证心跳响应快速独立。 */
    ServerNetworkStaWifiWorkTime_OnNetworkData();

    char ble_mac[13] = {0};
    char json[160] = {0};

    get_ble_mac_no_colon(ble_mac, sizeof(ble_mac));
    const char *epd_state = ServerNetworkStaEpdDisplay_IsBusy() ? "BUSY" : "IDLE";
    if (ble_mac[0] == '\0') {
        ESP_LOGW(TAG, "ping Ble_MAC empty, CH583 BLE_MAC not received yet");
        snprintf(json, sizeof(json),
                 "{\"func\":\"ping_result\",\"result\":%d,\"message\":\"Ble_MAC not ready\",\"EPD\":\"%s\",\"Ble_MAC\":\"\"}",
                 TDX_JSON_RESULT_BLE_MAC_EMPTY,
                 epd_state);
    } else {
        snprintf(json, sizeof(json),
                 "{\"func\":\"ping_result\",\"result\":%d,\"message\":\"ok\",\"EPD\":\"%s\",\"Ble_MAC\":\"%s\"}",
                 TDX_JSON_RESULT_OK,
                 epd_state,
                 ble_mac);
    }
    ESP_LOGI(TAG, "ping request uri=%s method=GET EPD=%s Ble_MAC=%s", req->uri, epd_state, ble_mac);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}
