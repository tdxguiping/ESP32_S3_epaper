#ifndef RELEASE_TRACE_H
#define RELEASE_TRACE_H

#include "app_cfg.h"

/* Lightweight literal/hex UART output; it never links printf formatting. */
#if APP_UART0_ENABLE && (UART0_BOOT_LOG_ENABLE || UART0_TRANSFER_LOG_ENABLE)
void release_trace_write(const UINT8 *data, UINT16 length);
void release_trace_hex8(UINT8 value);
void release_trace_hex32(UINT32 value);

#define RELEASE_TRACE_TEXT(text) do { \
    static const char trace_literal[] = text; \
    release_trace_write((const UINT8 *)trace_literal, (UINT16)(sizeof(trace_literal) - 1U)); \
} while(0)

#define RELEASE_TRACE_HEX8(label, value) do { \
    RELEASE_TRACE_TEXT(label); \
    release_trace_hex8((UINT8)(value)); \
    RELEASE_TRACE_TEXT("\r\n"); \
} while(0)

#define RELEASE_TRACE_HEX32(label, value) do { \
    RELEASE_TRACE_TEXT(label); \
    release_trace_hex32((UINT32)(value)); \
    RELEASE_TRACE_TEXT("\r\n"); \
} while(0)

#if UART0_BOOT_LOG_ENABLE
#define BOOT_LOG_TEXT(text)             RELEASE_TRACE_TEXT(text)
#define BOOT_LOG_HEX8(label, value)     RELEASE_TRACE_HEX8(label, value)
#define BOOT_LOG_HEX32(label, value)    RELEASE_TRACE_HEX32(label, value)
#else
#define BOOT_LOG_TEXT(text)             do { } while(0)
#define BOOT_LOG_HEX8(label, value)     do { } while(0)
#define BOOT_LOG_HEX32(label, value)    do { } while(0)
#endif

#if UART0_TRANSFER_LOG_ENABLE
#define TRANSFER_LOG_TEXT(text)          RELEASE_TRACE_TEXT(text)
#define TRANSFER_LOG_HEX8(label, value)  RELEASE_TRACE_HEX8(label, value)
#define TRANSFER_LOG_HEX32(label, value) RELEASE_TRACE_HEX32(label, value)
#else
#define TRANSFER_LOG_TEXT(text)          do { } while(0)
#define TRANSFER_LOG_HEX8(label, value)  do { } while(0)
#define TRANSFER_LOG_HEX32(label, value) do { } while(0)
#endif

#define TRACE_TEXT(text)             RELEASE_TRACE_TEXT(text)
#define TRACE_HEX8(label, value)     RELEASE_TRACE_HEX8(label, value)
#define TRACE_HEX32(label, value)    RELEASE_TRACE_HEX32(label, value)
#else
#define BOOT_LOG_TEXT(text)              do { } while(0)
#define BOOT_LOG_HEX8(label, value)      do { } while(0)
#define BOOT_LOG_HEX32(label, value)     do { } while(0)
#define TRANSFER_LOG_TEXT(text)          do { } while(0)
#define TRANSFER_LOG_HEX8(label, value)  do { } while(0)
#define TRANSFER_LOG_HEX32(label, value) do { } while(0)
#define TRACE_TEXT(text)                 do { } while(0)
#define TRACE_HEX8(label, value)         do { } while(0)
#define TRACE_HEX32(label, value)        do { } while(0)
#endif

#endif
