#include "led_status.h"

#include "ch583_wifi_uart_protocol.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tdx_cfg.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "led_status";
static TaskHandle_t s_led_task;
static QueueHandle_t s_led_event_queue;
static SemaphoreHandle_t s_power_off_done;
static volatile esp_err_t s_power_off_result = ESP_FAIL;

typedef struct {
    uint8_t mode;
    uint32_t interval_ms;
} user_led_runtime_t;

typedef struct {
    uint8_t type;
    uint8_t source;
    uint32_t value;
} user_led_event_t;

typedef struct {
    uint8_t green_mode;
    uint32_t green_interval_ms;
    uint8_t red_mode;
    uint32_t red_interval_ms;
    const char *reason;
} user_led_effect_t;

static user_led_state_t s_base_state = USER_LED_STATE_BOOTING;
static user_led_runtime_t s_red_runtime;
static user_led_runtime_t s_green_runtime;
static uint16_t s_activity_count[USER_LED_ACTIVITY_COUNT];
static uint32_t s_fault_flags;
static bool s_activity_delay_armed;
static TickType_t s_activity_blink_deadline;
static bool s_success_active;
static bool s_operation_fail_active;
static TickType_t s_temporary_result_deadline;
static bool s_apply_retry_armed;
static bool s_apply_retry_exhausted_logged;
static uint8_t s_apply_retry_count;
static TickType_t s_apply_retry_deadline;
static bool s_ota_active;
static bool s_factory_reset_active;
static bool s_restart_pending;
static bool s_power_off_pending;
static bool s_shutdown_pending;

static bool set_ch583_led_level(const char *port, int pin, const char *level)
{
    int ret = ch583_wifi_uart_send_gpio(port, pin, "OUT", level);
    if (ret < 0) {
        ESP_LOGE(TAG, "CH583 LED set failed port=%s pin=%d level=%s ret=%d",
                 port, pin, level, ret);
        return false;
    }
    return true;
}

static bool set_red(bool on)
{
    return set_ch583_led_level(USER_LED_CH583_RED_PORT,
                               USER_LED_CH583_RED_PIN,
                               on ? USER_LED_CH583_ON_LEVEL : USER_LED_CH583_OFF_LEVEL);
}

static bool set_green(bool on)
{
    return set_ch583_led_level(USER_LED_CH583_GREEN_PORT,
                               USER_LED_CH583_GREEN_PIN,
                               on ? USER_LED_CH583_ON_LEVEL : USER_LED_CH583_OFF_LEVEL);
}

static bool stop_blink(const char *led, user_led_runtime_t *runtime)
{
    if (runtime == NULL || runtime->mode != USER_LED_MODE_BLINK) {
        return true;
    }

    int ret = ch583_wifi_uart_send_led_blink_stop(led);
    if (ret < 0) {
        ESP_LOGE(TAG, "CH583 LED blink stop failed led=%s ret=%d", led, ret);
        return false;
    }

    runtime->mode = USER_LED_MODE_OFF;
    runtime->interval_ms = 0;
    return true;
}

static bool start_blink(const char *led, uint32_t interval_ms, user_led_runtime_t *runtime)
{
    if (runtime == NULL || interval_ms == 0) {
        return false;
    }
    if (runtime->mode == USER_LED_MODE_BLINK && runtime->interval_ms == interval_ms) {
        return true;
    }

    int ret = ch583_wifi_uart_send_led_blink(led, interval_ms);
    if (ret < 0) {
        ESP_LOGE(TAG, "CH583 LED blink start failed led=%s interval_ms=%lu ret=%d",
                 led, (unsigned long)interval_ms, ret);
        return false;
    }

    runtime->mode = USER_LED_MODE_BLINK;
    runtime->interval_ms = interval_ms;
    return true;
}

static bool apply_one_led(const char *led,
                          bool is_red,
                          uint8_t target_mode,
                          uint32_t interval_ms,
                          user_led_runtime_t *runtime)
{
    if (runtime == NULL) {
        return false;
    }
    if (target_mode == USER_LED_MODE_BLINK) {
        return start_blink(led, interval_ms, runtime);
    }
    if (runtime->mode == USER_LED_MODE_UNKNOWN) {
        int ret = ch583_wifi_uart_send_led_blink_stop(led);
        if (ret < 0) {
            ESP_LOGE(TAG, "CH583 LED unknown-state stop failed led=%s ret=%d", led, ret);
            return false;
        }
        runtime->mode = USER_LED_MODE_OFF;
        runtime->interval_ms = 0;
    }
    if (runtime->mode == USER_LED_MODE_BLINK && !stop_blink(led, runtime)) {
        return false;
    }
    if (runtime->mode == target_mode) {
        return true;
    }

    bool ok = is_red ? set_red(target_mode == USER_LED_MODE_SOLID)
                     : set_green(target_mode == USER_LED_MODE_SOLID);
    if (ok) {
        runtime->mode = target_mode;
        runtime->interval_ms = 0;
    }
    return ok;
}

static uint32_t normal_activity_total(void)
{
    return (uint32_t)s_activity_count[USER_LED_ACTIVITY_NETWORK] +
           (uint32_t)s_activity_count[USER_LED_ACTIVITY_UART_RX] +
           (uint32_t)s_activity_count[USER_LED_ACTIVITY_UART_TX];
}

static void select_base_green(user_led_effect_t *effect)
{
    effect->green_mode = USER_LED_MODE_OFF;
    effect->green_interval_ms = 0;
    switch (s_base_state) {
    case USER_LED_STATE_BOOTING:
        effect->green_mode = USER_LED_MODE_SOLID;
        break;
    case USER_LED_STATE_WIFI_CONNECTING:
        effect->green_mode = USER_LED_MODE_BLINK;
        effect->green_interval_ms = USER_LED_WIFI_CONNECT_BLINK_MS;
        break;
    case USER_LED_STATE_SERVER_READY:
        effect->green_mode = USER_LED_MODE_BLINK;
        effect->green_interval_ms = USER_LED_READY_BLINK_MS;
        break;
    default:
        break;
    }
}

static user_led_effect_t select_effect(void)
{
    user_led_effect_t effect = {
        .green_mode = USER_LED_MODE_OFF,
        .red_mode = USER_LED_MODE_OFF,
        .reason = "off",
    };

    if (s_shutdown_pending) {
        effect.reason = "power_off";
        return effect;
    }
    if (s_power_off_pending) {
        effect.green_mode = USER_LED_MODE_SOLID;
        effect.reason = "power_off_countdown";
        return effect;
    }
    if (s_restart_pending || (s_fault_flags & USER_LED_FAULT_FATAL) != 0) {
        effect.red_mode = USER_LED_MODE_SOLID;
        effect.reason = s_restart_pending ? "restart_pending" : "fatal";
        return effect;
    }
    if (s_ota_active) {
        effect.green_mode = USER_LED_MODE_SOLID;
        effect.red_mode = USER_LED_MODE_BLINK;
        effect.red_interval_ms = USER_LED_MID_BLINK_MS;
        effect.reason = "ota";
        return effect;
    }
    if (s_factory_reset_active) {
        effect.green_mode = USER_LED_MODE_BLINK;
        effect.green_interval_ms = USER_LED_FAST_BLINK_MS;
        effect.red_mode = USER_LED_MODE_SOLID;
        effect.reason = "factory_reset";
        return effect;
    }
    if ((s_fault_flags & (USER_LED_FAULT_WIFI_NO_CONFIG | USER_LED_FAULT_WIFI_AUTH)) != 0) {
        effect.red_mode = USER_LED_MODE_BLINK;
        effect.red_interval_ms = USER_LED_WIFI_ERROR_BLINK_MS;
        effect.reason = "wifi_fault";
        return effect;
    }
    if ((s_fault_flags & (USER_LED_FAULT_HTTP_SERVICE | USER_LED_FAULT_STORAGE)) != 0) {
        effect.red_mode = USER_LED_MODE_BLINK;
        effect.red_interval_ms = USER_LED_SERVICE_ERROR_BLINK_MS;
        effect.reason = "service_fault";
        return effect;
    }
    select_base_green(&effect);
    if (s_activity_count[USER_LED_ACTIVITY_EPD] > 0) {
        effect.red_mode = USER_LED_MODE_BLINK;
        effect.red_interval_ms = USER_LED_EPD_ACTIVITY_BLINK_MS;
        effect.reason = "epd";
        return effect;
    }
    if (normal_activity_total() > 0) {
        effect.red_mode = s_activity_delay_armed ? USER_LED_MODE_SOLID : USER_LED_MODE_BLINK;
        effect.red_interval_ms = s_activity_delay_armed ? 0 : USER_LED_NETWORK_ACTIVITY_BLINK_MS;
        effect.reason = s_activity_delay_armed ? "short_activity" : "data_activity";
        return effect;
    }
    if (s_success_active) {
        effect.green_mode = USER_LED_MODE_SOLID;
        effect.red_mode = USER_LED_MODE_OFF;
        effect.reason = "success";
        return effect;
    }
    if (s_operation_fail_active) {
        effect.red_mode = USER_LED_MODE_SOLID;
        effect.reason = "operation_fail";
        return effect;
    }

    effect.reason = "base";
    return effect;
}

static bool apply_effect(void)
{
    user_led_effect_t effect = select_effect();
    bool green_changed = s_green_runtime.mode != effect.green_mode ||
                         (effect.green_mode == USER_LED_MODE_BLINK &&
                          s_green_runtime.interval_ms != effect.green_interval_ms);
    bool red_changed = s_red_runtime.mode != effect.red_mode ||
                       (effect.red_mode == USER_LED_MODE_BLINK &&
                        s_red_runtime.interval_ms != effect.red_interval_ms);

    bool green_ok = !green_changed ||
                    apply_one_led("GREEN", false, effect.green_mode,
                                  effect.green_interval_ms, &s_green_runtime);
    bool red_ok = !red_changed ||
                  apply_one_led("RED", true, effect.red_mode,
                                effect.red_interval_ms, &s_red_runtime);
    bool applied = green_ok && red_ok;
    if ((green_changed || red_changed) && applied) {
        ESP_LOGI(TAG,
                 "effect applied reason=%s green_mode=%u green_ms=%lu red_mode=%u red_ms=%lu",
                 effect.reason,
                 (unsigned int)effect.green_mode,
                 (unsigned long)effect.green_interval_ms,
                 (unsigned int)effect.red_mode,
                 (unsigned long)effect.red_interval_ms);
    }
    if (applied) {
        s_apply_retry_armed = false;
        s_apply_retry_count = 0;
        s_apply_retry_exhausted_logged = false;
        return true;
    }

    if (s_apply_retry_count < USER_LED_APPLY_RETRY_MAX) {
        s_apply_retry_count++;
        s_apply_retry_armed = true;
        s_apply_retry_deadline = xTaskGetTickCount() +
                                 pdMS_TO_TICKS(USER_LED_APPLY_RETRY_MS);
    } else if (!s_apply_retry_exhausted_logged) {
        s_apply_retry_exhausted_logged = true;
        ESP_LOGW(TAG, "LED effect retries exhausted reason=%s retries=%u",
                 effect.reason, (unsigned int)s_apply_retry_count);
    }
    return false;
}

static bool deadline_reached(TickType_t now, TickType_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static TickType_t deadline_wait(TickType_t now, TickType_t deadline)
{
    int32_t remaining = (int32_t)(deadline - now);
    return remaining > 0 ? (TickType_t)remaining : 0;
}

static TickType_t next_wait_ticks(void)
{
    TickType_t now = xTaskGetTickCount();
    TickType_t wait_ticks = portMAX_DELAY;
    if (s_activity_delay_armed) {
        wait_ticks = deadline_wait(now, s_activity_blink_deadline);
    }
    if (s_success_active || s_operation_fail_active) {
        TickType_t result_wait = deadline_wait(now, s_temporary_result_deadline);
        if (wait_ticks == portMAX_DELAY || result_wait < wait_ticks) {
            wait_ticks = result_wait;
        }
    }
    if (s_apply_retry_armed) {
        TickType_t retry_wait = deadline_wait(now, s_apply_retry_deadline);
        if (wait_ticks == portMAX_DELAY || retry_wait < wait_ticks) {
            wait_ticks = retry_wait;
        }
    }
    return wait_ticks;
}

static void expire_timers(void)
{
    TickType_t now = xTaskGetTickCount();
    if (s_activity_delay_armed && deadline_reached(now, s_activity_blink_deadline)) {
        s_activity_delay_armed = false;
    }
    if ((s_success_active || s_operation_fail_active) &&
        deadline_reached(now, s_temporary_result_deadline)) {
        s_success_active = false;
        s_operation_fail_active = false;
        ESP_LOGI(TAG, "temporary result indication expired");
    }
    if (s_apply_retry_armed && deadline_reached(now, s_apply_retry_deadline)) {
        s_apply_retry_armed = false;
    }
}

static void set_fault(uint32_t fault_mask, bool active)
{
    if (active) {
        s_fault_flags |= fault_mask;
    } else {
        s_fault_flags &= ~fault_mask;
    }
}

static void handle_state(user_led_state_t state)
{
    bool changed = true;
    switch (state) {
    case USER_LED_STATE_OFF:
    case USER_LED_STATE_BOOTING:
        changed = s_base_state != state;
        s_base_state = state;
        break;
    case USER_LED_STATE_WIFI_CONNECTING:
        changed = s_base_state != state;
        s_base_state = state;
        set_fault(USER_LED_FAULT_WIFI_NO_CONFIG |
                  USER_LED_FAULT_WIFI_AUTH |
                  USER_LED_FAULT_HTTP_SERVICE, false);
        break;
    case USER_LED_STATE_SERVER_READY:
        changed = s_base_state != state;
        s_base_state = state;
        set_fault(USER_LED_FAULT_WIFI_NO_CONFIG |
                  USER_LED_FAULT_WIFI_AUTH |
                  USER_LED_FAULT_HTTP_SERVICE, false);
        break;
    case USER_LED_STATE_EPD_REFRESH:
    case USER_LED_STATE_EPD_FINISHED:
        ESP_LOGW(TAG, "EPD state requires ActivityBegin/ActivityEnd source tracking");
        return;
    case USER_LED_STATE_SUCCESS:
        s_success_active = true;
        s_operation_fail_active = false;
        s_temporary_result_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(USER_LED_SUCCESS_HOLD_MS);
        break;
    case USER_LED_STATE_WIFI_FAIL:
        changed = (s_fault_flags & USER_LED_FAULT_WIFI_AUTH) == 0;
        set_fault(USER_LED_FAULT_WIFI_AUTH, true);
        break;
    case USER_LED_STATE_OPERATION_FAIL:
        s_success_active = false;
        s_operation_fail_active = true;
        s_temporary_result_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(USER_LED_OPERATION_FAIL_HOLD_MS);
        break;
    case USER_LED_STATE_TRANSFER:
        ESP_LOGW(TAG, "TRANSFER state requires ActivityBegin/ActivityEnd source tracking");
        return;
    default:
        ESP_LOGW(TAG, "invalid business state=%d", (int)state);
        return;
    }
    if (changed) {
        ESP_LOGI(TAG, "business state=%d", (int)state);
    }
}

static void handle_activity(uint8_t source, bool begin)
{
    if (source >= USER_LED_ACTIVITY_COUNT) {
        ESP_LOGW(TAG, "invalid activity source=%u", (unsigned int)source);
        return;
    }

    uint32_t normal_before = normal_activity_total();
    if (begin) {
        if (s_activity_count[source] == UINT16_MAX) {
            ESP_LOGW(TAG, "activity counter saturated source=%u", (unsigned int)source);
            return;
        }
        s_activity_count[source]++;
        if (source != USER_LED_ACTIVITY_EPD && normal_before == 0) {
            s_activity_delay_armed = true;
            s_activity_blink_deadline = xTaskGetTickCount() +
                                        pdMS_TO_TICKS(USER_LED_ACTIVITY_BLINK_DELAY_MS);
        }
        return;
    }

    if (s_activity_count[source] == 0) {
        ESP_LOGW(TAG, "activity end without begin source=%u", (unsigned int)source);
        return;
    }
    s_activity_count[source]--;
    if (source != USER_LED_ACTIVITY_EPD && normal_activity_total() == 0) {
        s_activity_delay_armed = false;
    }
}

static bool force_all_leds_off(void)
{
    int ret = ch583_wifi_uart_send_led_blink_stop("RED");
    bool red_stop_ok = ret >= 0;
    if (!red_stop_ok) {
        ESP_LOGE(TAG, "force RED blink stop failed ret=%d", ret);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    bool red_gpio_ok = set_red(false);
    vTaskDelay(pdMS_TO_TICKS(100));
    ret = ch583_wifi_uart_send_led_blink_stop("GREEN");
    bool green_stop_ok = ret >= 0;
    if (!green_stop_ok) {
        ESP_LOGE(TAG, "force GREEN blink stop failed ret=%d", ret);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    bool green_gpio_ok = set_green(false);

    if (red_stop_ok && red_gpio_ok) {
        s_red_runtime = (user_led_runtime_t){.mode = USER_LED_MODE_OFF};
    }
    if (green_stop_ok && green_gpio_ok) {
        s_green_runtime = (user_led_runtime_t){.mode = USER_LED_MODE_OFF};
    }
    return red_stop_ok && red_gpio_ok && green_stop_ok && green_gpio_ok;
}

static void handle_event(const user_led_event_t *event)
{
    if (event == NULL) {
        return;
    }
    if (event->type == USER_LED_EVENT_CANCEL_POWER_OFF) {
        bool lock_was_active = s_shutdown_pending;
        s_shutdown_pending = false;
        s_power_off_pending = false;
        bool restored = apply_effect();
        s_power_off_result = restored ? ESP_OK : ESP_FAIL;
        if (lock_was_active) {
            if (restored) {
                ESP_LOGI(TAG, "power-off lock canceled, base LED effect restored");
            } else {
                ESP_LOGE(TAG, "power-off lock canceled but base LED effect restore failed");
            }
        }
        if (s_power_off_done != NULL) {
            if (xSemaphoreGive(s_power_off_done) != pdTRUE) {
                ESP_LOGW(TAG, "power-off cancel result semaphore already full");
            }
        }
        return;
    }
    if (event->type == USER_LED_EVENT_PREPARE_POWER_OFF) {
        bool all_off = force_all_leds_off();
        s_power_off_result = all_off ? ESP_OK : ESP_FAIL;
        if (all_off) {
            s_shutdown_pending = true;
            s_power_off_pending = false;
            s_activity_delay_armed = false;
            s_success_active = false;
            s_operation_fail_active = false;
            memset(s_activity_count, 0, sizeof(s_activity_count));
            ESP_LOGI(TAG, "power-off lock active, both LED commands completed");
        } else {
            ESP_LOGE(TAG, "power-off LED shutdown failed, keep system state active");
        }
        if (s_power_off_done != NULL) {
            if (xSemaphoreGive(s_power_off_done) != pdTRUE) {
                ESP_LOGW(TAG, "power-off result semaphore already full");
            }
        }
        return;
    }
    if (s_shutdown_pending) {
        return;
    }

    switch (event->type) {
    case USER_LED_EVENT_SET_STATE:
        handle_state((user_led_state_t)event->value);
        break;
    case USER_LED_EVENT_ACTIVITY_BEGIN:
        handle_activity(event->source, true);
        break;
    case USER_LED_EVENT_ACTIVITY_END:
        handle_activity(event->source, false);
        break;
    case USER_LED_EVENT_SET_FAULT:
        set_fault(event->value, true);
        break;
    case USER_LED_EVENT_CLEAR_FAULT:
        set_fault(event->value, false);
        break;
    case USER_LED_EVENT_SHOW_SUCCESS:
        handle_state(USER_LED_STATE_SUCCESS);
        break;
    case USER_LED_EVENT_SHOW_OPERATION_FAIL:
        handle_state(USER_LED_STATE_OPERATION_FAIL);
        break;
    case USER_LED_EVENT_OTA_BEGIN:
        s_ota_active = true;
        break;
    case USER_LED_EVENT_OTA_END:
        s_ota_active = false;
        if (event->value != 0) {
            s_restart_pending = true;
        }
        break;
    case USER_LED_EVENT_FACTORY_RESET_BEGIN:
        s_factory_reset_active = true;
        break;
    case USER_LED_EVENT_FACTORY_RESET_END:
        s_factory_reset_active = false;
        break;
    case USER_LED_EVENT_POWER_OFF_PENDING:
        s_power_off_pending = event->value != 0;
        break;
    case USER_LED_EVENT_RESTART_PENDING:
        s_restart_pending = event->value != 0;
        break;
    default:
        ESP_LOGW(TAG, "unknown LED event type=%u", (unsigned int)event->type);
        break;
    }
}

static void UserLedStatus_Task(void *arg)
{
    (void)arg;
    if (!force_all_leds_off()) {
        ESP_LOGW(TAG, "initial LED shutdown incomplete, apply target with bounded retry");
    }
    (void)apply_effect();

    for (;;) {
        user_led_event_t event = {0};
        TickType_t wait_ticks = next_wait_ticks();
        if (xQueueReceive(s_led_event_queue, &event, wait_ticks) == pdTRUE) {
            s_apply_retry_armed = false;
            s_apply_retry_count = 0;
            s_apply_retry_exhausted_logged = false;
            handle_event(&event);
        } else {
            expire_timers();
        }
        if (!s_shutdown_pending) {
            expire_timers();
            (void)apply_effect();
        }
    }
}

static bool post_event_wait(const user_led_event_t *event,
                            bool high_priority,
                            TickType_t wait_ticks)
{
#if USER_LED_STATUS_ENABLE
    if (event == NULL || s_led_event_queue == NULL || s_led_task == NULL) {
        ESP_LOGW(TAG, "LED event rejected before initialization");
        return false;
    }
    BaseType_t result = high_priority
        ? xQueueSendToFront(s_led_event_queue, event, wait_ticks)
        : xQueueSend(s_led_event_queue, event, wait_ticks);
    if (result != pdTRUE) {
        ESP_LOGW(TAG, "LED event queue full type=%u source=%u",
                 (unsigned int)event->type, (unsigned int)event->source);
        return false;
    }
    return true;
#else
    (void)event;
    (void)high_priority;
    (void)wait_ticks;
    return false;
#endif
}

static bool post_event(const user_led_event_t *event, bool high_priority)
{
    TickType_t wait_ticks = pdMS_TO_TICKS(high_priority
        ? USER_LED_POWER_OFF_EVENT_POST_WAIT_MS
        : USER_LED_EVENT_POST_WAIT_MS);
    return post_event_wait(event, high_priority, wait_ticks);
}

static void post_simple_event(uint8_t type, uint8_t source, uint32_t value)
{
    user_led_event_t event = {
        .type = type,
        .source = source,
        .value = value,
    };
    (void)post_event(&event, false);
}

esp_err_t UserLedStatus_Init(void)
{
#if USER_LED_STATUS_ENABLE
    if (s_led_task != NULL) {
        return ESP_OK;
    }
    if (s_power_off_done == NULL) {
        s_power_off_done = xSemaphoreCreateBinary();
        if (s_power_off_done == NULL) {
            ESP_LOGE(TAG, "create LED power-off semaphore failed");
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_led_event_queue == NULL) {
        s_led_event_queue = xQueueCreate(USER_LED_EVENT_QUEUE_LENGTH,
                                         sizeof(user_led_event_t));
        if (s_led_event_queue == NULL) {
            ESP_LOGE(TAG, "create LED event queue failed");
            vSemaphoreDelete(s_power_off_done);
            s_power_off_done = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    s_base_state = USER_LED_STATE_BOOTING;
    s_fault_flags = 0;
    s_activity_delay_armed = false;
    s_success_active = false;
    s_operation_fail_active = false;
    s_apply_retry_armed = false;
    s_apply_retry_exhausted_logged = false;
    s_apply_retry_count = 0;
    s_ota_active = false;
    s_factory_reset_active = false;
    s_restart_pending = false;
    s_power_off_pending = false;
    s_shutdown_pending = false;
    memset(s_activity_count, 0, sizeof(s_activity_count));
    s_red_runtime = (user_led_runtime_t){.mode = USER_LED_MODE_UNKNOWN};
    s_green_runtime = (user_led_runtime_t){.mode = USER_LED_MODE_UNKNOWN};

    BaseType_t task_ret = xTaskCreate(UserLedStatus_Task,
                                      "led_status",
                                      USER_LED_STATUS_TASK_STACK_SIZE,
                                      NULL,
                                      USER_LED_STATUS_TASK_PRIORITY,
                                      &s_led_task);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "create LED task failed");
        vQueueDelete(s_led_event_queue);
        s_led_event_queue = NULL;
        vSemaphoreDelete(s_power_off_done);
        s_power_off_done = NULL;
        s_led_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG,
             "single LED task started ready_ms=%u wifi_ms=%u activity_ms=%u epd_ms=%u",
             (unsigned int)USER_LED_READY_BLINK_MS,
             (unsigned int)USER_LED_WIFI_CONNECT_BLINK_MS,
             (unsigned int)USER_LED_NETWORK_ACTIVITY_BLINK_MS,
             (unsigned int)USER_LED_EPD_ACTIVITY_BLINK_MS);
    return ESP_OK;
#else
    ESP_LOGW(TAG, "LED status disabled by USER_LED_STATUS_ENABLE");
    return ESP_OK;
#endif
}

void UserLedStatus_Set(user_led_state_t state)
{
    post_simple_event(USER_LED_EVENT_SET_STATE, 0, (uint32_t)state);
}

void UserLedStatus_ActivityBegin(user_led_activity_t source)
{
    user_led_event_t event = {
        .type = USER_LED_EVENT_ACTIVITY_BEGIN,
        .source = (uint8_t)source,
    };
    (void)post_event_wait(&event, false, portMAX_DELAY);
}

void UserLedStatus_ActivityEnd(user_led_activity_t source)
{
    user_led_event_t event = {
        .type = USER_LED_EVENT_ACTIVITY_END,
        .source = (uint8_t)source,
    };
    (void)post_event_wait(&event, false, portMAX_DELAY);
}

static void post_fault(uint32_t fault, bool active)
{
    post_simple_event(active ? USER_LED_EVENT_SET_FAULT : USER_LED_EVENT_CLEAR_FAULT,
                      0, fault);
}

void UserLedStatus_SetWifiNoConfig(bool active)
{
    post_fault(USER_LED_FAULT_WIFI_NO_CONFIG, active);
}

void UserLedStatus_SetWifiAuthFailed(bool active)
{
    post_fault(USER_LED_FAULT_WIFI_AUTH, active);
}

void UserLedStatus_SetHttpFailed(bool active)
{
    post_fault(USER_LED_FAULT_HTTP_SERVICE, active);
}

void UserLedStatus_SetStorageFailed(bool active)
{
    post_fault(USER_LED_FAULT_STORAGE, active);
}

void UserLedStatus_SetFatalError(bool active)
{
    post_fault(USER_LED_FAULT_FATAL, active);
}

void UserLedStatus_ShowSuccess(void)
{
    post_simple_event(USER_LED_EVENT_SHOW_SUCCESS, 0, 0);
}

void UserLedStatus_ShowOperationFail(void)
{
    post_simple_event(USER_LED_EVENT_SHOW_OPERATION_FAIL, 0, 0);
}

void UserLedStatus_OtaBegin(void)
{
    post_simple_event(USER_LED_EVENT_OTA_BEGIN, 0, 0);
}

void UserLedStatus_OtaEnd(bool preparing_restart)
{
    post_simple_event(USER_LED_EVENT_OTA_END, 0, preparing_restart ? 1U : 0U);
}

void UserLedStatus_FactoryResetBegin(void)
{
    post_simple_event(USER_LED_EVENT_FACTORY_RESET_BEGIN, 0, 0);
}

void UserLedStatus_FactoryResetEnd(void)
{
    post_simple_event(USER_LED_EVENT_FACTORY_RESET_END, 0, 0);
}

void UserLedStatus_SetRestartPending(bool active)
{
    post_simple_event(USER_LED_EVENT_RESTART_PENDING, 0, active ? 1U : 0U);
}

void UserLedStatus_SetPowerOffPending(bool active)
{
    post_simple_event(USER_LED_EVENT_POWER_OFF_PENDING, 0, active ? 1U : 0U);
}

esp_err_t UserLedStatus_PreparePowerOffSync(void)
{
#if USER_LED_STATUS_ENABLE
    if (s_led_event_queue == NULL || s_led_task == NULL || s_power_off_done == NULL) {
        ESP_LOGE(TAG, "prepare power off failed because LED task is not ready");
        return ESP_ERR_INVALID_STATE;
    }

    (void)xSemaphoreTake(s_power_off_done, 0);
    s_power_off_result = ESP_FAIL;
    user_led_event_t event = {
        .type = USER_LED_EVENT_PREPARE_POWER_OFF,
    };
    if (!post_event(&event, true)) {
        ESP_LOGE(TAG, "prepare power off event post failed");
        return ESP_ERR_TIMEOUT;
    }
    if (xSemaphoreTake(s_power_off_done,
                       pdMS_TO_TICKS(USER_LED_POWER_OFF_ACK_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "prepare power off LED acknowledgement timeout");
        return ESP_ERR_TIMEOUT;
    }
    if (s_power_off_result != ESP_OK) {
        ESP_LOGE(TAG, "prepare power off LED shutdown failed ret=%s",
                 esp_err_to_name(s_power_off_result));
    }
    return s_power_off_result;
#else
    return ESP_OK;
#endif
}

esp_err_t UserLedStatus_CancelPowerOffSync(void)
{
#if USER_LED_STATUS_ENABLE
    if (s_led_event_queue == NULL || s_led_task == NULL || s_power_off_done == NULL) {
        ESP_LOGE(TAG, "cancel power off failed because LED task is not ready");
        return ESP_ERR_INVALID_STATE;
    }

    (void)xSemaphoreTake(s_power_off_done, 0);
    s_power_off_result = ESP_FAIL;
    user_led_event_t event = {
        .type = USER_LED_EVENT_CANCEL_POWER_OFF,
    };
    if (!post_event(&event, true)) {
        ESP_LOGE(TAG, "cancel power off event post failed");
        return ESP_ERR_TIMEOUT;
    }
    if (xSemaphoreTake(s_power_off_done,
                       pdMS_TO_TICKS(USER_LED_POWER_OFF_ACK_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "cancel power off LED acknowledgement timeout");
        return ESP_ERR_TIMEOUT;
    }
    if (s_power_off_result != ESP_OK) {
        ESP_LOGE(TAG, "cancel power off LED restore failed ret=%s",
                 esp_err_to_name(s_power_off_result));
    }
    return s_power_off_result;
#else
    return ESP_OK;
#endif
}

void UserLedStatus_PreparePowerOff(void)
{
    (void)UserLedStatus_PreparePowerOffSync();
}
