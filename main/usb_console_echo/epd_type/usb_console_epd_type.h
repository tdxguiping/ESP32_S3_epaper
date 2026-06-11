#pragma once

#include "esp_err.h"
#include "usb_console_http_text.h"

esp_err_t UsbConsoleEpdType_SendCurrent(void);
esp_err_t UsbConsoleEpdType_SendList(void);
esp_err_t UsbConsoleEpdType_HandleSet(const usb_console_http_request_t *request,
                                      usb_console_http_response_t *response);
esp_err_t UsbConsoleEpdType_HandleTest(const usb_console_http_request_t *request,
                                       usb_console_http_response_t *response);
