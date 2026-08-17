#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * These functions are private building blocks for the persistent_state module.
 * All firmware versions use the default NVS partition and the stable
 * image_state namespace so application-only OTA never changes the storage
 * location. Callers provide typed validation; app_nvs owns commits, size
 * queries, and low-level error reporting only.
 */
esp_err_t app_nvs_image_state_get_blob_size(const char *key, size_t *size);
esp_err_t app_nvs_image_state_read_blob(const char *key, void *value, size_t *size);
esp_err_t app_nvs_image_state_write_blob(const char *key, const void *value, size_t size);
esp_err_t app_nvs_image_state_erase_key(const char *key);

#ifdef __cplusplus
}
#endif
