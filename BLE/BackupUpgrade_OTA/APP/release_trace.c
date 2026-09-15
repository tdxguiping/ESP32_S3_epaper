#include "release_trace.h"

#if APP_UART0_ENABLE && (UART0_BOOT_LOG_ENABLE || UART0_TRANSFER_LOG_ENABLE || \
                         UART0_BLE_LOG_ENABLE || UART0_IMAGE_LOG_ENABLE || \
                         UART0_BUSY_LOG_ENABLE || UART0_OTA_LOG_ENABLE || \
                         UART0_FAULT_LOG_ENABLE)
#include "release_uart0.h"

void release_trace_write(const UINT8 *data, UINT16 length)
{
    release_uart0_write(data, length);
}

static void release_trace_nibble(UINT8 value)
{
    static const char digits[] = "0123456789ABCDEF";
    UINT8 text = (UINT8)digits[value & 0x0fU];
    release_trace_write(&text, 1);
}

void release_trace_hex8(UINT8 value)
{
    release_trace_nibble(value >> 4);
    release_trace_nibble(value);
}

void release_trace_hex32(UINT32 value)
{
    UINT8 shift;
    for(shift = 28; ; shift -= 4){
        release_trace_nibble((UINT8)(value >> shift));
        if(shift == 0) break;
    }
}
#endif
