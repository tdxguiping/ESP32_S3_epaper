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
    uint32_t version;
    uint32_t struct_size;
    uint32_t crc32;
    char func[USER_DAILY_IMAGE_FUNC_BUFFER_SIZE];
    uint32_t image_height;
    uint32_t image_width;
    int16_t orientation;
    uint16_t reserved0;
    int64_t timestamp;
    uint8_t retry_pending;
    uint8_t initial_run_pending;
    uint8_t reserved1[2];
    uint32_t last_daily_epd_epoch;
    int64_t retry_due_epoch;
    int64_t retry_target_epoch;
    int64_t last_completed_target_epoch;
    char api_url[USER_DAILY_IMAGE_API_URL_BUFFER_SIZE];
} daily_image_config_t;

esp_err_t DailyImageConfig_Parse(const char *body,
                                 size_t body_len,
                                 daily_image_config_t *config,
                                 int *sw,
                                 int *result_code,
                                 const char **error);
esp_err_t DailyImageConfig_Save(const daily_image_config_t *config);
esp_err_t DailyImageConfig_Load(daily_image_config_t *config);
esp_err_t DailyImageConfig_Erase(void);
esp_err_t DailyImageConfig_SaveRetryState(daily_image_config_t *config,
                                          bool retry_pending,
                                          int64_t retry_due_epoch,
                                          int64_t retry_target_epoch);
esp_err_t DailyImageConfig_SaveSuccessState(daily_image_config_t *config,
                                            int64_t completed_target_epoch);
esp_err_t DailyImageConfig_SaveDisplayStartState(
    daily_image_config_t *config,
    int64_t display_start_epoch);
esp_err_t DailyImageConfig_SaveInitialSuccessState(
    daily_image_config_t *config);
void DailyImageConfig_FormatEpoch(int64_t epoch, char *text, size_t text_size);

#ifdef __cplusplus
}
#endif
