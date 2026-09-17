#ifndef ZLIB_IMAGE_CODEC_H
#define ZLIB_IMAGE_CODEC_H

#include "CONFIG.h"

/* The codec owns all zlib, DEFLATE and inner-image state.  The caller only
 * supplies a sink for verified RAW image bytes. */
typedef int (*zlib_image_codec_sink_t)(void *context, const UINT8 *data, UINT16 length);

int zlib_image_codec_begin(zlib_image_codec_sink_t sink, void *context);
int zlib_image_codec_feed(const UINT8 *data, UINT16 length);
int zlib_image_codec_finish(void);
void zlib_image_codec_abort(void);
/* Stream pump: 0=need input, 1=end, 2=yield, -1=error. */
int zlib_image_codec_pump(const UINT8 *data, UINT16 length, UINT16 *consumed);

#endif
