#include "usb_console_transport.h"

#include <errno.h>
#include <unistd.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"
#include "tdx_cfg.h"

static const char *TAG = "usb_console_transport";

esp_err_t UsbConsoleTransport_Init(void)
{
    if (usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_vfs_use_nonblocking();
        ESP_RETURN_ON_ERROR(usb_serial_jtag_driver_uninstall(), TAG, "uninstall old USB Serial/JTAG driver failed");
    }

    // Use the SDK simple USB console VFS so protocol RX/TX never touches the driver's ringbuffers.
    // Chinese note: USB protocol RX/TX uses simple console VFS and avoids driver RX/TX ringbuffers.
    usb_serial_jtag_vfs_use_nonblocking();

    ESP_LOGI(TAG, "USB Serial/JTAG simple transport ready rx=%u tx=%u",
             (unsigned int)USB_CONSOLE_RX_BUF_SIZE,
             (unsigned int)USB_CONSOLE_TX_BUF_SIZE);
    return ESP_OK;
}

int UsbConsoleTransport_Read(uint8_t *data, size_t data_size, TickType_t ticks_to_wait)
{
    if (data == NULL || data_size == 0) {
        return 0;
    }

    size_t read_size = data_size > 64 ? 64 : data_size;

    // Read the hardware FIFO directly so USB RX does not take the VFS recursive lock.
    // Chinese note: USB RX reads the hardware FIFO directly and avoids the VFS read lock.
    int read_len = (int)usb_serial_jtag_ll_read_rxfifo(data, read_size);
    if (read_len > 0) {
        return read_len;
    }

    if (ticks_to_wait > 0) {
        // Sleep once for the caller timeout so the idle task can reset the watchdog.
        // Chinese note: no USB data means this task sleeps once, leaving CPU time for the idle watchdog.
        vTaskDelay(ticks_to_wait);
    }
    return 0;
}

void UsbConsoleTransport_FlushRx(void)
{
    uint8_t discard[64];

    while (usb_serial_jtag_ll_read_rxfifo(discard, sizeof(discard)) > 0) {
        vTaskDelay(1);
    }
}

esp_err_t UsbConsoleTransport_WriteAll(const void *data, size_t data_size, TickType_t ticks_to_wait)
{
    const uint8_t *cursor = (const uint8_t *)data;
    size_t left = data_size;
    TickType_t start_tick = xTaskGetTickCount();

    if (data == NULL && data_size > 0) {
        return ESP_ERR_INVALID_ARG;
    }

    while (left > 0) {
        int written = write(STDOUT_FILENO, cursor, left);
        if (written <= 0) {
            if ((xTaskGetTickCount() - start_tick) >= ticks_to_wait) {
                ESP_LOGW(TAG, "USB write timeout left=%u errno=%d", (unsigned int)left, errno);
                return ESP_ERR_TIMEOUT;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        cursor += written;
        left -= (size_t)written;
    }

    return ESP_OK;
}
