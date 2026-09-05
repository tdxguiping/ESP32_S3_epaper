#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*factory_welcome_continue_fn_t)(void *context);

esp_err_t FactoryWelcomeHttp_Get(const char *url,
                                 uint8_t *buffer,
                                 size_t buffer_capacity,
                                 size_t expected_size,
                                 size_t *received_size,
                                 uint32_t timeout_ms,
                                 factory_welcome_continue_fn_t should_continue,
                                 void *continue_context);

#ifdef __cplusplus
}
#endif
