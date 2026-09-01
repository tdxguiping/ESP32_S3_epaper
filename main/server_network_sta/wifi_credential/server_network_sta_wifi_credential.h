#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SERVER_NETWORK_STA_WIFI_SSID_MAX_BYTES          32U
#define SERVER_NETWORK_STA_WIFI_PASSWORD_MIN_BYTES       8U
#define SERVER_NETWORK_STA_WIFI_PASSWORD_MAX_BYTES      63U
#define SERVER_NETWORK_STA_WIFI_SSID_BUFFER_SIZE        33U
#define SERVER_NETWORK_STA_WIFI_PASSWORD_BUFFER_SIZE    64U

typedef enum {
    SERVER_NETWORK_STA_WIFI_CREDENTIAL_OK = 0,
    SERVER_NETWORK_STA_WIFI_CREDENTIAL_SSID_EMPTY,
    SERVER_NETWORK_STA_WIFI_CREDENTIAL_SSID_TOO_LONG,
    SERVER_NETWORK_STA_WIFI_CREDENTIAL_SSID_INVALID_UTF8,
    SERVER_NETWORK_STA_WIFI_CREDENTIAL_PASSWORD_INVALID_LENGTH,
    SERVER_NETWORK_STA_WIFI_CREDENTIAL_PASSWORD_INVALID_UTF8,
} server_network_sta_wifi_credential_result_t;

// Validate one textual WiFi credential using UTF-8 byte limits.
// An empty password selects an open network; secured passwords use 8..63 bytes.
server_network_sta_wifi_credential_result_t
ServerNetworkStaWifiCredential_Validate(const char *ssid,
                                        const char *password);

bool ServerNetworkStaWifiCredential_ResultIsSsidError(
    server_network_sta_wifi_credential_result_t result);

const char *ServerNetworkStaWifiCredential_ResultName(
    server_network_sta_wifi_credential_result_t result);

// Reject raw NUL bytes and unescaped JSON \u0000 sequences before cJSON
// converts a string into a NUL-terminated buffer without a decoded length.
bool ServerNetworkStaWifiCredential_JsonHasEmbeddedNul(
    const char *json,
    size_t json_length);

#ifdef __cplusplus
}
#endif
