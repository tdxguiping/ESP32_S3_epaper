#include "zlib_image_store.h"
#include "img_perf.h"

#if TDX_STORE_ZLIB
#include <string.h>
#include "flash_api.h"
#include "epd_driver.h"
#include "release_trace.h"

/* Each existing slot keeps its address/size. Page 0 is a commit record;
 * compressed bytes start at page 1. An erased record is never replayable. */
#define STORE_PAGE 256U
#define STORE_CAPACITY (EXTERN_FLASH_PIC_SIZE * 1024UL - STORE_PAGE)
#define STORE_MAGIC 0x31535A54UL /* little endian "TZS1" */

UINT8 TDX_SPI_FLASH_E_4096Bytes(UINT16 index, UINT16 block, UINT16 slot_kib);
UINT8 TDX_SPI_FLASH_W_256Bytes(UINT8 *data, UINT16 index, UINT16 block, UINT16 slot_kib);

static struct {
    UINT8 page[STORE_PAGE];
    UINT32 length;
    UINT16 used, block;
    UINT8 index, active;
} store;
static UINT32 store_write_ticks, store_write_pages;
static UINT32 store_read_ticks, store_read_pages, store_decode_ticks;
static UINT32 store_erase_count, store_first_erase_addr, store_last_erase_addr;
static UINT32 store_init_ticks, store_erase_ticks, store_program_ticks, store_verify_ticks, store_verify_pages;
static UINT16 store_fail_block, store_fail_diff;
static UINT8 store_fail_expected, store_fail_actual;

static UINT32 store_elapsed(UINT32 start)
{
    UINT32 now = RTC_GetCycle32k();
    return now >= start ? now - start : RTC_MAX_COUNT - start + now;
}

static UINT32 store_ms(UINT32 ticks)
{
    return (ticks / CAB_LSIFQ) * 1000U + ((ticks % CAB_LSIFQ) * 1000U) / CAB_LSIFQ;
}

static int verify_page(UINT16 block)
{
    UINT8 verify[STORE_PAGE];
    UINT16 i;
    UINT32 verify_start = RTC_GetCycle32k();
    UINT32 perf_verify = IP_Start(IP_VERIFY);

    Read256DataFromFlash(verify, store.index, block);
    IP_Toc(IP_VERIFY, perf_verify);
    store_verify_ticks += store_elapsed(verify_start);
    store_verify_pages++;
    if(memcmp(verify, store.page, STORE_PAGE) == 0) return 0;

    store_fail_block = block;
    store_fail_diff = 0xffffU;
    store_fail_expected = store_fail_actual = 0;
    for(i = 0; i < STORE_PAGE; i++) {
        if(verify[i] != store.page[i]) {
            store_fail_diff = i;
            store_fail_expected = store.page[i];
            store_fail_actual = verify[i];
            break;
        }
    }
    return -1;
}

static void store_log_write_fail(void)
{
    BOOT_LOG_TEXT("FLASH write-fail\r\n");
    BOOT_LOG_HEX32("FLASH block=", store_fail_block);
    BOOT_LOG_HEX32("FLASH used=", store.length);
    BOOT_LOG_HEX32("FLASH diff=", store_fail_diff);
    BOOT_LOG_HEX8("FLASH exp=", store_fail_expected);
    BOOT_LOG_HEX8("FLASH got=", store_fail_actual);
}

static int write_page(UINT16 block, UINT8 verify)
{
    UINT32 start = RTC_GetCycle32k();
    UINT32 phase_start;
    if(block && block % 16U == 0U) {
        store_erase_count++;
        store_last_erase_addr = (UINT32)EXTERN_FLASH_PIC_SIZE * 1024UL * store.index + (UINT32)block * 256UL;
        if(store_erase_count == 1) store_first_erase_addr = store_last_erase_addr;
        phase_start = RTC_GetCycle32k();
        if(TDX_SPI_FLASH_E_4096Bytes(store.index, block, EXTERN_FLASH_PIC_SIZE)) {
            store_fail_block = block;
            store_fail_diff = 0xffffU;
            store_fail_expected = store_fail_actual = 0;
            return -1;
        }
        store_erase_ticks += store_elapsed(phase_start);
    }
    phase_start = RTC_GetCycle32k();
    if(TDX_SPI_FLASH_W_256Bytes(store.page, store.index, block, EXTERN_FLASH_PIC_SIZE)) {
        store_fail_block = block;
        store_fail_diff = 0xffffU;
        store_fail_expected = store_fail_actual = 0;
        return -1;
    }
    store_program_ticks += store_elapsed(phase_start);
    store_write_ticks += store_elapsed(start);
    store_write_pages++;
    return verify ? verify_page(block) : 0;
}

int zlib_image_store_begin(UINT8 index)
{
    UINT8 i;
    UINT32 perf_verify;
    UINT32 init_start = RTC_GetCycle32k();
    UINT32 erase_start;
    int result = 0;
    zlib_image_store_abort();
    BOOT_LOG_TEXT("IMG SW=V28\r\n");
    /* Includes both common-image slots; never cross the 4 MiB Flash limit. */
    if(index >= 4096U / EXTERN_FLASH_PIC_SIZE) {
        BOOT_LOG_TEXT("FLASH index-fail\r\n");
        BOOT_LOG_HEX32("FLASH index=", index);
        return -1;
    }
    store.index = index;
    store.block = 1;
    store.length = store.used = 0;
    store_write_ticks = store_write_pages = 0;
    store_init_ticks = store_erase_ticks = store_program_ticks = store_verify_ticks = store_verify_pages = 0;
    store_erase_count = store_first_erase_addr = store_last_erase_addr = 0;
    store_fail_block = store_fail_diff = 0;
    store_fail_expected = store_fail_actual = 0;
    store_erase_count = 1;
    store_first_erase_addr = store_last_erase_addr = (UINT32)EXTERN_FLASH_PIC_SIZE * 1024UL * index;
    erase_start = RTC_GetCycle32k();
    if(TDX_SPI_FLASH_E_4096Bytes(index, 0, EXTERN_FLASH_PIC_SIZE)) {
        BOOT_LOG_TEXT("FLASH erase-fail\r\n");
        BOOT_LOG_HEX32("FLASH eraseAddr=", store_first_erase_addr);
        return -1;
    }
    store_erase_ticks = store_elapsed(erase_start);
    perf_verify = IP_Start(IP_VERIFY);
    Read256DataFromFlash(store.page, index, 0);
    for(i = 0; i < 16U; i++) if(store.page[i] != 0xff) { result = -1; break; }
    IP_Toc(IP_VERIFY, perf_verify);
    store_init_ticks = store_elapsed(init_start);
    if(result) {
        BOOT_LOG_TEXT("FLASH begin-blank-fail\r\n");
        BOOT_LOG_HEX32("FLASH begin-pos=", i);
        BOOT_LOG_HEX8("FLASH begin-byte=", store.page[i]);
        return result;
    }
    store.active = 1;
    return 0;
}

int zlib_image_store_feed(const UINT8 *data, UINT16 length)
{
    if(!store.active || !data || !length || length > STORE_CAPACITY - store.length) {
        BOOT_LOG_TEXT("FLASH feed-fail\r\n");
        BOOT_LOG_HEX8("FLASH active=", store.active);
        BOOT_LOG_HEX32("FLASH block=", store.block);
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
            UINT16 block = store.block++;
            if(write_page(block, block == 1U || block % 16U == 0U)) {
                store_log_write_fail();
                zlib_image_store_abort(); return -1;
            }
            store.used = 0;
        }
    }
    return 0;
}

int zlib_image_store_finish(void)
{
    UINT32 header[4];
    UINT16 last_block;
    if(!store.active || store.length < 6U) { zlib_image_store_abort(); return -1; }
    if(store.used) {
        memset(store.page + store.used, 0xff, STORE_PAGE - store.used);
        if(write_page(store.block, 1U)) { store_log_write_fail(); zlib_image_store_abort(); return -1; }
    } else {
        last_block = store.block - 1U;
        if(verify_page(last_block)) { store_log_write_fail(); zlib_image_store_abort(); return -1; }
    }
    header[0] = STORE_MAGIC;
    header[1] = store.length;
    header[2] = ~store.length;
    header[3] = EPD_GetDisplayMaxBuf();
    memset(store.page, 0xff, STORE_PAGE);
    memcpy(store.page, header, sizeof(header));
    /* Commit last, without erasing the first sector again. */
    store.active = 0;
    if(write_page(0, 1U)) { store_log_write_fail(); return -1; }
    return 0;
}

void zlib_image_store_abort(void)
{
    store.active = 0;
    store.used = 0;
}

int zlib_image_store_replay(UINT8 index, zlib_image_codec_sink_t sink, void *context)
{
    UINT32 header[4], remaining, perf_read, start;
    UINT16 block = 1;
    int result = -1;
    store_read_ticks = store_read_pages = store_decode_ticks = 0;
    if(store.active || index >= 4096U / EXTERN_FLASH_PIC_SIZE) return -1;
    perf_read = IP_Start(IP_READ);
    start = RTC_GetCycle32k();
    Read256DataFromFlash(store.page, index, 0);
    store_read_ticks += store_elapsed(start); store_read_pages++;
    IP_Toc(IP_READ, perf_read);
    memcpy(header, store.page, sizeof(header));
    remaining = header[1];
    if(header[0] != STORE_MAGIC || header[2] != ~remaining ||
       remaining < 6U || remaining > STORE_CAPACITY || header[3] != EPD_GetDisplayMaxBuf())
        return -1;
    if(zlib_image_codec_begin(sink, context)) return -1;
    while(remaining) {
        UINT16 count = remaining > STORE_PAGE ? STORE_PAGE : (UINT16)remaining;
        perf_read = IP_Start(IP_READ);
        start = RTC_GetCycle32k();
        Read256DataFromFlash(store.page, index, block++);
        store_read_ticks += store_elapsed(start); store_read_pages++;
        IP_Toc(IP_READ, perf_read);
        /* Exclude page padding: the decoder rejects trailing input. */
        start = RTC_GetCycle32k();
        if(zlib_image_codec_feed(store.page, count)) goto done;
        store_decode_ticks += store_elapsed(start);
        remaining -= count;
    }
    result = zlib_image_codec_finish();
done:
    zlib_image_codec_abort();
    if(result == 0) {
        BOOT_LOG_TEXT("IMG SW=V27\r\n");
#if TDX_LARGE_RAW_COLOR_DISABLE && TDX_LARGE_RAW_DIRECT
        BOOT_LOG_HEX8("IMG RAW=", 1);
#endif
        BOOT_LOG_HEX32("IMG FS=", store.index);
        BOOT_LOG_HEX32("IMG FB=", 1);
        BOOT_LOG_HEX32("IMG DA=", (UINT32)EXTERN_FLASH_PIC_SIZE * 1024UL * store.index + 256UL);
        BOOT_LOG_HEX32("T FW=", store_ms(store_write_ticks));
        BOOT_LOG_HEX32("T FI=", store_ms(store_init_ticks));
        BOOT_LOG_HEX32("T FET=", store_ms(store_erase_ticks));
        BOOT_LOG_HEX32("T FP=", store_ms(store_program_ticks));
        BOOT_LOG_HEX32("T FV=", store_ms(store_verify_ticks));
        BOOT_LOG_HEX32("T VC=", store_verify_pages);
        BOOT_LOG_HEX32("T WP=", store_write_pages);
        BOOT_LOG_HEX32("T FE=", store_erase_count);
        BOOT_LOG_HEX32("T EA=", store_first_erase_addr);
        BOOT_LOG_HEX32("T EZ=", store_last_erase_addr);
        BOOT_LOG_HEX32("T FR=", store_ms(store_read_ticks));
        BOOT_LOG_HEX32("T RP=", store_read_pages);
        BOOT_LOG_HEX32("T FD=", store_ms(store_decode_ticks));
    }
    return result;
}
#endif
