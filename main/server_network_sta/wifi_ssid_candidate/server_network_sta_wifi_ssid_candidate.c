#include "server_network_sta_wifi_ssid_candidate.h"

#include <string.h>

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return -1;
}

server_network_sta_ssid_candidate_result_t
ServerNetworkStaWifiSsidCandidate_DecodeHex(
    const char *hex,
    server_network_sta_ssid_encoding_t encoding,
    server_network_sta_ssid_candidate_t *candidate)
{
    if (hex == NULL || candidate == NULL) {
        return SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_ARGUMENT;
    }

    size_t hex_length = strlen(hex);
    if (hex_length < 2U ||
        hex_length > SERVER_NETWORK_STA_SSID_HEX_MAX_CHARS ||
        (hex_length & 1U) != 0U) {
        return SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_LENGTH;
    }

    memset(candidate, 0, sizeof(*candidate));
    candidate->encoding = encoding;
    candidate->length = (uint8_t)(hex_length / 2U);
    for (size_t index = 0; index < candidate->length; index++) {
        int high = hex_value(hex[index * 2U]);
        int low = hex_value(hex[index * 2U + 1U]);
        if (high < 0 || low < 0) {
            memset(candidate, 0, sizeof(*candidate));
            return SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_HEX;
        }
        candidate->bytes[index] = (uint8_t)((high << 4) | low);
        if (candidate->bytes[index] == 0U) {
            memset(candidate, 0, sizeof(*candidate));
            return SERVER_NETWORK_STA_SSID_CANDIDATE_EMBEDDED_NUL;
        }
    }
    return SERVER_NETWORK_STA_SSID_CANDIDATE_OK;
}

bool ServerNetworkStaWifiSsidCandidate_IsCountrySupported(
    const char country[SERVER_NETWORK_STA_WIFI_COUNTRY_SIZE])
{
    static const char supported[][SERVER_NETWORK_STA_WIFI_COUNTRY_SIZE] = {
        "01", "AT", "AU", "BE", "BG", "BR", "CA", "CH", "CN", "CY",
        "CZ", "DE", "DK", "EE", "ES", "FI", "FR", "GB", "GR", "HK",
        "HR", "HU", "IE", "IN", "IS", "IT", "JP", "KR", "LI", "LT",
        "LU", "LV", "MT", "MX", "NL", "NO", "NZ", "PL", "PT", "RO",
        "SE", "SI", "SK", "TW", "US",
    };

    if (country == NULL || country[0] == '\0' || country[1] == '\0' ||
        country[2] != '\0') {
        return false;
    }
    for (size_t index = 0; index < sizeof(supported) / sizeof(supported[0]);
         index++) {
        if (memcmp(country, supported[index],
                   SERVER_NETWORK_STA_WIFI_COUNTRY_SIZE) == 0) {
            return true;
        }
    }
    return false;
}

bool ServerNetworkStaWifiSsidCandidate_DisplayNeedsGbk(const char *display_ssid)
{
    if (display_ssid == NULL) {
        return false;
    }
    for (const uint8_t *cursor = (const uint8_t *)display_ssid;
         *cursor != 0U; cursor++) {
        if (*cursor >= 0x80U) {
            return true;
        }
    }
    return false;
}

static bool candidate_is_valid_gbk(
    const server_network_sta_ssid_candidate_t *candidate)
{
    bool has_double_byte_character = false;
    size_t index = 0;
    while (index < candidate->length) {
        uint8_t first = candidate->bytes[index++];
        if (first <= 0x7FU) {
            continue;
        }
        if (first < 0x81U || first > 0xFEU || index >= candidate->length) {
            return false;
        }
        uint8_t second = candidate->bytes[index++];
        if (second < 0x40U || second > 0xFEU || second == 0x7FU) {
            return false;
        }
        has_double_byte_character = true;
    }
    return has_double_byte_character;
}

server_network_sta_ssid_candidate_result_t
ServerNetworkStaWifiSsidCandidate_ValidateConfig(
    const server_network_sta_ssid_candidate_config_t *config)
{
    if (config == NULL) {
        return SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_ARGUMENT;
    }

    server_network_sta_wifi_credential_result_t credential_result =
        ServerNetworkStaWifiCredential_Validate(config->display_ssid,
                                                config->password);
    if (credential_result != SERVER_NETWORK_STA_WIFI_CREDENTIAL_OK) {
        return SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_ARGUMENT;
    }
    uint8_t required_count =
        ServerNetworkStaWifiSsidCandidate_DisplayNeedsGbk(config->display_ssid)
            ? 2U : 1U;
    if (config->candidate_count != required_count) {
        return SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_COUNT;
    }
    if (!ServerNetworkStaWifiSsidCandidate_IsCountrySupported(
            config->country)) {
        return SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_COUNTRY;
    }

    size_t display_length = strlen(config->display_ssid);
    const server_network_sta_ssid_candidate_t *utf8 = &config->candidates[0];
    if (utf8->encoding != SERVER_NETWORK_STA_SSID_ENCODING_UTF8 ||
        utf8->length != display_length ||
        memcmp(utf8->bytes, config->display_ssid, display_length) != 0) {
        return SERVER_NETWORK_STA_SSID_CANDIDATE_UTF8_MISMATCH;
    }
    if (config->candidate_count == 2U) {
        const server_network_sta_ssid_candidate_t *gbk = &config->candidates[1];
        if (gbk->encoding != SERVER_NETWORK_STA_SSID_ENCODING_GBK ||
            gbk->length == 0U) {
            return SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_ARGUMENT;
        }
        if (!candidate_is_valid_gbk(gbk)) {
            return SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_GBK;
        }
        if (utf8->length == gbk->length &&
            memcmp(utf8->bytes, gbk->bytes, utf8->length) == 0) {
            return SERVER_NETWORK_STA_SSID_CANDIDATE_DUPLICATE;
        }
    }
    return SERVER_NETWORK_STA_SSID_CANDIDATE_OK;
}

const char *ServerNetworkStaWifiSsidCandidate_EncodingName(
    server_network_sta_ssid_encoding_t encoding)
{
    return encoding == SERVER_NETWORK_STA_SSID_ENCODING_GBK ? "GBK" : "UTF8";
}

const char *ServerNetworkStaWifiSsidCandidate_ResultName(
    server_network_sta_ssid_candidate_result_t result)
{
    switch (result) {
    case SERVER_NETWORK_STA_SSID_CANDIDATE_OK: return "ok";
    case SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_ARGUMENT: return "invalid_argument";
    case SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_HEX: return "invalid_hex";
    case SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_LENGTH: return "invalid_length";
    case SERVER_NETWORK_STA_SSID_CANDIDATE_EMBEDDED_NUL: return "embedded_nul";
    case SERVER_NETWORK_STA_SSID_CANDIDATE_UTF8_MISMATCH: return "utf8_mismatch";
    case SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_GBK: return "invalid_gbk";
    case SERVER_NETWORK_STA_SSID_CANDIDATE_DUPLICATE: return "duplicate";
    case SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_COUNT: return "invalid_count";
    case SERVER_NETWORK_STA_SSID_CANDIDATE_INVALID_COUNTRY: return "invalid_country";
    default: return "unknown";
    }
}
