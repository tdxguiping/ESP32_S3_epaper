#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t FactoryWelcomeStorage_EnsureDirectory(const char *base_path);
esp_err_t FactoryWelcomeStorage_ReadManifest(const char *base_path,
                                             char *buffer,
                                             size_t buffer_capacity,
                                             size_t *manifest_size);
esp_err_t FactoryWelcomeStorage_ReadFileExact(const char *base_path,
                                              const char *file_name,
                                              uint8_t *buffer,
                                              size_t expected_size);
esp_err_t FactoryWelcomeStorage_WriteFileAtomic(const char *base_path,
                                                const char *file_name,
                                                const uint8_t *data,
                                                size_t data_size);
esp_err_t FactoryWelcomeStorage_WriteManifestAtomic(const char *base_path,
                                                    const char *json,
                                                    size_t json_size);

#ifdef __cplusplus
}
#endif
