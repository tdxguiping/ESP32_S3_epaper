#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Return the maximum zlib stream size required for an uncompressed input. */
size_t TdxZlibBuffer_GetCompressBound(size_t input_size);

/* Decompress one RFC 1950 zlib stream into a caller-owned output buffer. */
esp_err_t TdxZlibBuffer_Decompress(const uint8_t *compressed_data,
                                   size_t compressed_size,
                                   uint8_t *output_data,
                                   size_t output_capacity,
                                   size_t *output_size);

#ifdef __cplusplus
}
#endif
