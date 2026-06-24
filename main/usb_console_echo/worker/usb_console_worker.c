#include "usb_console_worker.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "server_network_sta.h"
#include "tdx_cfg.h"

typedef enum {
    USB_CONSOLE_WORK_ITEM_WIFI_CONNECT = 1,
    USB_CONSOLE_WORK_ITEM_JOB = 2,
} usb_console_work_type_t;

typedef struct {
    usb_console_work_type_t type;
    const char *name;
    usb_console_worker_job_fn_t job;
    void *ctx;
} usb_console_work_item_t;

static const char *TAG = "usb_console_worker";
static QueueHandle_t s_usb_console_worker_queue;
static TaskHandle_t s_usb_console_worker_task;
static bool s_wifi_connect_running;

static void handle_wifi_connect(void)
{
    if (s_wifi_connect_running) {
        ESP_LOGW(TAG, "wifi connect request ignored because previous connect is running");
        return;
    }

    s_wifi_connect_running = true;
    ESP_LOGI(TAG, "wifi connect task start base_path=%s", USB_CONSOLE_BASE_PATH);
    uint8_t ret = User_Network_mode_app_init_force(USB_CONSOLE_BASE_PATH);
    ESP_LOGI(TAG, "wifi connect task done ret=0x%02x", ret);
    s_wifi_connect_running = false;
}

static void UsbConsoleWorker_Task(void *arg)
{
    (void)arg;

    while (1) {
        usb_console_work_item_t item = {0};
        if (xQueueReceive(s_usb_console_worker_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (item.type) {
        case USB_CONSOLE_WORK_ITEM_WIFI_CONNECT:
            handle_wifi_connect();
            break;
        case USB_CONSOLE_WORK_ITEM_JOB:
            if (item.job != NULL) {
                ESP_LOGI(TAG, "job start name=%s", item.name != NULL ? item.name : "unknown");
                item.job(item.ctx);
                ESP_LOGI(TAG, "job done name=%s", item.name != NULL ? item.name : "unknown");
            }
            break;
        default:
            ESP_LOGW(TAG, "unknown work item type=%d", (int)item.type);
            break;
        }
    }
}

esp_err_t UsbConsoleWorker_Init(void)
{
    if (s_usb_console_worker_task != NULL) {
        return ESP_OK;
    }

    if (s_usb_console_worker_queue == NULL) {
        s_usb_console_worker_queue = xQueueCreate(USB_CONSOLE_WORKER_QUEUE_LENGTH,
                                                  sizeof(usb_console_work_item_t));
        if (s_usb_console_worker_queue == NULL) {
            ESP_LOGE(TAG, "create worker queue failed length=%u",
                     (unsigned int)USB_CONSOLE_WORKER_QUEUE_LENGTH);
            return ESP_ERR_NO_MEM;
        }
    }

    BaseType_t ret = xTaskCreate(UsbConsoleWorker_Task,
                                 "UsbConsoleWorker",
                                 USB_CONSOLE_WORKER_TASK_STACK_SIZE,
                                 NULL,
                                 USB_CONSOLE_WORKER_TASK_PRIORITY,
                                 &s_usb_console_worker_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "create worker task failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "worker task started stack=%u priority=%u queue=%u",
             (unsigned int)USB_CONSOLE_WORKER_TASK_STACK_SIZE,
             (unsigned int)USB_CONSOLE_WORKER_TASK_PRIORITY,
             (unsigned int)USB_CONSOLE_WORKER_QUEUE_LENGTH);
    return ESP_OK;
}

esp_err_t UsbConsoleWorker_SubmitJob(const char *name, usb_console_worker_job_fn_t job, void *ctx)
{
    if (s_usb_console_worker_queue == NULL || job == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    usb_console_work_item_t item = {
        .type = USB_CONSOLE_WORK_ITEM_JOB,
        .name = name,
        .job = job,
        .ctx = ctx,
    };
    if (xQueueSend(s_usb_console_worker_queue, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "worker queue full, drop job name=%s", name != NULL ? name : "unknown");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "job queued name=%s", name != NULL ? name : "unknown");
    return ESP_OK;
}

esp_err_t UsbConsoleWorker_SubmitWifiConnect(void)
{
    if (s_usb_console_worker_queue == NULL) {
        ESP_LOGE(TAG, "worker queue not ready");
        return ESP_ERR_INVALID_STATE;
    }

    usb_console_work_item_t item = {
        .type = USB_CONSOLE_WORK_ITEM_WIFI_CONNECT,
    };
    if (xQueueSend(s_usb_console_worker_queue, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "worker queue full, drop wifi connect request");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "wifi connect request queued");
    return ESP_OK;
}
