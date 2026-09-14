#include "release_uart0.h"

#include "CH58x_uart.h"
#include "CH58x_gpio.h"

#if APP_UART0_ENABLE

#if (RELEASE_UART0_RX_BUFFER_SIZE != 256U)
#error "RELEASE_UART0_RX_BUFFER_SIZE must remain 256"
#endif

static volatile UINT8 release_uart0_rx_buffer[RELEASE_UART0_RX_BUFFER_SIZE];
static volatile UINT8 release_uart0_rx_read_index;
static volatile UINT8 release_uart0_rx_write_index;
static volatile UINT16 release_uart0_overflow;

static void release_uart0_rx_push(UINT8 value)
{
    UINT8 next = (UINT8)(release_uart0_rx_write_index + 1U);

    if(next == release_uart0_rx_read_index)
    {
        release_uart0_overflow++;
        return;
    }

    release_uart0_rx_buffer[release_uart0_rx_write_index] = value;
    release_uart0_rx_write_index = next;
}

void release_uart0_init(void)
{
    GPIOPinRemap(DISABLE, RB_PIN_UART0);
    GPIOB_SetBits(GPIO_Pin_7);
    GPIOB_ModeCfg(GPIO_Pin_4, GPIO_ModeIN_PU);
    GPIOB_ModeCfg(GPIO_Pin_7, GPIO_ModeOut_PP_5mA);

    release_uart0_rx_read_index = 0;
    release_uart0_rx_write_index = 0;
    release_uart0_overflow = 0;

    UART0_DefInit();
    UART0_BaudRateCfg(RELEASE_UART0_BAUD_RATE);
    UART0_ByteTrigCfg(UART_1BYTE_TRIG);
    UART0_INTCfg(ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
    PFIC_SetPriority(UART0_IRQn, 0x00);
    PFIC_EnableIRQ(UART0_IRQn);
}

void release_uart0_write(const UINT8 *data, UINT16 length)
{
    if(data && length)
    {
        UART0_SendString((UINT8 *)data, length);
    }
}

UINT16 release_uart0_available(void)
{
    return (UINT8)(release_uart0_rx_write_index - release_uart0_rx_read_index);
}

UINT16 release_uart0_read(UINT8 *data, UINT16 capacity)
{
    UINT16 length = 0;

    if(!data)
    {
        return 0;
    }

    while((length < capacity) && (release_uart0_rx_read_index != release_uart0_rx_write_index))
    {
        data[length++] = release_uart0_rx_buffer[release_uart0_rx_read_index++];
    }

    return length;
}

UINT16 release_uart0_rx_overflow_count(void)
{
    return release_uart0_overflow;
}

void release_uart0_clear_rx(void)
{
    release_uart0_rx_read_index = release_uart0_rx_write_index;
    release_uart0_overflow = 0;
    UART0_CLR_RXFIFO();
}

__INTERRUPT
__HIGH_CODE
void UART0_IRQHandler(void)
{
    UINT8 status = UART0_GetITFlag();

    if(status == UART_II_LINE_STAT)
    {
        (void)UART0_GetLinSTA();
    }

    while(R8_UART0_RFC)
    {
        release_uart0_rx_push(UART0_RecvByte());
    }
}

#endif
