#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "tdx_cfg.h"

typedef struct {
    char method[USB_CONSOLE_HTTP_METHOD_MAX];
    char path[USB_CONSOLE_HTTP_PATH_MAX];
    char content_type[USB_CONSOLE_HTTP_CONTENT_TYPE_MAX];
    const char *body;
    size_t body_len;
    size_t content_length;
} usb_console_http_request_t;

typedef struct {
    int status;
    const char *reason;
    const char *content_type;
    char body[USB_CONSOLE_HTTP_RESPONSE_MAX];
} usb_console_http_response_t;

esp_err_t UsbConsoleHttp_TryParseRequest(char *data,
                                         size_t data_len,
                                         usb_console_http_request_t *request,
                                         size_t *request_len,
                                         bool *need_more);
void UsbConsoleHttp_SetJson(usb_console_http_response_t *response,
                            int status,
                            const char *reason,
                            const char *json);
esp_err_t UsbConsoleHttp_SendResponse(const usb_console_http_response_t *response);
