#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "server_network_sta_wifi_credential.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SERVER_NETWORK_STA_SSID_CANDIDATE_MAX_COUNT 2U
#define SERVER_NETWORK_STA_SSID_HEX_MAX_CHARS       64U
#define SERVER_NETWORK_STA_WIFI_COUNTRY_SIZE         3U
#define SERVER_NETWORK_STA_WIFI_DEFAULT_COUNTRY     "CN"

typedef enum {
    SERVER_NETWORK_STA_SSID_ENCODING_UTF8 = 0,
    SERVER_NETWORK_STA_SSID_ENCODING_GBK = 1,
} server_network_sta_ssid_encoding_t;

typedef struct {
    uint8_t bytes[SERVER_NETWORK_STA_WIFI_SSID_MAX_BYTES];
    uint8_t length;
    server_network_sta_ssid_encoding_t encoding;
} server_network_sta_ssid_candidate_t;

typedef struct {
    char display_ssid[SERVER_NETWORK_STA_WIFI_SSID_BUFFER_SIZE];
    char password[SERVER_NETWORK_STA_WIFI_PASSWORD_BUFFER_SIZE];
    char country[SERVER_NETWORK_STA_WIFI_COUNTRY_SIZE];
    server_network_sta_ssid_candidate_t
        candidates[SERVER_NETWORK_STA_SSID_CANDIDATE_MAX_COUNT];
    uint8_t candidate_count;
    int8_t selected_index;
} server_network_sta_ssid_candidate_config_t;

typedef enum {
    SERVER_NETWORK_STA_SSID_CANDIDATE_OK = 0,
    SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_ARGUMENT,
    SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_HEX,
    SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_LENGTH,
    SERVER_NETWORK_STA_SSID_CANDIDATE_EMBEDDED_NUL,
    SERVER_NETWORK_STA_SSID_CANDIDATE_UTF8_MISMATCH,
    SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_GBK,
    SERVER_NETWORK_STA_SSID_CANDIDATE_DUPLICATE,
    SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_COUNT,
    SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_COUNTRY,
} server_network_sta_ssid_candidate_result_t;

server_network_sta_ssid_candidate_result_t
ServerNetworkStaWifiSsidCandidate_DecodeHex(
    const char *hex,
    server_network_sta_ssid_encoding_t encoding,
    server_network_sta_ssid_candidate_t *candidate);

server_network_sta_ssid_candidate_result_t
ServerNetworkStaWifiSsidCandidate_ValidateConfig(
    const server_network_sta_ssid_candidate_config_t *config);

bool ServerNetworkStaWifiSsidCandidate_IsCountrySupported(
    const char country[SERVER_NETWORK_STA_WIFI_COUNTRY_SIZE]);

bool ServerNetworkStaWifiSsidCandidate_DisplayNeedsGbk(const char *display_ssid);

const char *ServerNetworkStaWifiSsidCandidate_EncodingName(
    server_network_sta_ssid_encoding_t encoding);

const char *ServerNetworkStaWifiSsidCandidate_ResultName(
    server_network_sta_ssid_candidate_result_t result);

#ifdef __cplusplus
}
#endif
