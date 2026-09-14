#ifndef RELEASE_UART0_H
#define RELEASE_UART0_H

#include "app_cfg.h"

/* UART0 default routing: RX=PB4, TX=PB7. */
#ifndef RELEASE_UART0_BAUD_RATE
#define RELEASE_UART0_BAUD_RATE       115200UL
#endif

/* One slot is reserved to distinguish full from empty (usable: 255 bytes). */
#ifndef RELEASE_UART0_RX_BUFFER_SIZE
#define RELEASE_UART0_RX_BUFFER_SIZE  256U
#endif

#if APP_UART0_ENABLE
void release_uart0_init(void);
void release_uart0_write(const UINT8 *data, UINT16 length);
UINT16 release_uart0_available(void);
UINT16 release_uart0_read(UINT8 *data, UINT16 capacity);
UINT16 release_uart0_rx_overflow_count(void);
void release_uart0_clear_rx(void);
#else
#define release_uart0_init()                     do { } while(0)
#define release_uart0_write(data, length)        do { (void)(data); (void)(length); } while(0)
#define release_uart0_available()                0U
#define release_uart0_read(data, capacity)       0U
#define release_uart0_rx_overflow_count()        0U
#define release_uart0_clear_rx()                 do { } while(0)
#endif

#endif
