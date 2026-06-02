#include "led_status.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tdx_cfg.h"

static const char *TAG = "led_status";
static TaskHandle_t s_led_task;
static volatile user_led_state_t s_led_state = USER_LED_STATE_OFF;

static void set_led_level(gpio_num_t pin, int level)
{
    esp_err_t ret = gpio_set_level(pin, level);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "gpio_set_level pin=%d level=%d failed: %s",
                 (int)pin, level, esp_err_to_name(ret));
    }
}

static void set_red(bool on)
{
    set_led_level(USER_LED_RED_PIN, on ? USER_LED_ON_LEVEL : USER_LED_OFF_LEVEL);
}

static void set_green(bool on)
{
    set_led_level(USER_LED_GREEN_PIN, on ? USER_LED_ON_LEVEL : USER_LED_OFF_LEVEL);
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
            set_red(false);
            set_green(true);
            vTaskDelay(pdMS_TO_TICKS(500));
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
    gpio_config_t gpio_conf = {
        .pin_bit_mask = (1ULL << USER_LED_GREEN_PIN) | (1ULL << USER_LED_RED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&gpio_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED gpio_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

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
