#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*factory_welcome_cancel_fn_t)(uint32_t generation);

esp_err_t FactoryWelcomeResource_Sync(const char *base_path,
                                      uint32_t generation,
                                      factory_welcome_cancel_fn_t is_canceled);

#ifdef __cplusplus
}
#endif

