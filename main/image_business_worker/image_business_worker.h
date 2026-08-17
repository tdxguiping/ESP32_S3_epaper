#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IMAGE_BUSINESS_OWNER_NONE = 0,
    IMAGE_BUSINESS_OWNER_DAILY,
    IMAGE_BUSINESS_OWNER_SLIDESHOW,
} image_business_owner_t;

typedef esp_err_t (*image_business_run_fn_t)(const void *payload,
                                             size_t payload_size);
typedef void (*image_business_cancel_fn_t)(const void *payload,
                                           size_t payload_size);

esp_err_t ImageBusinessWorker_Init(void);
esp_err_t ImageBusinessWorker_Submit(image_business_owner_t owner,
                                     image_business_run_fn_t run,
                                     image_business_cancel_fn_t cancel,
                                     const void *payload,
                                     size_t payload_size,
                                     uint32_t generation);
bool ImageBusinessWorker_CancelPending(image_business_owner_t owner);
bool ImageBusinessWorker_IsOwnerBusy(image_business_owner_t owner);
esp_err_t ImageBusinessWorker_WaitOwnerIdle(image_business_owner_t owner,
                                            TickType_t timeout_ticks);
bool ImageBusinessWorker_IsCurrentTask(void);
void ImageBusinessWorker_Wake(void);
bool ImageBusinessWorker_WaitInterruptible(TickType_t timeout_ticks);

#ifdef __cplusplus
}
#endif
