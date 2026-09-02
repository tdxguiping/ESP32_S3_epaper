#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "server_network_sta_wifi_ssid_candidate.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SERVER_NETWORK_STA_SSID_SCAN_ACTIVE_MAX_MS 120U

typedef struct {
    uint8_t selected_index;
    uint8_t channel;
    int8_t rssi;
    bool is_5ghz;
    uint32_t elapsed_ms;
} server_network_sta_ssid_scan_result_t;

esp_err_t ServerNetworkStaWifiSsidCandidateScan_PrepareRadio(
    const char country[SERVER_NETWORK_STA_WIFI_COUNTRY_SIZE]);

esp_err_t ServerNetworkStaWifiSsidCandidateScan_Select(
    const server_network_sta_ssid_candidate_config_t *config,
    server_network_sta_ssid_scan_result_t *result);

#ifdef __cplusplus
}
#endif
