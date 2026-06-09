#include "usb_console_echo.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tdx_cfg.h"
#include "usb_console_http_text.h"
#include "usb_console_router.h"
#include "usb_console_transport.h"
#include "usb_console_worker.h"

static const char *TAG = "usb_console_echo";
static TaskHandle_t s_usb_console_echo_task;

static esp_err_t send_simple_json(int status, const char *reason, const char *json)
{
    usb_console_http_response_t *response = (usb_console_http_response_t *)calloc(1, sizeof(*response));
    if (response == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // Allocate the large response body on heap so the USB task stack stays small.
    // Chinese note: response body is large, so it uses heap memory instead of the USB task stack.
    UsbConsoleHttp_SetJson(response, status, reason, json);
    esp_err_t ret = UsbConsoleHttp_SendResponse(response);
    free(response);
    return ret;
}

static bool grow_request_buffer(char **buffer, size_t *capacity, size_t required)
{
    char *new_buffer = NULL;
    size_t new_capacity = *capacity;
    const size_t max_capacity = USB_CONSOLE_HTTP_HEADER_MAX + USB_CONSOLE_HTTP_BODY_MAX;

    if (required > max_capacity) {
        return false;
    }

    while (new_capacity < required) {
        size_t next_capacity = new_capacity * 2;
        if (next_capacity < new_capacity || next_capacity > max_capacity) {
            next_capacity = max_capacity;
        }
        new_capacity = next_capacity;
    }

    if (new_capacity == *capacity) {
        return true;
    }

    new_buffer = (char *)realloc(*buffer, new_capacity);
    if (new_buffer == NULL) {
        return false;
    }

    *buffer = new_buffer;
    *capacity = new_capacity;
    return true;
}

static void drop_request_prefix(char *buffer, size_t *buffer_used, size_t request_len)
{
    if (request_len >= *buffer_used) {
        *buffer_used = 0;
        return;
    }

    memmove(buffer, buffer + request_len, *buffer_used - request_len);
    *buffer_used -= request_len;
}

static bool starts_with_http_method(const char *buffer, size_t buffer_used)
{
    static const char *methods[] = {"GET ", "POST ", "PUT ", "DELETE ", "PATCH ", "HEAD "};

    for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
        size_t method_len = strlen(methods[i]);
        if (buffer_used <= method_len &&
            strncmp(buffer, methods[i], buffer_used) == 0) {
            return true;
        }
        if (buffer_used > method_len &&
            strncmp(buffer, methods[i], method_len) == 0) {
            return true;
        }
    }

    return false;
}

static void keep_only_possible_http_prefix(char *buffer, size_t *buffer_used)
{
    static const char *methods[] = {"GET ", "POST ", "PUT ", "DELETE ", "PATCH ", "HEAD "};

    if (*buffer_used == 0 || starts_with_http_method(buffer, *buffer_used)) {
        return;
    }

    for (size_t pos = 1; pos < *buffer_used; pos++) {
        for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
            size_t method_len = strlen(methods[i]);
            size_t left = *buffer_used - pos;
            if (left <= method_len && strncmp(buffer + pos, methods[i], left) == 0) {
                memmove(buffer, buffer + pos, left);
                *buffer_used = left;
                return;
            }
            if (left > method_len && strncmp(buffer + pos, methods[i], method_len) == 0) {
                memmove(buffer, buffer + pos, left);
                *buffer_used = left;
                return;
            }
        }
    }

#if USB_CONSOLE_VERBOSE_LOG_ENABLE
    ESP_LOGD(TAG, "drop non-http USB bytes len=%u", (unsigned int)*buffer_used);
#endif
    *buffer_used = 0;
}

static void UsbConsoleEcho_Task(void *arg)
{
    (void)arg;

    uint8_t rx[USB_CONSOLE_RX_BUF_SIZE];
    char *request_buffer = NULL;
    size_t request_capacity = USB_CONSOLE_HTTP_HEADER_MAX;
    size_t request_used = 0;
    TickType_t request_start_tick = 0;

    request_buffer = (char *)malloc(request_capacity);
    if (request_buffer == NULL) {
        ESP_LOGE(TAG, "alloc USB request buffer failed size=%u", (unsigned int)request_capacity);
        vTaskDelete(NULL);
        return;
    }

    if (USB_CONSOLE_START_DELAY_MS > 0) {
        // Delay protocol RX so boot logs finish before the PC sends HTTP-like requests.
        // Chinese note: delay USB protocol RX at startup so boot logs do not mix into HTTP responses.
        ESP_LOGI(TAG, "USB console HTTP text entry delay %u ms",
                 (unsigned int)USB_CONSOLE_START_DELAY_MS);
        vTaskDelay(pdMS_TO_TICKS(USB_CONSOLE_START_DELAY_MS));
    }

    UsbConsoleTransport_FlushRx();
    ESP_LOGI(TAG, "USB console HTTP text entry ready");

    while (1) {
        int len = UsbConsoleTransport_Read(rx,
                                           sizeof(rx),
                                           pdMS_TO_TICKS(USB_CONSOLE_READ_TIMEOUT_MS));
        if (len > 0) {
            if (request_used == 0) {
                request_start_tick = xTaskGetTickCount();
            }

            if (!grow_request_buffer(&request_buffer, &request_capacity, request_used + (size_t)len + 1)) {
                ESP_LOGE(TAG, "USB request buffer full used=%u incoming=%d",
                         (unsigned int)request_used, len);
                (void)send_simple_json(413,
                                       "Payload Too Large",
                                       "{\"func\":\"usb_receive_result\",\"result\":1,\"message\":\"request too large\"}");
                request_used = 0;
                continue;
            }

            memcpy(request_buffer + request_used, rx, (size_t)len);
            request_used += (size_t)len;
            request_buffer[request_used] = '\0';
            request_start_tick = xTaskGetTickCount();
            keep_only_possible_http_prefix(request_buffer, &request_used);
#if USB_CONSOLE_VERBOSE_LOG_ENABLE
            ESP_LOGD(TAG, "USB received bytes=%d buffered=%u",
                     len, (unsigned int)request_used);
#endif
        } else if (request_used > 0) {
            TickType_t elapsed = xTaskGetTickCount() - request_start_tick;
            if (elapsed > pdMS_TO_TICKS(USB_CONSOLE_REQUEST_TIMEOUT_MS)) {
                ESP_LOGW(TAG, "USB request timeout buffered=%u", (unsigned int)request_used);
                (void)send_simple_json(408,
                                       "Request Timeout",
                                       "{\"func\":\"usb_receive_result\",\"result\":1,\"message\":\"request timeout\"}");
                request_used = 0;
            }
        }

        while (request_used > 0) {
            usb_console_http_request_t request = {0};
            size_t request_len = 0;
            bool need_more = false;
            esp_err_t ret = UsbConsoleHttp_TryParseRequest(request_buffer,
                                                          request_used,
                                                          &request,
                                                          &request_len,
                                                          &need_more);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "USB request parse failed ret=%s buffered=%u",
                         esp_err_to_name(ret), (unsigned int)request_used);
                (void)send_simple_json(400,
                                       "Bad Request",
                                       "{\"func\":\"usb_receive_result\",\"result\":1,\"message\":\"bad request\"}");
                request_used = 0;
                break;
            }
            if (need_more) {
                break;
            }

            ret = UsbConsoleRouter_Handle(&request);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "USB route response failed ret=%s", esp_err_to_name(ret));
            }

            drop_request_prefix(request_buffer, &request_used, request_len);
            if (request_used > 0) {
                request_buffer[request_used] = '\0';
                request_start_tick = xTaskGetTickCount();
#if USB_CONSOLE_VERBOSE_LOG_ENABLE
                ESP_LOGD(TAG, "USB keep pipelined bytes=%u", (unsigned int)request_used);
#endif
            }
        }
    }
}

esp_err_t UsbConsoleEcho_Init(void)
{
#if USB_CONSOLE_ENABLE
    if (s_usb_console_echo_task != NULL) {
        ESP_LOGW(TAG, "USB console entry already started");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(UsbConsoleTransport_Init(), TAG, "init USB transport failed");
    ESP_RETURN_ON_ERROR(UsbConsoleWorker_Init(), TAG, "init USB worker failed");

    BaseType_t ret = xTaskCreate(UsbConsoleEcho_Task,
                                 "UsbConsoleEcho",
                                 USB_CONSOLE_TASK_STACK_SIZE,
                                 NULL,
                                 USB_CONSOLE_TASK_PRIORITY,
                                 &s_usb_console_echo_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "create USB console entry task failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "USB console entry started stack=%u priority=%u",
             (unsigned int)USB_CONSOLE_TASK_STACK_SIZE,
             (unsigned int)USB_CONSOLE_TASK_PRIORITY);
    return ESP_OK;
#else
    ESP_LOGI(TAG, "USB console entry disabled");
    return ESP_OK;
#endif
}
