#include "usb_console_http_text.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "usb_console_transport.h"

static const char *TAG = "usb_console_http";

static char *find_header_end(char *data, size_t data_len, size_t *header_len)
{
    for (size_t i = 0; i + 3 < data_len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n') {
            *header_len = i + 4;
            return &data[i];
        }
    }

    for (size_t i = 0; i + 1 < data_len; i++) {
        if (data[i] == '\n' && data[i + 1] == '\n') {
            *header_len = i + 2;
            return &data[i];
        }
    }

    return NULL;
}

static bool header_name_equals(const char *line, const char *name)
{
    size_t name_len = strlen(name);
    return strncasecmp(line, name, name_len) == 0 && line[name_len] == ':';
}

static esp_err_t parse_content_length(const char *headers, size_t *out_len, bool *out_present)
{
    const char *line = headers;

    if (headers == NULL || out_len == NULL || out_present == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_len = 0;
    *out_present = false;

    while (line != NULL && *line != '\0') {
        const char *next = strstr(line, "\n");
        if (header_name_equals(line, "Content-Length")) {
            const char *value = strchr(line, ':');
            if (value != NULL) {
                char *end_ptr = NULL;
                const char *line_end = next != NULL ? next : line + strlen(line);

                value++;
                while (value < line_end && (*value == ' ' || *value == '\t')) {
                    value++;
                }
                if (value >= line_end || *value < '0' || *value > '9') {
                    return ESP_ERR_INVALID_ARG;
                }

                errno = 0;
                unsigned long parsed = strtoul(value, &end_ptr, 10);
                if (errno != 0 || end_ptr == value) {
                    return ESP_ERR_INVALID_ARG;
                }
                while (end_ptr < line_end && (*end_ptr == ' ' || *end_ptr == '\t' || *end_ptr == '\r')) {
                    end_ptr++;
                }
                if (end_ptr != line_end) {
                    return ESP_ERR_INVALID_ARG;
                }

                *out_len = (size_t)parsed;
                *out_present = true;
                return ESP_OK;
            }
        }
        if (next == NULL) {
            break;
        }
        line = next + 1;
    }

    return ESP_OK;
}

static bool method_requires_content_length(const char *method)
{
    return strcasecmp(method, "POST") == 0 ||
           strcasecmp(method, "PUT") == 0 ||
           strcasecmp(method, "PATCH") == 0;
}

static void parse_header_string(const char *headers, const char *name, char *out, size_t out_size)
{
    const char *line = headers;
    size_t name_len = name != NULL ? strlen(name) : 0;

    if (headers == NULL || name == NULL || out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';

    while (line != NULL && *line != '\0') {
        const char *next = strstr(line, "\n");
        if (strncasecmp(line, name, name_len) == 0 && line[name_len] == ':') {
            const char *value = line + name_len + 1;
            while (*value == ' ' || *value == '\t') {
                value++;
            }

            const char *end = next != NULL ? next : line + strlen(line);
            while (end > value && (end[-1] == '\r' || end[-1] == '\n' || end[-1] == ' ' || end[-1] == '\t')) {
                end--;
            }

            size_t copy_len = (size_t)(end - value);
            if (copy_len >= out_size) {
                copy_len = out_size - 1;
            }
            memcpy(out, value, copy_len);
            out[copy_len] = '\0';
            return;
        }
        if (next == NULL) {
            break;
        }
        line = next + 1;
    }
}

esp_err_t UsbConsoleHttp_TryParseRequest(char *data,
                                         size_t data_len,
                                         usb_console_http_request_t *request,
                                         size_t *request_len,
                                         bool *need_more)
{
    size_t header_len = 0;
    char *header_end = NULL;
    char *line_end = NULL;
    char *method_end = NULL;
    char *path_end = NULL;
    size_t content_length = 0;
    bool content_length_present = false;

    if (data == NULL || request == NULL || request_len == NULL || need_more == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *need_more = false;
    memset(request, 0, sizeof(*request));

    header_end = find_header_end(data, data_len, &header_len);
    if (header_end == NULL) {
        if (data_len > USB_CONSOLE_HTTP_HEADER_MAX) {
            ESP_LOGW(TAG, "USB request header too large len=%u", (unsigned int)data_len);
            return ESP_ERR_INVALID_SIZE;
        }
        *need_more = true;
        return ESP_OK;
    }

    line_end = memchr(data, '\n', header_len);
    if (line_end == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    method_end = memchr(data, ' ', (size_t)(line_end - data));
    if (method_end == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    path_end = memchr(method_end + 1, ' ', (size_t)(line_end - method_end - 1));
    if (path_end == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t method_len = (size_t)(method_end - data);
    size_t path_len = (size_t)(path_end - method_end - 1);
    if (method_len == 0 || method_len >= sizeof(request->method) ||
        path_len == 0 || path_len >= sizeof(request->path)) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(request->method, data, method_len);
    request->method[method_len] = '\0';
    memcpy(request->path, method_end + 1, path_len);
    request->path[path_len] = '\0';

    char saved_after_header = data[header_len];
    data[header_len] = '\0';
    esp_err_t length_ret = parse_content_length(data, &content_length, &content_length_present);
    parse_header_string(data, "Content-Type", request->content_type, sizeof(request->content_type));
    data[header_len] = saved_after_header;
    if (length_ret != ESP_OK) {
        ESP_LOGW(TAG, "USB request invalid Content-Length");
        return ESP_ERR_INVALID_ARG;
    }
    if (method_requires_content_length(request->method) && !content_length_present) {
        ESP_LOGW(TAG, "USB request missing Content-Length method=%s path=%s",
                 request->method,
                 request->path);
        return ESP_ERR_INVALID_ARG;
    }
    if (content_length > USB_CONSOLE_HTTP_BODY_MAX) {
        ESP_LOGW(TAG, "USB request body too large len=%u max=%u",
                 (unsigned int)content_length,
                 (unsigned int)USB_CONSOLE_HTTP_BODY_MAX);
        return ESP_ERR_INVALID_SIZE;
    }

    if (data_len < header_len + content_length) {
        *need_more = true;
        return ESP_OK;
    }

    request->content_length = content_length;
    request->body = data + header_len;
    request->body_len = content_length;
    *request_len = header_len + content_length;

#if USB_CONSOLE_VERBOSE_LOG_ENABLE
    ESP_LOGD(TAG, "USB HTTP request method=%s path=%s body_len=%u",
             request->method, request->path, (unsigned int)request->body_len);
#endif
    return ESP_OK;
}

void UsbConsoleHttp_SetJson(usb_console_http_response_t *response,
                            int status,
                            const char *reason,
                            const char *json)
{
    if (response == NULL) {
        return;
    }

    response->status = status;
    response->reason = reason != NULL ? reason : "OK";
    response->content_type = "application/json";
    snprintf(response->body, sizeof(response->body), "%s", json != NULL ? json : "{}");
}

esp_err_t UsbConsoleHttp_SendResponse(const usb_console_http_response_t *response)
{
    char header[256];
    size_t body_len = 0;
    int header_len = 0;

    if (response == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    body_len = strlen(response->body);
    header_len = snprintf(header, sizeof(header),
                          "HTTP/1.1 %d %s\r\n"
                          "Content-Type: %s\r\n"
                          "Content-Length: %u\r\n"
                          "Connection: keep-alive\r\n"
                          "\r\n",
                          response->status,
                          response->reason != NULL ? response->reason : "OK",
                          response->content_type != NULL ? response->content_type : "application/json",
                          (unsigned int)body_len);
    if (header_len <= 0 || header_len >= (int)sizeof(header)) {
        return ESP_ERR_INVALID_SIZE;
    }

#if USB_CONSOLE_VERBOSE_LOG_ENABLE
    ESP_LOGD(TAG, "USB HTTP response status=%d body_len=%u",
             response->status, (unsigned int)body_len);
#endif
    ESP_RETURN_ON_ERROR(UsbConsoleTransport_WriteAll(USB_CONSOLE_FRAME_HEAD,
                                                    strlen(USB_CONSOLE_FRAME_HEAD),
                                                    pdMS_TO_TICKS(USB_CONSOLE_WRITE_TIMEOUT_MS)),
                        TAG, "write USB response frame head failed");
    ESP_RETURN_ON_ERROR(UsbConsoleTransport_WriteAll(header,
                                                    (size_t)header_len,
                                                    pdMS_TO_TICKS(USB_CONSOLE_WRITE_TIMEOUT_MS)),
                        TAG, "write USB response header failed");
    ESP_RETURN_ON_ERROR(UsbConsoleTransport_WriteAll(response->body,
                                                    body_len,
                                                    pdMS_TO_TICKS(USB_CONSOLE_WRITE_TIMEOUT_MS)),
                        TAG, "write USB response body failed");
    return UsbConsoleTransport_WriteAll(USB_CONSOLE_FRAME_TAIL,
                                        strlen(USB_CONSOLE_FRAME_TAIL),
                                        pdMS_TO_TICKS(USB_CONSOLE_WRITE_TIMEOUT_MS));
}
