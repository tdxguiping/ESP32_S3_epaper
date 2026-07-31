#include "tdx_zlib_buffer.h"

#include <limits.h>

#include "esp_log.h"
#include "zlib.h"

static const char *TAG = "zlib-buffer";

size_t TdxZlibBuffer_GetCompressBound(size_t input_size)
{
    if (input_size > ULONG_MAX) {
        return 0;
    }
    return (size_t)compressBound((uLong)input_size);
}

esp_err_t TdxZlibBuffer_Decompress(const uint8_t *compressed_data,
                                   size_t compressed_size,
                                   uint8_t *output_data,
                                   size_t output_capacity,
                                   size_t *output_size)
{
    if (compressed_data == NULL || compressed_size == 0 ||
        output_data == NULL || output_capacity == 0 || output_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *output_size = 0;
    if (compressed_size > ULONG_MAX || output_capacity > ULONG_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    uLongf decoded_size = (uLongf)output_capacity;
    int zlib_ret = uncompress(output_data,
                              &decoded_size,
                              compressed_data,
                              (uLong)compressed_size);
    if (zlib_ret != Z_OK) {
        ESP_LOGE(TAG, "decompress failed input=%u capacity=%u zlib_ret=%d",
                 (unsigned int)compressed_size,
                 (unsigned int)output_capacity,
                 zlib_ret);
        return zlib_ret == Z_MEM_ERROR ? ESP_ERR_NO_MEM :
               zlib_ret == Z_BUF_ERROR ? ESP_ERR_INVALID_SIZE :
                                         ESP_FAIL;
    }

    *output_size = (size_t)decoded_size;
    return ESP_OK;
}
