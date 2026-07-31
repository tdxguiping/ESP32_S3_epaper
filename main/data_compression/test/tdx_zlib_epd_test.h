#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Read the generated zlib file from SD and submit it through the public EPD path. */
esp_err_t TdxZlibEpdTest_Run(const char *base_path);

#ifdef __cplusplus
}
#endif
