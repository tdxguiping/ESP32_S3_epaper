#include "usb_console_restart.h"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "usb_console_http_text.h"

static const char *TAG = "usb_console_restart";

static void usb_console_restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_LOGW(TAG, "restart by USB HTTP-like command");
    esp_restart();
}

esp_err_t UsbConsoleRestart_Handle(const usb_console_http_request_t *request,
                                   usb_console_http_response_t *response)
{
    (void)request;

    UsbConsoleHttp_SetJson(response,
                           200,
                           "OK",
                           "{\"func\":\"usb_restart_result\",\"result\":0,\"message\":\"restart scheduled\"}");

    BaseType_t ok = xTaskCreate(usb_console_restart_task,
                                "usb_restart",
                                3072,
                                NULL,
                                5,
                                NULL);
    if (ok != pdPASS) {
        UsbConsoleHttp_SetJson(response,
                               500,
                               "Internal Server Error",
                               "{\"func\":\"usb_restart_result\",\"result\":1009,\"message\":\"restart task create failed\",\"error\":\"task_create_failed\"}");
        return ESP_OK;
    }

    return ESP_OK;
}
