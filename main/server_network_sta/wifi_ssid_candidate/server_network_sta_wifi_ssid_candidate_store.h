#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "server_network_sta_wifi_ssid_candidate.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ServerNetworkStaWifiSsidCandidateStore_Save(
    const server_network_sta_ssid_candidate_config_t *config);

esp_err_t ServerNetworkStaWifiSsidCandidateStore_Load(
    server_network_sta_ssid_candidate_config_t *config);

esp_err_t ServerNetworkStaWifiSsidCandidateStore_Select(
    server_network_sta_ssid_candidate_config_t *config,
    uint8_t selected_index);

esp_err_t ServerNetworkStaWifiSsidCandidateStore_Clear(void);

bool ServerNetworkStaWifiSsidCandidateStore_HasConfig(void);

esp_err_t ServerNetworkStaWifiSsidCandidateStore_GetDisplaySsid(
    char *display_ssid,
    size_t display_ssid_size);

#ifdef __cplusplus
}
#endif
