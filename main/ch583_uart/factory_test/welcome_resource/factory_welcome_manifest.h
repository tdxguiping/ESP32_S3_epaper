#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "factory_welcome_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char url[FACTORY_WELCOME_URL_MAX_SIZE];
    char file_name[FACTORY_WELCOME_FILE_NAME_MAX_SIZE];
    size_t compressed_size;
} factory_welcome_file_t;

typedef struct {
    uint32_t version;
    size_t file_count;
    factory_welcome_file_t files[FACTORY_WELCOME_MAX_FILES];
} factory_welcome_manifest_t;

esp_err_t FactoryWelcomeManifest_ParseForResolution(
    const char *json,
    size_t json_size,
    const char *resolution,
    factory_welcome_manifest_t *manifest);

#ifdef __cplusplus
}
#endif

