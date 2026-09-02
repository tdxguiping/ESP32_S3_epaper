#include "server_network_sta_wifi_ssid_candidate_scan.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WIFI_SSID_SCAN_ALL_2G_CHANNELS 0x00007FFEU
#define WIFI_SSID_SCAN_ALL_5G_CHANNELS 0x1FFFFFFEU

static const char *TAG = "wifi_ssid_scan";

esp_err_t ServerNetworkStaWifiSsidCandidateScan_PrepareRadio(
    const char country[SERVER_NETWORK_STA_WIFI_COUNTRY_SIZE])
{
    if (!ServerNetworkStaWifiSsidCandidate_IsCountrySupported(country)) {
        return ESP_ERR_INVALID_ARG;
    }
    char active_country[3] = {0};
    esp_err_t ret = esp_wifi_get_country_code(active_country);
    if (ret != ESP_OK || memcmp(active_country, country, 2U) != 0) {
        ret = esp_wifi_set_country_code(country, true);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "country apply failed country=%s ret=%s",
                     country, esp_err_to_name(ret));
            return ret;
        }
    }

    wifi_band_mode_t band_mode = WIFI_BAND_MODE_2G_ONLY;
    ret = esp_wifi_get_band_mode(&band_mode);
    if (ret != ESP_OK || band_mode != WIFI_BAND_MODE_AUTO) {
        ret = esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "dual-band mode apply failed ret=%s",
                     esp_err_to_name(ret));
            return ret;
        }
    }
    ESP_LOGI(TAG,
             "WiFi radio ready country=%s band_mode=AUTO(2.4GHz+5GHz) ieee80211d=1",
             country);
    return ESP_OK;
}

static bool classify_channel_band(uint8_t channel, bool *is_5ghz)
{
    if (is_5ghz == NULL) {
        return false;
    }
    if (channel >= 1U && channel <= 14U) {
        *is_5ghz = false;
        return true;
    }
    if (channel >= 36U && CHANNEL_TO_BIT_NUMBER(channel) != 0U) {
        *is_5ghz = true;
        return true;
    }
    return false;
}

static esp_err_t scan_all_bands(
    const server_network_sta_ssid_candidate_config_t *config,
    server_network_sta_ssid_scan_result_t *result)
{
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {
            .min = 0,
            .max = SERVER_NETWORK_STA_SSID_SCAN_ACTIVE_MAX_MS,
        },
        .home_chan_dwell_time = 0,
    };
    scan_config.channel_bitmap.ghz_2_channels =
        WIFI_SSID_SCAN_ALL_2G_CHANNELS;
    scan_config.channel_bitmap.ghz_5_channels =
        WIFI_SSID_SCAN_ALL_5G_CHANNELS;

    TickType_t start_tick = xTaskGetTickCount();
    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "dual-band scan failed ret=%s", esp_err_to_name(ret));
        (void)esp_wifi_clear_ap_list();
        return ret;
    }

    uint16_t ap_count = 0;
    ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK) {
        (void)esp_wifi_clear_ap_list();
        return ret;
    }
    wifi_ap_record_t *records = NULL;
    if (ap_count > 0U) {
        records = calloc(ap_count, sizeof(*records));
        if (records == NULL) {
            (void)esp_wifi_clear_ap_list();
            return ESP_ERR_NO_MEM;
        }
        uint16_t record_count = ap_count;
        ret = esp_wifi_scan_get_ap_records(&record_count, records);
        if (ret != ESP_OK) {
            free(records);
            (void)esp_wifi_clear_ap_list();
            return ret;
        }
        ap_count = record_count;
    } else {
        (void)esp_wifi_clear_ap_list();
    }

    uint32_t elapsed_ms = (uint32_t)pdTICKS_TO_MS(
        xTaskGetTickCount() - start_tick);
    for (uint8_t candidate_index = 0;
         candidate_index < config->candidate_count; candidate_index++) {
        const server_network_sta_ssid_candidate_t *candidate =
            &config->candidates[candidate_index];
        for (uint16_t ap_index = 0; ap_index < ap_count; ap_index++) {
            size_t ap_ssid_length = strnlen((const char *)records[ap_index].ssid,
                                            sizeof(records[ap_index].ssid));
            if (ap_ssid_length == candidate->length &&
                memcmp(records[ap_index].ssid, candidate->bytes,
                       candidate->length) == 0) {
                bool is_5ghz = false;
                if (!classify_channel_band(records[ap_index].primary,
                                           &is_5ghz)) {
                    ESP_LOGE(TAG,
                             "matched SSID has invalid channel=%u",
                             (unsigned int)records[ap_index].primary);
                    free(records);
                    return ESP_ERR_INVALID_RESPONSE;
                }
                result->selected_index = candidate_index;
                result->channel = records[ap_index].primary;
                result->rssi = records[ap_index].rssi;
                result->is_5ghz = is_5ghz;
                result->elapsed_ms = elapsed_ms;
                free(records);
                return ESP_OK;
            }
        }
    }

    free(records);
    result->elapsed_ms = elapsed_ms;
    ESP_LOGI(TAG,
             "dual-band scan completed elapsed_ms=%lu ap_count=%u matched=0",
             (unsigned long)elapsed_ms,
             (unsigned int)ap_count);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t ServerNetworkStaWifiSsidCandidateScan_Select(
    const server_network_sta_ssid_candidate_config_t *config,
    server_network_sta_ssid_scan_result_t *result)
{
    if (config == NULL || result == NULL ||
        ServerNetworkStaWifiSsidCandidate_ValidateConfig(config) !=
            SERVER_NETWORK_STA_SSID_CANDIDATE_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));

    esp_err_t ret = ServerNetworkStaWifiSsidCandidateScan_PrepareRadio(
        config->country);
    if (ret != ESP_OK) {
        return ret;
    }
    return scan_all_bands(config, result);
}
