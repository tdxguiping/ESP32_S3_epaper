#include "server_network_sta_dataup_async.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "server_sta_data_async";

#define DATAUP_ASYNC_QUEUE_LENGTH 1
#define DATAUP_ASYNC_TASK_STACK_SIZE (12 * 1024)
#define DATAUP_ASYNC_TASK_PRIORITY 5
#define DATAUP_ASYNC_BUSY_TIMEOUT_MS 50000

typedef struct {
    char name[24];
    server_network_sta_dataup_async_fn_t process;
    server_network_sta_dataup_async_fn_t cleanup;
    void *ctx;
} dataup_async_job_t;

static QueueHandle_t s_dataup_async_queue;
static TaskHandle_t s_dataup_async_task;
static bool s_dataup_async_busy;
static bool s_dataup_async_timed_out;
static bool s_dataup_async_timeout_logged;
static int64_t s_dataup_async_start_us;
static char s_dataup_async_name[24];

static uint32_t elapsed_ms_since(int64_t start_us)
{
    return (uint32_t)((esp_timer_get_time() - start_us) / 1000);
}

static void dataup_async_clear_state(void)
{
    __atomic_store_n(&s_dataup_async_busy, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_dataup_async_timed_out, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_dataup_async_timeout_logged, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_dataup_async_start_us, 0, __ATOMIC_RELEASE);
    s_dataup_async_name[0] = '\0';
}

const char *ServerNetworkStaDataupAsync_StateName(server_network_sta_dataup_async_state_t state)
{
    switch (state) {
    case SERVER_NETWORK_STA_DATAUP_ASYNC_IDLE:
        return "idle";
    case SERVER_NETWORK_STA_DATAUP_ASYNC_BUSY:
        return "async_busy";
    case SERVER_NETWORK_STA_DATAUP_ASYNC_TIMEOUT:
        return "async_timeout";
    default:
        return "unknown";
    }
}

server_network_sta_dataup_async_state_t ServerNetworkStaDataupAsync_GetState(void)
{
    if (!__atomic_load_n(&s_dataup_async_busy, __ATOMIC_ACQUIRE)) {
        return SERVER_NETWORK_STA_DATAUP_ASYNC_IDLE;
    }
    if (__atomic_load_n(&s_dataup_async_timed_out, __ATOMIC_ACQUIRE)) {
        return SERVER_NETWORK_STA_DATAUP_ASYNC_TIMEOUT;
    }

    int64_t start_us = __atomic_load_n(&s_dataup_async_start_us, __ATOMIC_ACQUIRE);
    uint32_t elapsed_ms = start_us > 0 ? elapsed_ms_since(start_us) : 0;
    if (elapsed_ms > DATAUP_ASYNC_BUSY_TIMEOUT_MS) {
        bool expected = false;
        __atomic_store_n(&s_dataup_async_timed_out, true, __ATOMIC_RELEASE);
        if (__atomic_compare_exchange_n(&s_dataup_async_timeout_logged,
                                        &expected,
                                        true,
                                        false,
                                        __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
            ESP_LOGE(TAG, "dataup async timeout name=%s elapsed_ms=%u limit_ms=%u",
                     s_dataup_async_name[0] ? s_dataup_async_name : "<none>",
                     (unsigned int)elapsed_ms,
                     (unsigned int)DATAUP_ASYNC_BUSY_TIMEOUT_MS);
        }
        return SERVER_NETWORK_STA_DATAUP_ASYNC_TIMEOUT;
    }

    return SERVER_NETWORK_STA_DATAUP_ASYNC_BUSY;
}

static void dataup_async_worker_task(void *arg)
{
    (void)arg;
    dataup_async_job_t job = {0};

    ESP_LOGI(TAG, "dataup async worker start");
    for (;;) {
        if (xQueueReceive(s_dataup_async_queue, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        ESP_LOGI(TAG, "dataup async process start name=%s", job.name);
        if (job.process != NULL) {
            job.process(job.ctx);
        }
        if (job.cleanup != NULL) {
            job.cleanup(job.ctx);
        }
        ESP_LOGI(TAG, "dataup async process done name=%s", job.name);
        dataup_async_clear_state();
        memset(&job, 0, sizeof(job));
    }
}

esp_err_t ServerNetworkStaDataupAsync_Init(void)
{
    if (s_dataup_async_queue != NULL && s_dataup_async_task != NULL) {
        return ESP_OK;
    }

    if (s_dataup_async_queue == NULL) {
        s_dataup_async_queue = xQueueCreate(DATAUP_ASYNC_QUEUE_LENGTH, sizeof(dataup_async_job_t));
        if (s_dataup_async_queue == NULL) {
            ESP_LOGE(TAG, "dataup async queue create failed");
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_dataup_async_task == NULL) {
        BaseType_t task_ret = xTaskCreate(dataup_async_worker_task,
                                          "dataup_async",
                                          DATAUP_ASYNC_TASK_STACK_SIZE,
                                          NULL,
                                          DATAUP_ASYNC_TASK_PRIORITY,
                                          &s_dataup_async_task);
        if (task_ret != pdPASS) {
            ESP_LOGE(TAG, "dataup async worker create failed");
            s_dataup_async_task = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

esp_err_t ServerNetworkStaDataupAsync_Submit(const char *name,
                                             server_network_sta_dataup_async_fn_t process,
                                             server_network_sta_dataup_async_fn_t cleanup,
                                             void *ctx)
{
    if (process == NULL || cleanup == NULL || ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t init_ret = ServerNetworkStaDataupAsync_Init();
    if (init_ret != ESP_OK) {
        return init_ret;
    }

    server_network_sta_dataup_async_state_t state = ServerNetworkStaDataupAsync_GetState();
    if (state != SERVER_NETWORK_STA_DATAUP_ASYNC_IDLE) {
        return state == SERVER_NETWORK_STA_DATAUP_ASYNC_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_ERR_INVALID_STATE;
    }

    bool expected = false;
    if (!__atomic_compare_exchange_n(&s_dataup_async_busy,
                                     &expected,
                                     true,
                                     false,
                                     __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE)) {
        return ESP_ERR_INVALID_STATE;
    }

    dataup_async_job_t job = {
        .process = process,
        .cleanup = cleanup,
        .ctx = ctx,
    };
    snprintf(job.name, sizeof(job.name), "%s", name != NULL ? name : "dataup");
    snprintf(s_dataup_async_name, sizeof(s_dataup_async_name), "%s", job.name);
    __atomic_store_n(&s_dataup_async_start_us, esp_timer_get_time(), __ATOMIC_RELEASE);
    __atomic_store_n(&s_dataup_async_timed_out, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_dataup_async_timeout_logged, false, __ATOMIC_RELEASE);

    if (xQueueSend(s_dataup_async_queue, &job, 0) != pdTRUE) {
        dataup_async_clear_state();
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "dataup async submit name=%s", job.name);
    return ESP_OK;
}
