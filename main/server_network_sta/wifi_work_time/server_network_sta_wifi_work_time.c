#include "server_network_sta_wifi_work_time.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "ch583_wifi_uart_protocol.h"
#include "epd_display_app.h"
#include "epd_display_mode.h"
#include "led_status.h"
#include "server_network_sta_slideshow.h"
#include "tdx_cfg.h"

static const char *TAG = "server_sta_wifi_time";
static TickType_t s_wifi_work_start_tick = 0;
static TickType_t s_last_network_data_tick = 0;
static TickType_t s_last_power_off_send_tick = 0;
static TaskHandle_t s_work_state_task = NULL;
static uint32_t s_ota_hold_flags = 0;
// Zero means no HTTP activity has been recorded; nonzero stores the FreeRTOS tick plus one.
static uint32_t s_last_http_activity_tick_encoded = 0;
// Keep CH583 startup and validated business activity independent from HTTP activity.
static uint32_t s_last_ch583_activity_tick_encoded = 0;
static uint32_t s_guard_log_flags = 0;
static uint32_t s_runtime_state_flags = 0;
static bool s_one_shot_power_off_countdown_active = false;
static uint32_t s_one_shot_restore_continue_time = USER_WORK_STATE_DEFAULT_CONTINUE_SECONDS;
static uint32_t s_one_shot_restore_standby_time = USER_WORK_STATE_DEFAULT_STANDBY_SECONDS;
static bool s_image_save_in_progress = false;

static void format_epoch_local(int64_t epoch, char *buf, size_t buf_size)
{
    if (buf == NULL || buf_size == 0) {
        return;
    }
    time_t t = (time_t)epoch;
    struct tm tm_value = {0};
    localtime_r(&t, &tm_value);
    strftime(buf, buf_size, "%Y-%m-%d %H:%M:%S", &tm_value);
}

// Keep these globals compatible with the old sleep/work-time flow for BLE and HTTP handlers.
uint32_t working_time = 0;
uint32_t server_required_continue_work_time = USER_WORK_STATE_DEFAULT_CONTINUE_SECONDS;
uint32_t wifi_standby_time_s = USER_WORK_STATE_DEFAULT_STANDBY_SECONDS;

// Store all runtime sleep/work values in one NVS blob so future power-mode changes stay centralized.
typedef struct __attribute__((packed)) {
    uint16_t sleep_time_value;
    uint32_t working_time_value;
    uint32_t server_required_continue_work_time_value;
    uint32_t wifi_standby_time_s_value;
} user_work_state_nvs_blob_t;

static uint32_t clamp_continue_seconds(uint32_t seconds)
{
    if (seconds < USER_WORK_STATE_MIN_CONTINUE_SECONDS) {
        return USER_WORK_STATE_MIN_CONTINUE_SECONDS;
    }
    if (seconds > USER_WORK_STATE_MAX_CONTINUE_SECONDS) {
        return USER_WORK_STATE_MAX_CONTINUE_SECONDS;
    }
    return seconds;
}

static void log_work_state_blob(const char *label, const user_work_state_nvs_blob_t *blob)
{
    if (blob == NULL) {
        return;
    }
    ESP_LOGI(TAG,
             "%s sleep_time=%u working_time=%lu continue=%lu standby=%lu",
             label != NULL ? label : "work_state",
             (unsigned int)blob->sleep_time_value,
             (unsigned long)blob->working_time_value,
             (unsigned long)blob->server_required_continue_work_time_value,
             (unsigned long)blob->wifi_standby_time_s_value);
}

static user_work_state_nvs_blob_t make_work_state_blob(void)
{
    user_work_state_nvs_blob_t blob = {
        .sleep_time_value = sleep_time,
        .working_time_value = working_time,
        .server_required_continue_work_time_value = server_required_continue_work_time,
        .wifi_standby_time_s_value = wifi_standby_time_s,
    };
    return blob;
}

static void apply_work_state_blob(const user_work_state_nvs_blob_t *blob)
{
    if (blob == NULL) {
        return;
    }

    sleep_time = blob->sleep_time_value;
    working_time = 0;
    server_required_continue_work_time = clamp_continue_seconds(blob->server_required_continue_work_time_value);
    wifi_standby_time_s = blob->wifi_standby_time_s_value;

    s_wifi_work_start_tick = xTaskGetTickCount();
    s_last_network_data_tick = s_wifi_work_start_tick;
    s_last_power_off_send_tick = 0;
    ESP_LOGI(TAG,
             "restore globals sleep_time=%u working_time=%lu continue=%lu standby=%lu",
             (unsigned int)sleep_time,
             (unsigned long)working_time,
             (unsigned long)server_required_continue_work_time,
             (unsigned long)wifi_standby_time_s);
}

static esp_err_t load_work_state_from_nvs(user_work_state_nvs_blob_t *blob, size_t *stored_size)
{
    nvs_handle_t handle = 0;
    size_t size = 0;

    if (blob == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = nvs_open(USER_WORK_STATE_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "open work state nvs failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_get_blob(handle, USER_WORK_STATE_NVS_KEY, NULL, &size);
    if (ret == ESP_OK && size == sizeof(*blob)) {
        ret = nvs_get_blob(handle, USER_WORK_STATE_NVS_KEY, blob, &size);
    } else if (ret == ESP_OK) {
        ESP_LOGW(TAG, "work state blob size mismatch stored=%u expected=%u",
                 (unsigned int)size, (unsigned int)sizeof(*blob));
        ret = ESP_ERR_INVALID_SIZE;
    }
    nvs_close(handle);

    if (stored_size != NULL) {
        *stored_size = size;
    }
    return ret;
}

static esp_err_t save_work_state_to_nvs(void)
{
    nvs_handle_t handle = 0;
    user_work_state_nvs_blob_t blob = make_work_state_blob();

    esp_err_t ret = nvs_open(USER_WORK_STATE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "open work state nvs for write failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_blob(handle, USER_WORK_STATE_NVS_KEY, &blob, sizeof(blob));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);

    if (ret == ESP_OK) {
        user_work_state_nvs_blob_t read_blob = {0};
        size_t stored_size = 0;
        log_work_state_blob("save value", &blob);
        esp_err_t read_ret = load_work_state_from_nvs(&read_blob, &stored_size);
        if (read_ret == ESP_OK && memcmp(&blob, &read_blob, sizeof(blob)) == 0) {
            ESP_LOGI(TAG, "work state verify ok stored_size=%u", (unsigned int)stored_size);
        } else {
            ESP_LOGW(TAG, "work state verify failed read_ret=%s stored_size=%u",
                     esp_err_to_name(read_ret), (unsigned int)stored_size);
        }
    } else {
        ESP_LOGE(TAG, "save work state failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static bool parse_app_nvs_u32(const char *value, uint32_t *out_value)
{
    char *end_ptr = NULL;
    unsigned long parsed = 0;

    if (value == NULL || out_value == NULL || value[0] == '\0') {
        return false;
    }

    errno = 0;
    parsed = strtoul(value, &end_ptr, 10);
    if (errno != 0 || end_ptr == value || *end_ptr != '\0' || parsed > UINT32_MAX) {
        return false;
    }

    *out_value = (uint32_t)parsed;
    return true;
}

static esp_err_t save_work_time_vars_to_app_nvs(void)
{
    char value[16];
    esp_err_t ret = ESP_OK;
    esp_err_t write_ret = ESP_OK;

    snprintf(value, sizeof(value), "%lu", (unsigned long)server_required_continue_work_time);
    write_ret = app_nvs_write_str(SERVER_REQUIRED_CONTINUE_WORK_TIME_NVS_KEY, value);
    if (write_ret != ESP_OK && ret == ESP_OK) {
        ret = write_ret;
    }

    snprintf(value, sizeof(value), "%lu", (unsigned long)wifi_standby_time_s);
    write_ret = app_nvs_write_str(WIFI_STANDBY_TIME_S_NVS_KEY, value);
    if (write_ret != ESP_OK && ret == ESP_OK) {
        ret = write_ret;
    }

    ESP_LOGI(TAG, "save app nvs continue=%lu standby=%lu ret=%s",
             (unsigned long)server_required_continue_work_time,
             (unsigned long)wifi_standby_time_s,
             esp_err_to_name(ret));
    return ret;
}

static void load_work_time_vars_from_app_nvs(void)
{
    char value[16];
    char default_value[16];
    uint32_t parsed = 0;

    snprintf(default_value, sizeof(default_value), "%lu",
             (unsigned long)server_required_continue_work_time);
    if (app_nvs_read_str(SERVER_REQUIRED_CONTINUE_WORK_TIME_NVS_KEY,
                         value,
                         sizeof(value),
                         default_value) == ESP_OK &&
        parse_app_nvs_u32(value, &parsed)) {
        server_required_continue_work_time = clamp_continue_seconds(parsed);
    }

    snprintf(default_value, sizeof(default_value), "%lu",
             (unsigned long)wifi_standby_time_s);
    if (app_nvs_read_str(WIFI_STANDBY_TIME_S_NVS_KEY,
                         value,
                         sizeof(value),
                         default_value) == ESP_OK &&
        parse_app_nvs_u32(value, &parsed)) {
        wifi_standby_time_s = parsed;
    }

    ESP_LOGI(TAG, "load app nvs continue=%lu standby=%lu",
             (unsigned long)server_required_continue_work_time,
             (unsigned long)wifi_standby_time_s);
}

static uint32_t update_working_time_seconds(void)
{
    TickType_t now = xTaskGetTickCount();
    if (s_wifi_work_start_tick == 0) {
        s_wifi_work_start_tick = now;
    }
    working_time = (uint32_t)(((now - s_wifi_work_start_tick) * portTICK_PERIOD_MS) / 1000U);
    return working_time;
}

static uint32_t http_activity_hold_remaining_seconds(TickType_t now)
{
    uint32_t encoded_tick = __atomic_load_n(&s_last_http_activity_tick_encoded, __ATOMIC_ACQUIRE);
    if (encoded_tick == 0) {
        return 0;
    }

    TickType_t last_tick = (TickType_t)(encoded_tick - 1U);
    TickType_t hold_ticks = pdMS_TO_TICKS(USER_WORK_STATE_HTTP_ACTIVITY_HOLD_SECONDS * 1000U);
    TickType_t elapsed_ticks = now - last_tick;
    if (elapsed_ticks >= hold_ticks) {
        return 0;
    }

    uint32_t remaining_ms = (uint32_t)((hold_ticks - elapsed_ticks) * portTICK_PERIOD_MS);
    return (remaining_ms + 999U) / 1000U;
}

static uint32_t ch583_activity_hold_remaining_seconds(TickType_t now)
{
    uint32_t encoded_tick = __atomic_load_n(&s_last_ch583_activity_tick_encoded, __ATOMIC_ACQUIRE);
    if (encoded_tick == 0) {
        return 0;
    }

    TickType_t last_tick = (TickType_t)(encoded_tick - 1U);
    TickType_t hold_ticks = pdMS_TO_TICKS(USER_WORK_STATE_CH583_ACTIVITY_HOLD_SECONDS * 1000U);
    TickType_t elapsed_ticks = now - last_tick;
    if (elapsed_ticks >= hold_ticks) {
        return 0;
    }

    uint32_t remaining_ms = (uint32_t)((hold_ticks - elapsed_ticks) * portTICK_PERIOD_MS);
    return (remaining_ms + 999U) / 1000U;
}

//调用   ServerNetworkStaWifiWorkTime_OnNetworkData  从头计
// Configure CH583/CH585 wake timer before POWER_OFF so slideshow can resume after power cut.
static uint32_t slideshow_startup_delay_seconds(void)
{
    return (TDX_SLIDESHOW_STARTUP_DELAY_MS + 999U) / 1000U;
}

static uint32_t slideshow_wake_advance_seconds(uint32_t *startup_delay_out)
{
    uint32_t startup_delay = slideshow_startup_delay_seconds();
    if (startup_delay_out != NULL) {
        *startup_delay_out = startup_delay;
    }
    return startup_delay + TDX_SLIDESHOW_WAKE_EXTRA_ADVANCE_SECONDS;
}

static void reset_work_time_counter_for_slideshow_short_interval(void)
{
    s_wifi_work_start_tick = xTaskGetTickCount();
    working_time = 0;
}

static void restore_work_time_after_one_shot_skip(void)
{
    server_required_continue_work_time = s_one_shot_restore_continue_time;
    wifi_standby_time_s = s_one_shot_restore_standby_time;
    working_time = 0;
    s_wifi_work_start_tick = xTaskGetTickCount();
    s_last_network_data_tick = s_wifi_work_start_tick;
    s_last_power_off_send_tick = 0;
    s_one_shot_power_off_countdown_active = false;
    UserLedStatus_SetPowerOffPending(false);
}

static bool should_skip_one_shot_power_off_for_slideshow(void)
{
    if (!s_one_shot_power_off_countdown_active) {
        return false;
    }

    uint8_t mode = EpdDisplayMode_Get();
    if (mode != USER_EPD_DISPLAY_MODE_SLIDESHOW) {
        ESP_LOGI(TAG,
                 "one-shot power off skip slideshow remain check mode=%u(%s)",
                 (unsigned int)mode,
                 EpdDisplayMode_ToString(mode));
        return false;
    }

    uint32_t interval = 0;
    uint32_t elapsed = 0;
    uint32_t remain = 0;
    int64_t now_epoch = 0;
    int64_t next_epoch = 0;
    bool running = false;
    if (ServerNetworkStaSlideshow_GetScheduleTiming(&interval,
                                                    &now_epoch,
                                                    &next_epoch,
                                                    &remain,
                                                    &running) &&
        running) {
        char now_text[32] = {0};
        char next_text[32] = {0};
        format_epoch_local(now_epoch, now_text, sizeof(now_text));
        format_epoch_local(next_epoch, next_text, sizeof(next_text));
        if (remain <= USER_EPD_DONE_LOW_POWER_SLIDESHOW_MIN_REMAIN_SECONDS) {
            ESP_LOGI(TAG,
                     "one-shot power off skipped because slideshow next is soon remain=%lu min=%lu now=%lld(%s) next=%lld(%s)",
                     (unsigned long)remain,
                     (unsigned long)USER_EPD_DONE_LOW_POWER_SLIDESHOW_MIN_REMAIN_SECONDS,
                     (long long)now_epoch,
                     now_text,
                     (long long)next_epoch,
                     next_text);
            restore_work_time_after_one_shot_skip();
            return true;
        }
        ESP_LOGI(TAG,
                 "one-shot power off allowed by slideshow rtc remain=%lu min=%lu now=%lld(%s) next=%lld(%s)",
                 (unsigned long)remain,
                 (unsigned long)USER_EPD_DONE_LOW_POWER_SLIDESHOW_MIN_REMAIN_SECONDS,
                 (long long)now_epoch,
                 now_text,
                 (long long)next_epoch,
                 next_text);
        return false;
    }

    if (ServerNetworkStaSlideshow_GetRuntimeTiming(&interval, &elapsed, &running) &&
        running) {
        remain = interval > elapsed ? interval - elapsed : 0;
        if (remain <= USER_EPD_DONE_LOW_POWER_SLIDESHOW_MIN_REMAIN_SECONDS) {
            ESP_LOGI(TAG,
                     "one-shot power off skipped because slideshow next is soon remain=%lu min=%lu interval=%lu elapsed=%lu",
                     (unsigned long)remain,
                     (unsigned long)USER_EPD_DONE_LOW_POWER_SLIDESHOW_MIN_REMAIN_SECONDS,
                     (unsigned long)interval,
                     (unsigned long)elapsed);
            restore_work_time_after_one_shot_skip();
            return true;
        }
        ESP_LOGI(TAG,
                 "one-shot power off allowed by slideshow runtime remain=%lu min=%lu interval=%lu elapsed=%lu",
                 (unsigned long)remain,
                 (unsigned long)USER_EPD_DONE_LOW_POWER_SLIDESHOW_MIN_REMAIN_SECONDS,
                 (unsigned long)interval,
                 (unsigned long)elapsed);
    }
    return false;
}

static bool configure_ch583_wake_timer_before_power_off(void)
{
    uint32_t interval = TDX_SLIDESHOW_INTERVAL_MIN_SECONDS;
    bool random = false;
    bool slideshow_on = ServerNetworkStaSlideshow_IsSavedEnabled("/data", &interval, &random);
    int wake_ret;

    if (slideshow_on) {
        if (interval < TDX_SLIDESHOW_INTERVAL_MIN_SECONDS ||
            interval > TDX_SLIDESHOW_INTERVAL_MAX_SECONDS) {
            interval = TDX_SLIDESHOW_INTERVAL_MIN_SECONDS;
        }

        uint32_t runtime_interval = 0;
        uint32_t runtime_elapsed = 0;
        bool runtime_running = false;
        uint32_t wake_interval = interval;
        uint32_t startup_delay = 0;
        uint32_t wake_advance = slideshow_wake_advance_seconds(&startup_delay);
        int64_t schedule_now = 0;
        int64_t schedule_next = 0;
        uint32_t schedule_remain = 0;
        if (ServerNetworkStaSlideshow_GetScheduleTiming(&runtime_interval,
                                                        &schedule_now,
                                                        &schedule_next,
                                                        &schedule_remain,
                                                        &runtime_running) &&
            runtime_running) {
            wake_interval = schedule_remain > 0 ? schedule_remain : 1U;
            runtime_elapsed = runtime_interval > schedule_remain ?
                              runtime_interval - schedule_remain :
                              0;
            char now_text[32] = {0};
            char next_text[32] = {0};
            format_epoch_local(schedule_now, now_text, sizeof(now_text));
            format_epoch_local(schedule_next, next_text, sizeof(next_text));
            ESP_LOGI(TAG,
                     "slideshow rtc wake timing now=%lld(%s) next=%lld(%s) remain=%lu interval=%lu",
                     (long long)schedule_now,
                     now_text,
                     (long long)schedule_next,
                     next_text,
                     (unsigned long)schedule_remain,
                     (unsigned long)runtime_interval);
        } else if (ServerNetworkStaSlideshow_GetRuntimeTiming(&runtime_interval,
                                                              &runtime_elapsed,
                                                              &runtime_running) &&
                   runtime_running &&
                   runtime_interval > 0) {
            wake_interval = runtime_interval > runtime_elapsed ? runtime_interval - runtime_elapsed : 1U;
        }
        wake_interval = wake_interval > wake_advance ? wake_interval - wake_advance : 1U;

        if (!s_one_shot_power_off_countdown_active &&
            wake_interval < TDX_SLIDESHOW_POWER_OFF_MIN_WAKE_INTERVAL_SECONDS) {
            reset_work_time_counter_for_slideshow_short_interval();
            ESP_LOGI(TAG,
                     "slideshow wake interval too short, skip power off wake_interval=%lu min=%lu saved_interval=%lu elapsed=%lu startup_delay=%lu extra_advance=%lu wake_advance=%lu",
                     (unsigned long)wake_interval,
                     (unsigned long)TDX_SLIDESHOW_POWER_OFF_MIN_WAKE_INTERVAL_SECONDS,
                     (unsigned long)interval,
                     (unsigned long)runtime_elapsed,
                     (unsigned long)startup_delay,
                     (unsigned long)TDX_SLIDESHOW_WAKE_EXTRA_ADVANCE_SECONDS,
                     (unsigned long)wake_advance);
            return false;
        }

        ESP_LOGI(TAG,
                 "slideshow enabled before power off, wake timer on interval=%lu saved_interval=%lu elapsed=%lu startup_delay=%lu extra_advance=%lu wake_advance=%lu random=%d",
                 (unsigned long)wake_interval,
                 (unsigned long)interval,
                 (unsigned long)runtime_elapsed,
                 (unsigned long)startup_delay,
                 (unsigned long)TDX_SLIDESHOW_WAKE_EXTRA_ADVANCE_SECONDS,
                 (unsigned long)wake_advance,
                 random ? 1 : 0);
        wake_ret = ch583_wifi_uart_send_wake_timer_on(wake_interval);
    } else {
        ESP_LOGI(TAG, "slideshow disabled before power off, wake timer off");
        wake_ret = ch583_wifi_uart_send_wake_timer_off();
    }

    if (wake_ret < 0) {
        ESP_LOGW(TAG, "CH583 wake timer config failed ret=%d, continue power off", wake_ret);
    }
    return true;
}

static bool retry_pending_led_power_off_cancel(void)
{
    uint32_t runtime_flags = __atomic_load_n(&s_runtime_state_flags, __ATOMIC_ACQUIRE);
    if ((runtime_flags & USER_WORK_STATE_RUNTIME_LED_CANCEL_PENDING_BIT) == 0) {
        return true;
    }

    esp_err_t cancel_ret = UserLedStatus_CancelPowerOffSync();
    if (cancel_ret != ESP_OK) {
        return false;
    }

    (void)__atomic_fetch_and(&s_runtime_state_flags,
                             ~USER_WORK_STATE_RUNTIME_LED_CANCEL_PENDING_BIT,
                             __ATOMIC_ACQ_REL);
    UserLedStatus_SetPowerOffPending(false);
    ESP_LOGI(TAG, "pending LED power-off cancellation completed");
    return true;
}

static void work_state_task(void *arg)
{
    uint8_t counter = 0;
    uint8_t slideshow_debug_counter = 0;
#if CH583_WIFI_NFC_TEST_ENABLE
    bool nfc_test_done = false;
#endif
    (void)arg;

    while (true) {
        if (!retry_pending_led_power_off_cancel()) {
            vTaskDelay(pdMS_TO_TICKS(USER_WORK_STATE_TASK_INTERVAL_MS));
            continue;
        }

        uint32_t runtime_flags = __atomic_load_n(&s_runtime_state_flags, __ATOMIC_ACQUIRE);
        if ((runtime_flags & USER_WORK_STATE_RUNTIME_CH583_STARTUP_PENDING_BIT) != 0) {
            vTaskDelay(pdMS_TO_TICKS(USER_WORK_STATE_TASK_INTERVAL_MS));
            continue;
        }

        uint32_t elapsed = update_working_time_seconds();
        uint32_t clamped_continue_time = s_one_shot_power_off_countdown_active ?
                                         server_required_continue_work_time :
                                         clamp_continue_seconds(server_required_continue_work_time);
        if (!s_one_shot_power_off_countdown_active &&
            clamped_continue_time != server_required_continue_work_time) {
            server_required_continue_work_time = clamped_continue_time;
            (void)save_work_state_to_nvs();
            (void)save_work_time_vars_to_app_nvs();
        }

        uint32_t remaining = server_required_continue_work_time > elapsed ?
                             server_required_continue_work_time - elapsed :
                             0;

#if CH583_WIFI_NFC_TEST_ENABLE
        if (!nfc_test_done && elapsed >= CH583_WIFI_NFC_TEST_START_DELAY_SECONDS) {
            nfc_test_done = ch583_wifi_uart_test_nfc_step();
        }
#endif


        counter++;
        if(counter >30)
        {
         counter = 0;
         ESP_LOGI(TAG, "work_state status elapsed=%lu target=%lu remaining=%lu standby=%lu",
                 (unsigned long)elapsed,
                 (unsigned long)server_required_continue_work_time,
                 (unsigned long)remaining,
                 (unsigned long)wifi_standby_time_s);
        }

        uint32_t slideshow_interval = 0;
        uint32_t slideshow_elapsed = 0;
        uint32_t slideshow_rtc_remain = 0;
        int64_t slideshow_now_epoch = 0;
        int64_t slideshow_next_epoch = 0;
        bool slideshow_running = false;
        if (ServerNetworkStaSlideshow_GetScheduleTiming(&slideshow_interval,
                                                        &slideshow_now_epoch,
                                                        &slideshow_next_epoch,
                                                        &slideshow_rtc_remain,
                                                        &slideshow_running) &&
            slideshow_running) {
            slideshow_debug_counter++;
            if (slideshow_debug_counter >= 10 || slideshow_rtc_remain <= 3) {
                slideshow_debug_counter = 0;
                char now_text[32] = {0};
                char next_text[32] = {0};
                format_epoch_local(slideshow_now_epoch, now_text, sizeof(now_text));
                format_epoch_local(slideshow_next_epoch, next_text, sizeof(next_text));
                ESP_LOGI(TAG,
                         "slide_timer rtc active=1 interval=%lu now=%lld(%s) next=%lld(%s) remain=%lu epd=%s",
                         (unsigned long)slideshow_interval,
                         (long long)slideshow_now_epoch,
                         now_text,
                         (long long)slideshow_next_epoch,
                         next_text,
                         (unsigned long)slideshow_rtc_remain,
                         ServerNetworkStaEpdDisplay_IsBusy() ? "BUSY" : "IDLE");
            }
        } else if (ServerNetworkStaSlideshow_GetRuntimeTiming(&slideshow_interval,
                                                              &slideshow_elapsed,
                                                              &slideshow_running) &&
                   slideshow_running) {
            slideshow_debug_counter++;
            uint32_t slideshow_remaining = slideshow_interval > slideshow_elapsed ?
                                           slideshow_interval - slideshow_elapsed :
                                           0;
            if (slideshow_debug_counter >= 10 || slideshow_remaining <= 3) {
                slideshow_debug_counter = 0;
                ESP_LOGI(TAG,
                         "slide_timer legacy_tick active=1 interval=%lu elapsed=%lu remain=%lu epd=%s",
                         (unsigned long)slideshow_interval,
                         (unsigned long)slideshow_elapsed,
                         (unsigned long)slideshow_remaining,
                         ServerNetworkStaEpdDisplay_IsBusy() ? "BUSY" : "IDLE");
            }
        } else {
            slideshow_debug_counter = 0;
        }



        if (elapsed > server_required_continue_work_time) {
            uint32_t ota_hold_flags = __atomic_load_n(&s_ota_hold_flags, __ATOMIC_ACQUIRE);
            if (ota_hold_flags != 0) {
                if (counter == 0) {
                    ESP_LOGI(TAG,
                             "working_time timeout ignored during OTA elapsed=%lu target=%lu standby=%lu hold=0x%lx",
                             (unsigned long)elapsed,
                             (unsigned long)server_required_continue_work_time,
                             (unsigned long)wifi_standby_time_s,
                             (unsigned long)ota_hold_flags);
                }
            } else {
                TickType_t now = xTaskGetTickCount();
                TickType_t retry_interval_ticks = pdMS_TO_TICKS(20000);
                if (ServerNetworkStaEpdDisplay_IsBusy()) {
                    if (counter == 0) {
                        ESP_LOGI(TAG,
                                 "working_time timeout postponed because EPD busy elapsed=%lu target=%lu standby=%lu",
                                 (unsigned long)elapsed,
                                 (unsigned long)server_required_continue_work_time,
                                 (unsigned long)wifi_standby_time_s);
                    }
                    vTaskDelay(pdMS_TO_TICKS(USER_WORK_STATE_TASK_INTERVAL_MS));
                    continue;
                }
                if (ServerNetworkStaWifiWorkTime_IsImageSaveInProgress()) {
                    if (counter == 0) {
                        ESP_LOGI(TAG,
                                 "power off postponed because image save busy elapsed=%lu target=%lu standby=%lu",
                                 (unsigned long)elapsed,
                                 (unsigned long)server_required_continue_work_time,
                                 (unsigned long)wifi_standby_time_s);
                    }
                    vTaskDelay(pdMS_TO_TICKS(USER_WORK_STATE_TASK_INTERVAL_MS));
                    continue;
                }
                uint32_t http_hold_remaining = http_activity_hold_remaining_seconds(now);
                if (http_hold_remaining > 0) {
                    uint32_t old_log_flags = __atomic_fetch_or(&s_guard_log_flags,
                                                               USER_WORK_STATE_GUARD_LOG_HTTP_BIT,
                                                               __ATOMIC_ACQ_REL);
                    if ((old_log_flags & USER_WORK_STATE_GUARD_LOG_HTTP_BIT) == 0) {
                        ESP_LOGI(TAG,
                                 "power off postponed by HTTP activity remaining=%lu elapsed=%lu target=%lu",
                                 (unsigned long)http_hold_remaining,
                                 (unsigned long)elapsed,
                                 (unsigned long)server_required_continue_work_time);
                    }
                    vTaskDelay(pdMS_TO_TICKS(USER_WORK_STATE_TASK_INTERVAL_MS));
                    continue;
                }
                (void)__atomic_fetch_and(&s_guard_log_flags,
                                         ~USER_WORK_STATE_GUARD_LOG_HTTP_BIT,
                                         __ATOMIC_ACQ_REL);
                uint32_t ch583_hold_remaining = ch583_activity_hold_remaining_seconds(now);
                if (ch583_hold_remaining > 0) {
                    uint32_t old_log_flags = __atomic_fetch_or(&s_guard_log_flags,
                                                               USER_WORK_STATE_GUARD_LOG_CH583_BIT,
                                                               __ATOMIC_ACQ_REL);
                    if ((old_log_flags & USER_WORK_STATE_GUARD_LOG_CH583_BIT) == 0) {
                        ESP_LOGI(TAG,
                                 "power off postponed by CH583 activity remaining=%lu elapsed=%lu target=%lu",
                                 (unsigned long)ch583_hold_remaining,
                                 (unsigned long)elapsed,
                                 (unsigned long)server_required_continue_work_time);
                    }
                    vTaskDelay(pdMS_TO_TICKS(USER_WORK_STATE_TASK_INTERVAL_MS));
                    continue;
                }
                (void)__atomic_fetch_and(&s_guard_log_flags,
                                         ~USER_WORK_STATE_GUARD_LOG_CH583_BIT,
                                         __ATOMIC_ACQ_REL);
                if (s_last_power_off_send_tick != 0 &&
                    (now - s_last_power_off_send_tick) < retry_interval_ticks) {
                    vTaskDelay(pdMS_TO_TICKS(USER_WORK_STATE_TASK_INTERVAL_MS));
                    continue;
                }
                s_last_power_off_send_tick = now;

                if (should_skip_one_shot_power_off_for_slideshow()) {
                    vTaskDelay(pdMS_TO_TICKS(USER_WORK_STATE_TASK_INTERVAL_MS));
                    continue;
                }
                ESP_LOGI(TAG,
                         "working_time timeout, evaluate CH583 power off elapsed=%lu target=%lu standby=%lu",
                         (unsigned long)elapsed,
                         (unsigned long)server_required_continue_work_time,
                         (unsigned long)wifi_standby_time_s);
                if (!configure_ch583_wake_timer_before_power_off()) {
                    vTaskDelay(pdMS_TO_TICKS(USER_WORK_STATE_TASK_INTERVAL_MS));
                    continue;
                }
                UserLedStatus_SetPowerOffPending(true);
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_err_t led_ret = UserLedStatus_PreparePowerOffSync();
                if (led_ret != ESP_OK) {
                    UserLedStatus_SetPowerOffPending(false);
                    ESP_LOGE(TAG, "power off postponed because LED shutdown failed ret=%s",
                             esp_err_to_name(led_ret));
                    vTaskDelay(pdMS_TO_TICKS(USER_WORK_STATE_TASK_INTERVAL_MS));
                    continue;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
                uint32_t final_ota_hold_flags = __atomic_load_n(&s_ota_hold_flags, __ATOMIC_ACQUIRE);
                bool final_epd_busy = ServerNetworkStaEpdDisplay_IsBusy();
                bool final_image_save_busy = ServerNetworkStaWifiWorkTime_IsImageSaveInProgress();
                TickType_t final_now = xTaskGetTickCount();
                uint32_t final_http_hold_remaining = http_activity_hold_remaining_seconds(final_now);
                uint32_t final_ch583_hold_remaining = ch583_activity_hold_remaining_seconds(final_now);
                uint32_t final_elapsed = update_working_time_seconds();
                bool final_timer_active = final_elapsed <= server_required_continue_work_time;
                if (final_timer_active ||
                    final_ota_hold_flags != 0 ||
                    final_epd_busy ||
                    final_image_save_busy ||
                    final_http_hold_remaining > 0 ||
                    final_ch583_hold_remaining > 0) {
                    ESP_LOGI(TAG,
                             "power off canceled by final guard timer=%d ota=0x%lx epd=%d image_save=%d http_remaining=%lu ch583_remaining=%lu",
                             final_timer_active ? 1 : 0,
                             (unsigned long)final_ota_hold_flags,
                             final_epd_busy ? 1 : 0,
                             final_image_save_busy ? 1 : 0,
                             (unsigned long)final_http_hold_remaining,
                             (unsigned long)final_ch583_hold_remaining);
                    if (final_http_hold_remaining > 0) {
                        (void)__atomic_fetch_or(&s_guard_log_flags,
                                                USER_WORK_STATE_GUARD_LOG_HTTP_BIT,
                                                __ATOMIC_ACQ_REL);
                    }
                    if (final_ch583_hold_remaining > 0) {
                        (void)__atomic_fetch_or(&s_guard_log_flags,
                                                USER_WORK_STATE_GUARD_LOG_CH583_BIT,
                                                __ATOMIC_ACQ_REL);
                    }
                    (void)__atomic_fetch_or(&s_runtime_state_flags,
                                            USER_WORK_STATE_RUNTIME_LED_CANCEL_PENDING_BIT,
                                            __ATOMIC_ACQ_REL);
                    if (!retry_pending_led_power_off_cancel()) {
                        ESP_LOGE(TAG, "final guard failed to release LED power-off lock, retry scheduled");
                    }
                    s_last_power_off_send_tick = 0;
                    vTaskDelay(pdMS_TO_TICKS(USER_WORK_STATE_TASK_INTERVAL_MS));
                    continue;
                }
                int power_off_ret = ch583_wifi_uart_send_power_off();
                if (power_off_ret < 0) {
                    ESP_LOGW(TAG, "CH583 power off command failed ret=%d", power_off_ret);
                }
            }
        } else {
            s_last_power_off_send_tick = 0;
            (void)__atomic_fetch_and(&s_guard_log_flags,
                                     ~(USER_WORK_STATE_GUARD_LOG_HTTP_BIT |
                                       USER_WORK_STATE_GUARD_LOG_CH583_BIT),
                                     __ATOMIC_ACQ_REL);
        }

        vTaskDelay(pdMS_TO_TICKS(USER_WORK_STATE_TASK_INTERVAL_MS));
    }
}

static const char *find_json_key(const char *body, const char *key)
{
    char pattern[64];
    const char *pos = body;
    if (body == NULL || key == NULL) {
        return NULL;
    }
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    while ((pos = strstr(pos, pattern)) != NULL) {
        const char *after = pos + strlen(pattern);
        while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n') {
            after++;
        }
        if (*after == ':') {
            return pos;
        }
        pos += strlen(pattern);
    }
    return NULL;
}

static bool json_func_equals(const char *body, const char *func)
{
    const char *pos = find_json_key(body, "func");
    if (pos == NULL || func == NULL) {
        return false;
    }

    pos += strlen("func") + 2;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    if (*pos != ':') {
        return false;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    if (*pos != '"') {
        return false;
    }
    pos++;

    size_t func_len = strlen(func);
    return strncmp(pos, func, func_len) == 0 && pos[func_len] == '"';
}

static bool parse_json_int(const char *body, const char *key, int *out)
{
    const char *pos = find_json_key(body, key);
    char *end_ptr = NULL;
    long value = 0;
    if (pos == NULL || out == NULL) {
        return false;
    }

    pos += strlen(key) + 2;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    if (*pos != ':') {
        return false;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    if ((*pos == '-' && (pos[1] < '0' || pos[1] > '9')) ||
        (*pos != '-' && (*pos < '0' || *pos > '9'))) {
        return false;
    }

    errno = 0;
    value = strtol(pos, &end_ptr, 10);
    if (errno != 0 || end_ptr == pos || value < INT32_MIN || value > INT32_MAX) {
        return false;
    }
    while (*end_ptr == ' ' || *end_ptr == '\t' || *end_ptr == '\r' || *end_ptr == '\n') {
        end_ptr++;
    }
    if (*end_ptr != ',' && *end_ptr != '}') {
        return false;
    }
    *out = (int)value;
    return true;
}

static esp_err_t send_wifi_work_time_result(httpd_req_t *req, int result, const char *message)
{
    char json[176];
    if (result == TDX_JSON_RESULT_OK) {
        snprintf(json, sizeof(json),
                 "{\"func\":\"set_wifi_work_time_result\",\"result\":%d}",
                 TDX_JSON_RESULT_OK);
    } else {
        snprintf(json, sizeof(json),
                 "{\"func\":\"set_wifi_work_time_result\",\"result\":%d,\"message\":\"%s\"}",
                 result,
                 message != NULL ? message : "set wifi work time failed");
    }

    ESP_LOGI(TAG, "set_wifi_work_time response: %s", json);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

void ServerNetworkStaWifiWorkTime_OnNetworkData(void)
{
    working_time = 0;
    s_last_network_data_tick = xTaskGetTickCount();
    s_wifi_work_start_tick = s_last_network_data_tick;
    s_last_power_off_send_tick = 0;
    if (server_required_continue_work_time > 0) {
        ESP_LOGI(TAG, "activity reset working_time continue=%lu elapsed_ms=%u",
                 (unsigned long)server_required_continue_work_time,
                 (unsigned int)((s_last_network_data_tick - s_wifi_work_start_tick) * portTICK_PERIOD_MS));
    }
}

void ServerNetworkStaWifiWorkTime_OnHttpNetworkActivity(void)
{
    TickType_t now = xTaskGetTickCount();
    uint32_t encoded_tick = (uint32_t)now + 1U;
    if (encoded_tick == 0) {
        encoded_tick = 1U;
    }
    __atomic_store_n(&s_last_http_activity_tick_encoded, encoded_tick, __ATOMIC_RELEASE);
    s_last_power_off_send_tick = 0;
}

void ServerNetworkStaWifiWorkTime_OnCh583Activity(void)
{
    TickType_t now = xTaskGetTickCount();
    uint32_t encoded_tick = (uint32_t)now + 1U;
    if (encoded_tick == 0) {
        encoded_tick = 1U;
    }
    __atomic_store_n(&s_last_ch583_activity_tick_encoded, encoded_tick, __ATOMIC_RELEASE);
    s_last_power_off_send_tick = 0;
}

void ServerNetworkStaWifiWorkTime_OnCh583Initialized(void)
{
    // Record the timed hold before releasing the startup guard so shutdown cannot race initialization.
    ServerNetworkStaWifiWorkTime_OnCh583Activity();
    uint32_t old_flags = __atomic_fetch_and(&s_runtime_state_flags,
                                            ~USER_WORK_STATE_RUNTIME_CH583_STARTUP_PENDING_BIT,
                                            __ATOMIC_ACQ_REL);
    if ((old_flags & USER_WORK_STATE_RUNTIME_CH583_STARTUP_PENDING_BIT) != 0) {
        ESP_LOGI(TAG, "CH583 startup guard released, activity hold started seconds=%u",
                 (unsigned int)USER_WORK_STATE_CH583_ACTIVITY_HOLD_SECONDS);
    }
}

void ServerNetworkStaWifiWorkTime_RequestOneShotPowerOffCountdown(uint32_t seconds)
{
    if (seconds == 0) {
        return;
    }
    if (!s_one_shot_power_off_countdown_active) {
        s_one_shot_restore_continue_time = server_required_continue_work_time;
        s_one_shot_restore_standby_time = wifi_standby_time_s;
    }

    server_required_continue_work_time = seconds;
    wifi_standby_time_s = seconds;
    working_time = 0;
    s_wifi_work_start_tick = xTaskGetTickCount();
    s_last_network_data_tick = s_wifi_work_start_tick;
    s_last_power_off_send_tick = 0;
    s_one_shot_power_off_countdown_active = true;
    UserLedStatus_SetPowerOffPending(true);

    ESP_LOGI(TAG,
             "one-shot power off countdown requested target=%lu standby=%lu",
             (unsigned long)server_required_continue_work_time,
             (unsigned long)wifi_standby_time_s);
}

void ServerNetworkStaWifiWorkTime_SetImageSaveInProgress(bool in_progress)
{
    bool old_value = __atomic_exchange_n(&s_image_save_in_progress, in_progress, __ATOMIC_ACQ_REL);
    if (old_value != in_progress) {
        ESP_LOGI(TAG, "image save in progress=%d", in_progress ? 1 : 0);
    }
}

bool ServerNetworkStaWifiWorkTime_IsImageSaveInProgress(void)
{
    return __atomic_load_n(&s_image_save_in_progress, __ATOMIC_ACQUIRE);
}

void ServerNetworkStaWifiWorkTime_SetOtaWriteInProgress(bool in_progress)
{
    if (in_progress) {
        (void)__atomic_fetch_or(&s_ota_hold_flags,
                                USER_WORK_STATE_OTA_HOLD_WRITE_BIT,
                                __ATOMIC_ACQ_REL);
    } else {
        (void)__atomic_fetch_and(&s_ota_hold_flags,
                                 ~USER_WORK_STATE_OTA_HOLD_WRITE_BIT,
                                 __ATOMIC_ACQ_REL);
    }
    ESP_LOGI(TAG, "ota write in progress=%d hold=0x%lx",
             in_progress ? 1 : 0,
             (unsigned long)__atomic_load_n(&s_ota_hold_flags, __ATOMIC_ACQUIRE));
}

void ServerNetworkStaWifiWorkTime_SetOtaReceiveInProgress(bool in_progress)
{
    if (in_progress) {
        (void)__atomic_fetch_or(&s_ota_hold_flags,
                                USER_WORK_STATE_OTA_HOLD_RECEIVE_BIT,
                                __ATOMIC_ACQ_REL);
    } else {
        (void)__atomic_fetch_and(&s_ota_hold_flags,
                                 ~USER_WORK_STATE_OTA_HOLD_RECEIVE_BIT,
                                 __ATOMIC_ACQ_REL);
    }
    ESP_LOGI(TAG, "ota receive in progress=%d hold=0x%lx",
             in_progress ? 1 : 0,
             (unsigned long)__atomic_load_n(&s_ota_hold_flags, __ATOMIC_ACQUIRE));
}

esp_err_t ServerNetworkStaWifiWorkTime_Init(void)
{
    user_work_state_nvs_blob_t blob = {0};
    size_t stored_size = 0;

    s_wifi_work_start_tick = xTaskGetTickCount();
    s_last_network_data_tick = s_wifi_work_start_tick;
    s_last_power_off_send_tick = 0;
    (void)__atomic_fetch_or(&s_runtime_state_flags,
                            USER_WORK_STATE_RUNTIME_CH583_STARTUP_PENDING_BIT,
                            __ATOMIC_ACQ_REL);
    ESP_LOGI(TAG, "CH583 startup guard active until UART initialization completes");

    esp_err_t ret = load_work_state_from_nvs(&blob, &stored_size);
    if (ret == ESP_OK) {
        log_work_state_blob("read value", &blob);
        apply_work_state_blob(&blob);
    } else {
        sleep_time = 0;
        working_time = 0;
        server_required_continue_work_time = USER_WORK_STATE_DEFAULT_CONTINUE_SECONDS;
        wifi_standby_time_s = USER_WORK_STATE_DEFAULT_STANDBY_SECONDS;
        ESP_LOGW(TAG, "use default work state ret=%s stored_size=%u continue=%lu standby=%lu",
                 esp_err_to_name(ret), (unsigned int)stored_size,
                 (unsigned long)server_required_continue_work_time,
                 (unsigned long)wifi_standby_time_s);
        ret = save_work_state_to_nvs();
    }

    load_work_time_vars_from_app_nvs();
    esp_err_t app_nvs_ret = save_work_time_vars_to_app_nvs();
    if (ret == ESP_OK && app_nvs_ret != ESP_OK) {
        ret = app_nvs_ret;
    }

    if (s_work_state_task == NULL) {
        BaseType_t task_ret = xTaskCreate(work_state_task,
                                          "work_state",
                                          USER_WORK_STATE_TASK_STACK_SIZE,
                                          NULL,
                                          USER_WORK_STATE_TASK_PRIORITY,
                                          &s_work_state_task);
        if (task_ret != pdPASS) {
            ESP_LOGE(TAG, "create work_state task failed");
            return ESP_ERR_NO_MEM;
        }
    }

    return ret;
}

esp_err_t ServerNetworkStaWifiWorkTime_SetAndSave(uint32_t seconds)
{
    if (s_work_state_task == NULL) {
        ESP_LOGE(TAG, "set work time apply failed because work_state task is not ready");
        return ESP_ERR_INVALID_STATE;
    }

    server_required_continue_work_time = clamp_continue_seconds(seconds);
    wifi_standby_time_s = seconds;
    working_time = 0;
    s_wifi_work_start_tick = xTaskGetTickCount();
    s_last_network_data_tick = s_wifi_work_start_tick;
    s_last_power_off_send_tick = 0;
    s_one_shot_power_off_countdown_active = false;
    UserLedStatus_SetPowerOffPending(false);

    ESP_LOGI(TAG, "set work time requested=%lu continue=%lu standby=%lu",
             (unsigned long)seconds,
             (unsigned long)server_required_continue_work_time,
             (unsigned long)wifi_standby_time_s);
    esp_err_t ret = save_work_state_to_nvs();
    esp_err_t app_nvs_ret = save_work_time_vars_to_app_nvs();
    if (ret == ESP_OK && app_nvs_ret != ESP_OK) {
        ret = app_nvs_ret;
    }
    ESP_LOGI(TAG, "set work time save ret=%s app_nvs_ret=%s",
             esp_err_to_name(ret),
             esp_err_to_name(app_nvs_ret));
    return ret;
}

esp_err_t ServerNetworkStaWifiWorkTime_ProcessJson(httpd_req_t *req,
                                                   const char *body,
                                                   size_t body_len)
{
    (void)body_len;
    if (!json_func_equals(body, "set_wifi_work_time")) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    int seconds = 0;
    if (find_json_key(body, "time") != NULL) {
        ESP_LOGW(TAG, "set_wifi_work_time legacy time field is not supported body=%s",
                 body != NULL ? body : "<null>");
        return send_wifi_work_time_result(req,
                                          TDX_JSON_RESULT_PARAM_INVALID,
                                          "time field is not supported");
    }
    if (!parse_json_int(body, "seconds", &seconds)) {
        ESP_LOGW(TAG, "set_wifi_work_time invalid seconds body=%s", body != NULL ? body : "<null>");
        return send_wifi_work_time_result(req,
                                          TDX_JSON_RESULT_WIFI_WORK_TIME_MISSING,
                                          "set wifi work time failed");
    }
    if (seconds < SERVER_NETWORK_STA_WIFI_WORK_TIME_NETWORK_USB_MIN_SECONDS ||
        seconds > SERVER_NETWORK_STA_WIFI_WORK_TIME_MAX_SECONDS) {
        ESP_LOGW(TAG, "set_wifi_work_time seconds out of range seconds=%d body=%s",
                 seconds, body != NULL ? body : "<null>");
        return send_wifi_work_time_result(req,
                                          TDX_JSON_RESULT_WIFI_WORK_TIME_RANGE,
                                          "set wifi work time failed");
    }

    esp_err_t set_ret = ServerNetworkStaWifiWorkTime_SetAndSave((uint32_t)seconds);
    if (set_ret != ESP_OK) {
        ESP_LOGE(TAG, "set_wifi_work_time save failed: %s", esp_err_to_name(set_ret));
        return send_wifi_work_time_result(req,
                                          set_ret == ESP_ERR_INVALID_STATE ?
                                              TDX_JSON_RESULT_WIFI_WORK_TIME_APPLY_FAILED :
                                              TDX_JSON_RESULT_WIFI_WORK_TIME_SAVE_FAILED,
                                          "set wifi work time failed");
    }

    ESP_LOGI(TAG, "set_wifi_work_time updated seconds=%d max=%d working_time=%lu",
             seconds,
             SERVER_NETWORK_STA_WIFI_WORK_TIME_MAX_SECONDS,
             (unsigned long)update_working_time_seconds());
    ESP_LOGI(TAG, "set_wifi_work_time saved, CH583 power-off timeout is enabled");
    return send_wifi_work_time_result(req, TDX_JSON_RESULT_OK, NULL);
}
