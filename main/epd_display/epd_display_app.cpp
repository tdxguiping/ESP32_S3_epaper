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
    uint8_t epd_which_one;
} epd_display_job_t;

uint16_t sleep_time = 0;
EventGroupHandle_t sleep_group = NULL;

static QueueHandle_t s_epd_display_queue = NULL;
static TaskHandle_t s_epd_display_task = NULL;

static void log_heap_watermark(const char *point)
{
    ESP_LOGI(TAG,
             "heap %s free=%u min=%u psram=%u internal=%u",
             point,
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

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
        if (display_size > USER_INTERNAL_RAM_FALLBACK_MAX_SIZE) {
            ESP_LOGE(TAG, "display PSRAM alloc failed, size too large for internal RAM size=%u",
                     (unsigned int)display_size);
            return ESP_ERR_NO_MEM;
        }

        // English: Only small display buffers may fall back to internal RAM.
        // 中文：只有小显示缓冲允许退回内部 RAM，避免大图挤占系统内存。
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
    log_heap_watermark("display_queue");
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
        log_heap_watermark("epd_start");
        UserLedStatus_Set(USER_LED_STATE_EPD_REFRESH);
        if (job.data == NULL || job.size == 0) {
            ESP_LOGW(TAG, "EPD display task skip empty job");
            UserLedStatus_Set(USER_LED_STATE_OPERATION_FAIL);
        } else {
            ePaperDisplay.Set_EPD_which_one(job.epd_which_one);
            ESP_LOGI(TAG, "EPD target=%u", (unsigned int)job.epd_which_one);


        #if(EPD_type_ ==  EPD_800_480)                    
            ePaperDisplay.EPD_Init();
            ePaperDisplay.NT61522_Init_display();
            ePaperDisplay.NT61522_Display_net((const uint8_t *)job.data, job.size);
            ePaperDisplay.Epaper_Update();	

        #elif(EPD_type_ ==  EPD_1024_600)                    
            ePaperDisplay.EPD_Init();
            ePaperDisplay.NT61522_Init_display();
            ePaperDisplay.NT61522_Display_net((const uint8_t *)job.data, job.size);
            ePaperDisplay.NT61522_Display();

        #elif(EPD_type_ ==  EPD_1600_1200_79)
            ePaperDisplay.EPD_Init();
            ePaperDisplay.NT61522_Init_display();
            ePaperDisplay.NT61522_Display_net((const uint8_t *)job.data, job.size);
            ePaperDisplay.NT61522_Display();

        #elif(EPD_type_ ==  EPD_1600_1200_133)
            ePaperDisplay.EPD_Init();
            ePaperDisplay.NT61522_Init_display();
            ePaperDisplay.NT61522_Display_net((const uint8_t *)job.data, job.size);
            ePaperDisplay.NT61522_Display();

        #elif(EPD_type_ ==  EPD_1360_480_1085)                    
            ePaperDisplay.EPD_Init();
            ePaperDisplay.NT61522_Display_net((const uint8_t *)job.data, job.size);
            ePaperDisplay.Epaper_Update();	

        #elif(EPD_type_ ==  EPD_800_480_4s_75)                    
            ePaperDisplay.EPD_Init();
            ePaperDisplay.NT61522_Display_net((const uint8_t *)job.data, job.size);
            ePaperDisplay.Epaper_Update_and_Deepsleep();		
        #else
            ePaperDisplay.EPD_Init();
            ePaperDisplay.NT61522_Init_display();
            ePaperDisplay.NT61522_Display_net((const uint8_t *)job.data, job.size);
            ePaperDisplay.NT61522_Display();
        #endif


            UserLedStatus_Set(USER_LED_STATE_SUCCESS);
        }

        ESP_LOGI(TAG, "EPD display task done ptr=%p size=%u", job.data, (unsigned int)job.size);
        release_epd_job(&job);
        log_heap_watermark("epd_end");
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

    ESP_LOGI(TAG, "EPD startup init skip, init on first display");

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

esp_err_t ServerNetworkStaEpdDisplay_QueueToScreen(const uint8_t *display_buf, size_t display_size, uint8_t epd_which_one)
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
    job.epd_which_one = (epd_which_one == 2) ? 2 : 1;

    if (xQueueSend(s_epd_display_queue, &job, 0) != pdTRUE) {
        ESP_LOGE(TAG, "display queue full, keep old jobs and reject new ptr=%p size=%u",
                 job.data, (unsigned int)job.size);
        release_epd_job(&job);
        return ESP_ERR_TIMEOUT;
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

esp_err_t ServerNetworkStaEpdDisplay_Queue(const uint8_t *display_buf, size_t display_size)
{
    return ServerNetworkStaEpdDisplay_QueueToScreen(display_buf, display_size, 1);
}


// at file  epd_display_app.cpp 
// 写一个测试函数 （只用于临时测试），并在 main.c 中调用
// void test_epd_display_EPD_1600_1200_79(void)

// 函数 test_epd_display_EPD_1600_1200_79 
// 功能如下

// 1， 临时申请一个        960000 bytes 的变量
//     这个变量的前 10%，全部写成 0x00
//     接下去的10%，全部写成 0x02
//     接下去的10%，全部写成 0x03
//     接下去的10%，全部写成 0x04
//     接下去的10%，全部写成 0x05
//     接下去的10%，全部写成 0x06
//     余下的，全部写成 0x05
    
//    发信息给 函数 ServerNetworkStaEpdDisplay_Task 
//    将刚刚 申请的 变量
//    传给这个函数

   
//    在函数 ServerNetworkStaEpdDisplay_Task 中
//    调用 EPD 显示

void test_epd_display_EPD_1600_1200_79(void)
{
    static const size_t test_size = 960000;
    static const size_t block_size = test_size / 10;
    static const uint8_t block_values[] = {0x00, 0x02, 0x03, 0x04, 0x05, 0x06, 0x03, 0x04, 0x05, 0x06};

    uint8_t *test_buf = (uint8_t *)heap_caps_malloc(test_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (test_buf == NULL) {
        ESP_LOGE(TAG, "EPD test alloc failed size=%u", (unsigned int)test_size);
        return;
    }

    LOG_Purple("%s>%d",__func__,__LINE__);

    for (size_t i = 0; i < sizeof(block_values); ++i) {
        memset(test_buf + (i * block_size), block_values[i], block_size);
    }
    // memset(test_buf + (sizeof(block_values) * block_size),
    //        0x05,
    //        test_size - (sizeof(block_values) * block_size));

    epd_display_job_t job = {};
    job.data = test_buf;
    job.size = test_size;
    job.epd_which_one = 1;

    if (s_epd_display_queue == NULL ||
        xQueueSend(s_epd_display_queue, &job, 0) != pdTRUE) {
        ESP_LOGE(TAG, "EPD 1600x1200 test queue failed");
        release_epd_job(&job);
        return;
    }

    ESP_LOGI(TAG, "EPD 1600x1200 test queued size=%u", (unsigned int)test_size);
}

void test_epd_display_EPD_EPD_1024_600(void)
{
    static const size_t test_size = 307200;
    static const size_t block_size = test_size / 10;
    static const uint8_t block_values[] = {0x00, 0x02, 0x03, 0x04, 0x05, 0x06};

    uint8_t *test_buf = (uint8_t *)heap_caps_malloc(test_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (test_buf == NULL) {
        ESP_LOGE(TAG, "EPD 1024x600 test alloc failed size=%u", (unsigned int)test_size);
        return;
    }

    LOG_Purple("%s>%d", __func__, __LINE__);

    for (size_t i = 0; i < sizeof(block_values); ++i) {
        memset(test_buf + (i * block_size), block_values[i], block_size);
    }
    memset(test_buf + (sizeof(block_values) * block_size),
           0x05,
           test_size - (sizeof(block_values) * block_size));

    epd_display_job_t job = {};
    job.data = test_buf;
    job.size = test_size;
    job.epd_which_one = 1;

    if (s_epd_display_queue == NULL ||
        xQueueSend(s_epd_display_queue, &job, 0) != pdTRUE) {
        ESP_LOGE(TAG, "EPD 1024x600 test queue failed");
        release_epd_job(&job);
        return;
    }

    ESP_LOGI(TAG, "EPD 1024x600 test queued size=%u", (unsigned int)test_size);
}
