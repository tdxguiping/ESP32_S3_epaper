#include "led_status.h"

#include "ch583_wifi_uart_protocol.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tdx_cfg.h"

#include <stdbool.h>

static const char *TAG = "led_status";
static TaskHandle_t s_led_task;
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

static user_led_runtime_t s_red_runtime;
static user_led_runtime_t s_green_runtime;

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
    // Send red LED state through CH583 because ESP32-C5 has no local status LED.
    // 通过 CH583 发送红灯状态，因为 ESP32-C5 本机没有状态灯。

    set_ch583_led_level(USER_LED_CH583_RED_PORT,
                        USER_LED_CH583_RED_PIN,
                        on ? USER_LED_CH583_ON_LEVEL : USER_LED_CH583_OFF_LEVEL);
}

static void set_green(bool on)
{
    // Send green LED state through CH583 so existing status states keep the same behavior.
    // 通过 CH583 发送绿灯状态，让现有状态机保持相同表现。

    set_ch583_led_level(USER_LED_CH583_GREEN_PORT,
                        USER_LED_CH583_GREEN_PIN,
                        on ? USER_LED_CH583_ON_LEVEL : USER_LED_CH583_OFF_LEVEL);
}

static void set_all_off(void)
{
    set_red(false);
    set_green(false);
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

static bool set_red_off(void)
{
    if (!stop_blink("RED", &s_red_runtime)) {
        return false;
    }
    if (s_red_runtime.mode == USER_LED_MODE_SOLID) {
        set_red(false);
    }
    s_red_runtime.mode = USER_LED_MODE_OFF;
    s_red_runtime.interval_ms = 0;
    return true;
}

static bool set_green_off(void)
{
    if (!stop_blink("GREEN", &s_green_runtime)) {
        return false;
    }
    if (s_green_runtime.mode == USER_LED_MODE_SOLID) {
        set_green(false);
    }
    s_green_runtime.mode = USER_LED_MODE_OFF;
    s_green_runtime.interval_ms = 0;
    return true;
}

static void set_green_solid(void)
{
    if (!stop_blink("GREEN", &s_green_runtime)) {
        return;
    }
    if (s_green_runtime.mode != USER_LED_MODE_SOLID) {
        set_green(true);
    }
    s_green_runtime.mode = USER_LED_MODE_SOLID;
    s_green_runtime.interval_ms = 0;
}

static void apply_led_state(user_led_state_t state)
{
    ESP_LOGI(TAG, "apply LED state=%d", (int)state);
    switch (state) {
    case USER_LED_STATE_OFF:
        set_red_off();
        set_green_off();
        break;
    case USER_LED_STATE_BOOTING:
        if (set_red_off()) {
            (void)start_blink("GREEN", USER_LED_SLOW_BLINK_MS, &s_green_runtime);
        }
        break;
    case USER_LED_STATE_WIFI_CONNECTING:
        if (set_red_off()) {
            (void)start_blink("GREEN", USER_LED_MID_BLINK_MS, &s_green_runtime);
        }
        break;
    case USER_LED_STATE_SERVER_READY:
        if (set_red_off()) {
            (void)start_blink("GREEN", USER_LED_READY_BLINK_MS, &s_green_runtime);
        }
        break;
    case USER_LED_STATE_TRANSFER:
        if (set_green_off()) {
            (void)start_blink("RED", USER_LED_FAST_BLINK_MS, &s_red_runtime);
        }
        break;
    case USER_LED_STATE_EPD_REFRESH:
    case USER_LED_STATE_WIFI_FAIL:
        if (set_green_off()) {
            (void)start_blink("RED", USER_LED_MID_BLINK_MS, &s_red_runtime);
        }
        break;
    case USER_LED_STATE_OPERATION_FAIL:
        if (set_green_off()) {
            (void)start_blink("RED", USER_LED_FAST_BLINK_MS, &s_red_runtime);
        }
        break;
    case USER_LED_STATE_SUCCESS:
        if (set_red_off()) {
            set_green_solid();
        }
        break;
    default:
        set_red_off();
        set_green_off();
        break;
    }
}

static void UserLedStatus_Task(void *arg)
{
    (void)arg;
    user_led_state_t last_state = (user_led_state_t)-1;

    for (;;) {
        user_led_state_t state = s_led_state;
        if (state != last_state) {
            apply_led_state(state);
            last_state = state;
        }

        if (state == USER_LED_STATE_SUCCESS) {
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(USER_LED_SUCCESS_HOLD_MS)) == 0 &&
                s_led_state == USER_LED_STATE_SUCCESS) {
                s_led_state = USER_LED_STATE_SERVER_READY;
            }
        } else {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
    }
}

esp_err_t UserLedStatus_Init(void)
{
#if USER_LED_STATUS_ENABLE
    // Initialize CH583 LED outputs by sending both LEDs to the configured off level.
    // 初始化 CH583 LED 输出，先把两个灯都设置为配置的关闭电平。
    ESP_LOGI(TAG, "LED backend=CH583 green=%s%d red=%s%d on=%s off=%s",
             USER_LED_CH583_GREEN_PORT,
             USER_LED_CH583_GREEN_PIN,
             USER_LED_CH583_RED_PORT,
             USER_LED_CH583_RED_PIN,
             USER_LED_CH583_ON_LEVEL,
             USER_LED_CH583_OFF_LEVEL);
    (void)ch583_wifi_uart_send_led_blink_stop("RED");
    (void)ch583_wifi_uart_send_led_blink_stop("GREEN");
    set_all_off();
    s_red_runtime = (user_led_runtime_t){.mode = USER_LED_MODE_OFF};
    s_green_runtime = (user_led_runtime_t){.mode = USER_LED_MODE_OFF};
    s_led_state = USER_LED_STATE_BOOTING;
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
    if (s_led_task != NULL) {
        xTaskNotifyGive(s_led_task);
    }
#else
    (void)state;
#endif
}
