#include "server_network_sta_wifi_ssid_candidate_store.h"

#include <string.h>

#include "nvs.h"

#define WIFI_SSID_CANDIDATE_NVS_NAMESPACE "wifi_ssid_cand"
#define WIFI_SSID_CANDIDATE_NVS_KEY       "config"
#define WIFI_SSID_CANDIDATE_STORE_MAGIC   0x53434944UL
#define WIFI_SSID_CANDIDATE_STORE_VERSION 1U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    server_network_sta_ssid_candidate_config_t config;
} wifi_ssid_candidate_store_blob_t;

static esp_err_t write_store_blob(
    const server_network_sta_ssid_candidate_config_t *config)
{
    wifi_ssid_candidate_store_blob_t blob = {
        .magic = WIFI_SSID_CANDIDATE_STORE_MAGIC,
        .version = WIFI_SSID_CANDIDATE_STORE_VERSION,
        .size = sizeof(blob),
        .config = *config,
    };
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(WIFI_SSID_CANDIDATE_NVS_NAMESPACE,
                             NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_set_blob(handle, WIFI_SSID_CANDIDATE_NVS_KEY,
                       &blob, sizeof(blob));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

esp_err_t ServerNetworkStaWifiSsidCandidateStore_Save(
    const server_network_sta_ssid_candidate_config_t *config)
{
    if (config == NULL ||
        ServerNetworkStaWifiSsidCandidate_ValidateConfig(config) !=
            SERVER_NETWORK_STA_SSID_CANDIDATE_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    server_network_sta_ssid_candidate_config_t stored = *config;
    stored.selected_index = -1;
    return write_store_blob(&stored);
}

esp_err_t ServerNetworkStaWifiSsidCandidateStore_Load(
    server_network_sta_ssid_candidate_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(config, 0, sizeof(*config));
    config->selected_index = -1;

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(WIFI_SSID_CANDIDATE_NVS_NAMESPACE,
                             NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    wifi_ssid_candidate_store_blob_t blob = {0};
    size_t blob_size = sizeof(blob);
    ret = nvs_get_blob(handle, WIFI_SSID_CANDIDATE_NVS_KEY,
                       &blob, &blob_size);
    nvs_close(handle);
    if (ret != ESP_OK) {
        return ret;
    }
    if (blob_size != sizeof(blob) ||
        blob.magic != WIFI_SSID_CANDIDATE_STORE_MAGIC ||
        blob.version != WIFI_SSID_CANDIDATE_STORE_VERSION ||
        blob.size != sizeof(blob) ||
        ServerNetworkStaWifiSsidCandidate_ValidateConfig(&blob.config) !=
            SERVER_NETWORK_STA_SSID_CANDIDATE_OK ||
        blob.config.selected_index < -1 ||
        blob.config.selected_index >= (int8_t)blob.config.candidate_count) {
        return ESP_ERR_INVALID_STATE;
    }
    *config = blob.config;
    return ESP_OK;
}

esp_err_t ServerNetworkStaWifiSsidCandidateStore_Select(
    server_network_sta_ssid_candidate_config_t *config,
    uint8_t selected_index)
{
    if (config == NULL || selected_index >= config->candidate_count) {
        return ESP_ERR_INVALID_ARG;
    }

    const server_network_sta_ssid_candidate_t *selected =
        &config->candidates[selected_index];
    uint8_t ssid_blob[SERVER_NETWORK_STA_WIFI_SSID_BUFFER_SIZE] = {0};
    memcpy(ssid_blob, selected->bytes, selected->length);

    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open("nvs.net80211", NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_set_blob(handle, "sta.ssid", ssid_blob,
                       (size_t)selected->length + 1U);
    if (ret == ESP_OK) {
        ret = nvs_set_blob(handle, "sta.pswd", config->password,
                           strlen(config->password) + 1U);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    if (ret != ESP_OK) {
        return ret;
    }

    config->selected_index = (int8_t)selected_index;
    ret = write_store_blob(config);
    if (ret != ESP_OK) {
        config->selected_index = -1;
    }
    return ret;
}

esp_err_t ServerNetworkStaWifiSsidCandidateStore_Clear(void)
{
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(WIFI_SSID_CANDIDATE_NVS_NAMESPACE,
                             NVS_READWRITE, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_erase_all(handle);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

bool ServerNetworkStaWifiSsidCandidateStore_HasConfig(void)
{
    server_network_sta_ssid_candidate_config_t config = {0};
    return ServerNetworkStaWifiSsidCandidateStore_Load(&config) == ESP_OK;
}

esp_err_t ServerNetworkStaWifiSsidCandidateStore_GetDisplaySsid(
    char *display_ssid,
    size_t display_ssid_size)
{
    if (display_ssid == NULL || display_ssid_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    server_network_sta_ssid_candidate_config_t config = {0};
    esp_err_t ret = ServerNetworkStaWifiSsidCandidateStore_Load(&config);
    if (ret != ESP_OK || config.selected_index < 0) {
        display_ssid[0] = '\0';
        return ret == ESP_OK ? ESP_ERR_INVALID_STATE : ret;
    }
    strlcpy(display_ssid, config.display_ssid, display_ssid_size);
    return ESP_OK;
}
