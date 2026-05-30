#include "epd_display_app.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led_status.h"
#include "tdx_cfg.h"

static const char *TAG = "epd_display";

typedef struct {
    uint8_t *data;
    size_t size;
} epd_display_job_t;

uint16_t sleep_time = 0;
EventGroupHandle_t sleep_group = NULL;

static QueueHandle_t s_epd_display_queue = NULL;
static TaskHandle_t s_epd_display_task = NULL;

ePaperPort ePaperDisplay(USER_EPD_MOSI_PIN,
                         USER_EPD_SCK_PIN,
                         USER_EPD_DC_PIN,
                         USER_EPD_CS_PIN,
                         USER_EPD_CS2_PIN,
                         USER_EPD_RST_PIN,
                         USER_EPD_BUSY_PIN,
                         USER_EPD_WIDTH,
                         USER_EPD_HEIGHT,
                         USER_EPD_SCALE_MAX_WIDTH,
                         USER_EPD_SCALE_MAX_HEIGHT,
                         USER_EPD_SPI_HOST);

static void release_epd_job(epd_display_job_t *job)
{
    if (job != NULL && job->data != NULL) {
        heap_caps_free(job->data);
        job->data = NULL;
        job->size = 0;
    }
}

static esp_err_t copy_display_buffer(epd_display_job_t *job, const uint8_t *display_buf, size_t display_size)
{
    if (job == NULL || display_buf == NULL || display_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* English: Copy display data because receive_data_redirect_handler frees the HTTP body after dispatch. */
    /* 中文：复制显示数据，因为 receive_data_redirect_handler 分发完成后会释放 HTTP body。 */
    uint8_t *copy = (uint8_t *)heap_caps_malloc(display_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (copy == NULL) {
        copy = (uint8_t *)heap_caps_malloc(display_size, MALLOC_CAP_8BIT);
    }
    if (copy == NULL) {
        ESP_LOGE(TAG, "display buffer alloc failed size=%u", (unsigned int)display_size);
        return ESP_ERR_NO_MEM;
    }

    memcpy(copy, display_buf, display_size);
    job->data = copy;
    job->size = display_size;
    ESP_LOGI(TAG, "display buffer copied ptr=%p size=%u", copy, (unsigned int)display_size);
    return ESP_OK;
}

static void ServerNetworkStaEpdDisplay_Task(void *arg)
{
    (void)arg;
    epd_display_job_t job = {};

    for (;;) {
        if (xQueueReceive(s_epd_display_queue, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        ESP_LOGI(TAG, "EPD display task start ptr=%p size=%u", job.data, (unsigned int)job.size);
        UserLedStatus_Set(USER_LED_STATE_EPD_REFRESH);
        if (job.data == NULL || job.size == 0) {
            ESP_LOGW(TAG, "EPD display task skip empty job");
            release_epd_job(&job);
            UserLedStatus_Set(USER_LED_STATE_OPERATION_FAIL);
            continue;
        }

#if (EPD_type_ == EPD_800_480_4s_75)
        ePaperDisplay.EPD_Init();
        ePaperDisplay.NT61522_Display_net((const uint8_t *)job.data, job.size);
        ePaperDisplay.Epaper_Update_and_Deepsleep();
#elif (EPD_type_ == EPD_1360_480_1085)
        ePaperDisplay.EPD_Init();
        ePaperDisplay.NT61522_Display_net((const uint8_t *)job.data, job.size);
        ePaperDisplay.Epaper_Update();
#else
        ePaperDisplay.EPD_Init();
        ePaperDisplay.NT61522_Init_display();
        ePaperDisplay.NT61522_Display_net((const uint8_t *)job.data, job.size);
        ePaperDisplay.NT61522_Display();
#endif

        ESP_LOGI(TAG, "EPD display task done ptr=%p size=%u", job.data, (unsigned int)job.size);
        release_epd_job(&job);
        UserLedStatus_Set(USER_LED_STATE_SUCCESS);
    }
}

esp_err_t ServerNetworkStaEpdDisplay_Init(void)
{
#if USER_EPD_ENABLE
    if (sleep_group == NULL) {
        sleep_group = xEventGroupCreate();
        if (sleep_group == NULL) {
            ESP_LOGE(TAG, "create sleep_group failed");
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_epd_display_queue == NULL) {
        s_epd_display_queue = xQueueCreate(USER_EPD_DISPLAY_QUEUE_LENGTH, sizeof(epd_display_job_t));
        if (s_epd_display_queue == NULL) {
            ESP_LOGE(TAG, "create EPD display queue failed");
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(TAG, "EPD startup init begin");
    ePaperDisplay.EPD_Init();
    ESP_LOGI(TAG, "EPD startup init done");

    if (s_epd_display_task == NULL) {
        BaseType_t task_ret = xTaskCreate(ServerNetworkStaEpdDisplay_Task,
                                          "epd_display",
                                          USER_EPD_DISPLAY_TASK_STACK_SIZE,
                                          NULL,
                                          USER_EPD_DISPLAY_TASK_PRIORITY,
                                          &s_epd_display_task);
        if (task_ret != pdPASS) {
            ESP_LOGE(TAG, "create EPD display task failed");
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
#else
    ESP_LOGW(TAG, "EPD display disabled by USER_EPD_ENABLE");
    return ESP_OK;
#endif
}

esp_err_t ServerNetworkStaEpdDisplay_Queue(const uint8_t *display_buf, size_t display_size)
{
#if USER_EPD_ENABLE
    if (s_epd_display_queue == NULL) {
        ESP_LOGE(TAG, "display queue not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    epd_display_job_t job = {};
    esp_err_t ret = copy_display_buffer(&job, display_buf, display_size);
    if (ret != ESP_OK) {
        return ret;
    }

    epd_display_job_t old_job = {};
    if (xQueueSend(s_epd_display_queue, &job, 0) != pdTRUE) {
        if (xQueueReceive(s_epd_display_queue, &old_job, 0) == pdTRUE) {
            ESP_LOGW(TAG, "display queue full, drop old job ptr=%p size=%u",
                     old_job.data, (unsigned int)old_job.size);
            release_epd_job(&old_job);
        }
        if (xQueueSend(s_epd_display_queue, &job, 0) != pdTRUE) {
            ESP_LOGE(TAG, "display queue send failed ptr=%p size=%u", job.data, (unsigned int)job.size);
            release_epd_job(&job);
            return ESP_ERR_TIMEOUT;
        }
    }

    ESP_LOGI(TAG, "display job queued ptr=%p size=%u", job.data, (unsigned int)job.size);
    return ESP_OK;
#else
    (void)display_buf;
    (void)display_size;
    ESP_LOGW(TAG, "EPD display queue ignored because USER_EPD_ENABLE=0");
    return ESP_OK;
#endif
}
