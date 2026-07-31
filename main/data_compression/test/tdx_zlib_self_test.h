#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Compress the configured SD-card BIN file, decompress it to a temporary file,
 * and compare the original and decompressed files byte for byte.
 */
esp_err_t TdxZlibSelfTest_Run(const char *base_path);

#ifdef __cplusplus
}
#endif
