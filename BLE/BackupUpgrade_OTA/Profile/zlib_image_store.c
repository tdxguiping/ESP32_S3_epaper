#include "zlib_image_store.h"

#if TDX_STORE_ZLIB
#include <string.h>
#include "flash_api.h"
#include "epd_driver.h"

/* Each existing slot keeps its address/size. Page 0 is a commit record;
 * compressed bytes start at page 1. An erased record is never replayable. */
#define STORE_PAGE 256U
#define STORE_CAPACITY (EXTERN_FLASH_PIC_SIZE * 1024UL - STORE_PAGE)
#define STORE_MAGIC 0x31535A54UL /* little endian "TZS1" */

void TDX_SPI_FLASH_E_4096Bytes(UINT16 index, UINT16 block, UINT16 slot_kib);
void TDX_SPI_FLASH_W_256Bytes(UINT8 *data, UINT16 index, UINT16 block, UINT16 slot_kib);

static struct {
    UINT8 page[STORE_PAGE];
    UINT32 length;
    UINT16 used, block;
    UINT8 index, active;
} store;

static int write_page(UINT16 block)
{
    UINT8 verify[STORE_PAGE];
    if(block && block % 16U == 0U)
        TDX_SPI_FLASH_E_4096Bytes(store.index, block, EXTERN_FLASH_PIC_SIZE);
    TDX_SPI_FLASH_W_256Bytes(store.page, store.index, block, EXTERN_FLASH_PIC_SIZE);
    Read256DataFromFlash(verify, store.index, block);
    return memcmp(verify, store.page, STORE_PAGE) ? -1 : 0;
}

int zlib_image_store_begin(UINT8 index)
{
    UINT8 i;
    zlib_image_store_abort();
    /* Includes both common-image slots; never cross the 4 MiB Flash limit. */
    if(index >= 4096U / EXTERN_FLASH_PIC_SIZE) return -1;
    store.index = index;
    store.block = 1;
    store.length = store.used = 0;
    TDX_SPI_FLASH_E_4096Bytes(index, 0, EXTERN_FLASH_PIC_SIZE);
    Read256DataFromFlash(store.page, index, 0);
    for(i = 0; i < 16U; i++) if(store.page[i] != 0xff) return -1;
    store.active = 1;
    return 0;
}

int zlib_image_store_feed(const UINT8 *data, UINT16 length)
{
    if(!store.active || !data || !length || length > STORE_CAPACITY - store.length) {
        zlib_image_store_abort();
        return -1;
    }
    store.length += length;
    while(length) {
        UINT16 count = STORE_PAGE - store.used;
        if(count > length) count = length;
        memcpy(store.page + store.used, data, count);
        store.used += count;
        data += count;
        length -= count;
        if(store.used == STORE_PAGE) {
            if(write_page(store.block++)) { zlib_image_store_abort(); return -1; }
            store.used = 0;
        }
    }
    return 0;
}

int zlib_image_store_finish(void)
{
    UINT32 header[4];
    if(!store.active || store.length < 6U) { zlib_image_store_abort(); return -1; }
    if(store.used) {
        memset(store.page + store.used, 0xff, STORE_PAGE - store.used);
        if(write_page(store.block)) { zlib_image_store_abort(); return -1; }
    }
    header[0] = STORE_MAGIC;
    header[1] = store.length;
    header[2] = ~store.length;
    header[3] = EPD_GetDisplayMaxBuf();
    memset(store.page, 0xff, STORE_PAGE);
    memcpy(store.page, header, sizeof(header));
    /* Commit last, without erasing the first sector again. */
    store.active = 0;
    return write_page(0);
}

void zlib_image_store_abort(void)
{
    store.active = 0;
    store.used = 0;
}

int zlib_image_store_replay(UINT8 index, zlib_image_codec_sink_t sink, void *context)
{
    UINT32 header[4], remaining;
    UINT16 block = 1;
    int result = -1;
    if(store.active || index >= 4096U / EXTERN_FLASH_PIC_SIZE) return -1;
    Read256DataFromFlash(store.page, index, 0);
    memcpy(header, store.page, sizeof(header));
    remaining = header[1];
    if(header[0] != STORE_MAGIC || header[2] != ~remaining ||
       remaining < 6U || remaining > STORE_CAPACITY || header[3] != EPD_GetDisplayMaxBuf())
        return -1;
    if(zlib_image_codec_begin(sink, context)) return -1;
    while(remaining) {
        UINT16 count = remaining > STORE_PAGE ? STORE_PAGE : (UINT16)remaining;
        Read256DataFromFlash(store.page, index, block++);
        /* Exclude page padding: the decoder rejects trailing input. */
        if(zlib_image_codec_feed(store.page, count)) goto done;
        remaining -= count;
    }
    result = zlib_image_codec_finish();
done:
    zlib_image_codec_abort();
    return result;
}
#endif
