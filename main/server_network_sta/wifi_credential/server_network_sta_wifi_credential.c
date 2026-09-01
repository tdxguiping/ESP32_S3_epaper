#include "server_network_sta_wifi_credential.h"

#include <stdint.h>
#include <string.h>

static bool is_utf8_continuation(uint8_t byte)
{
    return (byte & 0xC0U) == 0x80U;
}

static bool is_valid_utf8_text(const char *text, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)text;
    size_t index = 0;

    while (index < length) {
        uint8_t first = bytes[index];
        if (first <= 0x7FU) {
            // Reject control bytes so logs and JSON stay textual and unambiguous.
            if (first < 0x20U || first == 0x7FU) {
                return false;
            }
            index++;
            continue;
        }

        if (first >= 0xC2U && first <= 0xDFU) {
            if (index + 1U >= length ||
                !is_utf8_continuation(bytes[index + 1U])) {
                return false;
            }
            index += 2U;
            continue;
        }

        if (first >= 0xE0U && first <= 0xEFU) {
            if (index + 2U >= length ||
                !is_utf8_continuation(bytes[index + 1U]) ||
                !is_utf8_continuation(bytes[index + 2U])) {
                return false;
            }
            if ((first == 0xE0U && bytes[index + 1U] < 0xA0U) ||
                (first == 0xEDU && bytes[index + 1U] >= 0xA0U)) {
                return false;
            }
            index += 3U;
            continue;
        }

        if (first >= 0xF0U && first <= 0xF4U) {
            if (index + 3U >= length ||
                !is_utf8_continuation(bytes[index + 1U]) ||
                !is_utf8_continuation(bytes[index + 2U]) ||
                !is_utf8_continuation(bytes[index + 3U])) {
                return false;
            }
            if ((first == 0xF0U && bytes[index + 1U] < 0x90U) ||
                (first == 0xF4U && bytes[index + 1U] > 0x8FU)) {
                return false;
            }
            index += 4U;
            continue;
        }

        return false;
    }

    return true;
}

server_network_sta_wifi_credential_result_t
ServerNetworkStaWifiCredential_Validate(const char *ssid,
                                        const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return SERVER_NETWORK_STA_WIFI_CREDENTIAL_SSID_EMPTY;
    }
    if (password == NULL) {
        return SERVER_NETWORK_STA_WIFI_CREDENTIAL_PASSWORD_INVALID_LENGTH;
    }

    size_t ssid_length = strlen(ssid);
    if (ssid_length > SERVER_NETWORK_STA_WIFI_SSID_MAX_BYTES) {
        return SERVER_NETWORK_STA_WIFI_CREDENTIAL_SSID_TOO_LONG;
    }
    if (!is_valid_utf8_text(ssid, ssid_length)) {
        return SERVER_NETWORK_STA_WIFI_CREDENTIAL_SSID_INVALID_UTF8;
    }

    size_t password_length = strlen(password);
    if (password_length != 0U &&
        (password_length < SERVER_NETWORK_STA_WIFI_PASSWORD_MIN_BYTES ||
         password_length > SERVER_NETWORK_STA_WIFI_PASSWORD_MAX_BYTES)) {
        return SERVER_NETWORK_STA_WIFI_CREDENTIAL_PASSWORD_INVALID_LENGTH;
    }
    if (password_length != 0U &&
        !is_valid_utf8_text(password, password_length)) {
        return SERVER_NETWORK_STA_WIFI_CREDENTIAL_PASSWORD_INVALID_UTF8;
    }

    return SERVER_NETWORK_STA_WIFI_CREDENTIAL_OK;
}

bool ServerNetworkStaWifiCredential_ResultIsSsidError(
    server_network_sta_wifi_credential_result_t result)
{
    return result == SERVER_NETWORK_STA_WIFI_CREDENTIAL_SSID_EMPTY ||
           result == SERVER_NETWORK_STA_WIFI_CREDENTIAL_SSID_TOO_LONG ||
           result == SERVER_NETWORK_STA_WIFI_CREDENTIAL_SSID_INVALID_UTF8;
}

const char *ServerNetworkStaWifiCredential_ResultName(
    server_network_sta_wifi_credential_result_t result)
{
    switch (result) {
    case SERVER_NETWORK_STA_WIFI_CREDENTIAL_OK:
        return "ok";
    case SERVER_NETWORK_STA_WIFI_CREDENTIAL_SSID_EMPTY:
        return "ssid_empty";
    case SERVER_NETWORK_STA_WIFI_CREDENTIAL_SSID_TOO_LONG:
        return "ssid_too_long";
    case SERVER_NETWORK_STA_WIFI_CREDENTIAL_SSID_INVALID_UTF8:
        return "ssid_invalid_utf8";
    case SERVER_NETWORK_STA_WIFI_CREDENTIAL_PASSWORD_INVALID_LENGTH:
        return "password_invalid_length";
    case SERVER_NETWORK_STA_WIFI_CREDENTIAL_PASSWORD_INVALID_UTF8:
        return "password_invalid_utf8";
    default:
        return "unknown";
    }
}

bool ServerNetworkStaWifiCredential_JsonHasEmbeddedNul(
    const char *json,
    size_t json_length)
{
    if (json == NULL) {
        return false;
    }

    for (size_t index = 0; index < json_length;) {
        if (json[index] == '\0') {
            return true;
        }
        if (json[index] != '\\') {
            index++;
            continue;
        }

        size_t slash_start = index;
        while (index < json_length && json[index] == '\\') {
            index++;
        }
        size_t slash_count = index - slash_start;
        if ((slash_count & 1U) != 0U && json_length - index >= 5U &&
            json[index] == 'u' && json[index + 1U] == '0' &&
            json[index + 2U] == '0' && json[index + 3U] == '0' &&
            json[index + 4U] == '0') {
            return true;
        }
    }

    return false;
}
