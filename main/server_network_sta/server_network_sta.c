#include "server_network_sta.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "mdns.h"
#include "nvs.h"

#include "file_serving_example_common.h"
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
static esp_timer_handle_t s_wifi_retry_timer;
static int s_wifi_connect_retry_num;
static TickType_t s_wifi_connect_start_tick;
static bool s_wifi_connect_active;
static bool s_wifi_scan_before_connect;
static volatile bool s_wifi_sta_connected_seen;
static volatile int s_wifi_last_connect_result = TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT;

#define SERVER_NETWORK_STA_CONNECT_RETRY_MAX 10
#define SERVER_NETWORK_STA_CONNECT_RETRY_BASE_DELAY_MS 100  // 500
#define SERVER_NETWORK_STA_CONNECT_RETRY_MAX_DELAY_MS 100  // 2000
#define SERVER_NETWORK_STA_CONNECT_START_DELAY_MS 5   // 500
#define SERVER_NETWORK_STA_SCAN_BEFORE_CONNECT 0
#define SERVER_NETWORK_STA_INIT_RETRY_ROUNDS 3
#define SERVER_NETWORK_STA_INIT_RETRY_DELAY_MS 100  // 2000
#if TDX_AUTO_LIGHT_SLEEP_ENABLE
#define SERVER_NETWORK_STA_CONNECTED_PS WIFI_PS_MAX_MODEM
#else
#define SERVER_NETWORK_STA_CONNECTED_PS WIFI_PS_NONE
#endif

static const char *wifi_disconnect_reason_name(int reason);

int ServerNetworkSta_GetLastConnectResult(void)
{
    return s_wifi_last_connect_result;
}

static bool server_network_sta_reason_is_auth_failure(int reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return true;
    default:
        return false;
    }
}

static void server_network_sta_set_ps(wifi_ps_type_t ps_type, const char *stage)
{
    esp_err_t ret = esp_wifi_set_ps(ps_type);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi PS set stage=%s type=%d", stage != NULL ? stage : "<null>", (int)ps_type);
    } else {
        ESP_LOGW(TAG, "WiFi PS set failed stage=%s type=%d ret=%s",
                 stage != NULL ? stage : "<null>", (int)ps_type, esp_err_to_name(ret));
    }
}

static bool server_network_sta_connect_window_expired(void)
{
    if (s_wifi_connect_start_tick == 0) {
        return false;
    }

    TickType_t elapsed = xTaskGetTickCount() - s_wifi_connect_start_tick;
    return elapsed >= pdMS_TO_TICKS(SERVER_NETWORK_STA_CONNECT_TIMEOUT_MS);
}

static uint32_t server_network_sta_retry_delay_ms(void)
{
    uint32_t delay_ms = SERVER_NETWORK_STA_CONNECT_RETRY_BASE_DELAY_MS *
                        (uint32_t)(s_wifi_connect_retry_num + 1);
    if (delay_ms > SERVER_NETWORK_STA_CONNECT_RETRY_MAX_DELAY_MS) {
        delay_ms = SERVER_NETWORK_STA_CONNECT_RETRY_MAX_DELAY_MS;
    }
    return delay_ms;
}

static void server_network_sta_retry_timer_cb(void *arg)
{
    (void)arg;

    if (!s_wifi_connect_active) {
        return;
    }
    if (server_network_sta_connect_window_expired()) {
        ESP_LOGW(TAG, "WiFi retry window expired, set fail");
        if (s_sta_event_group != NULL) {
            xEventGroupSetBits(s_sta_event_group, SERVER_NETWORK_STA_FAIL_BIT);
        }
        return;
    }

    server_network_sta_set_ps(WIFI_PS_NONE, "retry_connect");
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
        ESP_LOGE(TAG, "esp_wifi_connect delayed retry failed: %s", esp_err_to_name(ret));
        if (s_sta_event_group != NULL) {
            xEventGroupSetBits(s_sta_event_group, SERVER_NETWORK_STA_FAIL_BIT);
        }
    }
}

static void server_network_sta_stop_retry_timer(void)
{
    if (s_wifi_retry_timer != NULL) {
        (void)esp_timer_stop(s_wifi_retry_timer);
    }
}

static void server_network_sta_ensure_retry_timer(void)
{
    if (s_wifi_retry_timer != NULL) {
        return;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = server_network_sta_retry_timer_cb,
        .name = "sta_retry",
    };
    esp_err_t ret = esp_timer_create(&timer_args, &s_wifi_retry_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "create WiFi retry timer failed: %s", esp_err_to_name(ret));
    }
}

static void server_network_sta_schedule_retry(int reason)
{
    server_network_sta_ensure_retry_timer();
    if (s_wifi_retry_timer == NULL || s_sta_event_group == NULL) {
        if (s_sta_event_group != NULL) {
            xEventGroupSetBits(s_sta_event_group, SERVER_NETWORK_STA_FAIL_BIT);
        }
        return;
    }

    if (server_network_sta_connect_window_expired() ||
        s_wifi_connect_retry_num >= SERVER_NETWORK_STA_CONNECT_RETRY_MAX) {
        ESP_LOGW(TAG,
                 "WiFi retry exhausted reason=%d(%s) retry=%d/%d",
                 reason,
                 wifi_disconnect_reason_name(reason),
                 s_wifi_connect_retry_num,
                 SERVER_NETWORK_STA_CONNECT_RETRY_MAX);
        xEventGroupSetBits(s_sta_event_group, SERVER_NETWORK_STA_FAIL_BIT);
        return;
    }

    uint32_t delay_ms = server_network_sta_retry_delay_ms();
    s_wifi_connect_retry_num++;
    server_network_sta_stop_retry_timer();
    esp_err_t ret = esp_timer_start_once(s_wifi_retry_timer, (uint64_t)delay_ms * 1000ULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "start WiFi retry timer failed: %s", esp_err_to_name(ret));
        xEventGroupSetBits(s_sta_event_group, SERVER_NETWORK_STA_FAIL_BIT);
        return;
    }

    ESP_LOGW(TAG,
             "WiFi retry scheduled reason=%d(%s) retry=%d/%d delay_ms=%lu",
             reason,
             wifi_disconnect_reason_name(reason),
             s_wifi_connect_retry_num,
             SERVER_NETWORK_STA_CONNECT_RETRY_MAX,
             (unsigned long)delay_ms);
}

static void __attribute__((unused)) server_network_sta_scan_target_ssid(const char *ssid)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return;
    }

    wifi_scan_config_t scan_config = {
        .ssid = (uint8_t *)ssid,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi scan skip ret=%s", esp_err_to_name(ret));
        return;
    }

    uint16_t ap_count = 4;
    wifi_ap_record_t ap_records[4] = {0};
    ret = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    if (ret != ESP_OK || ap_count == 0) {
        ESP_LOGW(TAG, "WiFi scan no hit ret=%s count=%u",
                 esp_err_to_name(ret), (unsigned int)ap_count);
        return;
    }

    wifi_ap_record_t *best = &ap_records[0];
    for (uint16_t i = 1; i < ap_count; i++) {
        if (ap_records[i].rssi > best->rssi) {
            best = &ap_records[i];
        }
    }

    ESP_LOGI(TAG,
             "WiFi scan hit bssid=%02x:%02x:%02x:%02x:%02x:%02x ch=%u rssi=%d auth=%d",
             best->bssid[0], best->bssid[1], best->bssid[2],
             best->bssid[3], best->bssid[4], best->bssid[5],
             best->primary, best->rssi, best->authmode);
}

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
        if (s_wifi_scan_before_connect) {
            ESP_LOGI(TAG, "WiFi start wait scan");
            return;
        }
#if SERVER_NETWORK_STA_DEBUG_LOG_ENABLE
        ESP_LOGI(TAG, "WiFi start connect");
#endif
        server_network_sta_set_ps(WIFI_PS_NONE, "connecting");
        esp_err_t ret = esp_wifi_connect();
        if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
            ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(ret));
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
        s_wifi_sta_connected_seen = true;
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
        wifi_ap_record_t ap_info = {0};
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            ESP_LOGI(TAG,
                     "WiFi got_ip bssid=%02x:%02x:%02x:%02x:%02x:%02x ch=%u rssi=%d",
                     ap_info.bssid[0], ap_info.bssid[1], ap_info.bssid[2],
                     ap_info.bssid[3], ap_info.bssid[4], ap_info.bssid[5],
                     ap_info.primary, ap_info.rssi);
        }
        s_wifi_connect_active = false;
        s_wifi_last_connect_result = TDX_JSON_RESULT_OK;
        server_network_sta_stop_retry_timer();
        server_network_sta_set_ps(SERVER_NETWORK_STA_CONNECTED_PS, "got_ip");
        s_wifi_connect_retry_num = 0;
        xEventGroupSetBits(s_sta_event_group, SERVER_NETWORK_STA_CONNECTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        int reason = event ? event->reason : -1;
        if (!s_wifi_connect_active) {
            ESP_LOGI(TAG, "ignore inactive WiFi disconnected reason=%d(%s)",
                     reason, wifi_disconnect_reason_name(reason));
            return;
        }
        if (reason != 8) {
            ESP_LOGW(TAG, "WiFi disconnected reason=%d(%s) rssi=%d hint=%s",
                     reason, wifi_disconnect_reason_name(reason),
                     event ? event->rssi : 0, wifi_disconnect_reason_hint(reason));
        }
        if (server_network_sta_reason_is_auth_failure(reason)) {
            s_wifi_last_connect_result = TDX_JSON_RESULT_WIFI_AUTH_FAILED;
        }
        server_network_sta_schedule_retry(reason);
    }
}

static uint8_t ServerPort_NetworkSTAInit(wifi_credential_t credential)
{
    s_wifi_sta_connected_seen = false;
    s_wifi_last_connect_result = TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT;
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

    //ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
    //  test power only
    // ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
    //  test power only over

    //  test power only
    wifi_ps_type_t ps_type = WIFI_PS_NONE;
    esp_err_t ps_ret = esp_wifi_get_ps(&ps_type);
    ESP_LOGI(TAG, "PMDBG wifi_ps ret=%s type=%d, connected_ps=%d",
            esp_err_to_name(ps_ret),
            (int)ps_type,
            (int)SERVER_NETWORK_STA_CONNECTED_PS);
    //  test power only  over


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
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.failure_retry_cnt = 5;
    wifi_config.sta.channel = SERVER_NETWORK_STA_WIFI_CHANNEL_HINT;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
#if SERVER_NETWORK_STA_DEBUG_LOG_ENABLE
    ESP_LOGI(TAG, "WiFi config ssid_len=%u pass_len=%u timeout_ms=%u scan=%d ch=%u fail_retry=%u pmf_req=%d",
             (unsigned int)strlen(credential.ssid),
             (unsigned int)strlen(credential.password),
             (unsigned int)SERVER_NETWORK_STA_CONNECT_TIMEOUT_MS,
             (int)wifi_config.sta.scan_method,
             (unsigned int)wifi_config.sta.channel,
             (unsigned int)wifi_config.sta.failure_retry_cnt,
             wifi_config.sta.pmf_cfg.required ? 1 : 0);
#endif

    // Force STA mode here so the migrated branch does not accidentally enter AP, PPP, OTA, or other network modes.
    // 在这里强制使�?STA 模式，避免移植分支误�?AP、PPP、OTA 或其它网络模式�?    (void)esp_wifi_disconnect();
    s_wifi_connect_active = false;
    s_wifi_scan_before_connect = false;
    server_network_sta_stop_retry_timer();
    ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_INIT && ret != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGE(TAG, "esp_wifi_stop failed: %s", esp_err_to_name(ret));
        return 0;
    }

    //  test power only
    //wifi_config.sta.listen_interval = 3;
    //  test power only over

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    s_wifi_connect_retry_num = 0;
    s_wifi_connect_start_tick = 0;
    xEventGroupClearBits(s_sta_event_group, SERVER_NETWORK_STA_CONNECTED_BIT | SERVER_NETWORK_STA_FAIL_BIT);
    server_network_sta_set_ps(WIFI_PS_NONE, "connecting");
    if (SERVER_NETWORK_STA_CONNECT_START_DELAY_MS > 0) {
        ESP_LOGI(TAG, "WiFi connect start delay_ms=%u", (unsigned int)SERVER_NETWORK_STA_CONNECT_START_DELAY_MS);
        vTaskDelay(pdMS_TO_TICKS(SERVER_NETWORK_STA_CONNECT_START_DELAY_MS));
    }

    s_wifi_connect_active = true;
    s_wifi_connect_start_tick = xTaskGetTickCount();
    s_wifi_scan_before_connect = SERVER_NETWORK_STA_SCAN_BEFORE_CONNECT != 0;
    ret = esp_wifi_start();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
        s_wifi_connect_active = false;
        s_wifi_scan_before_connect = false;
        server_network_sta_stop_retry_timer();
        return 0;
    }

    bool connect_manually = (ret == ESP_ERR_INVALID_STATE);
#if SERVER_NETWORK_STA_SCAN_BEFORE_CONNECT
    server_network_sta_scan_target_ssid(credential.ssid);
    s_wifi_scan_before_connect = false;
    connect_manually = true;
#endif

    if (connect_manually) {
        ret = esp_wifi_connect();
        if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
            ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(ret));
            s_wifi_connect_active = false;
            server_network_sta_stop_retry_timer();
            return 0;
        }
    }

    EventBits_t bits = xEventGroupWaitBits(s_sta_event_group,
                                           SERVER_NETWORK_STA_CONNECTED_BIT | SERVER_NETWORK_STA_FAIL_BIT,
                                           pdTRUE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(SERVER_NETWORK_STA_CONNECT_TIMEOUT_MS));
    if (bits & SERVER_NETWORK_STA_CONNECTED_BIT) {
        s_wifi_connect_active = false;
        server_network_sta_stop_retry_timer();
        return 1;
    }

    s_wifi_connect_active = false;
    server_network_sta_stop_retry_timer();
    if (s_wifi_last_connect_result != TDX_JSON_RESULT_WIFI_AUTH_FAILED) {
        s_wifi_last_connect_result = s_wifi_sta_connected_seen
                                         ? TDX_JSON_RESULT_WIFI_GOT_IP_FAILED
                                         : TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT;
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
    ESP_ERROR_CHECK(mdns_hostname_set(USER_MDNS_HOSTNAME));
    ESP_ERROR_CHECK(mdns_instance_name_set(USER_MDNS_INSTANCE_NAME));
    ret = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "mdns_service_add failed: %s", esp_err_to_name(ret));
        return;
    }

    s_mdns_service_started = true;
    ESP_LOGI(TAG, "mDNS ready: http://%s.local/index.html", USER_MDNS_HOSTNAME);
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

static bool __attribute__((unused)) server_network_sta_skip_same_wifi(const wifi_credential_t *credential)
{
    wifi_config_t current_config = {0};
    wifi_ap_record_t ap_info = {0};
    esp_netif_ip_info_t ip_info = {0};
    wifi_ps_type_t ps_type = WIFI_PS_NONE;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    bool same_config = false;
    bool connected = false;
    bool has_ip = false;

    if (credential == NULL || !credential->is_valid) {
        return false;
    }

    esp_err_t cfg_ret = esp_wifi_get_config(WIFI_IF_STA, &current_config);
    if (cfg_ret == ESP_OK) {
        same_config = strcmp((const char *)current_config.sta.ssid, credential->ssid) == 0 &&
                      strcmp((const char *)current_config.sta.password, credential->password) == 0;
    }

    esp_err_t ap_ret = esp_wifi_sta_get_ap_info(&ap_info);
    connected = (ap_ret == ESP_OK);

    if (netif != NULL && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        has_ip = (ip_info.ip.addr != 0);
    }

    esp_err_t ps_ret = esp_wifi_get_ps(&ps_type);
    if (same_config && connected && has_ip) {
        ESP_LOGI(TAG,
                 "WiFi same credential ignored ssid=%s key=%s current_ssid=%s current_key=%s ip=" IPSTR " rssi=%d ch=%u ps=%d",
                 credential->ssid,
                 credential->password,
                 (const char *)current_config.sta.ssid,
                 (const char *)current_config.sta.password,
                 IP2STR(&ip_info.ip),
                 ap_info.rssi,
                 ap_info.primary,
                 ps_ret == ESP_OK ? (int)ps_type : -1);
        return true;
    }

    if (same_config) {
        ESP_LOGI(TAG,
                 "WiFi same credential but reconnect needed ssid=%s key=%s current_ssid=%s current_key=%s cfg=%s connected=%d ip=" IPSTR " ap_ret=%s",
                 credential->ssid,
                 credential->password,
                 (const char *)current_config.sta.ssid,
                 (const char *)current_config.sta.password,
                 esp_err_to_name(cfg_ret),
                 connected ? 1 : 0,
                 IP2STR(&ip_info.ip),
                 esp_err_to_name(ap_ret));
    }

    return false;
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

    #if 0  
    if (server_network_sta_skip_same_wifi(&credential)) {
        Mdns_init_config();
        esp_err_t server_ret = ServerPort_init(base_path);
        if (server_ret != ESP_OK) {
            ESP_LOGE(TAG, "HTTP server start failed after skip reconnect: %s", esp_err_to_name(server_ret));
            UserLedStatus_Set(USER_LED_STATE_OPERATION_FAIL);
            return SERVER_NETWORK_STA_CONNECT_FAIL;
        }
        UserLedStatus_Set(USER_LED_STATE_SERVER_READY);
        return SERVER_NETWORK_STA_OK;
    }
    #endif

    uint8_t sta_ret = 0;
    for (int round = 1; round <= SERVER_NETWORK_STA_INIT_RETRY_ROUNDS; round++) {
        UserLedStatus_Set(USER_LED_STATE_WIFI_CONNECTING);
        ESP_LOGI(TAG, "WiFi connect round %d/%d", round, SERVER_NETWORK_STA_INIT_RETRY_ROUNDS);
        sta_ret = ServerPort_NetworkSTAInit(credential);
        if (sta_ret != 0) {
            break;
        }
        if (round < SERVER_NETWORK_STA_INIT_RETRY_ROUNDS) {
            ESP_LOGW(TAG,
                     "WiFi connect round failed, retry after %d ms",
                     SERVER_NETWORK_STA_INIT_RETRY_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(SERVER_NETWORK_STA_INIT_RETRY_DELAY_MS));
        }
    }
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
