#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t UsbConsoleTransport_Init(void);
int UsbConsoleTransport_Read(uint8_t *data, size_t data_size, TickType_t ticks_to_wait);
void UsbConsoleTransport_FlushRx(void);
esp_err_t UsbConsoleTransport_WriteAll(const void *data, size_t data_size, TickType_t ticks_to_wait);
