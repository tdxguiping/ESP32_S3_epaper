#pragma once

#include "esp_err.h"
#include "usb_console_http_text.h"

esp_err_t UsbConsoleSavedImages_Handle(const usb_console_http_request_t *request,
                                       usb_console_http_response_t *response);
esp_err_t UsbConsoleSavedImages_Process(const usb_console_http_request_t *request,
                                       usb_console_http_response_t *response);
