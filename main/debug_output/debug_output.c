#include "debug_output.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "tdx_cfg.h"

static const char *TAG = "debug_output";

static vprintf_like_t s_old_vprintf;
static SemaphoreHandle_t s_debug_output_mutex;

static void debug_output_write_uart0_line(const char *line, size_t write_len)
{
#if USER_DEBUG_UART0_ENABLED
    if (line == NULL || write_len == 0) {
        return;
    }

    if (s_debug_output_mutex != NULL) {
        xSemaphoreTake(s_debug_output_mutex, portMAX_DELAY);
    }

    if (line[write_len - 1] == '\n') {
        if (write_len > 1 && line[write_len - 2] == '\r') {
            (void)uart_write_bytes(USER_DEBUG_UART_PORT, line, write_len);
        } else {
            (void)uart_write_bytes(USER_DEBUG_UART_PORT, line, write_len - 1);
            (void)uart_write_bytes(USER_DEBUG_UART_PORT, "\r\n", 2);
        }
    } else {
        (void)uart_write_bytes(USER_DEBUG_UART_PORT, line, write_len);
        (void)uart_write_bytes(USER_DEBUG_UART_PORT, "\r\n", 2);
    }

    if (s_debug_output_mutex != NULL) {
        xSemaphoreGive(s_debug_output_mutex);
    }
#else
    (void)line;
    (void)write_len;
#endif
}

static int debug_output_vprintf(const char *fmt, va_list args)
{
    int ret = 0;
    va_list uart_args;
    va_copy(uart_args, args);

#if USER_DEBUG_OUTPUT_TARGET == USER_DEBUG_OUTPUT_BOTH
    if (s_old_vprintf != NULL) {
        va_list usb_args;
        va_copy(usb_args, args);
        ret = s_old_vprintf(fmt, usb_args);
        va_end(usb_args);
    }
#endif

    char line[USER_DEBUG_UART_LOG_LINE_MAX];
    int len = vsnprintf(line, sizeof(line), fmt, uart_args);
    va_end(uart_args);

    if (len > 0) {
        size_t write_len = (size_t)len;
        if (write_len >= sizeof(line)) {
            write_len = sizeof(line) - 1;
        }

        debug_output_write_uart0_line(line, write_len);
    }

#if USER_DEBUG_OUTPUT_TARGET == USER_DEBUG_OUTPUT_UART0
    ret = len;
#endif
    return ret;
}

void UserDebugOutput_Printf(const char *fmt, ...)
{
    char line[USER_DEBUG_UART_LOG_LINE_MAX];
    va_list args;

    va_start(args, fmt);
    int len = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    if (len <= 0) {
        return;
    }

    size_t write_len = (size_t)len;
    if (write_len >= sizeof(line)) {
        write_len = sizeof(line) - 1;
    }

#if (USER_DEBUG_OUTPUT_TARGET == USER_DEBUG_OUTPUT_USB_SERIAL_JTAG) || \
    (USER_DEBUG_OUTPUT_TARGET == USER_DEBUG_OUTPUT_BOTH)
    (void)fwrite(line, 1, write_len, stdout);
#endif
    debug_output_write_uart0_line(line, write_len);
}

esp_err_t UserDebugOutput_Init(void)
{
#if USER_DEBUG_UART0_ENABLED
    if (s_debug_output_mutex == NULL) {
        s_debug_output_mutex = xSemaphoreCreateMutex();
        if (s_debug_output_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (!uart_is_driver_installed(USER_DEBUG_UART_PORT)) {
        esp_err_t ret = uart_driver_install(USER_DEBUG_UART_PORT,
                                            USER_DEBUG_UART_RX_BUF_SIZE,
                                            USER_DEBUG_UART_TX_BUF_SIZE,
                                            0,
                                            NULL,
                                            0);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    const uart_config_t uart_config = {
        .baud_rate = USER_DEBUG_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_XTAL,
    };

    esp_err_t ret = uart_param_config(USER_DEBUG_UART_PORT, &uart_config);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = uart_set_pin(USER_DEBUG_UART_PORT,
                       USER_DEBUG_UART_TX_PIN,
                       USER_DEBUG_UART_RX_PIN,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        return ret;
    }

    s_old_vprintf = esp_log_set_vprintf(debug_output_vprintf);
    ESP_LOGI(TAG, "runtime log output target=%d uart=%d tx=%d rx=%d baud=%d",
             USER_DEBUG_OUTPUT_TARGET,
             USER_DEBUG_UART_PORT,
             USER_DEBUG_UART_TX_PIN,
             USER_DEBUG_UART_RX_PIN,
             USER_DEBUG_UART_BAUD_RATE);
#else
    ESP_LOGI(TAG, "runtime log output target=USB Serial/JTAG");
#endif
    return ESP_OK;
}
