#include "ch583_uart_app.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ble_data_handler.h"
#include "ch583_wifi_uart_protocol.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "server_network_sta_wifi_work_time.h"
#include "tdx_cfg.h"

static const char *TAG = "ch583_uart";
static QueueHandle_t s_ch583_uart_event_queue;
static bool s_ch583_uart_started;
static QueueHandle_t s_ble_data_business_queue;
static TaskHandle_t s_ble_data_business_task;
static volatile bool s_ble_data_business_ready;
static volatile uint32_t s_deferred_ble_data_count;
static char *s_pending_ble_data_admission;
static bool s_pending_ble_data_deferred;

static void Ch583Uart_DispatchBleDataText(const char *data)
{
    if (data == NULL || data[0] == '\0') {
        ESP_LOGI(TAG, "CH583 BLE_DATA empty");
        User_HandleWifiJsonTextFromCh583("");
        return;
    }

    User_HandleWifiJsonTextFromCh583(data);
}

static void Ch583Uart_BleDataBusinessTask(void *argument)
{
    (void)argument;
    for (;;) {
        while (!__atomic_load_n(&s_ble_data_business_ready, __ATOMIC_ACQUIRE)) {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
        char *data = NULL;
        if (xQueueReceive(s_ble_data_business_queue, &data, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        Ch583Uart_DispatchBleDataText(data != NULL ? data : "");
        free(data);
    }
}

static esp_err_t Ch583Uart_HandleBleDataText(
    const char *data,
    ch583_wifi_ble_data_action_t action)
{
    if (action == CH583_WIFI_BLE_DATA_CANCEL) {
        free(s_pending_ble_data_admission);
        s_pending_ble_data_admission = NULL;
        s_pending_ble_data_deferred = false;
        return ESP_OK;
    }
    if (action == CH583_WIFI_BLE_DATA_COMMIT) {
        if (s_pending_ble_data_admission == NULL ||
            s_ble_data_business_queue == NULL) {
            return ESP_ERR_INVALID_STATE;
        }
        char *accepted = s_pending_ble_data_admission;
        bool deferred = s_pending_ble_data_deferred;
        s_pending_ble_data_admission = NULL;
        s_pending_ble_data_deferred = false;
        if (xQueueSend(s_ble_data_business_queue, &accepted, 0) != pdTRUE) {
            ESP_LOGE(TAG, "BLE_DATA reserved queue commit failed");
            free(accepted);
            return ESP_ERR_INVALID_STATE;
        }
        if (deferred &&
            !__atomic_load_n(&s_ble_data_business_ready, __ATOMIC_ACQUIRE)) {
            uint32_t deferred_count =
                __atomic_add_fetch(&s_deferred_ble_data_count,
                                   1U,
                                   __ATOMIC_ACQ_REL);
            ESP_LOGI(TAG, "startup BLE_DATA deferred count=%lu",
                     (unsigned long)deferred_count);
        }
        return ESP_OK;
    }
    if (action != CH583_WIFI_BLE_DATA_ADMIT || data == NULL ||
        s_ble_data_business_queue == NULL || s_pending_ble_data_admission != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (uxQueueSpacesAvailable(s_ble_data_business_queue) == 0U) {
        ESP_LOGE(TAG, "BLE_DATA business queue full len=%u",
                 (unsigned int)strlen(data));
        return ESP_ERR_TIMEOUT;
    }
    size_t data_len = strlen(data);
    char *copy = (char *)malloc(data_len + 1U);
    if (copy == NULL) {
        ESP_LOGE(TAG, "BLE_DATA queue alloc failed len=%u", (unsigned int)data_len);
        return ESP_ERR_NO_MEM;
    }
    memcpy(copy, data, data_len + 1U);
    s_pending_ble_data_admission = copy;
    s_pending_ble_data_deferred =
        !__atomic_load_n(&s_ble_data_business_ready, __ATOMIC_ACQUIRE);
    return ESP_OK;
}

void Ch583UartApp_SetBleDataBusinessReady(void)
{
    __atomic_store_n(&s_ble_data_business_ready, true, __ATOMIC_RELEASE);
    if (s_ble_data_business_task != NULL) {
        xTaskNotifyGive(s_ble_data_business_task);
    }
    uint32_t deferred = __atomic_exchange_n(&s_deferred_ble_data_count,
                                             0U,
                                             __ATOMIC_ACQ_REL);
    ESP_LOGI(TAG, "BLE_DATA business ready deferred=%lu",
             (unsigned long)deferred);
}

static void Ch583Uart_ReadAndProcess(size_t rx_size)
{
    uint8_t data[USER_CH583_UART_RECEIVE_BUF_SIZE + 1];

    while (rx_size > 0) {
        size_t want = rx_size;
        if (want > USER_CH583_UART_RECEIVE_BUF_SIZE) {
            want = USER_CH583_UART_RECEIVE_BUF_SIZE;
        }

        int len = uart_read_bytes(USER_CH583_UART_PORT,
                                  data,
                                  want,
                                  pdMS_TO_TICKS(20));
        if (len <= 0) {
            ESP_LOGD(TAG, "UART_DATA read empty rx_size=%u", (unsigned int)rx_size);
            break;
        }

        data[len] = '\0';
        ESP_LOGD(TAG, "UART_DATA read len=%d first=0x%02x", len, data[0]);

        // Feed UART bytes only after the driver reports data.
        // 只在 UART 驱动上报有数据后，才把字节送入 CH583 协议解析器。
        ch583_wifi_uart_process_bytes(data, (size_t)len, Ch583Uart_HandleBleDataText);

        if ((size_t)len >= rx_size) {
            break;
        }
        rx_size -= (size_t)len;
    }
}

static void User_UartEventTask(void *arg)
{
    (void)arg;
    uart_event_t event = {};
    UBaseType_t min_free_stack = 0;

    while (1) {
        if (s_ch583_uart_event_queue == NULL) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (xQueueReceive(s_ch583_uart_event_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (event.type) {
        case UART_DATA:
            // Read UART data from the event task to avoid the old 20 ms polling wakeups.
            // 在事件任务中读取 UART 数据，避免旧的 20ms 轮询唤醒 CPU。
            ESP_LOGD(TAG, "UART_DATA event size=%u", (unsigned int)event.size);
            Ch583Uart_ReadAndProcess(event.size);
            min_free_stack = uxTaskGetStackHighWaterMark(NULL);
            if (min_free_stack < 1024) {
                ESP_LOGW(TAG, "User_UartEventTask low stack watermark=%u bytes",
                         (unsigned int)min_free_stack);
            }
            break;
        case UART_BUFFER_FULL:
            // Report RX ring-buffer full events to confirm whether CH583 data is lost before the receive task reads it.
            /* 中文：打印 RX 环形缓冲区已满事件，用于确认 CH583 数据是否已经丢失。 */
            ESP_LOGW(TAG, "RX ring buffer full size=%u", (unsigned int)event.size);
            break;
        case UART_FIFO_OVF:
            // Report hardware FIFO overflow because this means bytes were lost before entering the driver ring buffer.
            /* 中文：打印硬件 FIFO 溢出事件，因为这表示字节在进入驱动缓冲区前已经丢失。 */
            ESP_LOGW(TAG, "RX hardware FIFO overflow size=%u", (unsigned int)event.size);
            uart_flush_input(USER_CH583_UART_PORT);
            break;
        case UART_FRAME_ERR:
            // Report frame errors to distinguish electrical or baud issues from software buffer pressure.
            /* 中文：打印帧错误，用于区分电气或波特率问题和软件缓冲区压力问题。 */
            ESP_LOGW(TAG, "frame error");
            break;
        case UART_PARITY_ERR:
            // Report parity errors even though the protocol uses no parity, so unexpected UART config issues are visible.
            /* 中文：虽然协议不使用校验位，仍打印奇偶校验错误，便于发现串口配置异常。 */
            ESP_LOGW(TAG, "parity error");
            break;
        default:
            ESP_LOGD(TAG, "uart event type=%d size=%u", event.type, (unsigned int)event.size);
            break;
        }
    }
}

static void __attribute__((unused)) User_UartReceiveTask(void *arg)
{
    (void)arg;
    uint8_t data[USER_CH583_UART_RECEIVE_BUF_SIZE + 1];

    while (1) {
        int len = uart_read_bytes(USER_CH583_UART_PORT,
                                  data,
                                  USER_CH583_UART_RECEIVE_BUF_SIZE,
                                  pdMS_TO_TICKS(20));
        if (len <= 0) {
            continue;
        }

        data[len] = '\0';
        //  ESP_LOGI(TAG, "RX len=%d first=0x%02x", len, data[0]);
        // Feed UART bytes to the copied CH583 V1 protocol parser while keeping this module independent.
        /* 中文：将串口字节送入复制过来的 CH583 V1 协议解析器，并保持本模块独立。 */
        ch583_wifi_uart_process_bytes(data, (size_t)len, Ch583Uart_HandleBleDataText);
    }
}

esp_err_t Ch583UartApp_Init(void)
{
    if (ch583_wifi_uart_protocol_init() != 0) {
        ESP_LOGE(TAG, "CH583 protocol mutex init failed");
        return ESP_ERR_NO_MEM;
    }
#if USER_CH583_UART_ENABLE
    if (s_ch583_uart_started) {
        ESP_LOGW(TAG, "CH583 UART already started");
        ServerNetworkStaWifiWorkTime_OnCh583Initialized();
        return ESP_OK;
    }

    if (s_ble_data_business_queue == NULL) {
        s_ble_data_business_queue =
            xQueueCreate(CH583_BLE_DATA_STARTUP_QUEUE_LENGTH, sizeof(char *));
        if (s_ble_data_business_queue == NULL) {
            ESP_LOGE(TAG, "create BLE_DATA business queue failed");
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_ble_data_business_task == NULL) {
        if (xTaskCreate(Ch583Uart_BleDataBusinessTask,
                        "ch583_ble_data",
                        CH583_BLE_DATA_BUSINESS_TASK_STACK_SIZE,
                        NULL,
                        CH583_BLE_DATA_BUSINESS_TASK_PRIORITY,
                        &s_ble_data_business_task) != pdPASS) {
            s_ble_data_business_task = NULL;
            ESP_LOGE(TAG, "create BLE_DATA business task failed");
            return ESP_ERR_NO_MEM;
        }
    }

    uart_config_t uart_config = {
        .baud_rate = USER_CH583_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_XTAL,
    };

    esp_err_t ret = ESP_OK;
    if (!uart_is_driver_installed(USER_CH583_UART_PORT)) {
        ret = uart_driver_install(USER_CH583_UART_PORT,
                                  USER_CH583_UART_DRIVER_RX_BUF_SIZE,
                                  USER_CH583_UART_DRIVER_TX_BUF_SIZE,
                                  USER_CH583_UART_EVENT_QUEUE_SIZE,
                                  &s_ch583_uart_event_queue,
                                  0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "uart_driver_install fail ret=%d(%s)", ret, esp_err_to_name(ret));
            return ret;
        }
    } else {
        ESP_LOGW(TAG, "driver already installed, keep existing UART ring buffer and event queue");
    }

    ret = uart_param_config(USER_CH583_UART_PORT, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config fail ret=%d(%s)", ret, esp_err_to_name(ret));
        return ret;
    }

    ret = uart_set_pin(USER_CH583_UART_PORT,
                       USER_CH583_UART_TX_PIN,
                       USER_CH583_UART_RX_PIN,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin tx=%d rx=%d fail ret=%d(%s)",
                 USER_CH583_UART_TX_PIN,
                 USER_CH583_UART_RX_PIN,
                 ret,
                 esp_err_to_name(ret));
        return ret;
    }

    // Configure UART RX wake threshold for future low-power testing, but keep failures non-fatal for board bring-up.
    /* 中文：配置 UART RX 唤醒阈值用于后续低功耗测试，但失败不影响串口接收。 */
    ret = uart_set_wakeup_threshold(USER_CH583_UART_PORT, USER_CH583_UART_WAKEUP_THRESHOLD);
    ESP_LOGI(TAG, "uart wake threshold ret=%d(%s) port=%d rx=%d threshold=%d",
             ret,
             esp_err_to_name(ret),
             USER_CH583_UART_PORT,
             USER_CH583_UART_RX_PIN,
             USER_CH583_UART_WAKEUP_THRESHOLD);
    if (ret == ESP_OK) {
        ret = esp_sleep_enable_uart_wakeup(USER_CH583_UART_PORT);
        ESP_LOGI(TAG, "uart wake enable ret=%d(%s) port=%d rx=%d",
                 ret,
                 esp_err_to_name(ret),
                 USER_CH583_UART_PORT,
                 USER_CH583_UART_RX_PIN);
    }

    if (s_ch583_uart_event_queue != NULL) {
        BaseType_t task_ok = xTaskCreate(User_UartEventTask,
                                         "User_UartEventTask",
                                         USER_CH583_UART_EVENT_TASK_STACK_SIZE,
                                         NULL,
                                         2,
                                         NULL);
        if (task_ok != pdPASS) {
            ESP_LOGE(TAG, "create User_UartEventTask failed stack=%u",
                     (unsigned int)USER_CH583_UART_EVENT_TASK_STACK_SIZE);
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(TAG, "User_UartEventTask stack=%u",
                 (unsigned int)USER_CH583_UART_EVENT_TASK_STACK_SIZE);
    }

    (void)ch583_wifi_uart_get_ble_mac();

    s_ch583_uart_started = true;
    ServerNetworkStaWifiWorkTime_OnCh583Initialized();
    ESP_LOGI(TAG, "CH583 UART started port=%d tx=%d rx=%d baud=%d rx_buf=%d tx_buf=%d event_queue=%d",
             USER_CH583_UART_PORT,
             USER_CH583_UART_TX_PIN,
             USER_CH583_UART_RX_PIN,
             USER_CH583_UART_BAUD_RATE,
             USER_CH583_UART_DRIVER_RX_BUF_SIZE,
             USER_CH583_UART_DRIVER_TX_BUF_SIZE,
             USER_CH583_UART_EVENT_QUEUE_SIZE);
    return ESP_OK;
#else
    ServerNetworkStaWifiWorkTime_OnCh583Initialized();
    ESP_LOGI(TAG, "CH583 UART disabled by USER_CH583_UART_ENABLE=0");
    return ESP_OK;
#endif
}
