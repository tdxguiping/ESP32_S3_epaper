#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "tdx_cfg.h"

// Persistent EPD mode assigned to local SD-card image browsing.
#define USER_EPD_DISPLAY_MODE_LOCAL_IMAGE_BROWSING 3U

// Local browsing is intentionally pinned to the SD-card mount and BIN directory.
#define LOCAL_IMAGE_BROWSING_BASE_PATH "/data"
#define LOCAL_IMAGE_BROWSING_BIN_DIRECTORY_PATH "/data/bin_img"
#define LOCAL_IMAGE_BROWSING_FILE_EXTENSION ".bin"
#define LOCAL_IMAGE_BROWSING_KEY_EVENT_ARG "PB2,PRESS"
#define LOCAL_IMAGE_BROWSING_FILE_NAME_MAX_LEN TDX_IMAGE_BASE_NAME_BUFFER_SIZE

// Keep the cursor in NVS so a power cycle continues from the same image position.
#define LOCAL_IMAGE_BROWSING_NVS_STATE_KEY "local_img_state"
#define LOCAL_IMAGE_BROWSING_STATE_VERSION 2U
#define LOCAL_IMAGE_BROWSING_TRANSACTION_IDLE 0U
#define LOCAL_IMAGE_BROWSING_TRANSACTION_PREPARED 1U

// Only one runtime request is accepted because an accepted request reserves the EPD.
#define LOCAL_IMAGE_BROWSING_QUEUE_LENGTH 1U
#define LOCAL_IMAGE_BROWSING_STARTUP_QUEUE_LENGTH 10U
#define LOCAL_IMAGE_BROWSING_TASK_STACK_SIZE (8U * 1024U)
#define LOCAL_IMAGE_BROWSING_TASK_PRIORITY 4U
// Warn only when runtime stack headroom becomes too small for safe maintenance.
#define LOCAL_IMAGE_BROWSING_STACK_WARNING_BYTES 1024U

typedef enum {
    LOCAL_IMAGE_BROWSING_TRIGGER_DEVICE_INFO_KEY_PB2 = 0,
    LOCAL_IMAGE_BROWSING_TRIGGER_KEY_EVENT_PB2_PRESS = 1,
} local_image_browsing_trigger_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t LocalImageBrowsing_Init(const char *base_path);
esp_err_t LocalImageBrowsing_RequestNext(local_image_browsing_trigger_t trigger,
                                         uint16_t protocol_seq);
bool LocalImageBrowsing_IsActive(void);
esp_err_t LocalImageBrowsing_ResetState(void);

#ifdef __cplusplus
}
#endif
