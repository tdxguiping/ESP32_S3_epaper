#include "server_network_sta.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "mdns.h"
#include "nvs.h"

#include "file_serving_example_common.h"
#include "ble_data_handler.h"
#include "led_status.h"

typedef struct {
    char ssid[33];
    char password[65];
    bool is_valid;
} wifi_credential_t;

static const char *TAG = "server_network_sta";
static EventGroupHandle_t s_sta_event_group;
static bool s_wifi_handlers_registered;
static bool s_mdns_service_started;
static bool s_file_server_started;
static esp_event_handler_instance_t s_wifi_event_instance;
static esp_event_handler_instance_t s_ip_event_instance;
static int s_auth_expire_retry_num;

#define SERVER_NETWORK_STA_AUTH_EXPIRE_MAX_RETRY 5

static void parse_zero_terminated_blob(const uint8_t *blob_data, size_t blob_len, char *out, size_t out_size)
{
    size_t copy_len = 0;

    if (blob_data == NULL || out == NULL || out_size == 0) {
        return;
    }

    while (copy_len + 1 < out_size && copy_len < blob_len && blob_data[copy_len] != '\0') {
        out[copy_len] = (char)blob_data[copy_len];
        copy_len++;
    }
    out[copy_len] = '\0';
}

static esp_err_t read_nvs_blob_string(nvs_handle_t handle, const char *key, char *out, size_t out_size)
{
    size_t blob_len = 0;
    esp_err_t ret = nvs_get_blob(handle, key, NULL, &blob_len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "read key=%s length failed: %s", key, esp_err_to_name(ret));
        return ret;
    }

    uint8_t *blob = (uint8_t *)malloc(blob_len);
    if (blob == NULL) {
        ESP_LOGE(TAG, "alloc key=%s blob failed len=%u", key, (unsigned int)blob_len);
        return ESP_ERR_NO_MEM;
    }

    ret = nvs_get_blob(handle, key, blob, &blob_len);
    if (ret == ESP_OK) {
        parse_zero_terminated_blob(blob, blob_len, out, out_size);
    }
    free(blob);
    return ret;
}

static esp_err_t read_nvs_string(nvs_handle_t handle, const char *key, char *out, size_t out_size)
{
    size_t len = out_size;
    esp_err_t ret = nvs_get_str(handle, key, out, &len);
    if (ret == ESP_OK) {
        out[out_size - 1] = '\0';
    } else {
        ESP_LOGW(TAG, "read wifi/%s failed: %s", key, esp_err_to_name(ret));
    }
    return ret;
}

static wifi_credential_t server_network_sta_read_saved_wifi(void)
{
    wifi_credential_t credential = {0};
    nvs_handle_t handle = 0;

    esp_err_t ret = nvs_open("wifi", NVS_READONLY, &handle);
    if (ret == ESP_OK) {
        esp_err_t ssid_ret = read_nvs_string(handle, "ssid", credential.ssid, sizeof(credential.ssid));
        esp_err_t pass_ret = read_nvs_string(handle, "password", credential.password, sizeof(credential.password));
        nvs_close(handle);
        if (ssid_ret == ESP_OK && pass_ret == ESP_OK && credential.ssid[0] != '\0') {
            credential.is_valid = true;
            ESP_LOGI(TAG, "WiFi credential ssid=%s password=%s",
                     credential.ssid, credential.password);
            return credential;
        }
    } else {
        ESP_LOGW(TAG, "open old wifi namespace failed: %s", esp_err_to_name(ret));
    }

    // Read the same STA credentials that ESP-IDF WiFi stores in nvs.net80211 for the Server Network STA path.
    ret = nvs_open("nvs.net80211", NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "open nvs.net80211 failed: %s", esp_err_to_name(ret));
        return credential;
    }

    ret = read_nvs_blob_string(handle, "sta.ssid", credential.ssid, sizeof(credential.ssid));
    if (ret != ESP_OK) {
        nvs_close(handle);
        return credential;
    }

    ret = read_nvs_blob_string(handle, "sta.pswd", credential.password, sizeof(credential.password));
    if (ret != ESP_OK) {
        nvs_close(handle);
        return credential;
    }

    nvs_close(handle);
    credential.is_valid = credential.ssid[0] != '\0';
    ESP_LOGI(TAG, "WiFi credential ssid=%s password=%s",
             credential.ssid, credential.password);
    return credential;
}

static const char *wifi_disconnect_reason_name(int reason)
{
    switch (reason) {
    case 1:
        return "UNSPECIFIED";
    case 2:
        return "AUTH_EXPIRE";
    case 4:
        return "INACTIVITY";
    case 8:
        return "ASSOC_LEAVE";
    case 14:
        return "MIC_FAILURE";
    case 15:
        return "4WAY_HANDSHAKE_TIMEOUT";
    case 16:
        return "GROUP_KEY_TIMEOUT";
    case 17:
        return "IE_DIFFERS";
    case 39:
        return "TIMEOUT";
    case 200:
        return "BEACON_TIMEOUT";
    case 201:
        return "NO_AP_FOUND";
    case 202:
        return "AUTH_FAIL";
    case 203:
        return "ASSOC_FAIL";
    case 204:
        return "HANDSHAKE_TIMEOUT";
    case 205:
        return "CONNECTION_FAIL";
    case 210:
        return "SECURITY_MISMATCH";
    default:
        return "UNKNOWN";
    }
}

static const char *wifi_disconnect_reason_hint(int reason)
{
    switch (reason) {
    case 15:
    case 202:
    case 204:
        return "password/security/rssi";
    case 201:
        return "ssid/channel/rssi";
    case 203:
    case 205:
        return "router/compat/rssi";
    case 200:
        return "weak-signal/router";
    case 210:
        return "authmode";
    default:
        return "check-router-log";
    }
}
static void server_network_sta_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
#if SERVER_NETWORK_STA_DEBUG_LOG_ENABLE
        ESP_LOGI(TAG, "WiFi start connect");
#endif
        esp_err_t ret = esp_wifi_connect();
        if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
            ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(ret));
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
#if SERVER_NETWORK_STA_DEBUG_LOG_ENABLE
        if (event != NULL) {
            ESP_LOGI(TAG, "WiFi connected ch=%u auth=%d bssid=%02x:%02x:%02x:%02x:%02x:%02x",
                     event->channel, event->authmode,
                     event->bssid[0], event->bssid[1], event->bssid[2],
                     event->bssid[3], event->bssid[4], event->bssid[5]);
        }
#endif
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi IP=" IPSTR, IP2STR(&event->ip_info.ip));
        s_auth_expire_retry_num = 0;
        xEventGroupSetBits(s_sta_event_group, SERVER_NETWORK_STA_CONNECTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        int reason = event ? event->reason : -1;
        if (reason != 8) {
            ESP_LOGW(TAG, "WiFi disconnected reason=%d(%s) rssi=%d hint=%s",
                     reason, wifi_disconnect_reason_name(reason),
                     event ? event->rssi : 0, wifi_disconnect_reason_hint(reason));
        }
        if (reason == WIFI_REASON_AUTH_EXPIRE &&
            s_auth_expire_retry_num < SERVER_NETWORK_STA_AUTH_EXPIRE_MAX_RETRY) {
            s_auth_expire_retry_num++;
            ESP_LOGW(TAG, "WiFi auth expired, retry %d/%d",
                     s_auth_expire_retry_num, SERVER_NETWORK_STA_AUTH_EXPIRE_MAX_RETRY);
            esp_err_t ret = esp_wifi_connect();
            if (ret == ESP_OK || ret == ESP_ERR_WIFI_CONN) {
                return;
            }
            ESP_LOGE(TAG, "esp_wifi_connect retry failed: %s", esp_err_to_name(ret));
        }
        xEventGroupSetBits(s_sta_event_group, SERVER_NETWORK_STA_FAIL_BIT);
    }
}

static uint8_t ServerPort_NetworkSTAInit(wifi_credential_t credential)
{
    if (!credential.is_valid) {
        return 0;
    }

    if (s_sta_event_group == NULL) {
        s_sta_event_group = xEventGroupCreate();
        if (s_sta_event_group == NULL) {
            ESP_LOGE(TAG, "create STA event group failed");
            return 0;
        }
    }

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(ret));
        return 0;
    }

    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif == NULL) {
        sta_netif = esp_netif_create_default_wifi_sta();
        if (sta_netif == NULL) {
            ESP_LOGE(TAG, "create default WiFi STA netif failed");
            return 0;
        }
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        return 0;
    }

    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));

    if (!s_wifi_handlers_registered) {
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            server_network_sta_event_handler,
                                                            NULL,
                                                            &s_wifi_event_instance));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                            IP_EVENT_STA_GOT_IP,
                                                            server_network_sta_event_handler,
                                                            NULL,
                                                            &s_ip_event_instance));
        s_wifi_handlers_registered = true;
    }

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, credential.ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, credential.password, sizeof(wifi_config.sta.password));
#if SERVER_NETWORK_STA_DEBUG_LOG_ENABLE
    ESP_LOGI(TAG, "WiFi config ssid_len=%u pass_len=%u timeout_ms=%u",
             (unsigned int)strlen(credential.ssid),
             (unsigned int)strlen(credential.password),
             (unsigned int)SERVER_NETWORK_STA_CONNECT_TIMEOUT_MS);
#endif

    // Force STA mode here so the migrated branch does not accidentally enter AP, PPP, OTA, or other network modes.
    // 在这里强制使�?STA 模式，避免移植分支误�?AP、PPP、OTA 或其它网络模式�?    (void)esp_wifi_disconnect();
    ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_INIT && ret != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGE(TAG, "esp_wifi_stop failed: %s", esp_err_to_name(ret));
        return 0;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    s_auth_expire_retry_num = 0;
    xEventGroupClearBits(s_sta_event_group, SERVER_NETWORK_STA_CONNECTED_BIT | SERVER_NETWORK_STA_FAIL_BIT);

    ret = esp_wifi_start();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
        return 0;
    }
    if (ret == ESP_ERR_INVALID_STATE) {
        ret = esp_wifi_connect();
        if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
            ESP_LOGE(TAG, "esp_wifi_connect failed when already started: %s", esp_err_to_name(ret));
            return 0;
        }
    }

    EventBits_t bits = xEventGroupWaitBits(s_sta_event_group,
                                           SERVER_NETWORK_STA_CONNECTED_BIT | SERVER_NETWORK_STA_FAIL_BIT,
                                           pdTRUE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(SERVER_NETWORK_STA_CONNECT_TIMEOUT_MS));
    if (bits & SERVER_NETWORK_STA_CONNECTED_BIT) {
        if(WiFi_config_net == true && (WiFi_config_from_ch583 == true || WiFi_config_from_ble == true))
        {
            ESP_LOGI(TAG, "send base info via %s",
                     WiFi_config_from_ch583 == true ? "CH583" : "BLE");
            send_base_info_to_mobile();
            WiFi_config_from_ch583 = false;
            WiFi_config_from_ble = false;

        }
        return 1;
    }

    ESP_LOGE(TAG, "Server Network STA failed bits=0x%08lx", (unsigned long)bits);
    return 0;
}

static void Mdns_init_config(void)
{
    if (s_mdns_service_started) {
        return;
    }

    // Start mDNS after STA gets IP so the host name resolves only when the HTTP server can be reached.
    esp_err_t ret = mdns_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_ERROR_CHECK(mdns_hostname_set("esp32-s3-photopainter"));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32-S3-WebServer"));
    ret = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "mdns_service_add failed: %s", esp_err_to_name(ret));
        return;
    }

    s_mdns_service_started = true;
    ESP_LOGI(TAG, "mDNS ready: http://esp32-s3-photopainter.local/index.html");
}

static esp_err_t ServerPort_init(const char *base_path)
{
    if (s_file_server_started) {
        return ESP_OK;
    }

    // Reuse the existing file_serving HTTP server so upload/download/delete behavior stays unchanged.
    esp_err_t ret = example_start_file_server(base_path);
    if (ret == ESP_OK) {
        s_file_server_started = true;
    }
    return ret;
}

uint8_t User_Network_mode_app_init(const char *base_path)
{
    wifi_credential_t credential = server_network_sta_read_saved_wifi();

    // strcpy(credential.ssid,"MERCURY_A662");
    // strcpy(credential.password,"mnbv0123");

    if (!credential.is_valid) {
        ESP_LOGW(TAG, "No saved WiFi credential, return 0xA1");
        UserLedStatus_Set(USER_LED_STATE_WIFI_FAIL);
        return SERVER_NETWORK_STA_NO_SAVED_WIFI;
    }

    UserLedStatus_Set(USER_LED_STATE_WIFI_CONNECTING);
    uint8_t sta_ret = ServerPort_NetworkSTAInit(credential);
    if (sta_ret == 0) {
        UserLedStatus_Set(USER_LED_STATE_WIFI_FAIL);
        return SERVER_NETWORK_STA_CONNECT_FAIL;
    }

    Mdns_init_config();

    esp_err_t server_ret = ServerPort_init(base_path);
    if (server_ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed: %s", esp_err_to_name(server_ret));
        UserLedStatus_Set(USER_LED_STATE_OPERATION_FAIL);
        return SERVER_NETWORK_STA_CONNECT_FAIL;
    }

    return SERVER_NETWORK_STA_OK;
}
