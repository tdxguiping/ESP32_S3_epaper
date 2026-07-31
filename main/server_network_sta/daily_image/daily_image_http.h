#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "daily_image_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*daily_image_continue_fn_t)(void);

esp_err_t DailyImageHttp_SelectDownloadUrl(
    const daily_image_config_t *config,
    char *download_url,
    size_t download_url_size,
    daily_image_continue_fn_t should_continue);

esp_err_t DailyImageHttp_Download(
    const char *download_url,
    uint8_t *buffer,
    size_t buffer_size,
    bool exact_size_required,
    size_t *downloaded_size,
    daily_image_continue_fn_t should_continue);

#ifdef __cplusplus
}
#endif
