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
static SemaphoreHandle_t s_led_control_mutex;
static volatile user_led_state_t s_led_state = USER_LED_STATE_OFF;

typedef enum {
    USER_LED_MODE_OFF = 0,
    USER_LED_MODE_SOLID,
    USER_LED_MODE_BLINK,
} user_led_mode_t;

typedef struct {
    user_led_mode_t mode;
    uint32_t interval_ms;
} user_led_runtime_t;

typedef struct {
    user_led_activity_t source;
    bool begin;
} user_led_activity_event_t;

static user_led_runtime_t s_red_runtime;
static user_led_runtime_t s_green_runtime;
static uint16_t s_activity_count[USER_LED_ACTIVITY_COUNT];
static bool s_red_delay_armed;
static TickType_t s_red_blink_deadline;
static bool s_shutdown_pending;

static void set_ch583_led_level(const char *port, int pin, const char *level)
{
    int ret = ch583_wifi_uart_send_gpio(port, pin, "OUT", level);
    if (ret < 0) {
        ESP_LOGW(TAG, "CH583 LED set failed port=%s pin=%d level=%s ret=%d",
                 port, pin, level, ret);
    }
}

static void set_red(bool on)
{
    set_ch583_led_level(USER_LED_CH583_RED_PORT,
                        USER_LED_CH583_RED_PIN,
                        on ? USER_LED_CH583_ON_LEVEL : USER_LED_CH583_OFF_LEVEL);
}

static void set_green(bool on)
{
    set_ch583_led_level(USER_LED_CH583_GREEN_PORT,
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
        ESP_LOGW(TAG, "CH583 LED blink stop failed led=%s ret=%d", led, ret);
        return false;
    }

    runtime->mode = USER_LED_MODE_OFF;
    runtime->interval_ms = 0;
    return true;
}

static bool start_blink(const char *led, uint32_t interval_ms, user_led_runtime_t *runtime)
{
    if (runtime == NULL) {
        return false;
    }
    if (runtime->mode == USER_LED_MODE_BLINK && runtime->interval_ms == interval_ms) {
        return true;
    }

    int ret = ch583_wifi_uart_send_led_blink(led, interval_ms);
    if (ret < 0) {
        ESP_LOGW(TAG, "CH583 LED blink start failed led=%s interval_ms=%lu ret=%d",
                 led, (unsigned long)interval_ms, ret);
        return false;
    }

    runtime->mode = USER_LED_MODE_BLINK;
    runtime->interval_ms = interval_ms;
    return true;
}

static void set_red_solid(void)
{
    if (!stop_blink("RED", &s_red_runtime)) {
        return;
    }
    if (s_red_runtime.mode != USER_LED_MODE_SOLID) {
        set_red(true);
    }
    s_red_runtime.mode = USER_LED_MODE_SOLID;
    s_red_runtime.interval_ms = 0;
}

static void set_red_off(void)
{
    if (!stop_blink("RED", &s_red_runtime)) {
        return;
    }
    if (s_red_runtime.mode == USER_LED_MODE_SOLID) {
        set_red(false);
    }
    s_red_runtime.mode = USER_LED_MODE_OFF;
    s_red_runtime.interval_ms = 0;
}

static uint32_t activity_total(void)
{
    uint32_t total = 0;
    for (size_t i = 0; i < USER_LED_ACTIVITY_COUNT; i++) {
        total += s_activity_count[i];
    }
    return total;
}

static void handle_activity_event(const user_led_activity_event_t *event)
{
    if (event == NULL || event->source >= USER_LED_ACTIVITY_COUNT || s_shutdown_pending) {
        return;
    }

    uint32_t total_before = activity_total();
    if (event->begin) {
        if (s_activity_count[event->source] < UINT16_MAX) {
            s_activity_count[event->source]++;
        }
        if (total_before == 0 && activity_total() > 0) {
            set_red_solid();
            s_red_delay_armed = true;
            s_red_blink_deadline = xTaskGetTickCount() +
                                   pdMS_TO_TICKS(USER_LED_ACTIVITY_BLINK_DELAY_MS);
        }
        return;
    }

    if (s_activity_count[event->source] > 0) {
        s_activity_count[event->source]--;
    }
    if (activity_total() == 0) {
        s_red_delay_armed = false;
        set_red_off();
    }
}

static TickType_t red_delay_wait_ticks(void)
{
    if (!s_red_delay_armed) {
        return portMAX_DELAY;
    }

    TickType_t now = xTaskGetTickCount();
    int32_t remaining = (int32_t)(s_red_blink_deadline - now);
    return remaining > 0 ? (TickType_t)remaining : 0;
}

static void UserLedStatus_Task(void *arg)
{
    (void)arg;

    for (;;) {
        user_led_activity_event_t event = {0};
        TickType_t wait_ticks;

        xSemaphoreTake(s_led_control_mutex, portMAX_DELAY);
        wait_ticks = red_delay_wait_ticks();
        xSemaphoreGive(s_led_control_mutex);

        if (xQueueReceive(s_led_event_queue, &event, wait_ticks) == pdTRUE) {
            xSemaphoreTake(s_led_control_mutex, portMAX_DELAY);
            handle_activity_event(&event);
            xSemaphoreGive(s_led_control_mutex);
            continue;
        }

        xSemaphoreTake(s_led_control_mutex, portMAX_DELAY);
        if (s_red_delay_armed && !s_shutdown_pending && activity_total() > 0) {
            s_red_delay_armed = false;
            (void)start_blink("RED", USER_LED_FAST_BLINK_MS, &s_red_runtime);
        }
        xSemaphoreGive(s_led_control_mutex);
    }
}

esp_err_t UserLedStatus_Init(void)
{
#if USER_LED_STATUS_ENABLE
    if (s_led_control_mutex == NULL) {
        s_led_control_mutex = xSemaphoreCreateMutex();
        if (s_led_control_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_led_event_queue == NULL) {
        s_led_event_queue = xQueueCreate(USER_LED_ACTIVITY_QUEUE_LENGTH,
                                         sizeof(user_led_activity_event_t));
        if (s_led_event_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(TAG, "LED backend=CH583 work_green_ms=%u red_delay_ms=%u red_blink_ms=%u",
             (unsigned int)USER_LED_WORK_BLINK_MS,
             (unsigned int)USER_LED_ACTIVITY_BLINK_DELAY_MS,
             (unsigned int)USER_LED_FAST_BLINK_MS);

    (void)ch583_wifi_uart_send_led_blink_stop("RED");
    (void)ch583_wifi_uart_send_led_blink_stop("GREEN");
    set_red(false);
    set_green(false);
    s_red_runtime = (user_led_runtime_t){.mode = USER_LED_MODE_OFF};
    s_green_runtime = (user_led_runtime_t){.mode = USER_LED_MODE_OFF};
    memset(s_activity_count, 0, sizeof(s_activity_count));
    s_shutdown_pending = false;
    s_red_delay_armed = false;

    if (!start_blink("GREEN", USER_LED_WORK_BLINK_MS, &s_green_runtime)) {
        ESP_LOGW(TAG, "start GREEN work light failed");
    }

    if (s_led_task == NULL) {
        BaseType_t task_ret = xTaskCreate(UserLedStatus_Task,
                                          "led_status",
                                          USER_LED_STATUS_TASK_STACK_SIZE,
                                          NULL,
                                          USER_LED_STATUS_TASK_PRIORITY,
                                          &s_led_task);
        if (task_ret != pdPASS) {
            ESP_LOGE(TAG, "create LED task failed");
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
#else
    ESP_LOGW(TAG, "LED status disabled by USER_LED_STATUS_ENABLE");
    return ESP_OK;
#endif
}

void UserLedStatus_Set(user_led_state_t state)
{
#if USER_LED_STATUS_ENABLE
    int state_value = (int)state;
    if (state_value < (int)USER_LED_STATE_OFF ||
        state_value > (int)USER_LED_STATE_OPERATION_FAIL ||
        state == s_led_state) {
        return;
    }
    s_led_state = state;
    ESP_LOGI(TAG, "business state=%d (GREEN independent, RED activity-driven)", state_value);
#else
    (void)state;
#endif
}

static void post_activity_event(user_led_activity_t source, bool begin)
{
#if USER_LED_STATUS_ENABLE
    if (source >= USER_LED_ACTIVITY_COUNT || s_led_event_queue == NULL || s_shutdown_pending) {
        return;
    }
    user_led_activity_event_t event = {
        .source = source,
        .begin = begin,
    };
    if (xQueueSend(s_led_event_queue, &event, portMAX_DELAY) != pdTRUE) {
        ESP_LOGW(TAG, "LED activity queue failed source=%d begin=%d", (int)source, begin ? 1 : 0);
    }
#else
    (void)source;
    (void)begin;
#endif
}

void UserLedStatus_ActivityBegin(user_led_activity_t source)
{
    post_activity_event(source, true);
}

void UserLedStatus_ActivityEnd(user_led_activity_t source)
{
    post_activity_event(source, false);
}

void UserLedStatus_PreparePowerOff(void)
{
#if USER_LED_STATUS_ENABLE
    if (s_led_control_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_led_control_mutex, portMAX_DELAY);
    s_shutdown_pending = true;
    s_red_delay_armed = false;
    memset(s_activity_count, 0, sizeof(s_activity_count));

    // Always send the physical stop/off commands before every POWER_OFF attempt.
    (void)ch583_wifi_uart_send_led_blink_stop("RED");
    vTaskDelay(pdMS_TO_TICKS(100));
    set_red(false);
    vTaskDelay(pdMS_TO_TICKS(100));
    s_red_runtime = (user_led_runtime_t){.mode = USER_LED_MODE_OFF};
    (void)ch583_wifi_uart_send_led_blink_stop("GREEN");
    vTaskDelay(pdMS_TO_TICKS(100));
    set_green(false);
    s_green_runtime = (user_led_runtime_t){.mode = USER_LED_MODE_OFF};
    xSemaphoreGive(s_led_control_mutex);
#endif
}
