#include "tdx_zlib_epd_test.h"

#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

#include "epd_display_app.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "tdx_cfg.h"
#include "tdx_shared_spi.h"

static const char *TAG = "zlib-epd-test";

esp_err_t TdxZlibEpdTest_Run(const char *base_path)
{
    if (base_path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char compressed_path[USER_ZLIB_TEST_PATH_BUFFER_SIZE];
    int path_len = snprintf(compressed_path,
                            sizeof(compressed_path),
                            "%s%s",
                            base_path,
                            USER_ZLIB_TEST_COMPRESSED_RELATIVE_PATH);
    if (path_len < 0 || (size_t)path_len >= sizeof(compressed_path)) {
        ESP_LOGE(TAG, "compressed test path is too long");
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "shared SPI lock failed ret=%s", esp_err_to_name(ret));
        return ret;
    }

    struct stat file_stat = {0};
    if (stat(compressed_path, &file_stat) != 0 || file_stat.st_size <= 0 ||
        (uint64_t)file_stat.st_size > SIZE_MAX) {
        ESP_LOGE(TAG, "compressed test file is missing or empty path=%s",
                 compressed_path);
        TdxSharedSpi_Unlock();
        return ESP_ERR_NOT_FOUND;
    }

    size_t compressed_size = (size_t)file_stat.st_size;
    uint8_t *compressed_data = (uint8_t *)heap_caps_malloc(
        compressed_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (compressed_data == NULL &&
        compressed_size <= USER_INTERNAL_RAM_FALLBACK_MAX_SIZE) {
        compressed_data = (uint8_t *)heap_caps_malloc(
            compressed_size, MALLOC_CAP_8BIT);
    }
    if (compressed_data == NULL) {
        ESP_LOGE(TAG, "compressed test buffer alloc failed size=%u",
                 (unsigned int)compressed_size);
        TdxSharedSpi_Unlock();
        return ESP_ERR_NO_MEM;
    }

    FILE *file = fopen(compressed_path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "open compressed test file failed path=%s",
                 compressed_path);
        heap_caps_free(compressed_data);
        TdxSharedSpi_Unlock();
        return ESP_ERR_NOT_FOUND;
    }

    size_t read_size = fread(compressed_data, 1, compressed_size, file);
    int close_ret = fclose(file);
    TdxSharedSpi_Unlock();
    if (read_size != compressed_size || close_ret != 0) {
        ESP_LOGE(TAG, "read compressed test file failed expected=%u actual=%u close=%d",
                 (unsigned int)compressed_size,
                 (unsigned int)read_size,
                 close_ret);
        heap_caps_free(compressed_data);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "compressed EPD test start path=%s size=%u",
             compressed_path,
             (unsigned int)compressed_size);
    int64_t display_start_us = esp_timer_get_time();
    ret = ServerNetworkStaEpdDisplay_QueueToScreenAndWait(
        compressed_data, compressed_size, 1);
    uint64_t display_elapsed_us =
        (uint64_t)(esp_timer_get_time() - display_start_us);
    heap_caps_free(compressed_data);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "compressed EPD test failed ret=%s elapsed_ms=%llu",
                 esp_err_to_name(ret),
                 (unsigned long long)(display_elapsed_us / 1000U));
        return ret;
    }

    ESP_LOGI(TAG, "compressed EPD test passed input=%u elapsed_ms=%llu",
             (unsigned int)compressed_size,
             (unsigned long long)(display_elapsed_us / 1000U));
    return ESP_OK;
}
