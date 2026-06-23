#include "gpio_test.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tdx_cfg.h"

static const char *TAG = "gpio_test";
static TaskHandle_t s_gpio_test_task;

static void gpio_test_task(void *arg)
{
    (void)arg;
    bool level = false;
    const TickType_t half_period_ticks = pdMS_TO_TICKS(USER_GPIO_TEST_PERIOD_MS / 2);

    for (;;) {
        level = !level;
        gpio_set_level(USER_GPIO_TEST_PIN_11, level ? 1 : 0);
        vTaskDelay(half_period_ticks);
    }
}

esp_err_t GpioTest_Init(void)
{
#if USER_GPIO_TEST_OUTPUT_ENABLE
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << USER_GPIO_TEST_PIN_11) |
                        (1ULL << USER_GPIO_TEST_PIN_12),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed ret=%s", esp_err_to_name(ret));
        return ret;
    }

    gpio_set_level(USER_GPIO_TEST_PIN_11, 0);
    gpio_set_level(USER_GPIO_TEST_PIN_12, USER_GPIO_TEST_PIN_12_LEVEL);
    ESP_LOGI(TAG, "GPIO test output pin11=%d toggle_period_ms=%u pin12=%d level=%d",
             USER_GPIO_TEST_PIN_11,
             (unsigned int)USER_GPIO_TEST_PERIOD_MS,
             USER_GPIO_TEST_PIN_12,
             USER_GPIO_TEST_PIN_12_LEVEL);

    if (s_gpio_test_task == NULL) {
        BaseType_t task_ret = xTaskCreate(gpio_test_task,
                                          "gpio_test",
                                          USER_GPIO_TEST_TASK_STACK_SIZE,
                                          NULL,
                                          USER_GPIO_TEST_TASK_PRIORITY,
                                          &s_gpio_test_task);
        if (task_ret != pdPASS) {
            ESP_LOGE(TAG, "create gpio_test task failed");
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
#else
    ESP_LOGI(TAG, "GPIO test output disabled");
    return ESP_OK;
#endif
}
