#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Compress one file into an RFC 1950 zlib stream.
 * Existing output content is replaced only after the input file is opened.
 */
esp_err_t TdxZlibFile_Compress(const char *source_path,
                               const char *compressed_path,
                               uint64_t *source_size,
                               uint64_t *compressed_size);

/*
 * Decompress one RFC 1950 zlib stream into a file.
 * A partial output file is removed when decompression fails.
 */
esp_err_t TdxZlibFile_Decompress(const char *compressed_path,
                                 const char *output_path,
                                 uint64_t *compressed_size,
                                 uint64_t *output_size);

#ifdef __cplusplus
}
#endif
