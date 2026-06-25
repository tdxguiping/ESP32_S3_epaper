#ifndef CH583_WIFI_UART_PROTOCOL_H
#define CH583_WIFI_UART_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

typedef void (*ch583_wifi_ble_data_callback_t)(const char *data);

#ifdef __cplusplus
extern "C" {
#endif

void ch583_wifi_uart_process_bytes(const uint8_t *data, size_t len, ch583_wifi_ble_data_callback_t ble_data_callback);
int ch583_wifi_uart_protocol_init(void);
int ch583_wifi_uart_send_wifi_data(const char *message);
const char *ch583_wifi_uart_get_ble_mac(void);
int ch583_wifi_uart_send_wake_timer_on(uint32_t seconds);
int ch583_wifi_uart_send_wake_timer_off(void);
int ch583_wifi_uart_send_power_off(void);
int ch583_wifi_uart_send_gpio(const char *port, int pin, const char *mode, const char *level);
int ch583_wifi_uart_send_gpio_read(const char *port, int pin);
int ch583_wifi_uart_send_led_blink(const char *led, uint32_t interval_ms);
int ch583_wifi_uart_send_led_blink_stop(const char *led);
int ch583_wifi_uart_test_gpio_pa1_high(void);

#ifdef __cplusplus
}
#endif

#endif
