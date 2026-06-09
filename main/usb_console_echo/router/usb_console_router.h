#pragma once

#include "esp_err.h"
#include "usb_console_http_text.h"

esp_err_t UsbConsoleRouter_Handle(const usb_console_http_request_t *request);

