#include "tdx_zlib_self_test.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "tdx_cfg.h"
#include "tdx_shared_spi.h"
#include "tdx_zlib_file.h"

static const char *TAG = "zlib-test";

static esp_err_t build_path(char *path,
                            size_t path_size,
                            const char *base_path,
                            const char *relative_path)
{
    int length = snprintf(path, path_size, "%s%s", base_path, relative_path);
    if (length < 0 || (size_t)length >= path_size) {
        ESP_LOGE(TAG, "test path too long relative=%s", relative_path);
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t compare_files(const char *original_path,
                               const char *decompressed_path,
                               uint64_t *compared_size)
{
    *compared_size = 0;

    FILE *original = fopen(original_path, "rb");
    if (original == NULL) {
        ESP_LOGE(TAG, "reopen original failed path=%s", original_path);
        return ESP_ERR_NOT_FOUND;
    }

    FILE *decompressed = fopen(decompressed_path, "rb");
    if (decompressed == NULL) {
        ESP_LOGE(TAG, "open decompressed file failed path=%s", decompressed_path);
        fclose(original);
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t *original_buffer = malloc(USER_ZLIB_STREAM_BUFFER_SIZE);
    uint8_t *decompressed_buffer = malloc(USER_ZLIB_STREAM_BUFFER_SIZE);
    if (original_buffer == NULL || decompressed_buffer == NULL) {
        ESP_LOGE(TAG, "allocate compare buffers failed size=%u",
                 (unsigned int)USER_ZLIB_STREAM_BUFFER_SIZE);
        free(original_buffer);
        free(decompressed_buffer);
        fclose(original);
        fclose(decompressed);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result = ESP_OK;
    for (;;) {
        size_t original_read =
            fread(original_buffer, 1, USER_ZLIB_STREAM_BUFFER_SIZE, original);
        size_t decompressed_read =
            fread(decompressed_buffer, 1, USER_ZLIB_STREAM_BUFFER_SIZE, decompressed);

        if (ferror(original) || ferror(decompressed)) {
            ESP_LOGE(TAG, "file compare read failed offset=%llu",
                     (unsigned long long)*compared_size);
            result = ESP_FAIL;
            break;
        }
        if (original_read != decompressed_read) {
            ESP_LOGE(TAG, "file compare size mismatch offset=%llu original=%u decompressed=%u",
                     (unsigned long long)*compared_size,
                     (unsigned int)original_read,
                     (unsigned int)decompressed_read);
            result = ESP_ERR_INVALID_SIZE;
            break;
        }
        if (original_read == 0) {
            break;
        }
        if (memcmp(original_buffer, decompressed_buffer, original_read) != 0) {
            ESP_LOGE(TAG, "file compare data mismatch offset=%llu",
                     (unsigned long long)*compared_size);
            result = ESP_FAIL;
            break;
        }

        *compared_size += original_read;
    }

    free(original_buffer);
    free(decompressed_buffer);
    if (fclose(original) != 0) {
        ESP_LOGE(TAG, "close original after compare failed");
        result = ESP_FAIL;
    }
    if (fclose(decompressed) != 0) {
        ESP_LOGE(TAG, "close decompressed after compare failed");
        result = ESP_FAIL;
    }
    return result;
}

esp_err_t TdxZlibSelfTest_Run(const char *base_path)
{
    if (base_path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char source_path[USER_ZLIB_TEST_PATH_BUFFER_SIZE];
    char compressed_path[USER_ZLIB_TEST_PATH_BUFFER_SIZE];
    char decompressed_path[USER_ZLIB_TEST_PATH_BUFFER_SIZE];

    esp_err_t ret = build_path(source_path,
                               sizeof(source_path),
                               base_path,
                               USER_ZLIB_TEST_SOURCE_RELATIVE_PATH);
    if (ret == ESP_OK) {
        ret = build_path(compressed_path,
                         sizeof(compressed_path),
                         base_path,
                         USER_ZLIB_TEST_COMPRESSED_RELATIVE_PATH);
    }
    if (ret == ESP_OK) {
        ret = build_path(decompressed_path,
                         sizeof(decompressed_path),
                         base_path,
                         USER_ZLIB_TEST_DECOMPRESSED_RELATIVE_PATH);
    }
    if (ret != ESP_OK) {
        return ret;
    }

    ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "shared SPI lock failed ret=%s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "file self-test start source=%s", source_path);
    bool remove_decompressed = false;

    uint64_t original_size = 0;
    uint64_t compressed_size = 0;
    int64_t compression_start_us = esp_timer_get_time();
    ret = TdxZlibFile_Compress(source_path,
                               compressed_path,
                               &original_size,
                               &compressed_size);
    uint64_t compression_elapsed_us =
        (uint64_t)(esp_timer_get_time() - compression_start_us);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "file compression test failed ret=%s elapsed_us=%llu elapsed_ms=%llu",
                 esp_err_to_name(ret),
                 (unsigned long long)compression_elapsed_us,
                 (unsigned long long)(compression_elapsed_us / 1000U));
        goto cleanup;
    }

    ESP_LOGI(TAG, "compression passed input=%llu output=%llu elapsed_us=%llu elapsed_ms=%llu path=%s",
             (unsigned long long)original_size,
             (unsigned long long)compressed_size,
             (unsigned long long)compression_elapsed_us,
             (unsigned long long)(compression_elapsed_us / 1000U),
             compressed_path);
    if (compressed_size >= original_size) {
        ESP_LOGW(TAG, "compressed file is not smaller input=%llu output=%llu",
                 (unsigned long long)original_size,
                 (unsigned long long)compressed_size);
    }

    uint64_t decompressed_input_size = 0;
    uint64_t decompressed_size = 0;
    int64_t decompression_start_us = esp_timer_get_time();
    ret = TdxZlibFile_Decompress(compressed_path,
                                 decompressed_path,
                                 &decompressed_input_size,
                                 &decompressed_size);
    uint64_t decompression_elapsed_us =
        (uint64_t)(esp_timer_get_time() - decompression_start_us);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "file decompression test failed ret=%s elapsed_us=%llu elapsed_ms=%llu",
                 esp_err_to_name(ret),
                 (unsigned long long)decompression_elapsed_us,
                 (unsigned long long)(decompression_elapsed_us / 1000U));
        goto cleanup;
    }
    remove_decompressed = true;

    ESP_LOGI(TAG, "decompression passed input=%llu output=%llu elapsed_us=%llu elapsed_ms=%llu",
             (unsigned long long)decompressed_input_size,
             (unsigned long long)decompressed_size,
             (unsigned long long)decompression_elapsed_us,
             (unsigned long long)(decompression_elapsed_us / 1000U));

    uint64_t compared_size = 0;
    ret = compare_files(source_path, decompressed_path, &compared_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "file comparison test failed ret=%s",
                 esp_err_to_name(ret));
    } else if (compared_size != original_size ||
               decompressed_size != original_size) {
        ESP_LOGE(TAG, "file comparison total mismatch original=%llu decompressed=%llu compared=%llu",
                 (unsigned long long)original_size,
                 (unsigned long long)decompressed_size,
                 (unsigned long long)compared_size);
        ret = ESP_ERR_INVALID_SIZE;
    } else {
        ESP_LOGI(TAG, "file self-test passed bytes=%llu",
                 (unsigned long long)compared_size);
    }

cleanup:
    if (remove_decompressed && remove(decompressed_path) != 0) {
        ESP_LOGW(TAG, "remove temporary decompressed file failed path=%s",
                 decompressed_path);
    }

    TdxSharedSpi_Unlock();
    return ret;
}
