#pragma once

#include "usb_console_http_text.h"

esp_err_t UsbConsoleRestart_Handle(const usb_console_http_request_t *request,
                                   usb_console_http_response_t *response);
