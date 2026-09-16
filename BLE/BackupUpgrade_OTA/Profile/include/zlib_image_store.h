#ifndef ZLIB_IMAGE_STORE_H
#define ZLIB_IMAGE_STORE_H

#include "app_cfg.h"
#include "zlib_image_codec.h"

/* Internal Flash format, independent of the phone's fIsZip=1 wire flag. */
#define IMAGE_FLASH_ZLIB 2U

#if TDX_STORE_ZLIB
int zlib_image_store_begin(UINT8 index);
int zlib_image_store_feed(const UINT8 *data, UINT16 length);
int zlib_image_store_finish(void);
void zlib_image_store_abort(void);
int zlib_image_store_replay(UINT8 index, zlib_image_codec_sink_t sink, void *context);
#endif
#endif
