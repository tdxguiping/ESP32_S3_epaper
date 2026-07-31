#include "tdx_zlib_file.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"
#include "tdx_cfg.h"
#include "zlib.h"

static const char *TAG = "zlib-file";

static esp_err_t close_file(FILE **file, const char *path)
{
    if (file == NULL || *file == NULL) {
        return ESP_OK;
    }

    if (fclose(*file) != 0) {
        *file = NULL;
        ESP_LOGE(TAG, "close failed path=%s", path);
        return ESP_FAIL;
    }

    *file = NULL;
    return ESP_OK;
}

esp_err_t TdxZlibFile_Compress(const char *source_path,
                               const char *compressed_path,
                               uint64_t *source_size,
                               uint64_t *compressed_size)
{
    if (source_path == NULL || compressed_path == NULL ||
        source_size == NULL || compressed_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *source_size = 0;
    *compressed_size = 0;

    FILE *source = fopen(source_path, "rb");
    if (source == NULL) {
        ESP_LOGE(TAG, "open source failed path=%s", source_path);
        return ESP_ERR_NOT_FOUND;
    }

    FILE *output = fopen(compressed_path, "wb");
    if (output == NULL) {
        ESP_LOGE(TAG, "open compressed output failed path=%s", compressed_path);
        (void)close_file(&source, source_path);
        return ESP_FAIL;
    }

    uint8_t *input_buffer = malloc(USER_ZLIB_STREAM_BUFFER_SIZE);
    uint8_t *output_buffer = malloc(USER_ZLIB_STREAM_BUFFER_SIZE);
    if (input_buffer == NULL || output_buffer == NULL) {
        ESP_LOGE(TAG, "allocate stream buffers failed size=%u",
                 (unsigned int)USER_ZLIB_STREAM_BUFFER_SIZE);
        free(input_buffer);
        free(output_buffer);
        (void)close_file(&source, source_path);
        (void)close_file(&output, compressed_path);
        (void)remove(compressed_path);
        return ESP_ERR_NO_MEM;
    }

    z_stream stream = {0};
    int zlib_ret = deflateInit(&stream, USER_ZLIB_COMPRESSION_LEVEL);
    if (zlib_ret != Z_OK) {
        ESP_LOGE(TAG, "deflate init failed zlib_ret=%d", zlib_ret);
        free(input_buffer);
        free(output_buffer);
        (void)close_file(&source, source_path);
        (void)close_file(&output, compressed_path);
        (void)remove(compressed_path);
        return ESP_FAIL;
    }

    esp_err_t result = ESP_OK;
    int flush = Z_NO_FLUSH;

    do {
        size_t bytes_read = fread(input_buffer, 1, USER_ZLIB_STREAM_BUFFER_SIZE, source);
        if (ferror(source)) {
            ESP_LOGE(TAG, "read source failed path=%s", source_path);
            result = ESP_FAIL;
            break;
        }

        *source_size += bytes_read;
        flush = feof(source) ? Z_FINISH : Z_NO_FLUSH;
        stream.next_in = input_buffer;
        stream.avail_in = (uInt)bytes_read;

        do {
            stream.next_out = output_buffer;
            stream.avail_out = USER_ZLIB_STREAM_BUFFER_SIZE;
            zlib_ret = deflate(&stream, flush);
            if (zlib_ret != Z_OK && zlib_ret != Z_STREAM_END) {
                ESP_LOGE(TAG, "deflate failed zlib_ret=%d", zlib_ret);
                result = ESP_FAIL;
                break;
            }

            size_t produced = USER_ZLIB_STREAM_BUFFER_SIZE - stream.avail_out;
            if (produced > 0 &&
                fwrite(output_buffer, 1, produced, output) != produced) {
                ESP_LOGE(TAG, "write compressed output failed path=%s",
                         compressed_path);
                result = ESP_FAIL;
                break;
            }
            *compressed_size += produced;
        } while (stream.avail_out == 0);

        if (result != ESP_OK) {
            break;
        }
    } while (flush != Z_FINISH);

    if (result == ESP_OK && zlib_ret != Z_STREAM_END) {
        ESP_LOGE(TAG, "deflate did not finish zlib_ret=%d", zlib_ret);
        result = ESP_FAIL;
    }

    (void)deflateEnd(&stream);
    free(input_buffer);
    free(output_buffer);

    if (close_file(&source, source_path) != ESP_OK) {
        result = ESP_FAIL;
    }
    if (close_file(&output, compressed_path) != ESP_OK) {
        result = ESP_FAIL;
    }
    if (result != ESP_OK) {
        (void)remove(compressed_path);
    }

    return result;
}

esp_err_t TdxZlibFile_Decompress(const char *compressed_path,
                                 const char *output_path,
                                 uint64_t *compressed_size,
                                 uint64_t *output_size)
{
    if (compressed_path == NULL || output_path == NULL ||
        compressed_size == NULL || output_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *compressed_size = 0;
    *output_size = 0;

    FILE *source = fopen(compressed_path, "rb");
    if (source == NULL) {
        ESP_LOGE(TAG, "open compressed input failed path=%s", compressed_path);
        return ESP_ERR_NOT_FOUND;
    }

    FILE *output = fopen(output_path, "wb");
    if (output == NULL) {
        ESP_LOGE(TAG, "open decompressed output failed path=%s", output_path);
        (void)close_file(&source, compressed_path);
        return ESP_FAIL;
    }

    uint8_t *input_buffer = malloc(USER_ZLIB_STREAM_BUFFER_SIZE);
    uint8_t *output_buffer = malloc(USER_ZLIB_STREAM_BUFFER_SIZE);
    if (input_buffer == NULL || output_buffer == NULL) {
        ESP_LOGE(TAG, "allocate stream buffers failed size=%u",
                 (unsigned int)USER_ZLIB_STREAM_BUFFER_SIZE);
        free(input_buffer);
        free(output_buffer);
        (void)close_file(&source, compressed_path);
        (void)close_file(&output, output_path);
        (void)remove(output_path);
        return ESP_ERR_NO_MEM;
    }

    z_stream stream = {0};
    int zlib_ret = inflateInit(&stream);
    if (zlib_ret != Z_OK) {
        ESP_LOGE(TAG, "inflate init failed zlib_ret=%d", zlib_ret);
        free(input_buffer);
        free(output_buffer);
        (void)close_file(&source, compressed_path);
        (void)close_file(&output, output_path);
        (void)remove(output_path);
        return ESP_FAIL;
    }

    esp_err_t result = ESP_OK;
    bool stream_finished = false;

    while (!stream_finished) {
        size_t bytes_read = fread(input_buffer, 1, USER_ZLIB_STREAM_BUFFER_SIZE, source);
        if (ferror(source)) {
            ESP_LOGE(TAG, "read compressed input failed path=%s", compressed_path);
            result = ESP_FAIL;
            break;
        }
        if (bytes_read == 0) {
            ESP_LOGE(TAG, "compressed stream ended before zlib end marker");
            result = ESP_ERR_INVALID_SIZE;
            break;
        }

        *compressed_size += bytes_read;
        stream.next_in = input_buffer;
        stream.avail_in = (uInt)bytes_read;

        do {
            stream.next_out = output_buffer;
            stream.avail_out = USER_ZLIB_STREAM_BUFFER_SIZE;
            zlib_ret = inflate(&stream, Z_NO_FLUSH);
            if (zlib_ret != Z_OK && zlib_ret != Z_STREAM_END) {
                ESP_LOGE(TAG, "inflate failed zlib_ret=%d", zlib_ret);
                result = ESP_FAIL;
                break;
            }

            size_t produced = USER_ZLIB_STREAM_BUFFER_SIZE - stream.avail_out;
            if (produced > 0 &&
                fwrite(output_buffer, 1, produced, output) != produced) {
                ESP_LOGE(TAG, "write decompressed output failed path=%s", output_path);
                result = ESP_FAIL;
                break;
            }
            *output_size += produced;

            if (zlib_ret == Z_STREAM_END) {
                stream_finished = true;
                break;
            }
        } while (stream.avail_out == 0);

        if (result != ESP_OK) {
            break;
        }
    }

    (void)inflateEnd(&stream);
    free(input_buffer);
    free(output_buffer);

    if (close_file(&source, compressed_path) != ESP_OK) {
        result = ESP_FAIL;
    }
    if (close_file(&output, output_path) != ESP_OK) {
        result = ESP_FAIL;
    }
    if (result != ESP_OK) {
        (void)remove(output_path);
    }

    return result;
}
