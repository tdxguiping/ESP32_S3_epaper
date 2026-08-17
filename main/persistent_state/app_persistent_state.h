#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "tdx_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char file_names[TDX_SLIDESHOW_MAX_FILES][TDX_SLIDESHOW_FILE_NAME_MAX_LEN];
    size_t file_count;
    size_t start_index;
    uint32_t interval;
    bool random;
} app_persistent_slideshow_config_t;

typedef struct {
    bool enabled;
    uint32_t interval;
    bool random;
    int64_t timestamp;
    int64_t anchor_epoch;
} app_persistent_slideshow_control_t;

typedef struct {
    bool valid;
    char file_name[TDX_IMAGE_BASE_NAME_BUFFER_SIZE];
} app_persistent_last_cast_t;

esp_err_t AppPersistentState_Init(void);
esp_err_t AppPersistentState_SaveSlideshowState(
    const app_persistent_slideshow_config_t *config,
    const app_persistent_slideshow_control_t *control,
    bool *control_stage_reached);
esp_err_t AppPersistentState_LoadSlideshowConfig(
    app_persistent_slideshow_config_t *config,
    uint32_t *generation);
esp_err_t AppPersistentState_LoadSlideshowControl(
    app_persistent_slideshow_control_t *control,
    uint32_t *generation);
esp_err_t AppPersistentState_SaveSlideshowControl(
    const app_persistent_slideshow_control_t *control);
esp_err_t AppPersistentState_SaveLastCast(const char *file_name);
esp_err_t AppPersistentState_LoadLastCast(app_persistent_last_cast_t *last_cast);
esp_err_t AppPersistentState_EraseLastCast(void);
esp_err_t AppPersistentState_EraseImageState(void);

#ifdef __cplusplus
}
#endif
