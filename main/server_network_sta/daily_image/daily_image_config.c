#include "daily_image_config.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "epd_type.h"
#include "nvs.h"

static const char *TAG = "daily_image_cfg";

static uint32_t daily_image_config_crc(const daily_image_config_t *config)
{
    daily_image_config_t copy;
    if (config == NULL) {
        return 0;
    }

    copy = *config;
    copy.crc32 = 0;
    return esp_crc32_le(0, (const uint8_t *)&copy, sizeof(copy));
}

static bool timestamp_is_reasonable(int64_t timestamp)
{
    if (timestamp <= 0 || (int64_t)(time_t)timestamp != timestamp) {
        return false;
    }
    time_t value = (time_t)timestamp;
    struct tm local = {0};
    localtime_r(&value, &local);
    return local.tm_year >= (2026 - 1900) && local.tm_year <= (2100 - 1900);
}

static bool json_u32(const cJSON *item, uint32_t *value)
{
    if (!cJSON_IsNumber(item) || value == NULL ||
        item->valuedouble <= 0 || item->valuedouble > UINT32_MAX) {
        return false;
    }
    uint32_t parsed = (uint32_t)item->valuedouble;
    if ((double)parsed != item->valuedouble) {
        return false;
    }
    *value = parsed;
    return true;
}

static bool json_i16(const cJSON *item, int16_t *value)
{
    if (!cJSON_IsNumber(item) || value == NULL ||
        item->valuedouble < INT16_MIN || item->valuedouble > INT16_MAX) {
        return false;
    }
    int16_t parsed = (int16_t)item->valuedouble;
    if ((double)parsed != item->valuedouble) {
        return false;
    }
    *value = parsed;
    return true;
}

static bool json_i64(const cJSON *item, int64_t *value)
{
    if (!cJSON_IsNumber(item) || value == NULL ||
        item->valuedouble <= 0.0 ||
        item->valuedouble > 4133980799.0) {
        return false;
    }
    int64_t parsed = (int64_t)item->valuedouble;
    if ((double)parsed != item->valuedouble) {
        return false;
    }
    *value = parsed;
    return true;
}

static bool json_switch(const cJSON *item, int *value)
{
    if (!cJSON_IsNumber(item) || value == NULL ||
        (item->valuedouble != 0.0 && item->valuedouble != 1.0)) {
        return false;
    }
    *value = (int)item->valuedouble;
    return true;
}

static void set_parse_error(int *result_code,
                            const char **error,
                            int code,
                            const char *error_text)
{
    if (result_code != NULL) {
        *result_code = code;
    }
    if (error != NULL) {
        *error = error_text;
    }
}

esp_err_t DailyImageConfig_Parse(const char *body,
                                 size_t body_len,
                                 daily_image_config_t *config,
                                 int *sw,
                                 int *result_code,
                                 const char **error)
{
    cJSON *root = NULL;
    cJSON *func = NULL;
    cJSON *height = NULL;
    cJSON *width = NULL;
    cJSON *orientation = NULL;
    cJSON *api_url = NULL;
    cJSON *timestamp = NULL;
    cJSON *sw_item = NULL;
    const epd_type_config_t *epd_config = NULL;

    if (body == NULL || body_len == 0 || config == NULL || sw == NULL) {
        set_parse_error(result_code, error, TDX_JSON_RESULT_JSON_INVALID, "invalid_json");
        return ESP_ERR_INVALID_ARG;
    }

    root = cJSON_ParseWithLength(body, body_len);
    if (root == NULL) {
        set_parse_error(result_code, error, TDX_JSON_RESULT_JSON_INVALID, "invalid_json");
        return ESP_ERR_INVALID_ARG;
    }

    func = cJSON_GetObjectItemCaseSensitive(root, "func");
    if (!cJSON_IsString(func) || func->valuestring == NULL ||
        strcmp(func->valuestring, "daily_download_file") != 0) {
        cJSON_Delete(root);
        return ESP_ERR_NOT_SUPPORTED;
    }

    *sw = -1;
    sw_item = cJSON_GetObjectItemCaseSensitive(root, "sw");
    if (!json_switch(sw_item, sw)) {
        set_parse_error(result_code, error,
                        TDX_JSON_RESULT_PARAM_INVALID,
                        "invalid_sw");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    memset(config, 0, sizeof(*config));
    if (*sw == 0) {
        if (result_code != NULL) {
            *result_code = TDX_JSON_RESULT_OK;
        }
        if (error != NULL) {
            *error = "no error";
        }
        ESP_LOGI(TAG, "parsed daily switch sw=0");
        cJSON_Delete(root);
        return ESP_OK;
    }

    height = cJSON_GetObjectItemCaseSensitive(root, "imageHeight");
    width = cJSON_GetObjectItemCaseSensitive(root, "imageWidth");
    orientation = cJSON_GetObjectItemCaseSensitive(root, "orientation");
    api_url = cJSON_GetObjectItemCaseSensitive(root, "api_url");
    timestamp = cJSON_GetObjectItemCaseSensitive(root, "timestamp");

    config->version = USER_DAILY_IMAGE_NVS_CONFIG_VERSION;
    config->struct_size = sizeof(*config);
    strlcpy(config->func, "daily_download_file", sizeof(config->func));

    if (!json_u32(height, &config->image_height) ||
        !json_u32(width, &config->image_width)) {
        set_parse_error(result_code, error,
                        TDX_JSON_RESULT_DAILY_IMAGE_SIZE_MISMATCH,
                        "invalid_image_size");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    epd_config = EpdType_GetCurrentConfig();
    if (epd_config == NULL ||
        config->image_height != epd_config->width ||
        config->image_width != epd_config->height) {
        ESP_LOGE(TAG,
                 "EPD size mismatch request_height=%u request_width=%u active_width=%u active_height=%u type=%u",
                 (unsigned int)config->image_height,
                 (unsigned int)config->image_width,
                 epd_config != NULL ? (unsigned int)epd_config->width : 0U,
                 epd_config != NULL ? (unsigned int)epd_config->height : 0U,
                 epd_config != NULL ? (unsigned int)epd_config->type : 0U);
        set_parse_error(result_code, error,
                        TDX_JSON_RESULT_DAILY_IMAGE_SIZE_MISMATCH,
                        "image_size_mismatch");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_SIZE;
    }

    if (!json_i16(orientation, &config->orientation)) {
        set_parse_error(result_code, error,
                        TDX_JSON_RESULT_DAILY_ORIENTATION_INVALID,
                        "invalid_orientation");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    if (!cJSON_IsString(api_url) || api_url->valuestring == NULL ||
        strncmp(api_url->valuestring, "https://", 8) != 0 ||
        strlen(api_url->valuestring) >= sizeof(config->api_url)) {
        set_parse_error(result_code, error,
                        TDX_JSON_RESULT_DAILY_API_URL_INVALID,
                        "invalid_api_url");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    strlcpy(config->api_url, api_url->valuestring, sizeof(config->api_url));

    if (!json_i64(timestamp, &config->timestamp) ||
        !timestamp_is_reasonable(config->timestamp)) {
        set_parse_error(result_code, error,
                        TDX_JSON_RESULT_DAILY_TIME_INVALID,
                        "invalid_timestamp");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    config->retry_pending = 0U;
    config->initial_run_pending = USER_DAILY_IMAGE_INITIAL_RUN_PENDING;
    config->last_daily_epd_epoch = 0U;
    config->retry_due_epoch = 0;
    config->retry_target_epoch = 0;
    config->last_completed_target_epoch = 0;
    config->crc32 = daily_image_config_crc(config);
    if (result_code != NULL) {
        *result_code = TDX_JSON_RESULT_OK;
    }
    if (error != NULL) {
        *error = "no error";
    }

    ESP_LOGI(TAG,
             "parsed sw=1 initial=%u type=%u imageHeight=%u imageWidth=%u bytes=%u orientation=%d timestamp=%lld api_url=%s",
             (unsigned int)config->initial_run_pending,
             (unsigned int)epd_config->type,
             (unsigned int)config->image_height,
             (unsigned int)config->image_width,
             (unsigned int)epd_config->display_size,
             (int)config->orientation,
             (long long)config->timestamp,
             config->api_url);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t validate_loaded_config(const daily_image_config_t *config)
{
    if (config == NULL ||
        config->version != USER_DAILY_IMAGE_NVS_CONFIG_VERSION ||
        config->struct_size != sizeof(*config) ||
        config->crc32 != daily_image_config_crc(config) ||
        strcmp(config->func, "daily_download_file") != 0 ||
        config->image_height == 0 ||
        config->image_width == 0 ||
        !timestamp_is_reasonable(config->timestamp) ||
        config->retry_pending > 1U ||
        config->initial_run_pending > USER_DAILY_IMAGE_INITIAL_RUN_PENDING ||
        (config->last_daily_epd_epoch != 0U &&
         !timestamp_is_reasonable((int64_t)config->last_daily_epd_epoch)) ||
        (config->retry_pending != 0U &&
         (!timestamp_is_reasonable(config->retry_due_epoch) ||
          !timestamp_is_reasonable(config->retry_target_epoch))) ||
        (config->retry_pending == 0U &&
         (config->retry_due_epoch != 0 || config->retry_target_epoch != 0)) ||
        (config->last_completed_target_epoch != 0 &&
         !timestamp_is_reasonable(config->last_completed_target_epoch)) ||
        strncmp(config->api_url, "https://", 8) != 0 ||
        strnlen(config->api_url, sizeof(config->api_url)) >= sizeof(config->api_url)) {
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}

esp_err_t DailyImageConfig_Save(const daily_image_config_t *config)
{
    if (config == NULL || validate_loaded_config(config) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = app_nvs_write_blob(USER_DAILY_IMAGE_NVS_KEY, config, sizeof(*config));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS write failed key=%s ret=%s",
                 USER_DAILY_IMAGE_NVS_KEY, esp_err_to_name(ret));
        return ret;
    }

    daily_image_config_t verify = {0};
    ret = app_nvs_read_blob(USER_DAILY_IMAGE_NVS_KEY, &verify, sizeof(verify));
    if (ret != ESP_OK || validate_loaded_config(&verify) != ESP_OK ||
        memcmp(config, &verify, sizeof(verify)) != 0) {
        ESP_LOGE(TAG, "NVS verify failed key=%s ret=%s",
                 USER_DAILY_IMAGE_NVS_KEY, esp_err_to_name(ret));
        return ret != ESP_OK ? ret : ESP_FAIL;
    }

    ESP_LOGI(TAG, "NVS saved key=%s bytes=%u crc=0x%08lx",
             USER_DAILY_IMAGE_NVS_KEY,
             (unsigned int)sizeof(*config),
             (unsigned long)config->crc32);
    return ESP_OK;
}

esp_err_t DailyImageConfig_Load(daily_image_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(config, 0, sizeof(*config));
    esp_err_t ret = app_nvs_read_blob(USER_DAILY_IMAGE_NVS_KEY, config, sizeof(*config));
    if (ret != ESP_OK) {
        if (ret != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "NVS load failed key=%s ret=%s",
                     USER_DAILY_IMAGE_NVS_KEY, esp_err_to_name(ret));
        }
        return ret;
    }

    ret = validate_loaded_config(config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS config invalid key=%s ret=%s",
                 USER_DAILY_IMAGE_NVS_KEY, esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t DailyImageConfig_Erase(void)
{
    return app_nvs_erase_key(USER_DAILY_IMAGE_NVS_KEY);
}

esp_err_t DailyImageConfig_SaveRetryState(daily_image_config_t *config,
                                          bool retry_pending,
                                          int64_t retry_due_epoch,
                                          int64_t retry_target_epoch)
{
    if (config == NULL ||
        (retry_pending &&
         (!timestamp_is_reasonable(retry_due_epoch) ||
          !timestamp_is_reasonable(retry_target_epoch)))) {
        return ESP_ERR_INVALID_ARG;
    }

    config->retry_pending = retry_pending ? 1U : 0U;
    config->retry_due_epoch = retry_pending ? retry_due_epoch : 0;
    config->retry_target_epoch = retry_pending ? retry_target_epoch : 0;
    config->crc32 = daily_image_config_crc(config);
    return DailyImageConfig_Save(config);
}

esp_err_t DailyImageConfig_SaveSuccessState(daily_image_config_t *config,
                                            int64_t completed_target_epoch)
{
    if (config == NULL || !timestamp_is_reasonable(completed_target_epoch)) {
        return ESP_ERR_INVALID_ARG;
    }

    config->retry_pending = 0U;
    config->retry_due_epoch = 0;
    config->retry_target_epoch = 0;
    config->last_completed_target_epoch = completed_target_epoch;
    config->crc32 = daily_image_config_crc(config);
    return DailyImageConfig_Save(config);
}

esp_err_t DailyImageConfig_SaveDisplayStartState(
    daily_image_config_t *config,
    int64_t display_start_epoch)
{
    if (config == NULL || !timestamp_is_reasonable(display_start_epoch) ||
        display_start_epoch > UINT32_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    config->last_daily_epd_epoch = (uint32_t)display_start_epoch;
    config->crc32 = daily_image_config_crc(config);
    return DailyImageConfig_Save(config);
}

esp_err_t DailyImageConfig_SaveInitialSuccessState(
    daily_image_config_t *config)
{
    if (config == NULL ||
        config->initial_run_pending !=
            USER_DAILY_IMAGE_INITIAL_RUN_PENDING) {
        return ESP_ERR_INVALID_ARG;
    }

    config->retry_pending = 0U;
    config->initial_run_pending = USER_DAILY_IMAGE_INITIAL_RUN_DONE;
    config->retry_due_epoch = 0;
    config->retry_target_epoch = 0;
    /*
     * The APP-triggered immediate display is not a timestamp slot. Leave the
     * slot unfinished so it can run later with the minimum-interval guard.
     */
    config->last_completed_target_epoch = 0;
    config->crc32 = daily_image_config_crc(config);
    return DailyImageConfig_Save(config);
}

void DailyImageConfig_FormatEpoch(int64_t epoch, char *text, size_t text_size)
{
    if (text == NULL || text_size == 0) {
        return;
    }
    if (!timestamp_is_reasonable(epoch)) {
        text[0] = '\0';
        return;
    }
    time_t value = (time_t)epoch;
    struct tm local = {0};
    localtime_r(&value, &local);
    strftime(text, text_size, "%Y-%m-%d %H:%M:%S", &local);
}
