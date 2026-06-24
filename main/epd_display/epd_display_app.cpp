#include "epd_display_app.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led_status.h"
#include "server_network_sta_wifi_work_time.h"
#include "tdx_cfg.h"
#include "epd_type.h"
#include "epd_test_1360_480_1085_3color_const.h"

static const char *TAG = "epd_display";

typedef struct {
    SemaphoreHandle_t done;
    volatile uint32_t refs;
    esp_err_t result;
} epd_display_completion_t;

typedef struct {
    uint8_t *data;
    size_t size;
    uint8_t epd_which_one;
    epd_display_completion_t *completion;
} epd_display_job_t;

uint16_t sleep_time = 0;
EventGroupHandle_t sleep_group = NULL;

static QueueHandle_t s_epd_display_queue = NULL;
static TaskHandle_t s_epd_display_task = NULL;

static epd_display_completion_t *create_completion(void)
{
    epd_display_completion_t *completion = (epd_display_completion_t *)calloc(1, sizeof(*completion));
    if (completion == NULL) {
        return NULL;
    }
    completion->done = xSemaphoreCreateBinary();
    if (completion->done == NULL) {
        free(completion);
        return NULL;
    }
    completion->refs = 2;
    completion->result = ESP_FAIL;
    return completion;
}

static void release_completion(epd_display_completion_t *completion)
{
    if (completion != NULL && __atomic_sub_fetch(&completion->refs, 1U, __ATOMIC_ACQ_REL) == 0U) {
        vSemaphoreDelete(completion->done);
        free(completion);
    }
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

        ServerNetworkStaWifiWorkTime_OnNetworkData();
        int64_t display_start_us = esp_timer_get_time();
        const epd_type_config_t *config = EpdType_GetCurrentConfig();
        esp_err_t display_ret = ESP_FAIL;
        if (config != NULL) {
            ESP_LOGI(TAG,
                     "EPD start target=%u type=%u name=%s resolution=%ux%u input=%u expected=%u",
                     (unsigned int)job.epd_which_one,
                     (unsigned int)config->type,
                     config->name,
                     (unsigned int)config->width,
                     (unsigned int)config->height,
                     (unsigned int)job.size,
                     (unsigned int)config->display_size);
        } else {
            ESP_LOGE(TAG, "EPD start invalid type=%u input=%u",
                     (unsigned int)EPD_type,
                     (unsigned int)job.size);
        }
        UserLedStatus_Set(USER_LED_STATE_EPD_REFRESH);
        if (job.data == NULL || job.size == 0) {
            ESP_LOGW(TAG, "EPD display task skip empty job");
            UserLedStatus_Set(USER_LED_STATE_OPERATION_FAIL);
        } else {
            ePaperDisplay.Set_EPD_which_one(job.epd_which_one);
            display_ret = EpdType_DisplayCurrent(ePaperDisplay, (const uint8_t *)job.data, job.size);
            UserLedStatus_Set(display_ret == ESP_OK ? USER_LED_STATE_SUCCESS : USER_LED_STATE_OPERATION_FAIL);
        }

        ESP_LOGI(TAG, "EPD done target=%u type=%u name=%s size=%u total_ms=%lld",
                 (unsigned int)job.epd_which_one,
                 (unsigned int)EPD_type,
                 config != NULL ? config->name : "INVALID",
                 (unsigned int)job.size,
                 (long long)((esp_timer_get_time() - display_start_us) / 1000));
        epd_display_completion_t *completion = job.completion;
        release_epd_job(&job);
        if (completion != NULL) {
            completion->result = display_ret;
            xSemaphoreGive(completion->done);
            release_completion(completion);
        }
    }
}

esp_err_t ServerNetworkStaEpdDisplay_Init(void)
{
#if USER_EPD_ENABLE
    // Load the saved EPD type before USB or network code reports the current display profile.
    // 在 USB 或网络代码上报当前屏幕配置前读取保存的 EPD 类型。
    ESP_ERROR_CHECK(EpdType_LoadSavedOrDefault());

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

    ESP_LOGI(TAG, "EPD queued target=%u size=%u",
             (unsigned int)job.epd_which_one,
             (unsigned int)job.size);
    return ESP_OK;
#else
    (void)display_buf;
    (void)display_size;
    ESP_LOGW(TAG, "EPD display queue ignored because USER_EPD_ENABLE=0");
    return ESP_OK;
#endif
}

esp_err_t ServerNetworkStaEpdDisplay_QueueToScreenAndWait(const uint8_t *display_buf, size_t display_size, uint8_t epd_which_one)
{
#if USER_EPD_ENABLE
    if (s_epd_display_queue == NULL) {
        ESP_LOGE(TAG, "display queue not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    epd_display_completion_t *completion = create_completion();
    if (completion == NULL) {
        ESP_LOGE(TAG, "display wait semaphore alloc failed");
        return ESP_ERR_NO_MEM;
    }

    epd_display_job_t job = {};
    esp_err_t ret = copy_display_buffer(&job, display_buf, display_size);
    if (ret != ESP_OK) {
        release_completion(completion);
        release_completion(completion);
        return ret;
    }
    job.epd_which_one = (epd_which_one == 2) ? 2 : 1;
    job.completion = completion;

    if (xQueueSend(s_epd_display_queue, &job, 0) != pdTRUE) {
        ESP_LOGE(TAG, "display queue full, reject sync job ptr=%p size=%u",
                 job.data, (unsigned int)job.size);
        release_epd_job(&job);
        release_completion(completion);
        release_completion(completion);
        return ESP_ERR_NOT_FINISHED;
    }

    ESP_LOGI(TAG, "EPD queued wait target=%u size=%u",
             (unsigned int)job.epd_which_one,
             (unsigned int)job.size);
    if (xSemaphoreTake(completion->done, pdMS_TO_TICKS(USER_EPD_DISPLAY_WAIT_TIMEOUT_MS)) != pdTRUE) {
        release_completion(completion);
        return ESP_ERR_TIMEOUT;
    }
    ret = completion->result;
    release_completion(completion);
    return ret;
#else
    (void)display_buf;
    (void)display_size;
    (void)epd_which_one;
    ESP_LOGW(TAG, "EPD display wait ignored because USER_EPD_ENABLE=0");
    return ESP_OK;
#endif
}

esp_err_t ServerNetworkStaEpdDisplay_Queue(const uint8_t *display_buf, size_t display_size)
{
    return ServerNetworkStaEpdDisplay_QueueToScreen(display_buf, display_size, 1);
}

esp_err_t test_epd_display_and_wait(void)
{
    const epd_type_config_t *config = EpdType_GetCurrentConfig();
    if (config == NULL || config->display_size == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t *test_buf = (uint8_t *)heap_caps_malloc(config->display_size,
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (test_buf == NULL && config->display_size <= USER_INTERNAL_RAM_FALLBACK_MAX_SIZE) {
        test_buf = (uint8_t *)heap_caps_malloc(config->display_size, MALLOC_CAP_8BIT);
    }
    if (test_buf == NULL) {
        ESP_LOGE(TAG, "EPD sync test alloc failed name=%s size=%u",
                 config->name,
                 (unsigned int)config->display_size);
        return ESP_ERR_NO_MEM;
    }

    static const uint8_t test_values[] = {0x00, 0xFF, 0x55, 0xAA};
    const size_t block_size = config->display_size / sizeof(test_values);
    for (size_t i = 0; i < sizeof(test_values); ++i) {
        size_t offset = i * block_size;
        size_t length = (i + 1U == sizeof(test_values))
                            ? config->display_size - offset
                            : block_size;
        memset(test_buf + offset, test_values[i], length);
    }

    uint8_t target_count = (config->type == EPD_TYPE_800_480_4S_75_2 ||
                            config->type == EPD_TYPE_800_480_4S_75_3)
                               ? 2U
                               : 1U;
    esp_err_t ret = ESP_OK;
    for (uint8_t target = 1U; target <= target_count && ret == ESP_OK; ++target) {
        ret = ServerNetworkStaEpdDisplay_QueueToScreenAndWait(test_buf,
                                                              config->display_size,
                                                              target);
    }
    heap_caps_free(test_buf);
    return ret;
}




static void log_epd_test_config(uint8_t requested_type)
{
    const epd_type_config_t *requested = EpdType_GetConfig(requested_type);
    const epd_type_config_t *active = EpdType_GetCurrentConfig();

    ESP_LOGI(TAG,
             "EPD test requested=%s(%u) resolution=%ux%u expected=%u active=%s(%u)",
             requested != NULL ? requested->name : "INVALID",
             (unsigned int)requested_type,
             requested != NULL ? (unsigned int)requested->width : 0U,
             requested != NULL ? (unsigned int)requested->height : 0U,
             requested != NULL ? (unsigned int)requested->display_size : 0U,
             active != NULL ? active->name : "INVALID",
             (unsigned int)EPD_type);

    if (requested_type != EPD_type) {
        ESP_LOGW(TAG, "EPD test type mismatch requested=%u active=%u",
                 (unsigned int)requested_type,
                 (unsigned int)EPD_type);
    }
}

static void test_epd_display_type(uint8_t requested_type)
{
    static const uint8_t color_6_block_values[] = {
        0x00, 0x01, 0x02, 0x03, 0x05, 0x06, 0x02, 0x03, 0x05, 0x06
    };
    static const uint8_t color_3_block_values[] = {
        0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff
    };
    static const uint8_t color_4_block_values[] = {
        0x00, 0xFF, 0x55, 0xAA, 0x00, 0xFF, 0x55, 0xAA, 0x00, 0xFF
    };
    static const size_t block_count = sizeof(color_6_block_values);
    const uint8_t *block_values = color_6_block_values;
    const epd_type_config_t *config = EpdType_GetConfig(requested_type);

    switch (requested_type) {
    case EPD_TYPE_800_480:
    case EPD_TYPE_1360_480_1085_3COLOR:
        block_values = color_3_block_values;
        break;
    case EPD_TYPE_1360_480_1085:
    case EPD_TYPE_800_480_4S_75:
        block_values = color_4_block_values;
        break;
    default:
        break;
    }

    log_epd_test_config(requested_type);
    ServerNetworkStaWifiWorkTime_OnNetworkData();
    if (config == NULL || config->display_size == 0) {
        ESP_LOGE(TAG, "EPD test invalid requested type=%u", (unsigned int)requested_type);
        return;
    }

    const size_t test_size = config->display_size;
    const size_t block_size = test_size / block_count;
    uint8_t *test_buf = (uint8_t *)heap_caps_malloc(test_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (test_buf == NULL) {
        ESP_LOGE(TAG, "EPD test alloc failed name=%s size=%u",
                 config->name, (unsigned int)test_size);
        return;
    }

    for (size_t i = 0; i < block_count; ++i) {
        memset(test_buf + (i * block_size), block_values[i], block_size);
    }
    memset(test_buf + (block_count * block_size),
           block_values[block_count - 1],
           test_size - (block_count * block_size));

    uint8_t target_count = (requested_type == EPD_TYPE_800_480_4S_75) ? 2U : 1U;
    for (uint8_t target = 1; target <= target_count; ++target) {
        uint8_t *target_buf = test_buf;
        if (target > 1U) {
            target_buf = (uint8_t *)heap_caps_malloc(test_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (target_buf == NULL) {
                ESP_LOGE(TAG, "EPD test alloc failed target=%u name=%s size=%u",
                         (unsigned int)target,
                         config->name,
                         (unsigned int)test_size);
                return;
            }
            memcpy(target_buf, test_buf, test_size);
        }

        epd_display_job_t job = {};
        job.data = target_buf;
        job.size = test_size;
        job.epd_which_one = target;

        if (s_epd_display_queue == NULL ||
            xQueueSend(s_epd_display_queue, &job, 0) != pdTRUE) {
            ESP_LOGE(TAG, "EPD test queue failed target=%u name=%s size=%u",
                     (unsigned int)target,
                     config->name,
                     (unsigned int)test_size);
            release_epd_job(&job);
            return;
        }

        ESP_LOGI(TAG, "EPD test queued target=%u name=%s type=%u size=%u",
                 (unsigned int)target,
                 config->name,
                 (unsigned int)requested_type,
                 (unsigned int)test_size);
    }
}

void test_epd_display_EPD_1600_1200_79(void)
{
    test_epd_display_type(EPD_TYPE_1600_1200_79);
}


void test_epd_display_EPD_1600_1200_133(void)
{
    test_epd_display_type(EPD_TYPE_1600_1200_133);
}

void test_epd_display_EPD_1600_1200_133_DKE(void)
{
    test_epd_display_type(EPD_TYPE_1600_1200_133_DKE);
}



void test_epd_display_EPD_EPD_1024_600(void)
{
    test_epd_display_type(EPD_TYPE_1024_600);
}

void test_epd_display_EPD_800_480(void)
{
    test_epd_display_type(EPD_TYPE_800_480);
}

void test_epd_display_EPD_1360_480_1085(void)
{
    test_epd_display_type(EPD_TYPE_1360_480_1085);
}

void test_epd_display_EPD_800_480_4S_75_2(void)
{
    static const uint8_t color_4_block_values[] = {
        0x00, 0xFF, 0x55, 0xAA, 0x00, 0xFF, 0x55, 0xAA, 0x00, 0xFF
    };
    static const size_t block_count = sizeof(color_4_block_values);
    const epd_type_config_t *config = EpdType_GetConfig(EPD_TYPE_800_480_4S_75_2);

    log_epd_test_config(EPD_TYPE_800_480_4S_75_2);
    if (config == NULL || config->display_size == 0) {
        ESP_LOGE(TAG, "EPD DKE 4S test invalid requested type=%u", (unsigned int)EPD_TYPE_800_480_4S_75_2);
        return;
    }
    if (config->display_size != 96000U) {
        ESP_LOGE(TAG, "EPD DKE 4S test size invalid input=%u expected=%u",
                 (unsigned int)config->display_size,
                 (unsigned int)96000U);
        return;
    }

    const size_t test_size = config->display_size;
    const size_t block_size = test_size / block_count;
    for (uint8_t target = 1U; target <= 2U; ++target) {
        uint8_t *test_buf = (uint8_t *)heap_caps_malloc(test_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (test_buf == NULL) {
            ESP_LOGE(TAG, "EPD DKE 4S test alloc failed target=%u size=%u",
                     (unsigned int)target,
                     (unsigned int)test_size);
            return;
        }

        // English: Fill one independent test buffer for each DKE screen target.
        // Chinese: Create one independent test image buffer for each DKE screen target.
        for (size_t i = 0; i < block_count; ++i) {
            memset(test_buf + (i * block_size), color_4_block_values[i], block_size);
        }
        memset(test_buf + (block_count * block_size),
               color_4_block_values[block_count - 1],
               test_size - (block_count * block_size));

        epd_display_job_t job = {};
        job.data = test_buf;
        job.size = test_size;
        job.epd_which_one = target;

        if (s_epd_display_queue == NULL ||
            xQueueSend(s_epd_display_queue, &job, 0) != pdTRUE) {
            ESP_LOGE(TAG, "EPD DKE 4S test queue failed target=%u size=%u",
                     (unsigned int)target,
                     (unsigned int)test_size);
            release_epd_job(&job);
            return;
        }

        ESP_LOGI(TAG, "EPD DKE 4S test queued target=%u size=%u",
                 (unsigned int)target,
                 (unsigned int)test_size);
    }
}

void test_epd_display_EPD_800_480_4S_75_3(void)
{
    static const uint8_t color_4_block_values[] = {
        0x00, 0xFF, 0x55, 0xAA, 0x00, 0xFF, 0x55, 0xAA, 0x00, 0xFF
    };
    static const size_t block_count = sizeof(color_4_block_values);
    const epd_type_config_t *config = EpdType_GetConfig(EPD_TYPE_800_480_4S_75_3);

    log_epd_test_config(EPD_TYPE_800_480_4S_75_3);
    if (config == NULL || config->display_size == 0) {
        ESP_LOGE(TAG, "EPD mofang 4S test invalid requested type=%u", (unsigned int)EPD_TYPE_800_480_4S_75_3);
        return;
    }
    if (config->display_size != 96000U) {
        ESP_LOGE(TAG, "EPD mofang 4S test size invalid input=%u expected=%u",
                 (unsigned int)config->display_size,
                 (unsigned int)96000U);
        return;
    }

    const size_t test_size = config->display_size;
    const size_t block_size = test_size / block_count;
    for (uint8_t target = 1U; target <= 2U; ++target) {
        uint8_t *test_buf = (uint8_t *)heap_caps_malloc(test_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (test_buf == NULL) {
            ESP_LOGE(TAG, "EPD mofang 4S test alloc failed target=%u size=%u",
                     (unsigned int)target,
                     (unsigned int)test_size);
            return;
        }

        // English: Fill one independent test buffer for each mofang screen target.
        // Chinese: Create one independent test image buffer for each mofang screen target.
        for (size_t i = 0; i < block_count; ++i) {
            memset(test_buf + (i * block_size), color_4_block_values[i], block_size);
        }
        memset(test_buf + (block_count * block_size),
               color_4_block_values[block_count - 1],
               test_size - (block_count * block_size));

        epd_display_job_t job = {};
        job.data = test_buf;
        job.size = test_size;
        job.epd_which_one = target;

        if (s_epd_display_queue == NULL ||
            xQueueSend(s_epd_display_queue, &job, 0) != pdTRUE) {
            ESP_LOGE(TAG, "EPD mofang 4S test queue failed target=%u size=%u",
                     (unsigned int)target,
                     (unsigned int)test_size);
            release_epd_job(&job);
            return;
        }

        ESP_LOGI(TAG, "EPD mofang 4S test queued target=%u size=%u",
                 (unsigned int)target,
                 (unsigned int)test_size);
    }
}

void test_epd_display_EPD_1360_480_1085_3COLOR_horizontal(void)
{
    const epd_type_config_t *config = EpdType_GetConfig(EPD_TYPE_1360_480_1085_3COLOR);
    static const uint8_t black_plane_by_color[] = {
        0x00, 0xff, 0xff
    };
    static const uint8_t red_plane_by_color[] = {
        0x00, 0xff, 0x00
    };

    log_epd_test_config(EPD_TYPE_1360_480_1085_3COLOR);
    if (config == NULL || config->display_size == 0) {
        ESP_LOGE(TAG, "EPD test invalid requested type=%u", (unsigned int)EPD_TYPE_1360_480_1085_3COLOR);
        return;
    }
    if (config->display_size != (4U * 40800U)) {
        ESP_LOGE(TAG, "EPD test 3color size invalid input=%u expected=%u",
                 (unsigned int)config->display_size,
                 (unsigned int)(4U * 40800U));
        return;
    }

    const size_t test_size = config->display_size;
    const size_t plane_size = test_size / 2U;
    const size_t block_size = plane_size / 10U;
    uint8_t *test_buf = (uint8_t *)heap_caps_malloc(test_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (test_buf == NULL) {
        ESP_LOGE(TAG, "EPD test alloc failed name=%s size=%u",
                 config->name, (unsigned int)test_size);
        return;
    }

    // Fill both EPD planes by the same 10 percent color bands used by the vendor Display_All mapping.
    // 中文：按原厂 Display_All 的颜色映射，以相同的 10% 色块同时填充黑白平面和红色平面。
    for (size_t offset = 0; offset < plane_size; offset += block_size) {
        size_t copy_len = block_size;
        if (copy_len > plane_size - offset) {
            copy_len = plane_size - offset;
        }
        size_t color_index = (offset / block_size) % 3U;
        memset(test_buf + offset, black_plane_by_color[color_index], copy_len);
        memset(test_buf + plane_size + offset, red_plane_by_color[color_index], copy_len);
    }

    epd_display_job_t job = {};
    job.data = test_buf;
    job.size = test_size;
    job.epd_which_one = 1;

    if (s_epd_display_queue == NULL ||
        xQueueSend(s_epd_display_queue, &job, 0) != pdTRUE) {
        ESP_LOGE(TAG, "EPD test queue failed name=%s size=%u",
                 config->name, (unsigned int)test_size);
        release_epd_job(&job);
        return;
    }

    ESP_LOGI(TAG, "EPD test queued name=%s type=%u size=%u",
             config->name,
             (unsigned int)EPD_TYPE_1360_480_1085_3COLOR,
             (unsigned int)test_size);
}

void test_epd_display_EPD_1360_480_1085_3COLOR_vertical(void)
{
    const epd_type_config_t *config = EpdType_GetConfig(EPD_TYPE_1360_480_1085_3COLOR);
    static const uint8_t black_plane_by_color[] = {
        0x00, 0xff, 0xff
    };
    static const uint8_t red_plane_by_color[] = {
        0x00, 0xff, 0x00
    };

    log_epd_test_config(EPD_TYPE_1360_480_1085_3COLOR);
    if (config == NULL || config->display_size == 0) {
        ESP_LOGE(TAG, "EPD vertical test invalid requested type=%u", (unsigned int)EPD_TYPE_1360_480_1085_3COLOR);
        return;
    }
    if (config->display_size != (4U * 40800U)) {
        ESP_LOGE(TAG, "EPD vertical test 3color size invalid input=%u expected=%u",
                 (unsigned int)config->display_size,
                 (unsigned int)(4U * 40800U));
        return;
    }

    const size_t test_size = config->display_size;
    const size_t plane_size = test_size / 2U;
    const size_t source_bytes = 85U * 2U;
    const size_t gate_bits = 480U;
    const size_t block_gate_bits = gate_bits / 10U;
    uint8_t *test_buf = (uint8_t *)heap_caps_malloc(test_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (test_buf == NULL) {
        ESP_LOGE(TAG, "EPD vertical test alloc failed name=%s size=%u",
                 config->name, (unsigned int)test_size);
        return;
    }

    // Fill gate-position bands inside each source byte stream to rotate the test bands from the horizontal pattern.
    // 中文：按每个 source 字节流内部的 Gate 位置切换颜色，用于把 horizontal 测试图案转成另一方向。
    for (size_t source = 0; source < source_bytes; ++source) {
        size_t source_offset = source * gate_bits;
        for (size_t row = 0; row < gate_bits; ++row) {
            size_t color_index = (row / block_gate_bits) % 3U;
            size_t offset = source_offset + row;
            test_buf[offset] = black_plane_by_color[color_index];
            test_buf[plane_size + offset] = red_plane_by_color[color_index];
        }
    }

    epd_display_job_t job = {};
    job.data = test_buf;
    job.size = test_size;
    job.epd_which_one = 1;

    if (s_epd_display_queue == NULL ||
        xQueueSend(s_epd_display_queue, &job, 0) != pdTRUE) {
        ESP_LOGE(TAG, "EPD vertical test queue failed name=%s size=%u",
                 config->name, (unsigned int)test_size);
        release_epd_job(&job);
        return;
    }

    ESP_LOGI(TAG, "EPD vertical test queued name=%s type=%u size=%u",
             config->name,
             (unsigned int)EPD_TYPE_1360_480_1085_3COLOR,
             (unsigned int)test_size);
}

void test_epd_display_EPD_1360_480_1085_3COLOR_const(void)
{
    const epd_type_config_t *config = EpdType_GetConfig(EPD_TYPE_1360_480_1085_3COLOR);
    const size_t plane_size = g_epd_test_1360_480_1085_3color_plane_size;
    const size_t test_size = g_epd_test_1360_480_1085_3color_image_size;

    log_epd_test_config(EPD_TYPE_1360_480_1085_3COLOR);
    if (config == NULL || config->display_size == 0) {
        ESP_LOGE(TAG, "EPD const test invalid requested type=%u", (unsigned int)EPD_TYPE_1360_480_1085_3COLOR);
        return;
    }
    if (config->display_size != test_size) {
        ESP_LOGE(TAG, "EPD const test size invalid input=%u expected=%u",
                 (unsigned int)config->display_size,
                 (unsigned int)test_size);
        return;
    }
    if (g_epd_test_1360_480_1085_3color_display_b_size == 0 ||
        g_epd_test_1360_480_1085_3color_display_r_size == 0 ||
        test_size != (plane_size * 2U)) {
        ESP_LOGE(TAG, "EPD const data invalid b=%u r=%u plane=%u image=%u",
                 (unsigned int)g_epd_test_1360_480_1085_3color_display_b_size,
                 (unsigned int)g_epd_test_1360_480_1085_3color_display_r_size,
                 (unsigned int)plane_size,
                 (unsigned int)test_size);
        return;
    }

    uint8_t *test_buf = (uint8_t *)heap_caps_malloc(test_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (test_buf == NULL) {
        ESP_LOGE(TAG, "EPD const test alloc failed name=%s size=%u",
                 config->name, (unsigned int)test_size);
        return;
    }

    const size_t half_size = plane_size / 2U;

    // English: Build B_L + R_L + B_R + R_R, repeating source constants when the data is short.
    // 中文：生成 B_L + R_L + B_R + R_R，常数数据不足时循环重复填满。
    for (size_t i = 0; i < half_size; ++i) {
        test_buf[i] = g_epd_test_1360_480_1085_3color_display_b[i % g_epd_test_1360_480_1085_3color_display_b_size];
        test_buf[half_size + i] = g_epd_test_1360_480_1085_3color_display_r[i % g_epd_test_1360_480_1085_3color_display_r_size];
        test_buf[(half_size * 2U) + i] =
            g_epd_test_1360_480_1085_3color_display_b[(half_size + i) % g_epd_test_1360_480_1085_3color_display_b_size];
        test_buf[(half_size * 3U) + i] =
            g_epd_test_1360_480_1085_3color_display_r[(half_size + i) % g_epd_test_1360_480_1085_3color_display_r_size];
    }

    epd_display_job_t job = {};
    job.data = test_buf;
    job.size = test_size;
    job.epd_which_one = 1;

    if (s_epd_display_queue == NULL ||
        xQueueSend(s_epd_display_queue, &job, 0) != pdTRUE) {
        ESP_LOGE(TAG, "EPD const test queue failed name=%s size=%u",
                 config->name, (unsigned int)test_size);
        release_epd_job(&job);
        return;
    }

    ESP_LOGI(TAG, "EPD const test queued name=%s type=%u size=%u plane=%u",
             config->name,
             (unsigned int)EPD_TYPE_1360_480_1085_3COLOR,
             (unsigned int)test_size,
             (unsigned int)plane_size);
}

void test_epd_display(void)
{
    switch (EPD_type) {
    case EPD_TYPE_800_480:
        test_epd_display_EPD_800_480();
        break;
    case EPD_TYPE_1024_600:
         test_epd_display_EPD_EPD_1024_600();
        break;

    case EPD_TYPE_1600_1200_79:
        test_epd_display_EPD_1600_1200_79();
        break;

    case EPD_TYPE_1600_1200_133:
        test_epd_display_EPD_1600_1200_133();
        break;

    case EPD_TYPE_1600_1200_133_DKE:
        test_epd_display_EPD_1600_1200_133_DKE();
        break;

    case EPD_TYPE_1360_480_1085:
        test_epd_display_EPD_1360_480_1085();
        break;
    case EPD_TYPE_800_480_4S_75:
        test_epd_display_type(EPD_TYPE_800_480_4S_75);
        break;
    case EPD_TYPE_800_480_4S_75_2:
        test_epd_display_EPD_800_480_4S_75_2();
        break;
    case EPD_TYPE_800_480_4S_75_3:
        test_epd_display_EPD_800_480_4S_75_3();
        break;
    case EPD_TYPE_1360_480_1085_3COLOR:
        // test_epd_display_EPD_1360_480_1085_3COLOR_horizontal();
        // test_epd_display_EPD_1360_480_1085_3COLOR_vertical();
        test_epd_display_EPD_1360_480_1085_3COLOR_const();
        break;
    default:
        ESP_LOGE(TAG, "display rejected invalid EPD type=%u", (unsigned int)EPD_type);
        break;
    }
}
