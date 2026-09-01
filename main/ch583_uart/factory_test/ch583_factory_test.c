#include "ch583_factory_test.h"

#include <stdio.h>
#include <string.h>

#include "ch583_wifi_uart_protocol.h"
#include "epd_display_mode.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "factory_reset.h"
#include "freertos/FreeRTOS.h"
#include "image_business_worker.h"
#include "local_image_browsing.h"
#include "nvs.h"
#include "server_network_sta.h"
#include "server_network_sta_daily_image.h"
#include "server_network_sta_slideshow.h"
#include "server_network_sta_wifi_recovery.h"
#include "tdx_cfg.h"

#define FACTORY_TEST_CONNECT_TIMEOUT_MS 15000U
#define FACTORY_TEST_STATUS_POLL_MS 500U

typedef enum {
    FACTORY_TEST_COMMAND_WIFI_MAC = 0,
    FACTORY_TEST_COMMAND_WIFI_CONNECT,
} factory_test_command_t;

typedef struct {
    factory_test_command_t command;
    uint16_t rx_seq;
    uint32_t generation;
    char ssid[FACTORY_TEST_SSID_MAX_LEN + 1U];
    char key[FACTORY_TEST_KEY_MAX_LEN + 1U];
} factory_test_request_t;

_Static_assert(sizeof(factory_test_request_t) <=
                   USER_IMAGE_BUSINESS_WORKER_PAYLOAD_SIZE,
               "factory test request exceeds image worker payload");

static const char *TAG = "factory_test";
static uint32_t s_factory_test_generation;
static uint32_t s_factory_test_active_generation;
static factory_test_request_t s_factory_test_admitted_request;
static bool s_factory_test_manages_wifi_this_boot;

static bool factory_test_request_is_current(uint32_t generation)
{
    return generation != 0U &&
           __atomic_load_n(&s_factory_test_active_generation,
                           __ATOMIC_ACQUIRE) == generation;
}

bool Ch583FactoryTest_IsBusy(void)
{
    return __atomic_load_n(&s_factory_test_active_generation,
                           __ATOMIC_ACQUIRE) != 0U;
}

bool Ch583FactoryTest_ManagesWifiThisBoot(void)
{
    return __atomic_load_n(&s_factory_test_manages_wifi_this_boot,
                           __ATOMIC_ACQUIRE);
}

static bool factory_test_text_is_valid(const char *text, size_t max_len)
{
    if (text == NULL || text[0] == '\0') {
        return false;
    }
    size_t len = strlen(text);
    if (len == 0U || len > max_len) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (ch < 0x21U || ch > 0x7EU || ch == '|' || ch == '^' || ch == '&') {
            return false;
        }
    }
    return true;
}

static esp_err_t factory_test_parse_request(const char *arg,
                                            size_t arg_len,
                                            factory_test_request_t *request)
{
    static const char wifi_mac_command[] = "facWifiMac";
    static const char wifi_connect_command[] = "facWifiCon";
    char parse_buf[sizeof(wifi_connect_command) + FACTORY_TEST_SSID_MAX_LEN +
                   FACTORY_TEST_KEY_MAX_LEN + 3U] = {0};

    if (arg == NULL || request == NULL || arg_len == 0U ||
        arg_len >= sizeof(parse_buf) || strlen(arg) != arg_len) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(request, 0, sizeof(*request));
    if (arg_len == sizeof(wifi_mac_command) - 1U &&
        memcmp(arg, wifi_mac_command, arg_len) == 0) {
        request->command = FACTORY_TEST_COMMAND_WIFI_MAC;
        return ESP_OK;
    }
    if (arg_len <= sizeof(wifi_connect_command) ||
        memcmp(arg, wifi_connect_command, sizeof(wifi_connect_command) - 1U) != 0 ||
        arg[sizeof(wifi_connect_command) - 1U] != ' ') {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(parse_buf, arg, arg_len);
    char *cursor = parse_buf + sizeof(wifi_connect_command) - 1U;
    while (*cursor == ' ') {
        cursor++;
    }
    char *ssid = cursor;
    while (*cursor != '\0' && *cursor != ' ') {
        cursor++;
    }
    if (*cursor == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    *cursor++ = '\0';
    while (*cursor == ' ') {
        cursor++;
    }
    char *key = cursor;
    while (*cursor != '\0' && *cursor != ' ') {
        cursor++;
    }
    if (*cursor != '\0' || strlen(key) < FACTORY_TEST_KEY_MIN_LEN ||
        !factory_test_text_is_valid(ssid, FACTORY_TEST_SSID_MAX_LEN) ||
        !factory_test_text_is_valid(key, FACTORY_TEST_KEY_MAX_LEN)) {
        return ESP_ERR_INVALID_ARG;
    }

    request->command = FACTORY_TEST_COMMAND_WIFI_CONNECT;
    strlcpy(request->ssid, ssid, sizeof(request->ssid));
    strlcpy(request->key, key, sizeof(request->key));
    return ESP_OK;
}

static esp_err_t factory_test_save_namespace_string(const char *name_space,
                                                    const char *ssid_key,
                                                    const char *key_key,
                                                    const char *ssid,
                                                    const char *key,
                                                    bool blob)
{
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(name_space, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    if (blob) {
        ret = nvs_set_blob(handle, ssid_key, ssid, strlen(ssid) + 1U);
        if (ret == ESP_OK) {
            ret = nvs_set_blob(handle, key_key, key, strlen(key) + 1U);
        }
    } else {
        ret = nvs_set_str(handle, ssid_key, ssid);
        if (ret == ESP_OK) {
            ret = nvs_set_str(handle, key_key, key);
        }
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static esp_err_t factory_test_save_wifi_credentials(const char *ssid,
                                                    const char *key)
{
    esp_err_t ret = factory_test_save_namespace_string("wifi",
                                                       "ssid",
                                                       "password",
                                                       ssid,
                                                       key,
                                                       false);
    if (ret != ESP_OK) {
        return ret;
    }
    return factory_test_save_namespace_string("nvs.net80211",
                                              "sta.ssid",
                                              "sta.pswd",
                                              ssid,
                                              key,
                                              true);
}

static int factory_test_connected_rssi(const server_network_sta_status_t *status)
{
    wifi_ap_record_t ap_info = {0};
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return status != NULL ? status->rssi : 0;
}

static bool factory_test_has_ip_for_request(
    const server_network_sta_status_t *status,
    uint32_t initial_credential_generation,
    uint32_t initial_connection_generation)
{
    return status != NULL &&
           status->credential_generation != initial_credential_generation &&
           status->connection_generation != initial_connection_generation &&
           status->has_ip &&
           status->ip[0] != '\0' &&
           strcmp(status->ip, "0.0.0.0") != 0;
}

static esp_err_t factory_test_make_mac_result(char *result, size_t result_size)
{
    uint8_t mac[6] = {0};
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi MAC read failed ret=%s", esp_err_to_name(ret));
        return ret;
    }
    const esp_app_desc_t *app = esp_app_get_description();
    if (app == NULL || app->version[0] == '\0') {
        ESP_LOGE(TAG, "app version unavailable");
        return ESP_ERR_INVALID_STATE;
    }

    char mac_text[13] = {0};
    snprintf(mac_text, sizeof(mac_text),
             "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    char crc_input[sizeof(mac_text) + sizeof(app->version)] = {0};
    int input_len = snprintf(crc_input, sizeof(crc_input), "%s%s",
                             mac_text, app->version);
    if (input_len <= 0 || input_len >= (int)sizeof(crc_input)) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint16_t business_crc = ch583_wifi_uart_crc16(crc_input,
                                                  (size_t)input_len);
    int result_len = snprintf(result, result_size,
                              "facWifiMac %s,%s,%04x",
                              mac_text, app->version, business_crc);
    return result_len > 0 && result_len < (int)result_size ?
           ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t factory_test_make_connect_result(
    const factory_test_request_t *request,
    char *result,
    size_t result_size)
{
    server_network_sta_status_t initial_status = {0};
    (void)ServerNetworkSta_GetStatus(&initial_status);

    esp_err_t ret = factory_test_save_wifi_credentials(request->ssid,
                                                       request->key);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "credential save failed ssid=%s ret=%s",
                 request->ssid, esp_err_to_name(ret));
    } else {
        esp_err_t recovery_ret =
            ServerNetworkStaWifiRecovery_OnFactoryCredentialsChanged();
        if (recovery_ret != ESP_OK) {
            ESP_LOGE(TAG, "WiFi recovery reset failed ret=%s",
                     esp_err_to_name(recovery_ret));
        }
        ret = ServerNetworkSta_RequestNewCredentialAsync("/data");
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "WiFi connect submit failed ssid=%s ret=%s",
                     request->ssid, esp_err_to_name(ret));
        }
    }

    bool connected = false;
    server_network_sta_status_t status = initial_status;
    TickType_t start_tick = xTaskGetTickCount();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connect started ssid=%s timeout_ms=%u",
                 request->ssid, FACTORY_TEST_CONNECT_TIMEOUT_MS);
        while (factory_test_request_is_current(request->generation) &&
               !FactoryReset_IsBusy()) {
            if (ServerNetworkSta_GetStatus(&status) == ESP_OK &&
                factory_test_has_ip_for_request(
                    &status,
                    initial_status.credential_generation,
                    initial_status.connection_generation)) {
                connected = true;
                break;
            }
            TickType_t elapsed = xTaskGetTickCount() - start_tick;
            if (elapsed >= pdMS_TO_TICKS(FACTORY_TEST_CONNECT_TIMEOUT_MS)) {
                break;
            }
            TickType_t remaining =
                pdMS_TO_TICKS(FACTORY_TEST_CONNECT_TIMEOUT_MS) - elapsed;
            TickType_t wait_ticks = pdMS_TO_TICKS(FACTORY_TEST_STATUS_POLL_MS);
            if (wait_ticks > remaining) {
                wait_ticks = remaining;
            }
            (void)ImageBusinessWorker_WaitInterruptible(wait_ticks);
        }
        // Check once more at the timeout boundary to avoid missing a new IP.
        if (!connected && factory_test_request_is_current(request->generation) &&
            !FactoryReset_IsBusy() &&
            ServerNetworkSta_GetStatus(&status) == ESP_OK) {
            connected = factory_test_has_ip_for_request(
                &status,
                initial_status.credential_generation,
                initial_status.connection_generation);
        }
    }

    if (!factory_test_request_is_current(request->generation) ||
        FactoryReset_IsBusy()) {
        return ESP_ERR_INVALID_STATE;
    }
    int rssi = connected ? factory_test_connected_rssi(&status) : 0;
    if (connected) {
        ESP_LOGI(TAG, "WiFi connected ssid=%s rssi=%d", request->ssid, rssi);
    } else {
        ESP_LOGW(TAG, "WiFi failed ssid=%s state=%s rssi=%d",
                 request->ssid,
                 ServerNetworkSta_StateName(status.state),
                 rssi);
    }
    int result_len = snprintf(result, result_size,
                              "facWifiCon %s %s %d",
                              connected ? "success" : "failed",
                              request->ssid,
                              rssi);
    return result_len > 0 && result_len < (int)result_size ?
           ESP_OK : ESP_ERR_INVALID_SIZE;
}

static void factory_test_finish(uint32_t generation,
                                factory_test_command_t command)
{
    if (!factory_test_request_is_current(generation)) {
        return;
    }
    esp_err_t mode_ret = EpdDisplayMode_Set(USER_EPD_DISPLAY_MODE_NORMAL);
    if (mode_ret != ESP_OK) {
        ESP_LOGE(TAG, "NORMAL mode restore failed ret=%s",
                 esp_err_to_name(mode_ret));
    }
    uint32_t expected_generation = generation;
    (void)__atomic_compare_exchange_n(&s_factory_test_active_generation,
                                      &expected_generation,
                                      0U,
                                      false,
                                      __ATOMIC_ACQ_REL,
                                      __ATOMIC_ACQUIRE);
    ESP_LOGI(TAG, "completed generation=%lu",
             (unsigned long)generation);
    if (command == FACTORY_TEST_COMMAND_WIFI_CONNECT) {
        ServerNetworkStaWifiRecovery_OnFactoryTestFinished();
    }
}

static esp_err_t factory_test_worker_run(const void *payload,
                                         size_t payload_size)
{
    if (payload == NULL || payload_size != sizeof(factory_test_request_t)) {
        return ESP_ERR_INVALID_ARG;
    }
    factory_test_request_t request = {0};
    memcpy(&request, payload, sizeof(request));
    if (!factory_test_request_is_current(request.generation)) {
        return ESP_ERR_INVALID_STATE;
    }

    char result[128] = {0};
    esp_err_t ret = request.command == FACTORY_TEST_COMMAND_WIFI_MAC ?
                    factory_test_make_mac_result(result, sizeof(result)) :
                    factory_test_make_connect_result(&request,
                                                     result,
                                                     sizeof(result));
    if (ret == ESP_OK && factory_test_request_is_current(request.generation)) {
        if (ch583_wifi_uart_send_factory_result(result) != 0) {
            ESP_LOGE(TAG, "FACTORY_RESULT send failed generation=%lu",
                     (unsigned long)request.generation);
            ret = ESP_FAIL;
        }
    } else if (ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "command failed type=%d generation=%lu ret=%s",
                 (int)request.command,
                 (unsigned long)request.generation,
                 esp_err_to_name(ret));
    }
    factory_test_finish(request.generation, request.command);
    memset(&request, 0, sizeof(request));
    memset(result, 0, sizeof(result));
    return ret;
}

static void factory_test_cancel_pending(const void *payload,
                                        size_t payload_size)
{
    factory_test_request_t request = {0};
    if (payload != NULL && payload_size == sizeof(request)) {
        memcpy(&request, payload, sizeof(request));
    }
    if (factory_test_request_is_current(request.generation)) {
        uint32_t expected_generation = request.generation;
        (void)__atomic_compare_exchange_n(&s_factory_test_active_generation,
                                          &expected_generation,
                                          0U,
                                          false,
                                          __ATOMIC_ACQ_REL,
                                          __ATOMIC_ACQUIRE);
        ESP_LOGW(TAG, "pending request canceled generation=%lu",
                 (unsigned long)request.generation);
    }
    memset(&request, 0, sizeof(request));
}

esp_err_t Ch583FactoryTest_Admit(uint16_t rx_seq,
                                const char *arg,
                                size_t arg_len,
                                uint32_t *admission)
{
    if (admission == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *admission = 0U;
    factory_test_request_t request = {0};
    esp_err_t ret = factory_test_parse_request(arg, arg_len, &request);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "invalid FACTORY_DATA seq=%u len=%u",
                 (unsigned int)rx_seq, (unsigned int)arg_len);
        return ret;
    }
    if (FactoryReset_IsBusy()) {
        ESP_LOGW(TAG, "request blocked by Factory Reset seq=%u",
                 (unsigned int)rx_seq);
        return ESP_ERR_INVALID_STATE;
    }

    ret = ImageBusinessWorker_SetExclusiveOwner(
        IMAGE_BUSINESS_OWNER_FACTORY_TEST);
    if (ret != ESP_OK) {
        return ret;
    }
    request.rx_seq = rx_seq;
    request.generation = __atomic_add_fetch(&s_factory_test_generation,
                                            1U,
                                            __ATOMIC_ACQ_REL);
    uint32_t previous_generation = __atomic_exchange_n(
        &s_factory_test_active_generation,
        request.generation,
        __ATOMIC_ACQ_REL);
    if (previous_generation != 0U) {
        ServerNetworkStaWifiRecovery_OnFactoryTestFinished();
    }

    ServerNetworkStaSlideshow_Stop();
    ServerNetworkStaDailyImage_Stop();
    LocalImageBrowsing_Stop();

    memcpy(&s_factory_test_admitted_request, &request, sizeof(request));
    *admission = request.generation;
    ESP_LOGI(TAG, "accepted seq=%u command=%d generation=%lu",
             (unsigned int)rx_seq,
             (int)request.command,
             (unsigned long)request.generation);
    memset(&request, 0, sizeof(request));
    return ESP_OK;
}

esp_err_t Ch583FactoryTest_Commit(uint32_t admission)
{
    if (admission == 0U || !factory_test_request_is_current(admission) ||
        s_factory_test_admitted_request.generation != admission ||
        FactoryReset_IsBusy()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = ImageBusinessWorker_SetExclusiveOwner(
        IMAGE_BUSINESS_OWNER_FACTORY_TEST);
    if (ret != ESP_OK || !factory_test_request_is_current(admission) ||
        FactoryReset_IsBusy()) {
        ImageBusinessWorker_ClearExclusiveOwner(
            IMAGE_BUSINESS_OWNER_FACTORY_TEST);
        return ret != ESP_OK ? ret : ESP_ERR_INVALID_STATE;
    }
    factory_test_request_t request = {0};
    memcpy(&request, &s_factory_test_admitted_request, sizeof(request));
    uint32_t replace_mask =
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_DAILY) |
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_SLIDESHOW) |
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_LOCAL_IMAGE) |
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_CAST) |
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_CAST2PIC) |
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_USB_CAST) |
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_USB_CAST2PIC) |
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_FACTORY_TEST);
    ret = ImageBusinessWorker_SubmitReplacingPending(
        IMAGE_BUSINESS_OWNER_FACTORY_TEST,
        factory_test_worker_run,
        factory_test_cancel_pending,
        &request,
        sizeof(request),
        request.generation,
        replace_mask);
    if (ret != ESP_OK) {
        if (factory_test_request_is_current(request.generation)) {
            uint32_t expected_generation = request.generation;
            (void)__atomic_compare_exchange_n(&s_factory_test_active_generation,
                                              &expected_generation,
                                              0U,
                                              false,
                                              __ATOMIC_ACQ_REL,
                                              __ATOMIC_ACQUIRE);
            ImageBusinessWorker_ClearExclusiveOwner(
                IMAGE_BUSINESS_OWNER_FACTORY_TEST);
        }
        ESP_LOGE(TAG, "submit failed seq=%u ret=%s",
                 (unsigned int)request.rx_seq, esp_err_to_name(ret));
        memset(&request, 0, sizeof(request));
        return ret;
    }
    if (request.command == FACTORY_TEST_COMMAND_WIFI_CONNECT) {
        __atomic_store_n(&s_factory_test_manages_wifi_this_boot,
                         true,
                         __ATOMIC_RELEASE);
    }
    memset(&s_factory_test_admitted_request, 0,
           sizeof(s_factory_test_admitted_request));
    memset(&request, 0, sizeof(request));
    return ESP_OK;
}

void Ch583FactoryTest_CancelAdmission(uint32_t admission)
{
    if (admission == 0U) {
        return;
    }
    uint32_t expected_generation = admission;
    if (__atomic_compare_exchange_n(&s_factory_test_active_generation,
                                    &expected_generation,
                                    0U,
                                    false,
                                    __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE)) {
        memset(&s_factory_test_admitted_request, 0,
               sizeof(s_factory_test_admitted_request));
        ImageBusinessWorker_ClearExclusiveOwner(
            IMAGE_BUSINESS_OWNER_FACTORY_TEST);
    }
}

void Ch583FactoryTest_CancelForFactoryReset(void)
{
    uint32_t generation = __atomic_exchange_n(
        &s_factory_test_active_generation, 0U, __ATOMIC_ACQ_REL);
    if (generation == 0U) {
        return;
    }
    ServerNetworkStaWifiRecovery_OnFactoryTestFinished();
    (void)ImageBusinessWorker_CancelPending(
        IMAGE_BUSINESS_OWNER_FACTORY_TEST);
    ImageBusinessWorker_ClearExclusiveOwner(
        IMAGE_BUSINESS_OWNER_FACTORY_TEST);
    ImageBusinessWorker_Wake();
    ESP_LOGW(TAG, "canceled by Factory Reset generation=%lu",
             (unsigned long)generation);
}
