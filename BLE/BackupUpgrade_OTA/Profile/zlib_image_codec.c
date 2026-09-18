/*
 * ZLIB_COLOR_V1 decoder.  This file deliberately contains the complete
 * inflater so transport, Flash and display code never need to know DEFLATE
 * state.  It accepts RFC1950/RFC1951 zlib streams with a 4 KiB window.
 */
#include <string.h>
#include "app_cfg.h"
#include "epd_driver.h"
#include "release_trace.h"
#include "img_perf.h"
#include "zlib_image_codec.h"
#include "tdx_image_stream.h"

#define ZIC_WINDOW             4096U
#if TDX_STORE_ZLIB
#define ZIC_OUT_BUFFER         IMG_ZLIB_BATCH
#else
#define ZIC_OUT_BUFFER          256U
#endif
#define ZIC_MAX_BITS             15U

enum {
    ZIC_HEADER, ZIC_BLOCK, ZIC_STORED_LEN, ZIC_STORED,
    ZIC_DYN_HEADER, ZIC_DYN_CLEN, ZIC_DYN_LENS, ZIC_CODES,
    ZIC_LEN_EXTRA, ZIC_DIST, ZIC_DIST_EXTRA, ZIC_MATCH,
    ZIC_TRAILER, ZIC_DONE, ZIC_BAD
};

typedef struct {
    UINT16 count[ZIC_MAX_BITS + 1];
    UINT16 first_code[ZIC_MAX_BITS + 1];
    UINT16 first_symbol[ZIC_MAX_BITS + 1];
    UINT16 symbols[320];
} zic_tree_t;

typedef struct {
    const UINT8 *in;
    UINT16 in_left;
    UINT32 bits;
    UINT8 nbits;
    UINT8 state, final, rle_byte, rle_pending;
    UINT16 stored, hlit, hdist, hclen, dyn_index, dyn_total, dyn_last;
    UINT16 repeat_base;
    UINT8 repeat_extra, repeat_value;
    UINT16 match_len, match_dist;
    UINT8 match_extra, dist_extra;
    UINT32 adler_s1, adler_s2, inflated, raw, raw_limit, inner_limit;
    UINT8 window[ZIC_WINDOW];
    UINT16 window_pos;
    UINT8 out[ZIC_OUT_BUFFER];
    UINT16 out_len;
    UINT8 lens[320];
    zic_tree_t litlen;
    /* The code-length tree is only needed while building a dynamic block;
     * reuse this storage for its final distance tree. */
    zic_tree_t dist;
    zlib_image_codec_sink_t sink;
    void *sink_context;
} zic_context_t;

static zic_context_t zic;
#if TDX_SMALL_STREAM_ENABLE
static UINT32 zic_budget_end;
static UINT8 zic_budgeted;
#define ZIC_YIELD_CHECK() do { if(zic_budgeted && zic.raw >= zic_budget_end) return 2; } while(0)
#else
#define ZIC_YIELD_CHECK() ((void)0)
#endif

static const UINT16 zic_len_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,
    115,131,163,195,227,258
};
static const UINT8 zic_len_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const UINT16 zic_dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
    1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const UINT8 zic_dist_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};
static const UINT8 zic_cl_order[19] = {
    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
};

static int zic_need(UINT8 n)
{
    while(zic.nbits < n && zic.in_left){
        zic.bits |= ((UINT32)*zic.in++) << zic.nbits;
        zic.in_left--;
        zic.nbits += 8;
    }
    return zic.nbits >= n;
}

static UINT32 zic_get(UINT8 n)
{
    UINT32 v = zic.bits & ((((UINT32)1) << n) - 1U);
    zic.bits >>= n;
    zic.nbits -= n;
    return v;
}

static int zic_tree_build(zic_tree_t *tree, const UINT8 *lengths, UINT16 count)
{
    UINT16 next[ZIC_MAX_BITS + 1];
    UINT16 code = 0, slot = 0, i;
    UINT8 len;
    int any = 0;

    memset(tree, 0, sizeof(*tree));
    for(i = 0; i < count; i++){
        if(lengths[i] > ZIC_MAX_BITS) return -1;
        if(lengths[i]) { tree->count[lengths[i]]++; any = 1; }
    }
    if(!any) return -1;
    for(i = 1; i <= ZIC_MAX_BITS; i++){
        code = (UINT16)((code + tree->count[i - 1]) << 1);
        if((UINT32)code + tree->count[i] > ((UINT32)1 << i)) return -1;
        tree->first_code[i] = code;
        tree->first_symbol[i] = slot;
        next[i] = slot;
        slot = (UINT16)(slot + tree->count[i]);
    }
    for(i = 0; i < count; i++){
        len = lengths[i];
        if(!len) continue;
        tree->symbols[next[len]++] = i;
    }
    return 0;
}

static int zic_decode(zic_tree_t *tree, UINT16 *symbol)
{
    UINT16 code = 0;
    UINT8 bits;
    /* Do not consume a partial code at a BLE packet boundary. */
    if(!zic_need(ZIC_MAX_BITS)) return 0;
    for(bits = 1; bits <= ZIC_MAX_BITS; bits++){
        code = (UINT16)((code << 1) | zic_get(1));
        if(code >= tree->first_code[bits] &&
           code - tree->first_code[bits] < tree->count[bits]){
            *symbol = tree->symbols[tree->first_symbol[bits] + code - tree->first_code[bits]];
            return 1;
        }
    }
    return -1;
}

static int zic_flush(void)
{
    if(zic.out_len) {
        UINT32 perf_output = IP_Start(IP_OUTPUT);
        int result = zic.sink ? zic.sink(zic.sink_context, zic.out, zic.out_len) : -1;
        IP_Toc(IP_OUTPUT, perf_output);
        if(result != 0) return -1;
    }
    zic.out_len = 0;
    return 0;
}

static int zic_raw_byte(UINT8 byte)
{
    if(zic.raw >= zic.raw_limit) return -1;
    zic.out[zic.out_len++] = byte;
    zic.raw++;
    if(zic.out_len == ZIC_OUT_BUFFER) return zic_flush();
    return 0;
}

static int zic_image_byte(UINT8 byte)
{
#if defined(ENABLE_SCREEN_COLOR_2) || defined(ENABLE_SCREEN_COLOR_3) || defined(ENABLE_SCREEN_COLOR_4)
    if(zic.rle_pending){
        UINT16 n = (UINT16)byte + 1U;
        zic.rle_pending = 0;
        while(n--) if(zic_raw_byte(zic.rle_byte)) return -1;
        return 0;
    }
    if(byte == 0x00 || byte == 0xff
#if defined(ENABLE_SCREEN_COLOR_4)
       || byte == 0x55 || byte == 0xaa
#endif
      ){
        zic.rle_byte = byte;
        zic.rle_pending = 1;
        return 0;
    }
#endif
    return zic_raw_byte(byte);
}

static int zic_emit(UINT8 byte)
{
    if(zic.inflated >= zic.inner_limit) return -1;
    zic.window[zic.window_pos++] = byte;
    zic.window_pos &= (ZIC_WINDOW - 1U);
    zic.inflated++;
    zic.adler_s1 += byte;
    if(zic.adler_s1 >= 65521U) zic.adler_s1 -= 65521U;
    zic.adler_s2 += zic.adler_s1;
    if(zic.adler_s2 >= 65521U) zic.adler_s2 -= 65521U;
    return zic_image_byte(byte);
}

static int zic_fixed_trees(void)
{
    UINT16 i;
    for(i = 0; i <= 143; i++) zic.lens[i] = 8;
    for(; i <= 255; i++) zic.lens[i] = 9;
    for(; i <= 279; i++) zic.lens[i] = 7;
    for(; i <= 287; i++) zic.lens[i] = 8;
    if(zic_tree_build(&zic.litlen, zic.lens, 288)) return -1;
    for(i = 0; i < 32; i++) zic.lens[i] = 5;
    return zic_tree_build(&zic.dist, zic.lens, 32);
}

static int zic_dynamic_trees(void)
{
    if(zic.lens[256] == 0) return -1;
    if(zic_tree_build(&zic.litlen, zic.lens, zic.hlit)) return -1;
    return zic_tree_build(&zic.dist, zic.lens + zic.hlit, zic.hdist);
}

static int zic_run(void)
{
    UINT16 sym, value;
    UINT8 btype;
    int rc;
    for(;;){
        ZIC_YIELD_CHECK();
        switch(zic.state){
        case ZIC_HEADER:
            if(!zic_need(16)) return 0;
            value = (UINT16)zic_get(8); btype = (UINT8)zic_get(8);
            if((value & 0x0fU) != 8 || (value >> 4) != 4 || ((UINT16)value * 256U + btype) % 31U || (btype & 0x20U)) return -1;
            zic.state = ZIC_BLOCK;
            break;
        case ZIC_BLOCK:
            if(!zic_need(3)) return 0;
            zic.final = (UINT8)zic_get(1); btype = (UINT8)zic_get(2);
            if(btype == 0){ zic.bits >>= (zic.nbits & 7U); zic.nbits &= (UINT8)~7U; zic.state = ZIC_STORED_LEN; }
            else if(btype == 1){ if(zic_fixed_trees()) return -1; zic.state = ZIC_CODES; }
            else if(btype == 2) zic.state = ZIC_DYN_HEADER;
            else return -1;
            break;
        case ZIC_STORED_LEN:
            if(!zic_need(32)) return 0;
            zic.stored = (UINT16)zic_get(16);
            if((UINT16)zic_get(16) != (UINT16)~zic.stored) return -1;
            zic.state = ZIC_STORED;
            break;
        case ZIC_STORED:
            while(zic.stored){
                ZIC_YIELD_CHECK();
                if(!zic_need(8)) return 0;
                if(zic_emit((UINT8)zic_get(8))) return -1;
                zic.stored--;
            }
            zic.state = zic.final ? ZIC_TRAILER : ZIC_BLOCK;
            break;
        case ZIC_DYN_HEADER:
            if(!zic_need(14)) return 0;
            zic.hlit = (UINT16)zic_get(5) + 257U;
            zic.hdist = (UINT16)zic_get(5) + 1U;
            zic.hclen = (UINT16)zic_get(4) + 4U;
            memset(zic.lens, 0, sizeof(zic.lens)); zic.dyn_index = 0;
            zic.state = ZIC_DYN_CLEN;
            break;
        case ZIC_DYN_CLEN:
            while(zic.dyn_index < zic.hclen){
                if(!zic_need(3)) return 0;
                zic.lens[zic_cl_order[zic.dyn_index++]] = (UINT8)zic_get(3);
            }
            if(zic_tree_build(&zic.dist, zic.lens, 19)) return -1;
            zic.dyn_total = zic.hlit + zic.hdist; zic.dyn_index = 0; zic.dyn_last = 0; zic.repeat_extra = 0;
            zic.state = ZIC_DYN_LENS;
            break;
        case ZIC_DYN_LENS:
            if(zic.repeat_extra){
                if(!zic_need(zic.repeat_extra)) return 0;
                value = (UINT16)(zic.repeat_base + zic_get(zic.repeat_extra));
                if((UINT32)zic.dyn_index + value > zic.dyn_total) return -1;
                while(value--) zic.lens[zic.dyn_index++] = zic.repeat_value;
                zic.repeat_extra = 0;
                if(zic.dyn_index == zic.dyn_total){ if(zic_dynamic_trees()) return -1; zic.state = ZIC_CODES; }
                break;
            }
            rc = zic_decode(&zic.dist, &sym); if(!rc) return 0; if(rc < 0) return -1;
            if(sym < 16){ zic.lens[zic.dyn_index++] = (UINT8)sym; zic.dyn_last = sym; }
            else if(sym == 16){
                if(!zic.dyn_index) return -1;
                zic.repeat_base = 3; zic.repeat_extra = 2; zic.repeat_value = (UINT8)zic.dyn_last;
            } else if(sym == 17){ zic.repeat_base = 3; zic.repeat_extra = 3; zic.repeat_value = 0; }
            else if(sym == 18){ zic.repeat_base = 11; zic.repeat_extra = 7; zic.repeat_value = 0; }
            else return -1;
            if(zic.dyn_index == zic.dyn_total){ if(zic_dynamic_trees()) return -1; zic.state = ZIC_CODES; }
            break;
        case ZIC_CODES:
            rc = zic_decode(&zic.litlen, &sym); if(!rc) return 0; if(rc < 0) return -1;
            if(sym < 256){ if(zic_emit((UINT8)sym)) return -1; }
            else if(sym == 256) zic.state = zic.final ? ZIC_TRAILER : ZIC_BLOCK;
            else if(sym <= 285){
                sym -= 257; zic.match_len = zic_len_base[sym]; zic.match_extra = zic_len_extra[sym]; zic.state = ZIC_LEN_EXTRA;
            } else return -1;
            break;
        case ZIC_LEN_EXTRA:
            if(zic.match_extra){ if(!zic_need(zic.match_extra)) return 0; zic.match_len += (UINT16)zic_get(zic.match_extra); }
            zic.state = ZIC_DIST;
            break;
        case ZIC_DIST:
            rc = zic_decode(&zic.dist, &sym); if(!rc) return 0; if(rc < 0 || sym >= 30) return -1;
            zic.match_dist = zic_dist_base[sym]; zic.dist_extra = zic_dist_extra[sym]; zic.state = ZIC_DIST_EXTRA;
            break;
        case ZIC_DIST_EXTRA:
            if(zic.dist_extra){ if(!zic_need(zic.dist_extra)) return 0; zic.match_dist += (UINT16)zic_get(zic.dist_extra); }
            if(!zic.match_dist || zic.match_dist > ZIC_WINDOW || zic.match_dist > zic.inflated) return -1;
            zic.state = ZIC_MATCH;
            break;
        case ZIC_MATCH:
            while(zic.match_len){
                ZIC_YIELD_CHECK();
                value = zic.window[(zic.window_pos + ZIC_WINDOW - zic.match_dist) & (ZIC_WINDOW - 1U)];
                if(zic_emit((UINT8)value)) return -1;
                zic.match_len--;
            }
            zic.state = ZIC_CODES;
            break;
        case ZIC_TRAILER:
            zic.bits >>= (zic.nbits & 7U); zic.nbits &= (UINT8)~7U;
            if(!zic_need(32)) return 0;
            value = (UINT16)zic_get(16);
            /* Adler is big-endian in the byte stream, get() reads little endian. */
            {
                UINT16 hi = (UINT16)zic_get(16);
                UINT32 expected = ((UINT32)((value >> 8) | (value << 8)) << 16) |
                                  (UINT16)((hi >> 8) | (hi << 8));
                if(expected != ((zic.adler_s2 << 16) | zic.adler_s1)) return -1;
            }
            zic.state = ZIC_DONE;
            break;
        case ZIC_DONE:
            return zic.in_left ? -1 : 1;
        default:
            return -1;
        }
    }
}

int zlib_image_codec_begin(zlib_image_codec_sink_t sink, void *context)
{
    UINT32 raw = EPD_GetDisplayMaxBuf();
    if(!sink || !raw) return -1;
    memset(&zic, 0, sizeof(zic));
#if TDX_SMALL_STREAM_ENABLE
    zic_budgeted = 0;
#endif
    zic.sink = sink; zic.sink_context = context;
    zic.state = ZIC_HEADER; zic.adler_s1 = 1;
    zic.raw_limit = raw;
#if defined(ENABLE_SCREEN_COLOR_6)
    zic.inner_limit = raw;
#else
    zic.inner_limit = raw * 2U;
#endif
    return 0;
}

int zlib_image_codec_feed(const UINT8 *data, UINT16 length)
{
    int rc;
    if(!data || !length || zic.state == ZIC_BAD || zic.state == ZIC_DONE) return -1;
    zic.in = data; zic.in_left = length;
    UINT32 perf_decode = IP_Start(IP_DECODE), perf_output = IP_Value(IP_OUTPUT);
    rc = zic_run();
    IP_Add(IP_DECODE, IP_Diff(IP_Now(), perf_decode) - (IP_Value(IP_OUTPUT) - perf_output));
    if(rc < 0 || (rc > 0 && zic.in_left)){
        TRACE_TEXT("ZLIB feed error\r\n");
        zic.state = ZIC_BAD;
        return -1;
    }
    return 0;
}

int zlib_image_codec_finish(void)
{
    UINT32 perf_decode = IP_Start(IP_DECODE), perf_output = IP_Value(IP_OUTPUT);
    int failed = zic.state == ZIC_BAD || zic_run() != 1 || zic.rle_pending ||
                 zic.raw != zic.raw_limit || zic_flush();
    IP_Add(IP_DECODE, IP_Diff(IP_Now(), perf_decode) - (IP_Value(IP_OUTPUT) - perf_output));
    if(failed){
        TRACE_HEX32("ZLIB finish raw=", zic.raw);
        TRACE_TEXT("ZLIB finish error\r\n");
        zic.state = ZIC_BAD;
        return -1;
    }
    return 0;
}

void zlib_image_codec_abort(void)
{
    memset(&zic, 0, sizeof(zic));
    zic.state = ZIC_BAD;
}

#if TDX_SMALL_STREAM_ENABLE
/* 0=need input, 1=stream end, 2=yield, -1=error. No input pointer is retained
 * after returning: the caller releases exactly *consumed FIFO bytes. */
int zlib_image_codec_pump(const UINT8 *data, UINT16 length, UINT16 *consumed)
{
    int rc;
    UINT32 start = IP_Start(IP_DECODE), output = IP_Value(IP_OUTPUT);
    if(!consumed || (!data && length) || zic.state == ZIC_BAD) return -1;
    *consumed = 0;
    zic.in = data; zic.in_left = length;
    {
        UINT32 remaining = ZIC_OUT_BUFFER - zic.out_len;
        UINT32 budget = remaining > TDX_STREAM_WORK_LIMIT ? TDX_STREAM_WORK_LIMIT : remaining;
        zic_budget_end = zic.raw + budget;
    }
    zic_budgeted = 1;
    rc = zic_run();
    zic_budgeted = 0;
    *consumed = length - zic.in_left;
    zic.in = NULL; zic.in_left = 0;
    IP_Add(IP_DECODE, IP_Diff(IP_Now(), start) - (IP_Value(IP_OUTPUT) - output));
    if(rc < 0) zic.state = ZIC_BAD;
    return rc;
}
#endif
