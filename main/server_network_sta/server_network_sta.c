#include "server_network_sta.h"

#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_vfs.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mdns.h"
#include "nvs.h"

#include "ch583_wifi_uart_protocol.h"
#include "file_serving_example_common.h"
#include "led_status.h"

typedef struct {
    char ssid[33];
    char password[65];
    bool is_valid;
} wifi_credential_t;

typedef enum {
    WIFI_MANAGER_EVENT_CONNECT = 1,
    WIFI_MANAGER_EVENT_NEW_CREDENTIAL,
    WIFI_MANAGER_EVENT_PROVISIONING,
    WIFI_MANAGER_EVENT_STA_CONNECTED,
    WIFI_MANAGER_EVENT_GOT_IP,
    WIFI_MANAGER_EVENT_LOST_IP,
    WIFI_MANAGER_EVENT_DISCONNECTED,
    WIFI_MANAGER_EVENT_RESYNC,
} wifi_manager_event_type_t;

typedef struct {
    wifi_manager_event_type_t type;
    bool force_reconnect;
    uint32_t request_id;
    uint8_t request_slot;
    int reason;
    int rssi;
    esp_ip4_addr_t ip;
    char base_path[ESP_VFS_PATH_MAX + 1];
} wifi_manager_event_t;

typedef struct {
    StaticSemaphore_t semaphore_buffer;
    SemaphoreHandle_t semaphore;
    uint32_t request_id;
    uint8_t result;
    bool in_use;
} wifi_request_slot_t;

typedef enum {
    WIFI_MANAGER_INIT_NONE = 0,
    WIFI_MANAGER_INIT_IN_PROGRESS,
    WIFI_MANAGER_INIT_READY,
    WIFI_MANAGER_INIT_FAILED,
} wifi_manager_init_state_t;

typedef enum {
    WIFI_AUTH_FAILURE_NONE = 0,
    WIFI_AUTH_FAILURE_HARD,
    WIFI_AUTH_FAILURE_TRANSIENT,
} wifi_auth_failure_class_t;

typedef struct {
    uint32_t connection_generation;
    uint32_t credential_generation;
    uint32_t disconnect_credential_generation;
    server_network_sta_disconnect_purpose_t disconnect_purpose;
    TickType_t expected_disconnect_deadline;
    bool disconnect_was_online;
    uint32_t wifi_retry_streak;
    uint32_t unstable_disconnect_count;
    uint32_t hard_auth_failure_count;
    uint32_t transient_auth_failure_count;
    TickType_t ready_stable_deadline;
    bool ready_stability_pending;
} wifi_manager_context_t;

static const char *TAG = "server_network_sta";
static QueueHandle_t s_wifi_manager_queue;
static TaskHandle_t s_wifi_manager_task;
static SemaphoreHandle_t s_status_mutex;
static StaticSemaphore_t s_status_mutex_buffer;
static server_network_sta_status_t s_status = {
    .state = SERVER_NETWORK_STA_STATE_IDLE,
    .last_result = TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT,
};
static TickType_t s_wifi_retry_deadline;
static TickType_t s_http_retry_deadline;
static TickType_t s_mdns_retry_deadline;
static TickType_t s_ready_stable_deadline;
static SemaphoreHandle_t s_request_mutex;
static StaticSemaphore_t s_request_mutex_buffer;
static wifi_request_slot_t s_request_slots[SERVER_NETWORK_STA_REQUEST_SLOT_COUNT];
static uint32_t s_next_request_id;
static portMUX_TYPE s_init_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile wifi_manager_init_state_t s_init_state = WIFI_MANAGER_INIT_NONE;
static atomic_bool s_resync_required;
static atomic_bool s_provisioning_required;

static bool s_wifi_stack_ready;
static bool s_wifi_handlers_registered;
static bool s_mdns_initialized;
static bool s_mdns_service_started;
static esp_event_handler_instance_t s_wifi_event_instance;
static esp_event_handler_instance_t s_ip_got_event_instance;
static esp_event_handler_instance_t s_ip_lost_event_instance;

#define SERVER_NETWORK_STA_HARD_AUTH_FAILURE_LIMIT 2
#define SERVER_NETWORK_STA_TRANSIENT_AUTH_FAILURE_LIMIT 3
#define SERVER_NETWORK_STA_INVALID_REQUEST_SLOT UINT8_MAX
#if TDX_AUTO_LIGHT_SLEEP_ENABLE
#define SERVER_NETWORK_STA_CONNECTED_PS WIFI_PS_MAX_MODEM
#else
#define SERVER_NETWORK_STA_CONNECTED_PS WIFI_PS_NONE
#endif

static const uint32_t s_wifi_retry_delay_table_ms[] = {
    1000,
    2000,
    3000,
    3500,
    4000,
    4500,
};

static const uint32_t s_mdns_retry_delay_table_ms[] = {
    5000,
    15000,
    30000,
    60000,
};

static void server_network_sta_manager_task(void *arg);

static void status_lock(void)
{
    if (s_status_mutex != NULL) {
        (void)xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    }
}

static void status_unlock(void)
{
    if (s_status_mutex != NULL) {
        (void)xSemaphoreGive(s_status_mutex);
    }
}

static void status_set_ready_stable_deadline(TickType_t deadline)
{
    status_lock();
    s_ready_stable_deadline = deadline;
    status_unlock();
}

static void status_set_last_result(int result)
{
    status_lock();
    s_status.last_result = result;
    status_unlock();
}

static bool state_has_online_service(server_network_sta_state_t state)
{
    return state == SERVER_NETWORK_STA_STATE_READY ||
           state == SERVER_NETWORK_STA_STATE_GOT_IP ||
           state == SERVER_NETWORK_STA_STATE_STARTING_SERVICES;
}

static bool state_is_connect_flow(server_network_sta_state_t state)
{
    return state == SERVER_NETWORK_STA_STATE_CONNECTING ||
           state == SERVER_NETWORK_STA_STATE_WAITING_IP;
}

static bool state_accepts_sta_connected(server_network_sta_state_t state)
{
    return state == SERVER_NETWORK_STA_STATE_CONNECTING;
}

static bool state_accepts_got_ip(server_network_sta_state_t state)
{
    return state != SERVER_NETWORK_STA_STATE_DISCONNECTING &&
           state != SERVER_NETWORK_STA_STATE_AUTH_FAILED &&
           state != SERVER_NETWORK_STA_STATE_NO_CONFIG &&
           state != SERVER_NETWORK_STA_STATE_FAILED;
}

static bool should_ignore_disconnected_event(server_network_sta_state_t state,
                                             server_network_sta_disconnect_purpose_t purpose)
{
    if (purpose != SERVER_NETWORK_STA_DISCONNECT_NONE) {
        return false;
    }
    return state == SERVER_NETWORK_STA_STATE_IDLE ||
           state == SERVER_NETWORK_STA_STATE_NO_CONFIG ||
           state == SERVER_NETWORK_STA_STATE_AUTH_FAILED ||
           state == SERVER_NETWORK_STA_STATE_FAILED;
}

static void status_enter_connecting(void)
{
    status_lock();
    s_status.state = SERVER_NETWORK_STA_STATE_CONNECTING;
    s_status.retry_type = SERVER_NETWORK_RETRY_NONE;
    s_status.disconnect_purpose = SERVER_NETWORK_STA_DISCONNECT_NONE;
    s_status.ap_connected = false;
    s_status.has_ip = false;
    s_status.http_running = example_file_server_is_running();
    s_status.http_ready = false;
    s_status.mdns_ready = false;
    s_status.ip[0] = '\0';
    s_status.wifi_retry_after_ms = 0;
    s_status.http_retry_count = 0;
    s_status.http_retry_after_ms = 0;
    s_status.mdns_retry_count = 0;
    s_status.mdns_retry_after_ms = 0;
    s_wifi_retry_deadline = 0;
    s_http_retry_deadline = 0;
    s_mdns_retry_deadline = 0;
    status_unlock();
}

static void status_enter_disconnecting(server_network_sta_disconnect_purpose_t purpose)
{
    status_lock();
    s_status.state = SERVER_NETWORK_STA_STATE_DISCONNECTING;
    s_status.retry_type = SERVER_NETWORK_RETRY_NONE;
    s_status.disconnect_purpose = purpose;
    s_status.ap_connected = false;
    s_status.has_ip = false;
    s_status.http_running = example_file_server_is_running();
    s_status.http_ready = false;
    s_status.mdns_ready = false;
    s_status.ip[0] = '\0';
    s_status.wifi_retry_after_ms = 0;
    s_status.http_retry_count = 0;
    s_status.http_retry_after_ms = 0;
    s_status.mdns_retry_count = 0;
    s_status.mdns_retry_after_ms = 0;
    s_wifi_retry_deadline = 0;
    s_http_retry_deadline = 0;
    s_mdns_retry_deadline = 0;
    status_unlock();
}

static void status_enter_waiting_ip(void)
{
    status_lock();
    s_status.state = SERVER_NETWORK_STA_STATE_WAITING_IP;
    s_status.retry_type = SERVER_NETWORK_RETRY_NONE;
    s_status.disconnect_purpose = SERVER_NETWORK_STA_DISCONNECT_NONE;
    s_status.ap_connected = true;
    s_status.has_ip = false;
    s_status.http_running = example_file_server_is_running();
    s_status.http_ready = false;
    s_status.mdns_ready = false;
    s_status.ip[0] = '\0';
    status_unlock();
}

static void status_enter_starting_services(void)
{
    status_lock();
    s_status.state = SERVER_NETWORK_STA_STATE_STARTING_SERVICES;
    s_status.retry_type = SERVER_NETWORK_RETRY_NONE;
    s_status.disconnect_purpose = SERVER_NETWORK_STA_DISCONNECT_NONE;
    s_status.ap_connected = true;
    s_status.has_ip = true;
    s_status.http_running = example_file_server_is_running();
    s_status.http_ready = false;
    s_status.mdns_ready = s_mdns_service_started;
    status_unlock();
}

static void status_enter_idle(void)
{
    status_lock();
    s_status.state = SERVER_NETWORK_STA_STATE_IDLE;
    s_status.retry_type = SERVER_NETWORK_RETRY_NONE;
    s_status.disconnect_purpose = SERVER_NETWORK_STA_DISCONNECT_NONE;
    s_status.ap_connected = false;
    s_status.has_ip = false;
    s_status.http_running = example_file_server_is_running();
    s_status.http_ready = false;
    s_status.mdns_ready = false;
    s_status.ip[0] = '\0';
    status_unlock();
}

static void status_clear_retries(void)
{
    status_lock();
    s_status.retry_type = SERVER_NETWORK_RETRY_NONE;
    s_status.wifi_retry_count = 0;
    s_status.wifi_retry_after_ms = 0;
    s_status.http_retry_count = 0;
    s_status.http_retry_after_ms = 0;
    s_status.mdns_retry_count = 0;
    s_status.mdns_retry_after_ms = 0;
    s_wifi_retry_deadline = 0;
    s_http_retry_deadline = 0;
    s_mdns_retry_deadline = 0;
    status_unlock();
}

static void status_enter_wifi_retry(uint32_t retry_count, uint32_t delay_ms,
                                    int reason, int rssi)
{
    status_lock();
    s_status.state = SERVER_NETWORK_STA_STATE_RETRY_WAIT;
    s_status.retry_type = SERVER_NETWORK_RETRY_WIFI;
    s_status.disconnect_purpose = SERVER_NETWORK_STA_DISCONNECT_NONE;
    s_status.wifi_retry_count = retry_count;
    s_status.wifi_retry_after_ms = delay_ms;
    s_status.ap_connected = false;
    s_status.has_ip = false;
    s_status.http_ready = false;
    s_status.mdns_ready = false;
    s_status.ip[0] = '\0';
    s_status.disconnect_reason = reason;
    s_status.rssi = rssi;
    s_status.http_retry_count = 0;
    s_status.http_retry_after_ms = 0;
    s_status.mdns_retry_count = 0;
    s_status.mdns_retry_after_ms = 0;
    s_wifi_retry_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(delay_ms);
    s_http_retry_deadline = 0;
    s_mdns_retry_deadline = 0;
    status_unlock();
}

static void status_enter_service_retry(uint32_t retry_count, uint32_t delay_ms)
{
    status_lock();
    s_status.state = SERVER_NETWORK_STA_STATE_RETRY_WAIT;
    s_status.retry_type = SERVER_NETWORK_RETRY_HTTP;
    s_status.disconnect_purpose = SERVER_NETWORK_STA_DISCONNECT_NONE;
    s_status.http_retry_count = retry_count;
    s_status.http_retry_after_ms = delay_ms;
    s_status.http_ready = false;
    s_http_retry_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(delay_ms);
    status_unlock();
}

static void status_set_mdns_retry(uint32_t retry_count, uint32_t delay_ms)
{
    status_lock();
    s_status.mdns_ready = false;
    s_status.mdns_retry_count = retry_count;
    s_status.mdns_retry_after_ms = delay_ms;
    if (s_status.state == SERVER_NETWORK_STA_STATE_READY) {
        s_status.retry_type = SERVER_NETWORK_RETRY_MDNS;
    }
    s_mdns_retry_deadline = delay_ms == 0 ? 0 :
                            xTaskGetTickCount() + pdMS_TO_TICKS(delay_ms);
    status_unlock();
}

static void status_enter_ready(const esp_ip4_addr_t *ip)
{
    status_lock();
    s_status.state = SERVER_NETWORK_STA_STATE_READY;
    s_status.disconnect_purpose = SERVER_NETWORK_STA_DISCONNECT_NONE;
    s_status.last_result = TDX_JSON_RESULT_OK;
    s_status.ap_connected = true;
    s_status.has_ip = true;
    s_status.http_running = true;
    s_status.http_ready = true;
    s_status.mdns_ready = s_mdns_service_started;
    s_status.wifi_retry_after_ms = 0;
    s_status.http_retry_count = 0;
    s_status.http_retry_after_ms = 0;
    s_wifi_retry_deadline = 0;
    s_http_retry_deadline = 0;
    if (s_mdns_service_started) {
        s_status.retry_type = SERVER_NETWORK_RETRY_NONE;
        s_status.mdns_retry_count = 0;
        s_status.mdns_retry_after_ms = 0;
        s_mdns_retry_deadline = 0;
    } else {
        s_status.retry_type = SERVER_NETWORK_RETRY_MDNS;
    }
    if (ip != NULL) {
        snprintf(s_status.ip, sizeof(s_status.ip), IPSTR, IP2STR(ip));
    }
    status_unlock();
}

static void status_enter_got_ip(const esp_ip4_addr_t *ip)
{
    status_lock();
    s_status.state = SERVER_NETWORK_STA_STATE_GOT_IP;
    s_status.retry_type = SERVER_NETWORK_RETRY_NONE;
    s_status.disconnect_purpose = SERVER_NETWORK_STA_DISCONNECT_NONE;
    s_status.ap_connected = true;
    s_status.has_ip = true;
    s_status.http_ready = false;
    s_status.wifi_retry_after_ms = 0;
    s_status.http_retry_count = 0;
    s_status.http_retry_after_ms = 0;
    s_status.mdns_retry_count = 0;
    s_status.mdns_retry_after_ms = 0;
    s_wifi_retry_deadline = 0;
    s_http_retry_deadline = 0;
    s_mdns_retry_deadline = 0;
    if (ip != NULL) {
        snprintf(s_status.ip, sizeof(s_status.ip), IPSTR, IP2STR(ip));
    }
    status_unlock();
}

static void status_enter_terminal(server_network_sta_state_t state, int result,
                                  int reason, int rssi)
{
    status_lock();
    s_status.state = state;
    s_status.last_result = result;
    s_status.retry_type = SERVER_NETWORK_RETRY_NONE;
    s_status.disconnect_purpose = SERVER_NETWORK_STA_DISCONNECT_NONE;
    s_status.wifi_retry_count = 0;
    s_status.wifi_retry_after_ms = 0;
    s_status.http_retry_count = 0;
    s_status.http_retry_after_ms = 0;
    s_status.mdns_retry_count = 0;
    s_status.mdns_retry_after_ms = 0;
    s_status.ap_connected = false;
    s_status.has_ip = false;
    s_status.http_running = example_file_server_is_running();
    s_status.http_ready = false;
    s_status.mdns_ready = false;
    s_status.ip[0] = '\0';
    s_status.disconnect_reason = reason;
    s_status.rssi = rssi;
    s_wifi_retry_deadline = 0;
    s_http_retry_deadline = 0;
    s_mdns_retry_deadline = 0;
    status_unlock();
}

static bool should_log_retry(uint32_t retry_count)
{
    return retry_count <= 5 || (retry_count % 10) == 0;
}

static uint32_t retry_delay_ms(uint32_t retry_count)
{
    size_t index = retry_count;
    size_t count = sizeof(s_wifi_retry_delay_table_ms) / sizeof(s_wifi_retry_delay_table_ms[0]);
    if (index >= count) {
        index = count - 1;
    }
    return s_wifi_retry_delay_table_ms[index];
}

static uint32_t mdns_retry_delay_ms(uint32_t retry_count)
{
    size_t index = retry_count;
    size_t count = sizeof(s_mdns_retry_delay_table_ms) / sizeof(s_mdns_retry_delay_table_ms[0]);
    if (index >= count) {
        index = count - 1;
    }
    return s_mdns_retry_delay_table_ms[index];
}

static void set_wifi_ps(wifi_ps_type_t ps_type, const char *stage)
{
    esp_err_t ret = esp_wifi_set_ps(ps_type);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi PS failed stage=%s type=%d ret=%s",
                 stage, (int)ps_type, esp_err_to_name(ret));
    } else {
        ESP_LOGD(TAG, "WiFi PS stage=%s type=%d", stage, (int)ps_type);
    }
}

static wifi_auth_failure_class_t classify_auth_failure(int reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return WIFI_AUTH_FAILURE_HARD;
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return WIFI_AUTH_FAILURE_TRANSIENT;
    default:
        return WIFI_AUTH_FAILURE_NONE;
    }
}

static void reset_connection_health(wifi_manager_context_t *context)
{
    if (context == NULL) {
        return;
    }
    context->wifi_retry_streak = 0;
    context->unstable_disconnect_count = 0;
    context->hard_auth_failure_count = 0;
    context->transient_auth_failure_count = 0;
    context->ready_stability_pending = false;
    context->ready_stable_deadline = 0;
    status_set_ready_stable_deadline(0);
}

static void begin_connection_attempt(wifi_manager_context_t *context)
{
    context->connection_generation++;
    status_lock();
    s_status.connection_generation = context->connection_generation;
    s_status.credential_generation = context->credential_generation;
    status_unlock();
}

static uint32_t next_wifi_retry(wifi_manager_context_t *context, bool disconnected_after_ip,
                                uint32_t *retry_count)
{
    if (disconnected_after_ip) {
        context->unstable_disconnect_count++;
        if (context->wifi_retry_streak < context->unstable_disconnect_count) {
            context->wifi_retry_streak = context->unstable_disconnect_count;
        }
    } else {
        context->wifi_retry_streak++;
    }
    if (context->wifi_retry_streak == 0) {
        context->wifi_retry_streak = 1;
    }
    if (retry_count != NULL) {
        *retry_count = context->wifi_retry_streak;
    }
    return retry_delay_ms(context->wifi_retry_streak - 1);
}

static void clear_expected_disconnect(wifi_manager_context_t *context)
{
    context->disconnect_purpose = SERVER_NETWORK_STA_DISCONNECT_NONE;
    context->disconnect_credential_generation = 0;
    context->expected_disconnect_deadline = 0;
    context->disconnect_was_online = false;
    status_lock();
    s_status.disconnect_purpose = SERVER_NETWORK_STA_DISCONNECT_NONE;
    status_unlock();
}

static esp_err_t start_expected_disconnect(wifi_manager_context_t *context,
                                           server_network_sta_disconnect_purpose_t purpose)
{
    status_lock();
    context->disconnect_was_online = s_status.has_ip ||
                                     state_has_online_service(s_status.state);
    status_unlock();
    context->disconnect_purpose = purpose;
    context->disconnect_credential_generation = context->credential_generation;
    context->expected_disconnect_deadline =
        xTaskGetTickCount() + pdMS_TO_TICKS(SERVER_NETWORK_STA_EXPECTED_DISCONNECT_TIMEOUT_MS);
    status_enter_disconnecting(purpose);
    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK) {
        clear_expected_disconnect(context);
    }
    return ret;
}

static const char *disconnect_reason_name(int reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE: return "AUTH_EXPIRE";
    case WIFI_REASON_ASSOC_LEAVE: return "ASSOC_LEAVE";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT";
    case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT: return "GROUP_KEY_TIMEOUT";
    case WIFI_REASON_BEACON_TIMEOUT: return "BEACON_TIMEOUT";
    case WIFI_REASON_NO_AP_FOUND: return "NO_AP_FOUND";
    case WIFI_REASON_AUTH_FAIL: return "AUTH_FAIL";
    case WIFI_REASON_ASSOC_FAIL: return "ASSOC_FAIL";
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "HANDSHAKE_TIMEOUT";
    case WIFI_REASON_CONNECTION_FAIL: return "CONNECTION_FAIL";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY: return "SECURITY_MISMATCH";
    default: return "OTHER";
    }
}

static void parse_zero_terminated_blob(const uint8_t *blob_data, size_t blob_len,
                                       char *out, size_t out_size)
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

static esp_err_t read_nvs_blob_string(nvs_handle_t handle, const char *key,
                                      char *out, size_t out_size)
{
    size_t blob_len = 0;
    esp_err_t ret = nvs_get_blob(handle, key, NULL, &blob_len);
    if (ret != ESP_OK || blob_len == 0) {
        return ret != ESP_OK ? ret : ESP_ERR_INVALID_SIZE;
    }

    uint8_t *blob = malloc(blob_len);
    if (blob == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ret = nvs_get_blob(handle, key, blob, &blob_len);
    if (ret == ESP_OK) {
        parse_zero_terminated_blob(blob, blob_len, out, out_size);
    }
    free(blob);
    return ret;
}

static esp_err_t read_nvs_string(nvs_handle_t handle, const char *key,
                                 char *out, size_t out_size)
{
    size_t len = out_size;
    esp_err_t ret = nvs_get_str(handle, key, out, &len);
    if (ret == ESP_OK) {
        out[out_size - 1] = '\0';
    }
    return ret;
}

static wifi_credential_t read_saved_wifi(void)
{
    wifi_credential_t credential = {0};
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open("wifi", NVS_READONLY, &handle);
    if (ret == ESP_OK) {
        esp_err_t ssid_ret = read_nvs_string(handle, "ssid", credential.ssid,
                                             sizeof(credential.ssid));
        esp_err_t pass_ret = read_nvs_string(handle, "password", credential.password,
                                             sizeof(credential.password));
        nvs_close(handle);
        if (ssid_ret == ESP_OK && pass_ret == ESP_OK && credential.ssid[0] != '\0') {
            credential.is_valid = true;
#if SERVER_NETWORK_STA_LOG_PASSWORD_PLAINTEXT
            ESP_LOGI(TAG, "WiFi credential loaded ssid=%s password=%s",
                     credential.ssid, credential.password);
#else
            ESP_LOGI(TAG, "WiFi credential loaded ssid=%s",
                     credential.ssid);
#endif
            return credential;
        }
    }

    ret = nvs_open("nvs.net80211", NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return credential;
    }
    esp_err_t ssid_ret = read_nvs_blob_string(handle, "sta.ssid", credential.ssid,
                                              sizeof(credential.ssid));
    esp_err_t pass_ret = read_nvs_blob_string(handle, "sta.pswd", credential.password,
                                              sizeof(credential.password));
    nvs_close(handle);
    credential.is_valid = ssid_ret == ESP_OK && pass_ret == ESP_OK && credential.ssid[0] != '\0';
    if (credential.is_valid) {
#if SERVER_NETWORK_STA_LOG_PASSWORD_PLAINTEXT
        ESP_LOGI(TAG, "WiFi credential loaded ssid=%s password=%s",
                 credential.ssid, credential.password);
#else
        ESP_LOGI(TAG, "WiFi credential loaded ssid=%s",
                 credential.ssid);
#endif
    }
    return credential;
}

static esp_err_t mdns_start_once(void)
{
    if (s_mdns_service_started) {
        return ESP_OK;
    }
    esp_err_t ret = ESP_OK;
    if (!s_mdns_initialized) {
        ret = mdns_init();
        if (ret != ESP_OK) {
            return ret;
        }
        s_mdns_initialized = true;
    }
    ret = mdns_hostname_set(USER_MDNS_HOSTNAME);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = mdns_instance_name_set(USER_MDNS_INSTANCE_NAME);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    if (ret == ESP_ERR_INVALID_ARG &&
        mdns_service_exists("_http", "_tcp", USER_MDNS_HOSTNAME)) {
        ret = ESP_OK;
    }
    if (ret == ESP_OK) {
        s_mdns_service_started = true;
        ESP_LOGI(TAG, "mDNS ready host=%s.local", USER_MDNS_HOSTNAME);
        return ESP_OK;
    }
    return ret;
}

static esp_err_t file_server_start_once(const char *base_path)
{
    if (example_file_server_is_ready()) {
        return ESP_OK;
    }
    esp_err_t ret = example_start_file_server(base_path);
    bool ready = ret == ESP_OK && example_file_server_is_ready();
    return ready ? ESP_OK : (ret == ESP_OK ? ESP_FAIL : ret);
}

static bool physical_link_has_ip(esp_ip4_addr_t *ip)
{
    esp_netif_ip_info_t info = {0};
    wifi_ap_record_t ap = {0};
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    bool ready = netif != NULL &&
                 esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
                 esp_netif_get_ip_info(netif, &info) == ESP_OK &&
                 info.ip.addr != 0;
    if (ready && ip != NULL) {
        *ip = info.ip;
    }
    return ready;
}

static void post_manager_event(const wifi_manager_event_t *event)
{
    if (s_wifi_manager_queue == NULL || event == NULL) {
        return;
    }
    if (xQueueSend(s_wifi_manager_queue, event, 0) != pdTRUE) {
        if (event->type == WIFI_MANAGER_EVENT_PROVISIONING) {
            atomic_store(&s_provisioning_required, true);
        } else {
            atomic_store(&s_resync_required, true);
        }
        ESP_LOGE(TAG, "WiFi manager queue full event=%d", (int)event->type);
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    wifi_manager_event_t event = {0};

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        event.type = WIFI_MANAGER_EVENT_STA_CONNECTED;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconnected = data;
        event.type = WIFI_MANAGER_EVENT_DISCONNECTED;
        event.reason = disconnected != NULL ? disconnected->reason : -1;
        event.rssi = disconnected != NULL ? disconnected->rssi : 0;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *got_ip = data;
        event.type = WIFI_MANAGER_EVENT_GOT_IP;
        if (got_ip != NULL) {
            event.ip = got_ip->ip_info.ip;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        event.type = WIFI_MANAGER_EVENT_LOST_IP;
    } else {
        return;
    }
    post_manager_event(&event);
}

static esp_err_t ensure_wifi_stack(void)
{
    if (s_wifi_stack_ready) {
        return ESP_OK;
    }
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }
    if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == NULL &&
        esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }
    if (!s_wifi_handlers_registered) {
        ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                  wifi_event_handler, NULL,
                                                  &s_wifi_event_instance);
        if (ret == ESP_OK) {
            ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                      wifi_event_handler, NULL,
                                                      &s_ip_got_event_instance);
        }
        if (ret == ESP_OK) {
            ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_LOST_IP,
                                                      wifi_event_handler, NULL,
                                                      &s_ip_lost_event_instance);
        }
        if (ret != ESP_OK) {
            if (s_ip_lost_event_instance != NULL) {
                (void)esp_event_handler_instance_unregister(
                    IP_EVENT, IP_EVENT_STA_LOST_IP, s_ip_lost_event_instance);
                s_ip_lost_event_instance = NULL;
            }
            if (s_ip_got_event_instance != NULL) {
                (void)esp_event_handler_instance_unregister(
                    IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_got_event_instance);
                s_ip_got_event_instance = NULL;
            }
            if (s_wifi_event_instance != NULL) {
                (void)esp_event_handler_instance_unregister(
                    WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_event_instance);
                s_wifi_event_instance = NULL;
            }
            return ret;
        }
        s_wifi_handlers_registered = true;
    }
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_wifi_start();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }
    s_wifi_stack_ready = true;
    return ESP_OK;
}

static esp_err_t apply_wifi_config(const wifi_credential_t *credential)
{
    if (credential == NULL || !credential->is_valid) {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, credential->ssid, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, credential->password, sizeof(config.sta.password));
    config.sta.scan_method = WIFI_FAST_SCAN;
    config.sta.failure_retry_cnt = 0;
    config.sta.channel = SERVER_NETWORK_STA_WIFI_CHANNEL_HINT;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    return esp_wifi_set_config(WIFI_IF_STA, &config);
}

static bool same_active_config(const wifi_credential_t *credential)
{
    wifi_config_t current = {0};
    return credential != NULL && credential->is_valid &&
           esp_wifi_get_config(WIFI_IF_STA, &current) == ESP_OK &&
           strcmp((const char *)current.sta.ssid, credential->ssid) == 0 &&
           strcmp((const char *)current.sta.password, credential->password) == 0;
}

static bool allocate_request_slot(uint8_t *slot_index, uint32_t *request_id)
{
    if (slot_index == NULL || request_id == NULL || s_request_mutex == NULL) {
        return false;
    }
    (void)xSemaphoreTake(s_request_mutex, portMAX_DELAY);
    for (uint8_t i = 0; i < sizeof(s_request_slots) / sizeof(s_request_slots[0]); i++) {
        wifi_request_slot_t *slot = &s_request_slots[i];
        if (!slot->in_use) {
            while (xSemaphoreTake(slot->semaphore, 0) == pdTRUE) {
            }
            slot->in_use = true;
            slot->request_id = ++s_next_request_id;
            slot->result = SERVER_NETWORK_STA_CONNECT_FAIL;
            *slot_index = i;
            *request_id = slot->request_id;
            (void)xSemaphoreGive(s_request_mutex);
            return true;
        }
    }
    (void)xSemaphoreGive(s_request_mutex);
    return false;
}

static void complete_pending_request(uint8_t *slot_index, uint32_t *request_id,
                                     uint8_t result)
{
    if (slot_index == NULL || request_id == NULL ||
        *slot_index == SERVER_NETWORK_STA_INVALID_REQUEST_SLOT ||
        *slot_index >= sizeof(s_request_slots) / sizeof(s_request_slots[0])) {
        return;
    }
    (void)xSemaphoreTake(s_request_mutex, portMAX_DELAY);
    wifi_request_slot_t *slot = &s_request_slots[*slot_index];
    if (slot->in_use && slot->request_id == *request_id) {
        slot->result = result;
        (void)xSemaphoreGive(slot->semaphore);
    }
    (void)xSemaphoreGive(s_request_mutex);
    *slot_index = SERVER_NETWORK_STA_INVALID_REQUEST_SLOT;
    *request_id = 0;
}

static void complete_pending_requests(uint8_t *primary_slot, uint32_t *primary_id,
                                      uint8_t *coalesced_slot, uint32_t *coalesced_id,
                                      uint8_t result)
{
    complete_pending_request(primary_slot, primary_id, result);
    complete_pending_request(coalesced_slot, coalesced_id, result);
}

static void server_network_sta_manager_task(void *arg)
{
    (void)arg;
    wifi_credential_t credential = {0};
    char base_path[ESP_VFS_PATH_MAX + 1] = "/data";
    uint8_t pending_request_slot = SERVER_NETWORK_STA_INVALID_REQUEST_SLOT;
    uint32_t pending_request_id = 0;
    uint8_t coalesced_request_slot = SERVER_NETWORK_STA_INVALID_REQUEST_SLOT;
    uint32_t coalesced_request_id = 0;
    wifi_manager_context_t context = {0};
    uint32_t http_retry_count = 0;
    uint32_t mdns_retry_count = 0;
    TickType_t connection_deadline = 0;
    bool connection_enabled = false;

    while (true) {
        TickType_t wait_ticks = portMAX_DELAY;
        server_network_sta_status_t snapshot = {0};
        (void)ServerNetworkSta_GetStatus(&snapshot);
        TickType_t now = xTaskGetTickCount();
        TickType_t state_deadline = 0;
        if (snapshot.state == SERVER_NETWORK_STA_STATE_CONNECTING ||
            snapshot.state == SERVER_NETWORK_STA_STATE_WAITING_IP) {
            state_deadline = connection_deadline;
        } else if (snapshot.state == SERVER_NETWORK_STA_STATE_RETRY_WAIT) {
            state_deadline = snapshot.retry_type == SERVER_NETWORK_RETRY_HTTP
                                 ? s_http_retry_deadline : s_wifi_retry_deadline;
        }
        if (state_deadline != 0) {
            wait_ticks = (int32_t)(state_deadline - now) > 0 ? state_deadline - now : 0;
        }
        if (connection_enabled && snapshot.has_ip && !s_mdns_service_started &&
            s_mdns_retry_deadline != 0) {
            TickType_t mdns_wait = (int32_t)(s_mdns_retry_deadline - now) > 0
                                      ? s_mdns_retry_deadline - now : 0;
            if (wait_ticks == portMAX_DELAY || mdns_wait < wait_ticks) {
                wait_ticks = mdns_wait;
            }
        }
        if (context.disconnect_purpose != SERVER_NETWORK_STA_DISCONNECT_NONE &&
            context.expected_disconnect_deadline != 0) {
            TickType_t disconnect_wait =
                (int32_t)(context.expected_disconnect_deadline - now) > 0
                    ? context.expected_disconnect_deadline - now : 0;
            if (wait_ticks == portMAX_DELAY || disconnect_wait < wait_ticks) {
                wait_ticks = disconnect_wait;
            }
        }
        if (context.ready_stability_pending && context.ready_stable_deadline != 0) {
            TickType_t stable_wait = (int32_t)(context.ready_stable_deadline - now) > 0
                                         ? context.ready_stable_deadline - now : 0;
            if (wait_ticks == portMAX_DELAY || stable_wait < wait_ticks) {
                wait_ticks = stable_wait;
            }
        }

        wifi_manager_event_t event = {0};
        bool have_event = false;
        if (atomic_exchange(&s_provisioning_required, false)) {
            event.type = WIFI_MANAGER_EVENT_PROVISIONING;
            have_event = true;
        } else if (atomic_exchange(&s_resync_required, false)) {
            event.type = WIFI_MANAGER_EVENT_RESYNC;
            have_event = true;
        }
        if (!have_event && xQueueReceive(s_wifi_manager_queue, &event, wait_ticks) != pdTRUE) {
            now = xTaskGetTickCount();
            if (context.ready_stability_pending &&
                (int32_t)(now - context.ready_stable_deadline) >= 0 &&
                snapshot.state == SERVER_NETWORK_STA_STATE_READY && physical_link_has_ip(NULL)) {
                reset_connection_health(&context);
                status_lock();
                s_status.wifi_retry_count = 0;
                s_status.wifi_retry_after_ms = 0;
                status_unlock();
                ESP_LOGI(TAG, "WiFi READY stable_ms=%u; retry/auth counters reset",
                         SERVER_NETWORK_STA_READY_STABLE_RESET_MS);
                continue;
            }
            if (context.disconnect_purpose != SERVER_NETWORK_STA_DISCONNECT_NONE &&
                (int32_t)(now - context.expected_disconnect_deadline) >= 0) {
                server_network_sta_disconnect_purpose_t purpose = context.disconnect_purpose;
                uint32_t disconnect_credential_generation =
                    context.disconnect_credential_generation;
                clear_expected_disconnect(&context);
                wifi_ap_record_t ap = {0};
                bool ap_still_connected = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
                if (ap_still_connected) {
                    esp_ip4_addr_t current_ip = {0};
                    if (purpose != SERVER_NETWORK_STA_DISCONNECT_RECONFIGURE &&
                        physical_link_has_ip(&current_ip)) {
                        atomic_store(&s_resync_required, true);
                        ESP_LOGI(TAG, "WiFi recovered before disconnect timeout");
                        continue;
                    }
                    esp_err_t disconnect_ret = start_expected_disconnect(&context, purpose);
                    if (disconnect_ret == ESP_OK) {
                        ESP_LOGD(TAG, "WiFi disconnect pending purpose=%d; retry command",
                                 (int)purpose);
                        continue;
                    }
                }
                if (purpose == SERVER_NETWORK_STA_DISCONNECT_RECONFIGURE) {
                    if (disconnect_credential_generation != context.credential_generation) {
                        ESP_LOGD(TAG, "Ignore obsolete reconfigure timeout credential_gen=%lu active=%lu",
                                 (unsigned long)disconnect_credential_generation,
                                 (unsigned long)context.credential_generation);
                        continue;
                    }
                    esp_err_t ret = apply_wifi_config(&credential);
                    if (ret == ESP_OK) {
                        begin_connection_attempt(&context);
                        set_wifi_ps(WIFI_PS_NONE, "reconfigure_timeout");
                        status_enter_connecting();
                        ret = esp_wifi_connect();
                        connection_deadline = now + pdMS_TO_TICKS(SERVER_NETWORK_STA_CONNECT_FLOW_TIMEOUT_MS);
                    }
                    if (ret != ESP_OK) {
                        uint32_t retry_count = 0;
                        uint32_t delay_ms = next_wifi_retry(&context, false, &retry_count);
                        status_enter_wifi_retry(retry_count, delay_ms, -5, 0);
                        if (should_log_retry(retry_count)) {
                            ESP_LOGW(TAG, "WiFi reconfigure timeout recovery failed retry=%lu "
                                     "delay_ms=%lu ret=%s",
                                     (unsigned long)retry_count, (unsigned long)delay_ms,
                                     esp_err_to_name(ret));
                        }
                    }
                } else if (purpose == SERVER_NETWORK_STA_DISCONNECT_DHCP_RECOVERY ||
                           purpose == SERVER_NETWORK_STA_DISCONNECT_CONNECT_RECOVERY) {
                    uint32_t retry_count = 0;
                    uint32_t delay_ms = next_wifi_retry(
                        &context, purpose == SERVER_NETWORK_STA_DISCONNECT_DHCP_RECOVERY,
                        &retry_count);
                    status_enter_wifi_retry(retry_count, delay_ms, -3, 0);
                } else if (purpose == SERVER_NETWORK_STA_DISCONNECT_PROVISIONING) {
                    status_enter_idle();
                } else if (purpose == SERVER_NETWORK_STA_DISCONNECT_NO_CONFIG) {
                    status_enter_terminal(SERVER_NETWORK_STA_STATE_NO_CONFIG,
                                          TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT, 0, 0);
                }
                continue;
            }
            if (connection_enabled && snapshot.has_ip && !s_mdns_service_started &&
                s_mdns_retry_deadline != 0 &&
                (int32_t)(now - s_mdns_retry_deadline) >= 0) {
                esp_err_t mdns_ret = mdns_start_once();
                if (mdns_ret == ESP_OK) {
                    mdns_retry_count = 0;
                    status_set_mdns_retry(0, 0);
                    status_lock();
                    s_status.mdns_ready = true;
                    if (s_status.state == SERVER_NETWORK_STA_STATE_READY) {
                        s_status.retry_type = SERVER_NETWORK_RETRY_NONE;
                    }
                    status_unlock();
                } else {
                    uint32_t delay_ms = mdns_retry_delay_ms(mdns_retry_count++);
                    status_set_mdns_retry(mdns_retry_count, delay_ms);
                    if (should_log_retry(mdns_retry_count)) {
                        ESP_LOGW(TAG, "mDNS retry=%lu delay_ms=%lu ret=%s",
                                 (unsigned long)mdns_retry_count,
                                 (unsigned long)delay_ms, esp_err_to_name(mdns_ret));
                    }
                }
                continue;
            }
            if (snapshot.state == SERVER_NETWORK_STA_STATE_RETRY_WAIT) {
                if (snapshot.retry_type == SERVER_NETWORK_RETRY_HTTP) {
                    status_enter_starting_services();
                    esp_err_t http_ret = file_server_start_once(base_path);
                    esp_ip4_addr_t ip = {0};
                    if (http_ret == ESP_OK && physical_link_has_ip(&ip)) {
                        status_enter_ready(&ip);
                        context.ready_stability_pending = true;
                        context.ready_stable_deadline = xTaskGetTickCount() +
                            pdMS_TO_TICKS(SERVER_NETWORK_STA_READY_STABLE_RESET_MS);
                        status_set_ready_stable_deadline(context.ready_stable_deadline);
                        UserLedStatus_Set(USER_LED_STATE_SERVER_READY);
                        UserLedStatus_SetHttpFailed(false);
                        ESP_LOGI(TAG, "Network READY ip=" IPSTR " http=1 mdns=%d",
                                 IP2STR(&ip), s_mdns_service_started ? 1 : 0);
                    } else {
                        UserLedStatus_SetHttpFailed(true);
                        http_retry_count++;
                        status_enter_service_retry(http_retry_count, SERVER_NETWORK_STA_HTTP_RETRY_MS);
                        if (should_log_retry(http_retry_count)) {
                            ESP_LOGW(TAG, "HTTP service retry=%lu delay_ms=%u ret=%s",
                                     (unsigned long)http_retry_count,
                                     SERVER_NETWORK_STA_HTTP_RETRY_MS,
                                     esp_err_to_name(http_ret));
                        }
                    }
                } else {
                    status_enter_connecting();
                    UserLedStatus_Set(USER_LED_STATE_WIFI_CONNECTING);
                    set_wifi_ps(WIFI_PS_NONE, "connecting");
                    esp_err_t ret = apply_wifi_config(&credential);
                    if (ret == ESP_OK) {
                        begin_connection_attempt(&context);
                        ret = esp_wifi_connect();
                    }
                    connection_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(SERVER_NETWORK_STA_CONNECT_FLOW_TIMEOUT_MS);
                    if (ret != ESP_OK) {
                        uint32_t retry_count = 0;
                        uint32_t delay_ms = next_wifi_retry(&context, false, &retry_count);
                        status_enter_wifi_retry(retry_count, delay_ms, -1, 0);
                        if (should_log_retry(retry_count)) {
                            ESP_LOGW(TAG, "WiFi connect submit failed retry=%lu delay_ms=%lu ret=%s",
                                     (unsigned long)retry_count, (unsigned long)delay_ms,
                                     esp_err_to_name(ret));
                        }
                        complete_pending_requests(&pending_request_slot, &pending_request_id,
                                                  &coalesced_request_slot, &coalesced_request_id,
                                                  SERVER_NETWORK_STA_CONNECT_FAIL);
                    }
                }
            } else {
                status_set_last_result(snapshot.state == SERVER_NETWORK_STA_STATE_WAITING_IP
                                           ? TDX_JSON_RESULT_WIFI_GOT_IP_FAILED
                                           : TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT);
                server_network_sta_disconnect_purpose_t purpose =
                    snapshot.state == SERVER_NETWORK_STA_STATE_WAITING_IP
                        ? SERVER_NETWORK_STA_DISCONNECT_DHCP_RECOVERY
                        : SERVER_NETWORK_STA_DISCONNECT_CONNECT_RECOVERY;
                esp_err_t disconnect_ret = start_expected_disconnect(&context, purpose);
                if (disconnect_ret != ESP_OK) {
                    uint32_t retry_count = 0;
                    uint32_t delay_ms = next_wifi_retry(&context, false, &retry_count);
                    status_enter_wifi_retry(retry_count, delay_ms, -2, 0);
                    UserLedStatus_Set(USER_LED_STATE_WIFI_CONNECTING);
                    if (should_log_retry(retry_count)) {
                        ESP_LOGW(TAG, "WiFi timeout state=%d retry=%lu delay_ms=%lu",
                                 (int)snapshot.state, (unsigned long)retry_count,
                                 (unsigned long)delay_ms);
                    }
                }
                complete_pending_requests(&pending_request_slot, &pending_request_id,
                                          &coalesced_request_slot, &coalesced_request_id,
                                          SERVER_NETWORK_STA_CONNECT_FAIL);
            }
            continue;
        }

        if (event.type == WIFI_MANAGER_EVENT_RESYNC) {
            esp_ip4_addr_t current_ip = {0};
            if (connection_enabled && physical_link_has_ip(&current_ip)) {
                event.type = WIFI_MANAGER_EVENT_GOT_IP;
                event.ip = current_ip;
                ESP_LOGW(TAG, "WiFi event resync: physical link has IP");
            } else if (connection_enabled) {
                event.type = WIFI_MANAGER_EVENT_DISCONNECTED;
                event.reason = -4;
                ESP_LOGW(TAG, "WiFi event resync: physical link disconnected");
            } else {
                continue;
            }
        }

        if (event.type == WIFI_MANAGER_EVENT_CONNECT ||
            event.type == WIFI_MANAGER_EVENT_NEW_CREDENTIAL) {
            bool new_credential = event.type == WIFI_MANAGER_EVENT_NEW_CREDENTIAL;
            if (!new_credential && !event.force_reconnect &&
                pending_request_slot != SERVER_NETWORK_STA_INVALID_REQUEST_SLOT) {
                bool same_base_path = strcmp(event.base_path, base_path) == 0;
                if (same_base_path &&
                    coalesced_request_slot == SERVER_NETWORK_STA_INVALID_REQUEST_SLOT) {
                    coalesced_request_slot = event.request_slot;
                    coalesced_request_id = event.request_id;
                    ESP_LOGI(TAG,
                             "WiFi connect request joined active connect request_id=%lu active_request_id=%lu",
                             (unsigned long)event.request_id,
                             (unsigned long)pending_request_id);
                } else {
                    uint8_t rejected_slot = event.request_slot;
                    uint32_t rejected_id = event.request_id;
                    complete_pending_request(&rejected_slot, &rejected_id,
                                             SERVER_NETWORK_STA_CONNECT_SUPERSEDED);
                    ESP_LOGW(TAG, "WiFi connect request not joined request_id=%lu reason=%s",
                             (unsigned long)event.request_id,
                             same_base_path ? "join_slot_busy" : "base_path_mismatch");
                }
                continue;
            }
            complete_pending_requests(&pending_request_slot, &pending_request_id,
                                      &coalesced_request_slot, &coalesced_request_id,
                                      SERVER_NETWORK_STA_CONNECT_SUPERSEDED);
            pending_request_slot = event.request_slot;
            pending_request_id = event.request_id;
            if (event.base_path[0] != '\0') {
                strlcpy(base_path, event.base_path, sizeof(base_path));
            }
            credential = read_saved_wifi();
            connection_enabled = true;
            if (new_credential) {
                context.credential_generation++;
            }
            status_lock();
            s_status.credential_generation = context.credential_generation;
            status_unlock();
            clear_expected_disconnect(&context);
            reset_connection_health(&context);
            http_retry_count = 0;
            mdns_retry_count = 0;
            status_clear_retries();
            status_set_last_result(TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT);
            if (!credential.is_valid) {
                connection_enabled = false;
                status_enter_terminal(SERVER_NETWORK_STA_STATE_NO_CONFIG,
                                      TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT, 0, 0);
                if (s_wifi_stack_ready) {
                    esp_err_t disconnect_ret = start_expected_disconnect(
                        &context, SERVER_NETWORK_STA_DISCONNECT_NO_CONFIG);
                    if (disconnect_ret != ESP_OK) {
                        status_enter_terminal(SERVER_NETWORK_STA_STATE_NO_CONFIG,
                                              TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT, 0, 0);
                    }
                }
                UserLedStatus_SetWifiNoConfig(true);
                (void)ch583_wifi_uart_send_wifi_provision_status(0);
                ESP_LOGW(TAG, "WiFi no saved credential");
                complete_pending_requests(&pending_request_slot, &pending_request_id,
                                          &coalesced_request_slot, &coalesced_request_id,
                                          SERVER_NETWORK_STA_NO_SAVED_WIFI);
                continue;
            }
            (void)ch583_wifi_uart_send_wifi_provision_status(1);
            esp_err_t ret = ensure_wifi_stack();
            if (ret != ESP_OK) {
                connection_enabled = false;
                status_enter_terminal(SERVER_NETWORK_STA_STATE_FAILED,
                                      TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT, 0, 0);
                UserLedStatus_SetFatalError(true);
                ESP_LOGE(TAG, "WiFi stack init failed ret=%s", esp_err_to_name(ret));
                complete_pending_requests(&pending_request_slot, &pending_request_id,
                                          &coalesced_request_slot, &coalesced_request_id,
                                          SERVER_NETWORK_STA_CONNECT_FAIL);
                continue;
            }
            UserLedStatus_Set(USER_LED_STATE_WIFI_CONNECTING);
            UserLedStatus_SetWifiNoConfig(false);
            UserLedStatus_SetWifiAuthFailed(false);
            esp_ip4_addr_t current_ip = {0};
            if (!event.force_reconnect && same_active_config(&credential) &&
                physical_link_has_ip(&current_ip)) {
                event.type = WIFI_MANAGER_EVENT_GOT_IP;
                event.ip = current_ip;
            } else {
                wifi_ap_record_t ap = {0};
                bool connected = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
                bool connect_in_progress = state_is_connect_flow(snapshot.state);
                bool need_disconnect = connected || connect_in_progress;
                if (need_disconnect) {
                    ret = start_expected_disconnect(&context,
                                                    SERVER_NETWORK_STA_DISCONNECT_RECONFIGURE);
                    if (ret == ESP_OK) {
                        ESP_LOGI(TAG, "WiFi reconfigure pending credential_gen=%lu",
                                 (unsigned long)context.credential_generation);
                        continue;
                    }
                }
                {
                    ret = apply_wifi_config(&credential);
                    if (ret == ESP_OK) {
                        begin_connection_attempt(&context);
                        set_wifi_ps(WIFI_PS_NONE, "connecting");
                        status_enter_connecting();
                        ret = esp_wifi_connect();
                        connection_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(SERVER_NETWORK_STA_CONNECT_FLOW_TIMEOUT_MS);
                    }
                }
                if (ret != ESP_OK) {
                    uint32_t retry_count = 0;
                    uint32_t delay_ms = next_wifi_retry(&context, false, &retry_count);
                    status_enter_wifi_retry(retry_count, delay_ms, -1, 0);
                    UserLedStatus_Set(USER_LED_STATE_WIFI_CONNECTING);
                    ESP_LOGW(TAG, "WiFi start failed retry=1 delay_ms=%lu ret=%s",
                             (unsigned long)delay_ms, esp_err_to_name(ret));
                    complete_pending_requests(&pending_request_slot, &pending_request_id,
                                              &coalesced_request_slot, &coalesced_request_id,
                                              SERVER_NETWORK_STA_CONNECT_FAIL);
                    continue;
                }
                ESP_LOGI(TAG, "WiFi connect start ssid=%s", credential.ssid);
                continue;
            }
        }

        if (event.type == WIFI_MANAGER_EVENT_PROVISIONING) {
            complete_pending_requests(&pending_request_slot, &pending_request_id,
                                      &coalesced_request_slot, &coalesced_request_id,
                                      SERVER_NETWORK_STA_CONNECT_SUPERSEDED);
            reset_connection_health(&context);
            status_clear_retries();
            connection_enabled = false;
            if (s_wifi_stack_ready) {
                esp_err_t disconnect_ret = start_expected_disconnect(
                    &context, SERVER_NETWORK_STA_DISCONNECT_PROVISIONING);
                if (disconnect_ret != ESP_OK) {
                    status_enter_idle();
                }
            } else {
                status_enter_idle();
            }
            ESP_LOGI(TAG, "WiFi provisioning requested");
            continue;
        }

        if (event.type == WIFI_MANAGER_EVENT_STA_CONNECTED) {
            if (!connection_enabled ||
                context.disconnect_purpose != SERVER_NETWORK_STA_DISCONNECT_NONE) {
                continue;
            }
            if (!state_accepts_sta_connected(snapshot.state)) {
                ESP_LOGD(TAG, "Ignore STA_CONNECTED state=%s",
                         ServerNetworkSta_StateName(snapshot.state));
                continue;
            }
            esp_ip4_addr_t current_ip = {0};
            if (physical_link_has_ip(&current_ip)) {
                if (snapshot.has_ip) {
                    ESP_LOGW(TAG, "Ignore stale STA_CONNECTED event state=%s",
                             ServerNetworkSta_StateName(snapshot.state));
                    continue;
                }
                event.type = WIFI_MANAGER_EVENT_GOT_IP;
                event.ip = current_ip;
            } else {
                status_enter_waiting_ip();
                connection_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(SERVER_NETWORK_STA_AP_OR_IP_TIMEOUT_MS);
                continue;
            }
        }

        if (event.type == WIFI_MANAGER_EVENT_GOT_IP) {
            if (!connection_enabled ||
                context.disconnect_purpose != SERVER_NETWORK_STA_DISCONNECT_NONE) {
                continue;
            }
            if (!state_accepts_got_ip(snapshot.state)) {
                ESP_LOGD(TAG, "Ignore GOT_IP state=%s",
                         ServerNetworkSta_StateName(snapshot.state));
                continue;
            }
            esp_ip4_addr_t current_ip = {0};
            if (!physical_link_has_ip(&current_ip)) {
                ESP_LOGW(TAG, "Ignore stale GOT_IP event");
                continue;
            }
            event.ip = current_ip;
            status_enter_got_ip(&event.ip);
            set_wifi_ps(SERVER_NETWORK_STA_CONNECTED_PS, "got_ip");
            ESP_LOGI(TAG, "WiFi GOT_IP ip=" IPSTR, IP2STR(&event.ip));

            status_enter_starting_services();
            esp_err_t mdns_ret = mdns_start_once();
            if (mdns_ret != ESP_OK) {
                uint32_t mdns_delay_ms = mdns_retry_delay_ms(mdns_retry_count++);
                status_set_mdns_retry(mdns_retry_count, mdns_delay_ms);
                ESP_LOGW(TAG, "mDNS unavailable retry=%lu delay_ms=%lu ret=%s",
                         (unsigned long)mdns_retry_count,
                         (unsigned long)mdns_delay_ms, esp_err_to_name(mdns_ret));
            } else {
                mdns_retry_count = 0;
                status_set_mdns_retry(0, 0);
            }
            esp_err_t http_ret = file_server_start_once(base_path);
            esp_ip4_addr_t verified_ip = {0};
            if (http_ret == ESP_OK && physical_link_has_ip(&verified_ip)) {
                status_enter_ready(&verified_ip);
                context.ready_stability_pending = true;
                context.ready_stable_deadline = xTaskGetTickCount() +
                    pdMS_TO_TICKS(SERVER_NETWORK_STA_READY_STABLE_RESET_MS);
                status_set_ready_stable_deadline(context.ready_stable_deadline);
                UserLedStatus_Set(USER_LED_STATE_SERVER_READY);
                UserLedStatus_SetHttpFailed(false);
                ESP_LOGI(TAG, "Network READY ip=" IPSTR " http=1 mdns=%d",
                         IP2STR(&verified_ip), s_mdns_service_started ? 1 : 0);
                complete_pending_requests(&pending_request_slot, &pending_request_id,
                                          &coalesced_request_slot, &coalesced_request_id,
                                          SERVER_NETWORK_STA_OK);
            } else {
                http_retry_count = 1;
                status_enter_service_retry(http_retry_count, SERVER_NETWORK_STA_HTTP_RETRY_MS);
                UserLedStatus_SetHttpFailed(true);
                ESP_LOGW(TAG, "HTTP service unavailable retry_ms=%u ret=%s",
                         SERVER_NETWORK_STA_HTTP_RETRY_MS, esp_err_to_name(http_ret));
                complete_pending_requests(&pending_request_slot, &pending_request_id,
                                          &coalesced_request_slot, &coalesced_request_id,
                                          SERVER_NETWORK_STA_CONNECT_FAIL);
            }
            continue;
        }

        if (event.type == WIFI_MANAGER_EVENT_LOST_IP) {
            if (!connection_enabled ||
                (snapshot.state == SERVER_NETWORK_STA_STATE_RETRY_WAIT &&
                 snapshot.retry_type == SERVER_NETWORK_RETRY_WIFI)) {
                continue;
            }
            if (physical_link_has_ip(NULL)) {
                ESP_LOGW(TAG, "Ignore stale LOST_IP event");
                continue;
            }
            server_network_sta_state_t old_state = snapshot.state;
            context.ready_stability_pending = false;
            context.ready_stable_deadline = 0;
            status_set_ready_stable_deadline(0);
            esp_err_t disconnect_ret = start_expected_disconnect(
                &context, SERVER_NETWORK_STA_DISCONNECT_DHCP_RECOVERY);
            if (disconnect_ret != ESP_OK) {
                uint32_t retry_count = 0;
                bool was_online = state_has_online_service(old_state);
                uint32_t delay_ms = next_wifi_retry(&context, was_online, &retry_count);
                status_enter_wifi_retry(retry_count, delay_ms, -3, 0);
                UserLedStatus_Set(USER_LED_STATE_WIFI_CONNECTING);
                ESP_LOGW(TAG, "WiFi LOST_IP retry=%lu delay_ms=%lu",
                         (unsigned long)retry_count, (unsigned long)delay_ms);
            }
            complete_pending_requests(&pending_request_slot, &pending_request_id,
                                      &coalesced_request_slot, &coalesced_request_id,
                                      SERVER_NETWORK_STA_CONNECT_FAIL);
            continue;
        }

        if (event.type == WIFI_MANAGER_EVENT_DISCONNECTED) {
            esp_ip4_addr_t current_ip = {0};
            if (connection_enabled && physical_link_has_ip(&current_ip)) {
                ESP_LOGW(TAG, "Ignore stale DISCONNECTED event reason=%d", event.reason);
                continue;
            }
            status_lock();
            server_network_sta_state_t old_state = s_status.state;
            status_unlock();
            context.ready_stability_pending = false;
            context.ready_stable_deadline = 0;
            status_set_ready_stable_deadline(0);

            server_network_sta_disconnect_purpose_t purpose = context.disconnect_purpose;
            uint32_t disconnect_credential_generation =
                context.disconnect_credential_generation;
            bool disconnect_was_online = context.disconnect_was_online;
            clear_expected_disconnect(&context);
            if (should_ignore_disconnected_event(old_state, purpose)) {
                ESP_LOGD(TAG, "Ignore DISCONNECTED state=%s reason=%d",
                         ServerNetworkSta_StateName(old_state), event.reason);
                continue;
            }
            if (purpose == SERVER_NETWORK_STA_DISCONNECT_RECONFIGURE) {
                if (disconnect_credential_generation != context.credential_generation) {
                    ESP_LOGD(TAG, "Ignore obsolete reconfigure event credential_gen=%lu active=%lu",
                             (unsigned long)disconnect_credential_generation,
                             (unsigned long)context.credential_generation);
                    atomic_store(&s_resync_required, true);
                    continue;
                }
                esp_err_t ret = apply_wifi_config(&credential);
                if (ret == ESP_OK) {
                    begin_connection_attempt(&context);
                    set_wifi_ps(WIFI_PS_NONE, "connecting");
                    status_enter_connecting();
                    ret = esp_wifi_connect();
                    connection_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(SERVER_NETWORK_STA_CONNECT_FLOW_TIMEOUT_MS);
                }
                if (ret == ESP_OK) {
                    continue;
                }
                uint32_t retry_count = 0;
                uint32_t delay_ms = next_wifi_retry(&context, false, &retry_count);
                status_enter_wifi_retry(retry_count, delay_ms, event.reason, event.rssi);
                if (should_log_retry(retry_count)) {
                    ESP_LOGW(TAG, "WiFi reconfigure failed retry=%lu delay_ms=%lu ret=%s",
                             (unsigned long)retry_count, (unsigned long)delay_ms,
                             esp_err_to_name(ret));
                }
                continue;
            }
            if (purpose == SERVER_NETWORK_STA_DISCONNECT_PROVISIONING) {
                status_enter_idle();
                continue;
            }
            if (purpose == SERVER_NETWORK_STA_DISCONNECT_NO_CONFIG) {
                status_enter_terminal(SERVER_NETWORK_STA_STATE_NO_CONFIG,
                                      TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT, 0, 0);
                continue;
            }
            if (!connection_enabled) {
                continue;
            }

            bool was_ready = disconnect_was_online || state_has_online_service(old_state);
            wifi_auth_failure_class_t auth_class = classify_auth_failure(event.reason);
            bool auth_limit_reached = false;
            if (auth_class == WIFI_AUTH_FAILURE_HARD) {
                context.hard_auth_failure_count++;
                context.transient_auth_failure_count = 0;
                auth_limit_reached = context.hard_auth_failure_count >=
                                     SERVER_NETWORK_STA_HARD_AUTH_FAILURE_LIMIT;
            } else if (auth_class == WIFI_AUTH_FAILURE_TRANSIENT) {
                context.transient_auth_failure_count++;
                context.hard_auth_failure_count = 0;
                auth_limit_reached = context.transient_auth_failure_count >=
                                     SERVER_NETWORK_STA_TRANSIENT_AUTH_FAILURE_LIMIT;
            } else {
                context.hard_auth_failure_count = 0;
                context.transient_auth_failure_count = 0;
            }
            if (auth_limit_reached) {
                connection_enabled = false;
                status_enter_terminal(SERVER_NETWORK_STA_STATE_AUTH_FAILED,
                                      TDX_JSON_RESULT_WIFI_AUTH_FAILED,
                                      event.reason, event.rssi);
                UserLedStatus_SetWifiAuthFailed(true);
                ESP_LOGW(TAG, "WiFi AUTH_FAILED reason=%d(%s) hard=%lu transient=%lu ssid=%s",
                         event.reason, disconnect_reason_name(event.reason),
                         (unsigned long)context.hard_auth_failure_count,
                         (unsigned long)context.transient_auth_failure_count,
                         credential.ssid);
                complete_pending_requests(&pending_request_slot, &pending_request_id,
                                          &coalesced_request_slot, &coalesced_request_id,
                                          SERVER_NETWORK_STA_CONNECT_FAIL);
                continue;
            }

            uint32_t retry_count = 0;
            uint32_t delay_ms = next_wifi_retry(&context, was_ready, &retry_count);
            status_enter_wifi_retry(retry_count, delay_ms, event.reason, event.rssi);
            UserLedStatus_Set(USER_LED_STATE_WIFI_CONNECTING);
            if (should_log_retry(retry_count)) {
                ESP_LOGW(TAG, "WiFi disconnected reason=%d(%s) rssi=%d retry=%lu "
                         "delay_ms=%lu hard_auth=%lu transient_auth=%lu",
                         event.reason, disconnect_reason_name(event.reason), event.rssi,
                         (unsigned long)retry_count, (unsigned long)delay_ms,
                         (unsigned long)context.hard_auth_failure_count,
                         (unsigned long)context.transient_auth_failure_count);
            }
            if (auth_class == WIFI_AUTH_FAILURE_NONE) {
                complete_pending_requests(&pending_request_slot, &pending_request_id,
                                          &coalesced_request_slot, &coalesced_request_id,
                                          SERVER_NETWORK_STA_CONNECT_FAIL);
            }
        }
    }
}

esp_err_t ServerNetworkSta_Init(void)
{
    while (true) {
        portENTER_CRITICAL(&s_init_lock);
        if (s_init_state == WIFI_MANAGER_INIT_READY) {
            portEXIT_CRITICAL(&s_init_lock);
            return ESP_OK;
        }
        if (s_init_state == WIFI_MANAGER_INIT_NONE ||
            s_init_state == WIFI_MANAGER_INIT_FAILED) {
            s_init_state = WIFI_MANAGER_INIT_IN_PROGRESS;
            portEXIT_CRITICAL(&s_init_lock);
            break;
        }
        portEXIT_CRITICAL(&s_init_lock);
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    esp_err_t init_result = ESP_OK;
    if (s_status_mutex == NULL) {
        s_status_mutex = xSemaphoreCreateMutexStatic(&s_status_mutex_buffer);
    }
    if (s_status_mutex == NULL) {
        init_result = ESP_ERR_NO_MEM;
        goto init_done;
    }
    if (s_request_mutex == NULL) {
        s_request_mutex = xSemaphoreCreateMutexStatic(&s_request_mutex_buffer);
    }
    if (s_request_mutex == NULL) {
        init_result = ESP_ERR_NO_MEM;
        goto init_done;
    }
    for (size_t i = 0; i < sizeof(s_request_slots) / sizeof(s_request_slots[0]); i++) {
        if (s_request_slots[i].semaphore == NULL) {
            s_request_slots[i].semaphore =
                xSemaphoreCreateBinaryStatic(&s_request_slots[i].semaphore_buffer);
        }
        if (s_request_slots[i].semaphore == NULL) {
            init_result = ESP_ERR_NO_MEM;
            goto init_done;
        }
    }
    if (s_wifi_manager_queue == NULL) {
        s_wifi_manager_queue = xQueueCreate(SERVER_NETWORK_STA_MANAGER_QUEUE_LENGTH,
                                            sizeof(wifi_manager_event_t));
    }
    if (s_wifi_manager_queue == NULL) {
        init_result = ESP_ERR_NO_MEM;
        goto init_done;
    }
    if (s_wifi_manager_task == NULL) {
        BaseType_t ret = xTaskCreate(server_network_sta_manager_task, "wifi_manager",
                                     SERVER_NETWORK_STA_MANAGER_STACK_SIZE, NULL,
                                     SERVER_NETWORK_STA_MANAGER_PRIORITY,
                                     &s_wifi_manager_task);
        if (ret != pdPASS) {
            s_wifi_manager_task = NULL;
            init_result = ESP_ERR_NO_MEM;
            goto init_done;
        }
    }

init_done:
    portENTER_CRITICAL(&s_init_lock);
    s_init_state = init_result == ESP_OK ? WIFI_MANAGER_INIT_READY : WIFI_MANAGER_INIT_FAILED;
    portEXIT_CRITICAL(&s_init_lock);
    return init_result;
}

esp_err_t ServerNetworkSta_GetStatus(server_network_sta_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_status_mutex == NULL) {
        *status = s_status;
        return ESP_OK;
    }
    status_lock();
    *status = s_status;
    TickType_t now = xTaskGetTickCount();
    status->wifi_retry_after_ms = s_wifi_retry_deadline != 0 &&
                                  (int32_t)(s_wifi_retry_deadline - now) > 0
                                      ? pdTICKS_TO_MS(s_wifi_retry_deadline - now) : 0;
    status->http_retry_after_ms = s_http_retry_deadline != 0 &&
                                  (int32_t)(s_http_retry_deadline - now) > 0
                                      ? pdTICKS_TO_MS(s_http_retry_deadline - now) : 0;
    status->mdns_retry_after_ms = s_mdns_retry_deadline != 0 &&
                                  (int32_t)(s_mdns_retry_deadline - now) > 0
                                      ? pdTICKS_TO_MS(s_mdns_retry_deadline - now) : 0;
    status->ready_stable_remaining_ms = s_ready_stable_deadline != 0 &&
                                        (int32_t)(s_ready_stable_deadline - now) > 0
                                            ? pdTICKS_TO_MS(s_ready_stable_deadline - now) : 0;
    status_unlock();
    return ESP_OK;
}

const char *ServerNetworkSta_StateName(server_network_sta_state_t state)
{
    switch (state) {
    case SERVER_NETWORK_STA_STATE_IDLE: return "IDLE";
    case SERVER_NETWORK_STA_STATE_NO_CONFIG: return "NO_CONFIG";
    case SERVER_NETWORK_STA_STATE_CONNECTING: return "CONNECTING";
    case SERVER_NETWORK_STA_STATE_WAITING_IP: return "WAITING_IP";
    case SERVER_NETWORK_STA_STATE_GOT_IP: return "GOT_IP";
    case SERVER_NETWORK_STA_STATE_STARTING_SERVICES: return "STARTING_SERVICES";
    case SERVER_NETWORK_STA_STATE_READY: return "READY";
    case SERVER_NETWORK_STA_STATE_RETRY_WAIT: return "RETRY_WAIT";
    case SERVER_NETWORK_STA_STATE_AUTH_FAILED: return "AUTH_FAILED";
    case SERVER_NETWORK_STA_STATE_DISCONNECTING: return "DISCONNECTING";
    case SERVER_NETWORK_STA_STATE_FAILED: return "FAILED";
    default: return "UNKNOWN";
    }
}

const char *ServerNetworkSta_DisconnectPurposeName(
    server_network_sta_disconnect_purpose_t purpose)
{
    switch (purpose) {
    case SERVER_NETWORK_STA_DISCONNECT_NONE: return "none";
    case SERVER_NETWORK_STA_DISCONNECT_RECONFIGURE: return "reconfigure";
    case SERVER_NETWORK_STA_DISCONNECT_CONNECT_RECOVERY: return "connect_recovery";
    case SERVER_NETWORK_STA_DISCONNECT_DHCP_RECOVERY: return "dhcp_recovery";
    case SERVER_NETWORK_STA_DISCONNECT_PROVISIONING: return "provisioning";
    case SERVER_NETWORK_STA_DISCONNECT_NO_CONFIG: return "no_config";
    default: return "unknown";
    }
}

int ServerNetworkSta_GetLastConnectResult(void)
{
    server_network_sta_status_t status = {0};
    (void)ServerNetworkSta_GetStatus(&status);
    return status.last_result;
}

void ServerNetworkSta_RequestProvisioning(void)
{
    if (ServerNetworkSta_Init() != ESP_OK) {
        ESP_LOGE(TAG, "WiFi provisioning manager init failed");
        return;
    }
    wifi_manager_event_t event = {
        .type = WIFI_MANAGER_EVENT_PROVISIONING,
    };
    post_manager_event(&event);
}

static uint8_t submit_connect(const char *base_path, bool force_reconnect,
                              bool new_credential)
{
    if (ServerNetworkSta_Init() != ESP_OK) {
        return SERVER_NETWORK_STA_CONNECT_FAIL;
    }
    uint8_t request_slot = SERVER_NETWORK_STA_INVALID_REQUEST_SLOT;
    uint32_t request_id = 0;
    if (!allocate_request_slot(&request_slot, &request_id)) {
        return SERVER_NETWORK_STA_CONNECT_FAIL;
    }
    wifi_manager_event_t event = {
        .type = new_credential ? WIFI_MANAGER_EVENT_NEW_CREDENTIAL
                               : WIFI_MANAGER_EVENT_CONNECT,
        .force_reconnect = force_reconnect,
        .request_slot = request_slot,
        .request_id = request_id,
    };
    strlcpy(event.base_path, base_path != NULL ? base_path : "/data", sizeof(event.base_path));
    if (xQueueSend(s_wifi_manager_queue, &event, pdMS_TO_TICKS(100)) != pdTRUE) {
        (void)xSemaphoreTake(s_request_mutex, portMAX_DELAY);
        s_request_slots[request_slot].in_use = false;
        (void)xSemaphoreGive(s_request_mutex);
        return SERVER_NETWORK_STA_CONNECT_FAIL;
    }
    uint8_t result = SERVER_NETWORK_STA_CONNECT_FAIL;
    BaseType_t received = xSemaphoreTake(s_request_slots[request_slot].semaphore,
                                         pdMS_TO_TICKS(SERVER_NETWORK_STA_SYNC_REQUEST_TIMEOUT_MS));
    (void)xSemaphoreTake(s_request_mutex, portMAX_DELAY);
    if (received == pdTRUE && s_request_slots[request_slot].request_id == request_id) {
        result = s_request_slots[request_slot].result;
    }
    if (s_request_slots[request_slot].request_id == request_id) {
        s_request_slots[request_slot].in_use = false;
    }
    (void)xSemaphoreGive(s_request_mutex);
    if (received != pdTRUE) {
        ESP_LOGW(TAG, "WiFi connect request timeout request_id=%lu timeout_ms=%u",
                 (unsigned long)request_id, SERVER_NETWORK_STA_SYNC_REQUEST_TIMEOUT_MS);
    }
    return result;
}

uint8_t User_Network_mode_app_init(const char *base_path)
{
    return submit_connect(base_path, false, false);
}

uint8_t User_Network_mode_app_init_force(const char *base_path)
{
    return submit_connect(base_path, true, false);
}

uint8_t User_Network_mode_app_new_credential(const char *base_path)
{
    return submit_connect(base_path, true, true);
}
