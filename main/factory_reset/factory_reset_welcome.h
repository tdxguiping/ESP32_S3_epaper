#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint8_t *data;
    size_t size;
    char file_name[128];
} factory_reset_welcome_file_t;

esp_err_t FactoryResetWelcome_Load(const char *base_path,
                                   factory_reset_welcome_file_t *file);
void FactoryResetWelcome_Release(factory_reset_welcome_file_t *file);
