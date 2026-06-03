/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* HTTP File Server Example, SD card / SPIFFS mount functions.

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#if SOC_SDMMC_HOST_SUPPORTED
#include "driver/sdmmc_host.h"
#endif
#include "sdmmc_cmd.h"
#include "file_serving_example_common.h"

static const char *TAG = "example_mount";
#define STORAGE_INFO_MAX_FILES 50
#define STORAGE_MOUNT_RETRY_COUNT 3
#define STORAGE_MOUNT_RETRY_DELAY_MS 1000
#define STORAGE_MOUNT_POWER_READY_DELAY_MS 1500

static bool path_has_suffix(const char *path, const char *suffix)
{
    size_t path_len = strlen(path);
    size_t suffix_len = strlen(suffix);
    if (path_len < suffix_len) {
        return false;
    }
    return strcmp(path + path_len - suffix_len, suffix) == 0;
}

static void print_text_file_content(const char *path)
{
    char buf[513];
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        ESP_LOGW(TAG, "Storage txt : %s open failed", path);
        return;
    }

    size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[len] = '\0';

    ESP_LOGI(TAG, "Storage txt : %s content=%s", path, buf);
    if (len == sizeof(buf) - 1) {
        ESP_LOGI(TAG, "Storage txt : %s content truncated to %u bytes", path, (unsigned int)len);
    }
}

static void list_storage_tree(const char *path, int depth, size_t *file_count)
{
    if (file_count == NULL || *file_count >= STORAGE_INFO_MAX_FILES) {
        return;
    }

    DIR *dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGW(TAG, "Storage list: open dir failed path=%s", path);
        return;
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (*file_count >= STORAGE_INFO_MAX_FILES) {
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char child_path[256];
        int path_len = snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);
        if (path_len < 0 || path_len >= (int)sizeof(child_path)) {
            ESP_LOGW(TAG, "Storage list: path too long base=%s name=%s", path, entry->d_name);
            continue;
        }

        struct stat st = {0};
        if (stat(child_path, &st) != 0) {
            ESP_LOGW(TAG, "Storage file: %s stat failed", child_path);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            ESP_LOGI(TAG, "Storage dir : %s", child_path);
            if (depth < 8) {
                list_storage_tree(child_path, depth + 1, file_count);
            }
        } else {
            (*file_count)++;
            ESP_LOGI(TAG, "Storage file: %s size=%u", child_path, (unsigned int)st.st_size);
            if (path_has_suffix(child_path, ".txt")) {
                print_text_file_content(child_path);
            }
        }
    }

    closedir(dir);
}

void example_print_storage_info(const char *base_path)
{
    struct stat st = {0};
    bool path_stat_ok = false;
    bool path_open_ok = false;

#ifdef CONFIG_EXAMPLE_MOUNT_SD_CARD
    ESP_LOGI(TAG, "Storage type: SD card");
#else
    ESP_LOGI(TAG, "Storage type: SPIFFS");
#endif

    if (base_path == NULL || base_path[0] == '\0') {
        ESP_LOGE(TAG, "Storage base path invalid");
        return;
    }

    path_stat_ok = (stat(base_path, &st) == 0);
    DIR *root_dir = opendir(base_path);
    path_open_ok = (root_dir != NULL);
    if (root_dir != NULL) {
        closedir(root_dir);
    }

#ifdef CONFIG_EXAMPLE_MOUNT_SD_CARD
    ESP_LOGI(TAG, "Storage mount check path=%s stat=%s opendir=%s",
             base_path,
             path_stat_ok ? "ok" : "fail",
             path_open_ok ? "ok" : "fail");
    if (!path_stat_ok && !path_open_ok) {
        ESP_LOGE(TAG, "Storage missing or not mounted path=%s", base_path);
        return;
    }
#else
    bool spiffs_mounted = esp_spiffs_mounted(NULL);
    ESP_LOGI(TAG, "Storage mount check path=%s mounted=%s stat=%s opendir=%s",
             base_path,
             spiffs_mounted ? "yes" : "no",
             path_stat_ok ? "ok" : "fail",
             path_open_ok ? "ok" : "fail");
    if (!spiffs_mounted || !path_open_ok) {
        ESP_LOGE(TAG, "Storage missing or not mounted path=%s", base_path);
        return;
    }
#endif

    ESP_LOGI(TAG, "Storage mounted: yes path=%s", base_path);

#ifdef CONFIG_EXAMPLE_MOUNT_SD_CARD
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    esp_err_t fs_ret = esp_vfs_fat_info(base_path, &total_bytes, &free_bytes);
    if (fs_ret == ESP_OK) {
        ESP_LOGI(TAG, "SD status: normal total=%llu free=%llu used=%llu",
                 (unsigned long long)total_bytes,
                 (unsigned long long)free_bytes,
                 (unsigned long long)(total_bytes - free_bytes));
    } else {
        ESP_LOGE(TAG, "SD status: abnormal info_ret=%s", esp_err_to_name(fs_ret));
    }
#else
    size_t total = 0;
    size_t used = 0;
    esp_err_t info_ret = esp_spiffs_info(NULL, &total, &used);
    if (info_ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS status: normal total=%u used=%u free=%u",
                 (unsigned int)total,
                 (unsigned int)used,
                 (unsigned int)(total >= used ? total - used : 0));
    } else {
        ESP_LOGE(TAG, "SPIFFS status: abnormal info_ret=%s", esp_err_to_name(info_ret));
    }
#endif

    size_t file_count = 0;
    ESP_LOGI(TAG, "Storage list begin max_files=%d", STORAGE_INFO_MAX_FILES);
    list_storage_tree(base_path, 0, &file_count);
    ESP_LOGI(TAG, "Storage list end files=%u%s",
             (unsigned int)file_count,
             file_count >= STORAGE_INFO_MAX_FILES ? " limit_reached" : "");
}

#ifdef CONFIG_EXAMPLE_MOUNT_SD_CARD

esp_err_t example_mount_storage(const char* base_path)
{
    ESP_LOGI(TAG, "Initializing SD card");
    ESP_LOGI(TAG, "SD mount config: format_if_mount_failed=true");
    ESP_LOGI(TAG, "SD wait power ready %d ms", STORAGE_MOUNT_POWER_READY_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(STORAGE_MOUNT_POWER_READY_DELAY_MS));

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    esp_err_t ret = ESP_FAIL;
    sdmmc_card_t* card = NULL;

#ifdef CONFIG_EXAMPLE_USE_SDMMC_HOST
    ESP_LOGI(TAG, "Using SDMMC peripheral");
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    // To use 1-line SD mode, change this to 1:
    slot_config.width = 4;

#ifdef SOC_SDMMC_USE_GPIO_MATRIX
    // For chips which support GPIO Matrix for SDMMC peripheral, specify the pins.
    slot_config.clk = CONFIG_EXAMPLE_PIN_SDMMC_CLK;
    slot_config.cmd = CONFIG_EXAMPLE_PIN_CMD;
    slot_config.d0 = CONFIG_EXAMPLE_PIN_D0;
    slot_config.d1 = CONFIG_EXAMPLE_PIN_D1;
    slot_config.d2 = CONFIG_EXAMPLE_PIN_D2;
    slot_config.d3 = CONFIG_EXAMPLE_PIN_D3;
    ESP_LOGI(TAG, "SDMMC pins: clk=%d cmd=%d d0=%d d1=%d d2=%d d3=%d width=%d",
             slot_config.clk,
             slot_config.cmd,
             slot_config.d0,
             slot_config.d1,
             slot_config.d2,
             slot_config.d3,
             slot_config.width);
#endif // SOC_SDMMC_USE_GPIO_MATRIX

    // Enable internal pullups on enabled pins. The internal pullups
    // are insufficient however, please make sure 10k external pullups are
    // connected on the bus. This is for debug / example purpose only.
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    int first_width = slot_config.width;
    for (int width_pass = 0; width_pass < 2 && ret != ESP_OK; width_pass++) {
        if (width_pass == 1) {
            if (first_width == 1) {
                break;
            }
            slot_config.width = 1;
            ESP_LOGW(TAG, "SDMMC 4-bit mount failed, retry with 1-bit mode");
        }

        for (int attempt = 1; attempt <= STORAGE_MOUNT_RETRY_COUNT; attempt++) {
            ret = esp_vfs_fat_sdmmc_mount(base_path, &host, &slot_config, &mount_config, &card);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "SDMMC mount ok width=%d attempt=%d", slot_config.width, attempt);
                break;
            }

            ESP_LOGW(TAG, "SDMMC mount width=%d attempt %d/%d failed ret=%s",
                     slot_config.width,
                     attempt,
                     STORAGE_MOUNT_RETRY_COUNT,
                     esp_err_to_name(ret));
            sdmmc_host_deinit();
            if (attempt < STORAGE_MOUNT_RETRY_COUNT) {
                vTaskDelay(pdMS_TO_TICKS(STORAGE_MOUNT_RETRY_DELAY_MS));
            }
        }
    }

#else // CONFIG_EXAMPLE_USE_SDMMC_HOST

    ESP_LOGI(TAG, "Using SPI peripheral");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = CONFIG_EXAMPLE_PIN_MOSI,
        .miso_io_num = CONFIG_EXAMPLE_PIN_MISO,
        .sclk_io_num = CONFIG_EXAMPLE_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return ret;
    }

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = CONFIG_EXAMPLE_PIN_CS;
    slot_config.host_id = host.slot;
    for (int attempt = 1; attempt <= STORAGE_MOUNT_RETRY_COUNT; attempt++) {
        ret = esp_vfs_fat_sdspi_mount(base_path, &host, &slot_config, &mount_config, &card);
        if (ret == ESP_OK) {
            if (attempt > 1) {
                ESP_LOGI(TAG, "SDSPI mount ok after retry attempt=%d", attempt);
            }
            break;
        }

        ESP_LOGW(TAG, "SDSPI mount attempt %d/%d failed ret=%s",
                 attempt,
                 STORAGE_MOUNT_RETRY_COUNT,
                 esp_err_to_name(ret));
        if (attempt < STORAGE_MOUNT_RETRY_COUNT) {
            vTaskDelay(pdMS_TO_TICKS(STORAGE_MOUNT_RETRY_DELAY_MS));
        }
    }

#endif // !CONFIG_EXAMPLE_USE_SDMMC_HOST

    if (ret != ESP_OK){
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return ret;
    }

    sdmmc_card_print_info(stdout, card);
    example_print_storage_info(base_path);
    return ESP_OK;
}

#else // CONFIG_EXAMPLE_MOUNT_SD_CARD

/* Function to initialize SPIFFS */
esp_err_t example_mount_storage(const char* base_path)
{
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = base_path,
        .partition_label = NULL,
        .max_files = 5,   // This sets the maximum number of files that can be open at the same time
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    example_print_storage_info(base_path);
    return ESP_OK;
}

#endif // !CONFIG_EXAMPLE_MOUNT_SD_CARD
