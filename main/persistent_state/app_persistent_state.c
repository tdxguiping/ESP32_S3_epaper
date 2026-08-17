#include "app_persistent_state.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "app_nvs.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "app_persistent_state";
static SemaphoreHandle_t s_state_mutex;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t total_size;
    uint32_t generation;
    uint32_t interval;
    uint16_t file_count;
    uint16_t start_index;
    uint8_t random;
    uint8_t reserved[3];
    uint32_t crc32;
    char file_names[][TDX_SLIDESHOW_FILE_NAME_MAX_LEN];
} slideshow_config_record_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t generation;
    uint32_t interval;
    int64_t timestamp;
    int64_t anchor_epoch;
    uint8_t enabled;
    uint8_t random;
    uint8_t reserved[2];
    uint32_t crc32;
} slideshow_control_record_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t generation;
    char file_name[TDX_IMAGE_BASE_NAME_BUFFER_SIZE];
    uint8_t valid;
    uint8_t reserved[2];
    uint32_t crc32;
} last_cast_record_t;

static uint32_t record_crc32(void *record, size_t size, size_t crc_offset)
{
    uint32_t saved_crc = 0;
    uint8_t *bytes = (uint8_t *)record;
    memcpy(&saved_crc, bytes + crc_offset, sizeof(saved_crc));
    memset(bytes + crc_offset, 0, sizeof(saved_crc));
    uint32_t crc = esp_crc32_le(0, bytes, size);
    memcpy(bytes + crc_offset, &saved_crc, sizeof(saved_crc));
    return crc;
}

static bool image_base_name_is_safe(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return false;
    }
    if (strstr(name, "..") != NULL || strchr(name, '/') != NULL ||
        strchr(name, '\\') != NULL || strchr(name, '"') != NULL) {
        return false;
    }
    size_t len = strlen(name);
    if (len == 0 || len > TDX_IMAGE_BASE_NAME_MAX_BYTES) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        unsigned char value = (unsigned char)name[i];
        if (value < 0x20U || value > 0x7EU) {
            return false;
        }
    }
    return true;
}

static bool config_value_is_valid(const app_persistent_slideshow_config_t *config)
{
    if (config == NULL || config->file_count == 0 ||
        config->file_count > TDX_SLIDESHOW_MAX_FILES ||
        config->start_index >= config->file_count ||
        config->interval < TDX_SLIDESHOW_INTERVAL_MIN_SECONDS ||
        config->interval > TDX_SLIDESHOW_INTERVAL_MAX_SECONDS) {
        return false;
    }
    for (size_t i = 0; i < config->file_count; ++i) {
        if (!image_base_name_is_safe(config->file_names[i])) {
            return false;
        }
    }
    return true;
}

static bool control_value_is_valid(const app_persistent_slideshow_control_t *control)
{
    if (control == NULL ||
        control->interval < TDX_SLIDESHOW_INTERVAL_MIN_SECONDS ||
        control->interval > TDX_SLIDESHOW_INTERVAL_MAX_SECONDS) {
        return false;
    }
    if (control->enabled && (control->timestamp <= 0 || control->anchor_epoch <= 0)) {
        return false;
    }
    return true;
}

static esp_err_t lock_state(void)
{
    if (s_state_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE ?
           ESP_OK : ESP_ERR_TIMEOUT;
}

static void unlock_state(void)
{
    xSemaphoreGive(s_state_mutex);
}

static esp_err_t write_and_verify_blob(const char *key, const void *value, size_t size)
{
    esp_err_t ret = app_nvs_image_state_write_blob(key, value, size);
    if (ret != ESP_OK) {
        return ret;
    }
    void *verify = malloc(size);
    if (verify == NULL) {
        return ESP_ERR_NO_MEM;
    }
    size_t verify_size = size;
    ret = app_nvs_image_state_read_blob(key, verify, &verify_size);
    if (ret == ESP_OK && (verify_size != size || memcmp(value, verify, size) != 0)) {
        ret = ESP_ERR_INVALID_CRC;
    }
    free(verify);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS write verification failed key=%s ret=%s",
                 key, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t load_config_locked(app_persistent_slideshow_config_t *config,
                                    uint32_t *generation)
{
    size_t size = 0;
    esp_err_t ret = app_nvs_image_state_get_blob_size(
        APP_STATE_NVS_SLIDESHOW_CONFIG_KEY, &size);
    if (ret != ESP_OK) {
        return ret;
    }
    size_t header_size = offsetof(slideshow_config_record_t, file_names);
    if (size < header_size || size > APP_STATE_SLIDESHOW_CONFIG_BLOB_MAX_SIZE) {
        ESP_LOGE(TAG, "slideshow config NVS size invalid size=%u",
                 (unsigned int)size);
        return ESP_ERR_INVALID_SIZE;
    }
    slideshow_config_record_t *record =
        (slideshow_config_record_t *)calloc(1, size);
    if (record == NULL) {
        return ESP_ERR_NO_MEM;
    }
    size_t read_size = size;
    ret = app_nvs_image_state_read_blob(APP_STATE_NVS_SLIDESHOW_CONFIG_KEY,
                                      record,
                                      &read_size);
    bool file_count_valid = record->file_count > 0 &&
                            record->file_count <= TDX_SLIDESHOW_MAX_FILES;
    size_t expected_size = file_count_valid ?
                           header_size +
                               (size_t)record->file_count * TDX_SLIDESHOW_FILE_NAME_MAX_LEN :
                           0;
    uint32_t stored_crc = record->crc32;
    if (ret == ESP_OK &&
        (read_size != size || record->magic != APP_STATE_SLIDESHOW_CONFIG_MAGIC ||
         record->version != APP_STATE_SLIDESHOW_CONFIG_VERSION ||
         record->header_size != header_size || record->total_size != size ||
         !file_count_valid || expected_size != size ||
         stored_crc != record_crc32(record,
                                                              size,
                                                              offsetof(slideshow_config_record_t,
                                                                       crc32)))) {
        ret = ESP_ERR_INVALID_CRC;
    }
    if (ret == ESP_OK) {
        memset(config, 0, sizeof(*config));
        config->file_count = record->file_count;
        config->start_index = record->start_index;
        config->interval = record->interval;
        config->random = false;
        for (size_t i = 0; i < config->file_count; ++i) {
            memcpy(config->file_names[i],
                   record->file_names[i],
                   TDX_SLIDESHOW_FILE_NAME_MAX_LEN);
            config->file_names[i][TDX_IMAGE_BASE_NAME_MAX_BYTES] = '\0';
        }
        if (!config_value_is_valid(config)) {
            ret = ESP_ERR_INVALID_STATE;
        }
    }
    if (ret == ESP_OK && generation != NULL) {
        *generation = record->generation;
    }
    free(record);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "slideshow config NVS validation failed ret=%s",
                 esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t load_control_locked(app_persistent_slideshow_control_t *control,
                                     uint32_t *generation)
{
    slideshow_control_record_t record = {0};
    size_t size = sizeof(record);
    esp_err_t ret = app_nvs_image_state_read_blob(
        APP_STATE_NVS_SLIDESHOW_CONTROL_KEY, &record, &size);
    uint32_t stored_crc = record.crc32;
    if (ret == ESP_OK &&
        (size != sizeof(record) || record.magic != APP_STATE_SLIDESHOW_CONTROL_MAGIC ||
         record.version != APP_STATE_SLIDESHOW_CONTROL_VERSION ||
         record.size != sizeof(record) ||
         stored_crc != record_crc32(&record,
                                    sizeof(record),
                                    offsetof(slideshow_control_record_t, crc32)))) {
        ret = ESP_ERR_INVALID_CRC;
    }
    if (ret == ESP_OK) {
        memset(control, 0, sizeof(*control));
        control->enabled = record.enabled != 0;
        control->interval = record.interval;
        control->random = false;
        control->timestamp = record.timestamp;
        control->anchor_epoch = record.anchor_epoch;
        if (!control_value_is_valid(control)) {
            ret = ESP_ERR_INVALID_STATE;
        }
    }
    if (ret == ESP_OK && generation != NULL) {
        *generation = record.generation;
    }
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "slideshow control NVS validation failed ret=%s",
                 esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t save_control_locked(const app_persistent_slideshow_control_t *control,
                                     uint32_t generation)
{
    if (!control_value_is_valid(control)) {
        return ESP_ERR_INVALID_ARG;
    }
    slideshow_control_record_t record = {
        .magic = APP_STATE_SLIDESHOW_CONTROL_MAGIC,
        .version = APP_STATE_SLIDESHOW_CONTROL_VERSION,
        .size = sizeof(record),
        .generation = generation,
        .interval = control->interval,
        .timestamp = control->timestamp,
        .anchor_epoch = control->anchor_epoch,
        .enabled = control->enabled ? 1U : 0U,
        .random = 0U,
    };
    record.crc32 = record_crc32(&record,
                                sizeof(record),
                                offsetof(slideshow_control_record_t, crc32));
    return write_and_verify_blob(APP_STATE_NVS_SLIDESHOW_CONTROL_KEY,
                                 &record,
                                 sizeof(record));
}

esp_err_t AppPersistentState_Init(void)
{
    if (s_state_mutex == NULL) {
        s_state_mutex = xSemaphoreCreateMutex();
        if (s_state_mutex == NULL) {
            ESP_LOGE(TAG, "persistent-state mutex allocation failed");
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_LOGI(TAG, "persistent image-state ready partition=default namespace=%s",
             APP_STATE_NVS_NAMESPACE);
    return ESP_OK;
}

esp_err_t AppPersistentState_SaveSlideshowState(
    const app_persistent_slideshow_config_t *config,
    const app_persistent_slideshow_control_t *control,
    bool *control_stage_reached)
{
    if (control_stage_reached != NULL) {
        *control_stage_reached = false;
    }
    if (!config_value_is_valid(config) || !control_value_is_valid(control)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = lock_state();
    if (ret != ESP_OK) {
        return ret;
    }
    uint32_t old_generation = 0;
    app_persistent_slideshow_config_t *old_config =
        (app_persistent_slideshow_config_t *)calloc(1, sizeof(*old_config));
    if (old_config == NULL ||
        load_config_locked(old_config, &old_generation) != ESP_OK) {
        old_generation = 0;
    }
    free(old_config);
    uint32_t generation = old_generation + 1U;
    if (generation == 0) {
        generation = 1U;
    }
    size_t header_size = offsetof(slideshow_config_record_t, file_names);
    size_t record_size = header_size +
                         config->file_count * TDX_SLIDESHOW_FILE_NAME_MAX_LEN;
    slideshow_config_record_t *record =
        (slideshow_config_record_t *)calloc(1, record_size);
    if (record == NULL) {
        unlock_state();
        return ESP_ERR_NO_MEM;
    }
    record->magic = APP_STATE_SLIDESHOW_CONFIG_MAGIC;
    record->version = APP_STATE_SLIDESHOW_CONFIG_VERSION;
    record->header_size = (uint16_t)header_size;
    record->total_size = (uint32_t)record_size;
    record->generation = generation;
    record->interval = config->interval;
    record->file_count = (uint16_t)config->file_count;
    record->start_index = (uint16_t)config->start_index;
    record->random = 0;
    for (size_t i = 0; i < config->file_count; ++i) {
        memcpy(record->file_names[i],
               config->file_names[i],
               TDX_SLIDESHOW_FILE_NAME_MAX_LEN);
    }
    record->crc32 = record_crc32(record,
                                 record_size,
                                 offsetof(slideshow_config_record_t, crc32));
    ret = write_and_verify_blob(APP_STATE_NVS_SLIDESHOW_CONFIG_KEY,
                                record,
                                record_size);
    free(record);
    if (ret == ESP_OK) {
        if (control_stage_reached != NULL) {
            *control_stage_reached = true;
        }
        ret = save_control_locked(control, generation);
    }
    unlock_state();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG,
                 "slideshow state saved generation=%lu count=%u start=%u interval=%lu",
                 (unsigned long)generation,
                 (unsigned int)config->file_count,
                 (unsigned int)config->start_index,
                 (unsigned long)config->interval);
    }
    return ret;
}

esp_err_t AppPersistentState_LoadSlideshowConfig(
    app_persistent_slideshow_config_t *config,
    uint32_t *generation)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = lock_state();
    if (ret == ESP_OK) {
        ret = load_config_locked(config, generation);
        unlock_state();
    }
    return ret;
}

esp_err_t AppPersistentState_LoadSlideshowControl(
    app_persistent_slideshow_control_t *control,
    uint32_t *generation)
{
    if (control == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = lock_state();
    if (ret == ESP_OK) {
        ret = load_control_locked(control, generation);
        unlock_state();
    }
    return ret;
}

esp_err_t AppPersistentState_SaveSlideshowControl(
    const app_persistent_slideshow_control_t *control)
{
    if (!control_value_is_valid(control)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = lock_state();
    if (ret != ESP_OK) {
        return ret;
    }
    uint32_t generation = 0;
    app_persistent_slideshow_config_t *config =
        (app_persistent_slideshow_config_t *)calloc(1, sizeof(*config));
    esp_err_t config_ret = config != NULL ?
                           load_config_locked(config, &generation) : ESP_ERR_NO_MEM;
    free(config);
    if (control->enabled && config_ret != ESP_OK) {
        unlock_state();
        return config_ret;
    }
    if (!control->enabled && config_ret != ESP_OK) {
        app_persistent_slideshow_control_t old_control = {0};
        if (load_control_locked(&old_control, &generation) != ESP_OK) {
            generation = 0;
        }
    }
    app_persistent_slideshow_control_t existing = {0};
    uint32_t existing_generation = 0;
    if (load_control_locked(&existing, &existing_generation) == ESP_OK &&
        existing_generation == generation &&
        existing.enabled == control->enabled &&
        existing.interval == control->interval &&
        existing.timestamp == control->timestamp &&
        existing.anchor_epoch == control->anchor_epoch) {
        unlock_state();
        return ESP_OK;
    }
    ret = save_control_locked(control, generation);
    unlock_state();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "slideshow control saved enabled=%d generation=%lu interval=%lu",
                 control->enabled ? 1 : 0,
                 (unsigned long)generation,
                 (unsigned long)control->interval);
    }
    return ret;
}

esp_err_t AppPersistentState_SaveLastCast(const char *file_name)
{
    if (!image_base_name_is_safe(file_name)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = lock_state();
    if (ret != ESP_OK) {
        return ret;
    }
    last_cast_record_t old_record = {0};
    size_t old_size = sizeof(old_record);
    uint32_t generation = 1;
    if (app_nvs_image_state_read_blob(APP_STATE_NVS_LAST_CAST_KEY,
                                    &old_record,
                                    &old_size) == ESP_OK &&
        old_size == sizeof(old_record) && old_record.generation != UINT32_MAX) {
        generation = old_record.generation + 1U;
    }
    last_cast_record_t record = {
        .magic = APP_STATE_LAST_CAST_MAGIC,
        .version = APP_STATE_LAST_CAST_VERSION,
        .size = sizeof(record),
        .generation = generation,
        .valid = 1U,
    };
    strlcpy(record.file_name, file_name, sizeof(record.file_name));
    record.crc32 = record_crc32(&record,
                                sizeof(record),
                                offsetof(last_cast_record_t, crc32));
    ret = write_and_verify_blob(APP_STATE_NVS_LAST_CAST_KEY,
                                &record,
                                sizeof(record));
    unlock_state();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "last cast saved file=%s generation=%lu",
                 file_name, (unsigned long)generation);
    }
    return ret;
}

esp_err_t AppPersistentState_LoadLastCast(app_persistent_last_cast_t *last_cast)
{
    if (last_cast == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = lock_state();
    if (ret != ESP_OK) {
        return ret;
    }
    last_cast_record_t record = {0};
    size_t size = sizeof(record);
    ret = app_nvs_image_state_read_blob(APP_STATE_NVS_LAST_CAST_KEY, &record, &size);
    uint32_t stored_crc = record.crc32;
    if (ret == ESP_OK &&
        (size != sizeof(record) || record.magic != APP_STATE_LAST_CAST_MAGIC ||
         record.version != APP_STATE_LAST_CAST_VERSION || record.size != sizeof(record) ||
         record.valid == 0 ||
         memchr(record.file_name, '\0', sizeof(record.file_name)) == NULL ||
         !image_base_name_is_safe(record.file_name) ||
         stored_crc != record_crc32(&record,
                                    sizeof(record),
                                    offsetof(last_cast_record_t, crc32)))) {
        ret = ESP_ERR_INVALID_CRC;
    }
    if (ret == ESP_OK) {
        memset(last_cast, 0, sizeof(*last_cast));
        last_cast->valid = true;
        strlcpy(last_cast->file_name, record.file_name, sizeof(last_cast->file_name));
    }
    unlock_state();
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "last-cast NVS validation failed ret=%s",
                 esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t AppPersistentState_EraseLastCast(void)
{
    esp_err_t ret = lock_state();
    if (ret == ESP_OK) {
        ret = app_nvs_image_state_erase_key(APP_STATE_NVS_LAST_CAST_KEY);
        unlock_state();
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "last cast state cleared");
    }
    return ret;
}

esp_err_t AppPersistentState_EraseImageState(void)
{
    esp_err_t ret = lock_state();
    if (ret != ESP_OK) {
        return ret;
    }
    esp_err_t config_ret =
        app_nvs_image_state_erase_key(APP_STATE_NVS_SLIDESHOW_CONFIG_KEY);
    esp_err_t control_ret =
        app_nvs_image_state_erase_key(APP_STATE_NVS_SLIDESHOW_CONTROL_KEY);
    esp_err_t cast_ret = app_nvs_image_state_erase_key(APP_STATE_NVS_LAST_CAST_KEY);
    unlock_state();
    ret = config_ret != ESP_OK ? config_ret :
          control_ret != ESP_OK ? control_ret :
          cast_ret;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "all persistent image state erased");
    } else {
        ESP_LOGE(TAG,
                 "persistent image-state erase failed config=%s control=%s cast=%s",
                 esp_err_to_name(config_ret),
                 esp_err_to_name(control_ret),
                 esp_err_to_name(cast_ret));
    }
    return ret;
}
