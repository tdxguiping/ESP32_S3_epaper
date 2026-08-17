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
    IMAGE_BUSINESS_OWNER_LOCAL_IMAGE,
    IMAGE_BUSINESS_OWNER_CAST,
    IMAGE_BUSINESS_OWNER_CAST2PIC,
    IMAGE_BUSINESS_OWNER_USB_CAST,
    IMAGE_BUSINESS_OWNER_USB_CAST2PIC,
    IMAGE_BUSINESS_OWNER_FACTORY_RESET,
} image_business_owner_t;

#define IMAGE_BUSINESS_OWNER_MASK(owner) (1UL << (unsigned int)(owner))

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
esp_err_t ImageBusinessWorker_SubmitReplacingPending(
    image_business_owner_t owner,
    image_business_run_fn_t run,
    image_business_cancel_fn_t cancel,
    const void *payload,
    size_t payload_size,
    uint32_t generation,
    uint32_t replace_pending_owner_mask);
esp_err_t ImageBusinessWorker_SubmitReplacingPendingUnlessBusy(
    image_business_owner_t owner,
    image_business_run_fn_t run,
    image_business_cancel_fn_t cancel,
    const void *payload,
    size_t payload_size,
    uint32_t generation,
    uint32_t replace_pending_owner_mask,
    uint32_t reject_busy_owner_mask);
bool ImageBusinessWorker_CancelPending(image_business_owner_t owner);
bool ImageBusinessWorker_IsOwnerBusy(image_business_owner_t owner);
bool ImageBusinessWorker_IsAnyOwnerBusy(uint32_t owner_mask);
image_business_owner_t ImageBusinessWorker_GetCurrentOwner(void);
bool ImageBusinessWorker_HasPendingCommand(void);
esp_err_t ImageBusinessWorker_WaitOwnerIdle(image_business_owner_t owner,
                                             TickType_t timeout_ticks);
bool ImageBusinessWorker_IsCurrentTask(void);
void ImageBusinessWorker_Wake(void);
bool ImageBusinessWorker_WaitInterruptible(TickType_t timeout_ticks);

#ifdef __cplusplus
}
#endif
