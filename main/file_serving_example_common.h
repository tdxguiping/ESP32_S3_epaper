/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* HTTP File Server Example, common declarations

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include "sdkconfig.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EXAMPLE_STORAGE_TYPE_UNKNOWN = 0,
    EXAMPLE_STORAGE_TYPE_SD_CARD,
    EXAMPLE_STORAGE_TYPE_SPIFFS,
} example_storage_type_t;

esp_err_t example_mount_storage(const char *base_path);
void example_print_storage_info(const char *base_path);
example_storage_type_t example_storage_get_type(void);
bool example_storage_is_sd_card(void);
bool example_storage_supports_directories(void);
esp_err_t example_storage_get_free_bytes(const char *base_path, uint64_t *free_bytes);

esp_err_t example_start_file_server(const char *base_path);

#ifdef __cplusplus
}
#endif
