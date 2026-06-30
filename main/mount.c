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
#include <errno.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#if SOC_SDMMC_HOST_SUPPORTED
#include "driver/sdmmc_host.h"
#endif
#include "sdmmc_cmd.h"
#include "file_serving_example_common.h"
#include "tdx_cfg.h"
#include "tdx_shared_spi.h"

static const char *TAG = "example_mount";
#define STORAGE_INFO_MAX_FILES 50
#define STORAGE_MOUNT_RETRY_COUNT 3
#define STORAGE_MOUNT_RETRY_DELAY_MS 300
#define STORAGE_MOUNT_POWER_READY_DELAY_MS 1000
#define STORAGE_SPIFFS_PARTITION_LABEL "assets"
#define STORAGE_SD_FAIL_COUNT_NVS_KEY "sd_fail_count"
#define STORAGE_SD_FAIL_RESTART_LIMIT 3

static example_storage_type_t s_storage_type = EXAMPLE_STORAGE_TYPE_UNKNOWN;

void example_print_storage_info(const char *base_path);

static esp_err_t ensure_storage_dir(const char *path)
{
    struct stat st = {0};
    if (path == NULL || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }

    if (stat(path, &st) == 0) {
        TdxSharedSpi_Unlock();
        return S_ISDIR(st.st_mode) ? ESP_OK : ESP_FAIL;
    }

    if (mkdir(path, 0775) == 0 || errno == EEXIST) {
        ESP_LOGI(TAG, "Storage dir create: %s", path);
        TdxSharedSpi_Unlock();
        return ESP_OK;
    }

    int err = errno;
    if (err == ENOTSUP || err == EOPNOTSUPP) {
        ESP_LOGW(TAG, "Storage mkdir not supported, skip path=%s errno=%d", path, err);
        TdxSharedSpi_Unlock();
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Storage mkdir failed path=%s errno=%d", path, err);
    TdxSharedSpi_Unlock();
    return ESP_FAIL;
}

static void ensure_default_storage_dirs(const char *base_path)
{
    static const char *dirs[] = {
        "01_sys_init_img",
        "02_sys_ap_img",
        "03_sys_ap_html",
        "04_sys_ai_img",
        "05_user_ai_img",
        "06_user_foundation_img",
        "cast_img",
        "bin_img",
        "jpg_img",
    };

    if (base_path == NULL || base_path[0] == '\0') {
        return;
    }

    if (s_storage_type == EXAMPLE_STORAGE_TYPE_SPIFFS) {
        ESP_LOGI(TAG, "SPIFFS uses flat paths, skip directory creation");
        return;
    }

    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        char path[128];
        int len = snprintf(path, sizeof(path), "%s/%s", base_path, dirs[i]);
        if (len < 0 || len >= (int)sizeof(path)) {
            ESP_LOGW(TAG, "Storage default dir path too long base=%s name=%s", base_path, dirs[i]);
            continue;
        }
        (void)ensure_storage_dir(path);
    }
}

static esp_err_t mount_spiffs_storage(const char *base_path)
{
    ESP_LOGI(TAG, "Initializing SPIFFS fallback label=%s", STORAGE_SPIFFS_PARTITION_LABEL);

    esp_vfs_spiffs_conf_t conf = {
        .base_path = base_path,
        .partition_label = STORAGE_SPIFFS_PARTITION_LABEL,
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format SPIFFS");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition label=%s", STORAGE_SPIFFS_PARTITION_LABEL);
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0;
    size_t used = 0;
    ret = esp_spiffs_info(STORAGE_SPIFFS_PARTITION_LABEL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SPIFFS fallback mounted: total=%u used=%u free=%u",
             (unsigned int)total,
             (unsigned int)used,
             (unsigned int)(total >= used ? total - used : 0));
    s_storage_type = EXAMPLE_STORAGE_TYPE_SPIFFS;
    ensure_default_storage_dirs(base_path);
    example_print_storage_info(base_path);
    return ESP_OK;
}

static uint8_t storage_read_sd_fail_count(void)
{
    uint8_t fail_count = 0;
    esp_err_t ret = app_nvs_read_u8(STORAGE_SD_FAIL_COUNT_NVS_KEY, &fail_count, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD fail count read failed ret=%s, use 0", esp_err_to_name(ret));
        fail_count = 0;
    }
    return fail_count;
}

static void storage_write_sd_fail_count(uint8_t fail_count)
{
    esp_err_t ret = app_nvs_write_u8(STORAGE_SD_FAIL_COUNT_NVS_KEY, fail_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD fail count write failed value=%u ret=%s",
                 (unsigned int)fail_count,
                 esp_err_to_name(ret));
    }
}

static esp_err_t handle_sd_mount_failure(const char *base_path, esp_err_t mount_ret)
{
    uint8_t fail_count = storage_read_sd_fail_count();
    if (fail_count < UINT8_MAX) {
        fail_count++;
    }
    storage_write_sd_fail_count(fail_count);

    if (fail_count <= STORAGE_SD_FAIL_RESTART_LIMIT) {
        ESP_LOGE(TAG,
                 "SD mount failed ret=%s fail_count=%u action=restart",
                 esp_err_to_name(mount_ret),
                 (unsigned int)fail_count);
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
        return mount_ret;
    }

    ESP_LOGE(TAG,
             "SD mount failed ret=%s fail_count=%u action=spiffs_fallback_reset_counter",
             esp_err_to_name(mount_ret),
             (unsigned int)fail_count);
    storage_write_sd_fail_count(0);
    return mount_spiffs_storage(base_path);
}

example_storage_type_t example_storage_get_type(void)
{
    return s_storage_type;
}

bool example_storage_is_sd_card(void)
{
    return s_storage_type == EXAMPLE_STORAGE_TYPE_SD_CARD;
}

bool example_storage_supports_directories(void)
{
    return s_storage_type == EXAMPLE_STORAGE_TYPE_SD_CARD;
}

esp_err_t example_storage_get_free_bytes(const char *base_path, uint64_t *free_bytes)
{
    if (free_bytes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *free_bytes = 0;

    if (s_storage_type == EXAMPLE_STORAGE_TYPE_SD_CARD) {
        uint64_t total_bytes = 0;
        esp_err_t lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
        if (lock_ret != ESP_OK) {
            return lock_ret;
        }
        esp_err_t ret = esp_vfs_fat_info(base_path, &total_bytes, free_bytes);
        TdxSharedSpi_Unlock();
        return ret;
    }

    if (s_storage_type == EXAMPLE_STORAGE_TYPE_SPIFFS) {
        size_t total = 0;
        size_t used = 0;
        esp_err_t ret = esp_spiffs_info(STORAGE_SPIFFS_PARTITION_LABEL, &total, &used);
        if (ret != ESP_OK || total < used) {
            return ret != ESP_OK ? ret : ESP_FAIL;
        }
        *free_bytes = (uint64_t)(total - used);
        return ESP_OK;
    }

    return ESP_ERR_INVALID_STATE;
}

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
    if (TdxSharedSpi_Lock(portMAX_DELAY) != ESP_OK) {
        return;
    }
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        TdxSharedSpi_Unlock();
        ESP_LOGW(TAG, "Storage txt : %s open failed", path);
        return;
    }

    size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    TdxSharedSpi_Unlock();
    buf[len] = '\0';

    ESP_LOGI(TAG, "Storage txt : %s content=%s", path, buf);
    if (len == sizeof(buf) - 1) {
        ESP_LOGI(TAG, "Storage txt : %s content truncated to %u bytes", path, (unsigned int)len);
    }
}

static void __attribute__((unused)) list_storage_tree(const char *path, int depth, size_t *file_count)
{
    if (file_count == NULL || *file_count >= STORAGE_INFO_MAX_FILES) {
        return;
    }

    if (TdxSharedSpi_Lock(portMAX_DELAY) != ESP_OK) {
        return;
    }
    DIR *dir = opendir(path);
    if (dir == NULL) {
        TdxSharedSpi_Unlock();
        ESP_LOGW(TAG, "Storage list: open dir failed path=%s", path);
        return;
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        vTaskDelay(pdMS_TO_TICKS(1));

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
    TdxSharedSpi_Unlock();
}

void example_print_storage_info(const char *base_path)
{
    struct stat st = {0};
    bool path_stat_ok = false;
    bool path_open_ok = false;

    ESP_LOGI(TAG, "Storage type: %s",
             s_storage_type == EXAMPLE_STORAGE_TYPE_SD_CARD ? "SD card" :
             s_storage_type == EXAMPLE_STORAGE_TYPE_SPIFFS ? "SPIFFS" : "unknown");

    if (base_path == NULL || base_path[0] == '\0') {
        ESP_LOGE(TAG, "Storage base path invalid");
        return;
    }

    if (TdxSharedSpi_Lock(portMAX_DELAY) != ESP_OK) {
        ESP_LOGE(TAG, "Storage shared SPI lock failed path=%s", base_path);
        return;
    }
    path_stat_ok = (stat(base_path, &st) == 0);
    DIR *root_dir = opendir(base_path);
    path_open_ok = (root_dir != NULL);
    if (root_dir != NULL) {
        closedir(root_dir);
    }
    TdxSharedSpi_Unlock();

    if (s_storage_type == EXAMPLE_STORAGE_TYPE_SPIFFS) {
        bool spiffs_mounted = esp_spiffs_mounted(STORAGE_SPIFFS_PARTITION_LABEL);
        ESP_LOGI(TAG, "Storage mount check path=%s mounted=%s stat=%s opendir=%s",
                 base_path,
                 spiffs_mounted ? "yes" : "no",
                 path_stat_ok ? "ok" : "fail",
                 path_open_ok ? "ok" : "fail");
        if (!spiffs_mounted || !path_open_ok) {
            ESP_LOGE(TAG, "Storage missing or not mounted path=%s", base_path);
            return;
        }
    } else {
        ESP_LOGI(TAG, "Storage mount check path=%s stat=%s opendir=%s",
                 base_path,
                 path_stat_ok ? "ok" : "fail",
                 path_open_ok ? "ok" : "fail");
        if (!path_stat_ok && !path_open_ok) {
            ESP_LOGE(TAG, "Storage missing or not mounted path=%s", base_path);
            return;
        }
    }

    ESP_LOGI(TAG, "Storage mounted: yes path=%s", base_path);

    if (s_storage_type == EXAMPLE_STORAGE_TYPE_SD_CARD) {
        uint64_t total_bytes = 0;
        uint64_t free_bytes = 0;
        if (TdxSharedSpi_Lock(portMAX_DELAY) != ESP_OK) {
            ESP_LOGE(TAG, "SD status: shared SPI lock failed");
            return;
        }
        esp_err_t fs_ret = esp_vfs_fat_info(base_path, &total_bytes, &free_bytes);
        TdxSharedSpi_Unlock();
        if (fs_ret == ESP_OK) {
            ESP_LOGI(TAG, "SD status: normal total=%llu free=%llu used=%llu",
                     (unsigned long long)total_bytes,
                     (unsigned long long)free_bytes,
                     (unsigned long long)(total_bytes - free_bytes));
        } else {
            ESP_LOGE(TAG, "SD status: abnormal info_ret=%s", esp_err_to_name(fs_ret));
        }
    } else if (s_storage_type == EXAMPLE_STORAGE_TYPE_SPIFFS) {
        size_t total = 0;
        size_t used = 0;
        esp_err_t info_ret = esp_spiffs_info(STORAGE_SPIFFS_PARTITION_LABEL, &total, &used);
        if (info_ret == ESP_OK) {
            ESP_LOGI(TAG, "SPIFFS status: normal total=%u used=%u free=%u",
                     (unsigned int)total,
                     (unsigned int)used,
                     (unsigned int)(total >= used ? total - used : 0));
        } else {
            ESP_LOGE(TAG, "SPIFFS status: abnormal info_ret=%s", esp_err_to_name(info_ret));
        }
    }

#if USER_STORAGE_LIST_ON_STARTUP_ENABLE
    size_t file_count = 0;
    ESP_LOGI(TAG, "Storage list begin max_files=%d", STORAGE_INFO_MAX_FILES);
    list_storage_tree(base_path, 0, &file_count);
    ESP_LOGI(TAG, "Storage list end files=%u%s",
             (unsigned int)file_count,
             file_count >= STORAGE_INFO_MAX_FILES ? " limit_reached" : "");
#else
    ESP_LOGI(TAG, "Storage list skipped");
#endif
}

#ifdef CONFIG_EXAMPLE_MOUNT_SD_CARD

static void storage_release_sdmmc_pins(void)
{
#if defined(CONFIG_EXAMPLE_USE_SDMMC_HOST) && defined(SOC_SDMMC_USE_GPIO_MATRIX)
    const gpio_num_t pins[] = {
        CONFIG_EXAMPLE_PIN_SDMMC_CLK,
        CONFIG_EXAMPLE_PIN_CMD,
        CONFIG_EXAMPLE_PIN_D0,
        CONFIG_EXAMPLE_PIN_D1,
        CONFIG_EXAMPLE_PIN_D2,
        CONFIG_EXAMPLE_PIN_D3,
    };

    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        gpio_reset_pin(pins[i]);
        gpio_set_direction(pins[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(pins[i], GPIO_PULLDOWN_ONLY);
    }
#endif
}

esp_err_t example_mount_storage(const char* base_path)
{
#if !USER_STORAGE_SD_CARD_ENABLE
    ESP_LOGI(TAG, "SD card disabled by config, mount SPIFFS");
    return mount_spiffs_storage(base_path);
#else
    ESP_LOGI(TAG, "Initializing SD card");
    ESP_LOGI(TAG, "SD mount config: format_if_mount_failed=false");
    ESP_LOGI(TAG, "SD wait power ready %d ms", STORAGE_MOUNT_POWER_READY_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(STORAGE_MOUNT_POWER_READY_DELAY_MS));

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
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
        if (attempt < STORAGE_MOUNT_RETRY_COUNT && STORAGE_MOUNT_RETRY_DELAY_MS > 0) {
            vTaskDelay(pdMS_TO_TICKS(STORAGE_MOUNT_RETRY_DELAY_MS));
        }
    }

#else // CONFIG_EXAMPLE_USE_SDMMC_HOST

    ESP_LOGI(TAG, "Using SPI peripheral");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = USER_SD_SPI_HOST;
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = USER_SD_SPI_MOSI_PIN,
        .miso_io_num = USER_SD_SPI_MISO_PIN,
        .sclk_io_num = USER_SD_SPI_CLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    // Print the C5 SDSPI wiring before mounting so board bring-up can verify the shared SPI bus.
    // 挂载前打印 C5 SDSPI 接线，方便板级调试时确认共用 SPI 总线配置。
    ESP_LOGI(TAG, "SDSPI pins: host=%d mosi=%d miso=%d clk=%d cs=%d",
             (int)host.slot,
             USER_SD_SPI_MOSI_PIN,
             USER_SD_SPI_MISO_PIN,
             USER_SD_SPI_CLK_PIN,
             USER_SD_SPI_CS_PIN);

    esp_err_t shared_spi_lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (shared_spi_lock_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to lock shared SPI bus.");
        return shared_spi_lock_ret;
    }

    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret == ESP_ERR_INVALID_STATE) {
        // Reuse the SPI bus initialized by the EPD driver because C5 shares SD and EPD SPI pins.
        // 复用墨水屏驱动已经初始化的 SPI 总线，因为 C5 的 SD 和墨水屏共用 SPI 引脚。
        ESP_LOGW(TAG, "SDSPI bus already initialized, reuse host=%d", (int)host.slot);
        ret = ESP_OK;
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        TdxSharedSpi_Unlock();
        return ret;
    }

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = USER_SD_SPI_CS_PIN;
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
    TdxSharedSpi_Unlock();

#endif // !CONFIG_EXAMPLE_USE_SDMMC_HOST

    if (ret != ESP_OK){
        storage_release_sdmmc_pins();
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount SD filesystem.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SD card (%s).",
                     esp_err_to_name(ret));
        }
        return handle_sd_mount_failure(base_path, ret);
    }

    sdmmc_card_print_info(stdout, card);
    if (storage_read_sd_fail_count() != 0) {
        ESP_LOGI(TAG, "SD mount ok reset fail_count=0");
        storage_write_sd_fail_count(0);
    }
    s_storage_type = EXAMPLE_STORAGE_TYPE_SD_CARD;
    ensure_default_storage_dirs(base_path);
    example_print_storage_info(base_path);
    return ESP_OK;
#endif
}

#else // CONFIG_EXAMPLE_MOUNT_SD_CARD

/* Function to initialize SPIFFS */
esp_err_t example_mount_storage(const char* base_path)
{
    return mount_spiffs_storage(base_path);
}

#endif // !CONFIG_EXAMPLE_MOUNT_SD_CARD
