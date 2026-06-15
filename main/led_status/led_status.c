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

static void blink_red(TickType_t delay_ticks)
{
    set_green(false);
    set_red(true);
    vTaskDelay(delay_ticks);
    set_red(false);
    vTaskDelay(delay_ticks);
}

static void blink_green(TickType_t delay_ticks)
{
    set_red(false);
    set_green(true);
    vTaskDelay(delay_ticks);
    set_green(false);
    vTaskDelay(delay_ticks);
}

static void UserLedStatus_Task(void *arg)
{
    (void)arg;
    user_led_state_t last_state = USER_LED_STATE_OFF;

    for (;;) {
        user_led_state_t state = s_led_state;
        if (state != last_state) {
            last_state = state;
        }

        switch (state) {
        case USER_LED_STATE_OFF:
            set_all_off();
            vTaskDelay(pdMS_TO_TICKS(500));
            break;
        case USER_LED_STATE_BOOTING:
            blink_green(pdMS_TO_TICKS(USER_LED_SLOW_BLINK_MS));
            break;
        case USER_LED_STATE_WIFI_CONNECTING:
            blink_green(pdMS_TO_TICKS(USER_LED_MID_BLINK_MS));
            break;
        case USER_LED_STATE_SERVER_READY:
            blink_green(pdMS_TO_TICKS(USER_LED_READY_BLINK_MS));
            break;
            case USER_LED_STATE_TRANSFER:
                /* Blink red while receiving upload data. */
                /* 上传接收过程中闪红灯。 */
                blink_red(pdMS_TO_TICKS(USER_LED_FAST_BLINK_MS));
                break;

            case USER_LED_STATE_EPD_REFRESH:
                /* Blink red while refreshing the e-paper display. */
                /* 墨水屏刷新过程中闪红灯。 */
                blink_red(pdMS_TO_TICKS(USER_LED_MID_BLINK_MS));
                break;
            case USER_LED_STATE_SUCCESS:
            set_red(false);
            set_green(true);
            vTaskDelay(pdMS_TO_TICKS(USER_LED_SUCCESS_HOLD_MS));
            s_led_state = USER_LED_STATE_SERVER_READY;
            break;
        case USER_LED_STATE_WIFI_FAIL:
            blink_red(pdMS_TO_TICKS(USER_LED_MID_BLINK_MS));
            break;
        case USER_LED_STATE_OPERATION_FAIL:
            blink_red(pdMS_TO_TICKS(USER_LED_FAST_BLINK_MS));
            break;
        default:
            set_all_off();
            vTaskDelay(pdMS_TO_TICKS(500));
            break;
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
    set_all_off();
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
    s_led_state = state;
#else
    (void)state;
#endif
}
