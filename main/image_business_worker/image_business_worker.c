#include "image_business_worker.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tdx_cfg.h"

static const char *TAG = "image_worker";

typedef struct {
    bool valid;
    image_business_owner_t owner;
    image_business_run_fn_t run;
    image_business_cancel_fn_t cancel;
    uint32_t generation;
    size_t payload_size;
    uint8_t payload[USER_IMAGE_BUSINESS_WORKER_PAYLOAD_SIZE];
} image_business_command_t;

#define IMAGE_BUSINESS_WORKER_STACK_DEPTH \
    (USER_IMAGE_BUSINESS_WORKER_STACK_SIZE / sizeof(StackType_t))

static StaticTask_t s_worker_tcb;
static StackType_t s_worker_stack[IMAGE_BUSINESS_WORKER_STACK_DEPTH];
static TaskHandle_t s_worker_task;
static StaticSemaphore_t s_state_mutex_control;
static SemaphoreHandle_t s_state_mutex;
static image_business_command_t s_pending_command;
static image_business_owner_t s_current_owner;
static bool s_initialized;

static const char *owner_name(image_business_owner_t owner)
{
    switch (owner) {
    case IMAGE_BUSINESS_OWNER_DAILY:
        return "DAILY";
    case IMAGE_BUSINESS_OWNER_SLIDESHOW:
        return "SLIDESHOW";
    case IMAGE_BUSINESS_OWNER_LOCAL_IMAGE:
        return "LOCAL_IMAGE";
    case IMAGE_BUSINESS_OWNER_CAST:
        return "CAST";
    case IMAGE_BUSINESS_OWNER_CAST2PIC:
        return "CAST2PIC";
    case IMAGE_BUSINESS_OWNER_USB_CAST:
        return "USB_CAST";
    case IMAGE_BUSINESS_OWNER_USB_CAST2PIC:
        return "USB_CAST2PIC";
    case IMAGE_BUSINESS_OWNER_FACTORY_RESET:
        return "FACTORY_RESET";
    default:
        return "NONE";
    }
}

static void log_stack_usage(image_business_owner_t owner,
                            uint32_t generation,
                            esp_err_t ret)
{
    UBaseType_t min_free = uxTaskGetStackHighWaterMark(NULL);
    size_t peak_used = min_free < USER_IMAGE_BUSINESS_WORKER_STACK_SIZE
                           ? USER_IMAGE_BUSINESS_WORKER_STACK_SIZE - min_free
                           : 0U;
    ESP_LOGI(TAG,
             "job done owner=%s generation=%lu ret=%s min_free=%u peak_used=%u configured=%u",
             owner_name(owner),
             (unsigned long)generation,
             esp_err_to_name(ret),
             (unsigned int)min_free,
             (unsigned int)peak_used,
             (unsigned int)USER_IMAGE_BUSINESS_WORKER_STACK_SIZE);
}

static void image_business_worker_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "started stack=%u priority=%u",
             (unsigned int)USER_IMAGE_BUSINESS_WORKER_STACK_SIZE,
             (unsigned int)USER_IMAGE_BUSINESS_WORKER_PRIORITY);

    for (;;) {
        image_business_command_t command = {0};
        if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
            if (s_pending_command.valid) {
                memcpy(&command, &s_pending_command, sizeof(command));
                memset(&s_pending_command, 0, sizeof(s_pending_command));
                s_current_owner = command.owner;
            }
            xSemaphoreGive(s_state_mutex);
        }

        if (!command.valid) {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        ESP_LOGI(TAG, "job start owner=%s generation=%lu",
                 owner_name(command.owner),
                 (unsigned long)command.generation);
        esp_err_t ret = command.run(command.payload, command.payload_size);

        if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE) {
            s_current_owner = IMAGE_BUSINESS_OWNER_NONE;
            xSemaphoreGive(s_state_mutex);
        }
        log_stack_usage(command.owner, command.generation, ret);
        memset(&command, 0, sizeof(command));
    }
}

esp_err_t ImageBusinessWorker_Init(void)
{
    if (s_initialized && s_worker_task != NULL && s_state_mutex != NULL) {
        return ESP_OK;
    }

    if (s_state_mutex == NULL) {
        s_state_mutex = xSemaphoreCreateMutexStatic(&s_state_mutex_control);
        if (s_state_mutex == NULL) {
            ESP_LOGE(TAG, "static state mutex create failed");
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(TAG,
             "static resources internal_free=%u internal_largest=%u stack=%u tcb=%u command=%u mutex=%u",
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                                   MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_largest_free_block(
                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned int)USER_IMAGE_BUSINESS_WORKER_STACK_SIZE,
             (unsigned int)sizeof(s_worker_tcb),
             (unsigned int)sizeof(s_pending_command),
             (unsigned int)sizeof(s_state_mutex_control));

    s_worker_task = xTaskCreateStatic(image_business_worker_task,
                                      "image_worker",
                                      IMAGE_BUSINESS_WORKER_STACK_DEPTH,
                                      NULL,
                                      USER_IMAGE_BUSINESS_WORKER_PRIORITY,
                                      s_worker_stack,
                                      &s_worker_tcb);
    if (s_worker_task == NULL) {
        ESP_LOGE(TAG, "static worker create failed");
        return ESP_ERR_NO_MEM;
    }
    s_initialized = true;
    return ESP_OK;
}

esp_err_t ImageBusinessWorker_SubmitReplacingPending(
    image_business_owner_t owner,
    image_business_run_fn_t run,
    image_business_cancel_fn_t cancel,
    const void *payload,
    size_t payload_size,
    uint32_t generation,
    uint32_t replace_pending_owner_mask)
{
    return ImageBusinessWorker_SubmitReplacingPendingUnlessBusy(
        owner,
        run,
        cancel,
        payload,
        payload_size,
        generation,
        replace_pending_owner_mask,
        0U);
}

esp_err_t ImageBusinessWorker_SubmitReplacingPendingUnlessBusy(
    image_business_owner_t owner,
    image_business_run_fn_t run,
    image_business_cancel_fn_t cancel,
    const void *payload,
    size_t payload_size,
    uint32_t generation,
    uint32_t replace_pending_owner_mask,
    uint32_t reject_busy_owner_mask)
{
    if (!s_initialized || s_worker_task == NULL || s_state_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (owner <= IMAGE_BUSINESS_OWNER_NONE ||
        owner > IMAGE_BUSINESS_OWNER_FACTORY_RESET || run == NULL ||
        payload_size > sizeof(s_pending_command.payload) ||
        (payload_size > 0U && payload == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (owner != IMAGE_BUSINESS_OWNER_FACTORY_RESET &&
        (s_current_owner == IMAGE_BUSINESS_OWNER_FACTORY_RESET ||
         (s_pending_command.valid &&
          s_pending_command.owner == IMAGE_BUSINESS_OWNER_FACTORY_RESET))) {
        ESP_LOGW(TAG,
                 "submit blocked by factory reset owner=%s pending_owner=%s current_owner=%s",
                 owner_name(owner),
                 s_pending_command.valid ? owner_name(s_pending_command.owner) : "NONE",
                 owner_name(s_current_owner));
        xSemaphoreGive(s_state_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if ((reject_busy_owner_mask & IMAGE_BUSINESS_OWNER_MASK(s_current_owner)) != 0U ||
        (s_pending_command.valid &&
         (reject_busy_owner_mask & IMAGE_BUSINESS_OWNER_MASK(s_pending_command.owner)) != 0U)) {
        ESP_LOGW(TAG,
                 "submit busy owner=%s pending_owner=%s current_owner=%s",
                 owner_name(owner),
                 s_pending_command.valid ? owner_name(s_pending_command.owner) : "NONE",
                 owner_name(s_current_owner));
        xSemaphoreGive(s_state_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_pending_command.valid &&
        (replace_pending_owner_mask &
         IMAGE_BUSINESS_OWNER_MASK(s_pending_command.owner)) == 0U) {
        ESP_LOGE(TAG,
                 "submit blocked owner=%s pending_owner=%s current_owner=%s",
                 owner_name(owner),
                 owner_name(s_pending_command.owner),
                 owner_name(s_current_owner));
        xSemaphoreGive(s_state_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    if (s_pending_command.valid) {
        image_business_owner_t replaced_owner = s_pending_command.owner;
        uint32_t replaced_generation = s_pending_command.generation;
        /* The state lock keeps the old payload valid until cancellation completes. */
        if (s_pending_command.cancel != NULL) {
            s_pending_command.cancel(s_pending_command.payload,
                                     s_pending_command.payload_size);
        }
        ESP_LOGI(TAG,
                 "pending replaced old_owner=%s old_generation=%lu new_owner=%s generation=%lu",
                 owner_name(replaced_owner),
                 (unsigned long)replaced_generation,
                 owner_name(owner),
                 (unsigned long)generation);
    }

    memset(&s_pending_command, 0, sizeof(s_pending_command));
    s_pending_command.valid = true;
    s_pending_command.owner = owner;
    s_pending_command.run = run;
    s_pending_command.cancel = cancel;
    s_pending_command.generation = generation;
    s_pending_command.payload_size = payload_size;
    if (payload_size > 0U) {
        memcpy(s_pending_command.payload, payload, payload_size);
    }
    xSemaphoreGive(s_state_mutex);
    xTaskNotifyGive(s_worker_task);
    return ESP_OK;
}

esp_err_t ImageBusinessWorker_Submit(image_business_owner_t owner,
                                     image_business_run_fn_t run,
                                     image_business_cancel_fn_t cancel,
                                     const void *payload,
                                     size_t payload_size,
                                     uint32_t generation)
{
    return ImageBusinessWorker_SubmitReplacingPending(owner,
                                                       run,
                                                       cancel,
                                                       payload,
                                                       payload_size,
                                                       generation,
                                                       0U);
}

bool ImageBusinessWorker_CancelPending(image_business_owner_t owner)
{
    image_business_command_t canceled = {0};
    if (!s_initialized || s_state_mutex == NULL ||
        owner == IMAGE_BUSINESS_OWNER_NONE ||
        xSemaphoreTake(s_state_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    if (s_pending_command.valid && s_pending_command.owner == owner) {
        memcpy(&canceled, &s_pending_command, sizeof(canceled));
        memset(&s_pending_command, 0, sizeof(s_pending_command));
    }
    xSemaphoreGive(s_state_mutex);

    if (canceled.valid) {
        if (canceled.cancel != NULL) {
            canceled.cancel(canceled.payload, canceled.payload_size);
        }
        ESP_LOGI(TAG, "pending canceled owner=%s generation=%lu",
                 owner_name(owner), (unsigned long)canceled.generation);
        ImageBusinessWorker_Wake();
        return true;
    }
    return false;
}

bool ImageBusinessWorker_IsOwnerBusy(image_business_owner_t owner)
{
    if (!s_initialized || s_state_mutex == NULL ||
        owner == IMAGE_BUSINESS_OWNER_NONE ||
        xSemaphoreTake(s_state_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    bool busy = s_current_owner == owner ||
                (s_pending_command.valid && s_pending_command.owner == owner);
    xSemaphoreGive(s_state_mutex);
    return busy;
}

bool ImageBusinessWorker_IsAnyOwnerBusy(uint32_t owner_mask)
{
    if (!s_initialized || s_state_mutex == NULL || owner_mask == 0U ||
        xSemaphoreTake(s_state_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    bool busy = (owner_mask & IMAGE_BUSINESS_OWNER_MASK(s_current_owner)) != 0U ||
                (s_pending_command.valid &&
                 (owner_mask & IMAGE_BUSINESS_OWNER_MASK(s_pending_command.owner)) != 0U);
    xSemaphoreGive(s_state_mutex);
    return busy;
}

image_business_owner_t ImageBusinessWorker_GetCurrentOwner(void)
{
    if (!s_initialized || s_state_mutex == NULL ||
        xSemaphoreTake(s_state_mutex, portMAX_DELAY) != pdTRUE) {
        return IMAGE_BUSINESS_OWNER_NONE;
    }
    image_business_owner_t owner = s_current_owner;
    xSemaphoreGive(s_state_mutex);
    return owner;
}

bool ImageBusinessWorker_HasPendingCommand(void)
{
    if (!s_initialized || s_state_mutex == NULL ||
        xSemaphoreTake(s_state_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    bool pending = s_pending_command.valid;
    xSemaphoreGive(s_state_mutex);
    return pending;
}

esp_err_t ImageBusinessWorker_WaitOwnerIdle(image_business_owner_t owner,
                                            TickType_t timeout_ticks)
{
    TickType_t start = xTaskGetTickCount();
    while (ImageBusinessWorker_IsOwnerBusy(owner)) {
        if ((xTaskGetTickCount() - start) >= timeout_ticks) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(50U));
    }
    return ESP_OK;
}

bool ImageBusinessWorker_IsCurrentTask(void)
{
    return s_worker_task != NULL && xTaskGetCurrentTaskHandle() == s_worker_task;
}

void ImageBusinessWorker_Wake(void)
{
    if (s_worker_task != NULL) {
        xTaskNotifyGive(s_worker_task);
    }
}

bool ImageBusinessWorker_WaitInterruptible(TickType_t timeout_ticks)
{
    if (!ImageBusinessWorker_IsCurrentTask()) {
        return false;
    }
    return ulTaskNotifyTake(pdTRUE, timeout_ticks) > 0U;
}
