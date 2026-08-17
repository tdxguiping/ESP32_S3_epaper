#include "usb_console_cast.h"

#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "image_business_worker.h"
#include "server_network_sta_wifi_work_time.h"
#include "tdx_cfg.h"
#include "usb_console_common.h"
#include "usb_console_http_text.h"

typedef struct {
    usb_console_http_request_t request;
    char *body;
    int64_t queued_us;
} usb_console_cast_job_t;

typedef struct {
    usb_console_cast_job_t *job;
} usb_cast_worker_command_t;

_Static_assert(sizeof(usb_cast_worker_command_t) <= USER_IMAGE_BUSINESS_WORKER_PAYLOAD_SIZE,
               "USB cast command exceeds image worker payload");

static uint32_t s_usb_cast_generation;

static const char *TAG = "usb_console_cast";

static uint32_t elapsed_ms_since(int64_t start_us)
{
    return (uint32_t)((esp_timer_get_time() - start_us) / 1000);
}

static void free_cast_job(usb_console_cast_job_t *job)
{
    if (job == NULL) {
        return;
    }
    free(job->body);
    free(job);
}

static esp_err_t cast_worker_job(void *ctx)
{
    usb_console_cast_job_t *job = (usb_console_cast_job_t *)ctx;
    usb_console_http_response_t *response = NULL;

    if (job == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int64_t total_start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "cast worker process body_len=%u content_len=%u queue_wait_ms=%lu",
             (unsigned int)job->request.body_len,
             (unsigned int)job->request.content_length,
             (unsigned long)elapsed_ms_since(job->queued_us));

    response = (usb_console_http_response_t *)calloc(1, sizeof(*response));
    if (response == NULL) {
        ESP_LOGE(TAG, "cast response alloc failed body_len=%u",
                 (unsigned int)job->request.body_len);
        free_cast_job(job);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = UsbConsoleCast_Process(&job->request, response);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "cast worker failed ret=%s", esp_err_to_name(ret));
        UsbConsoleHttp_SetJson(response,
                               200,
                               "OK",
                               "{\"func\":\"cast_result\",\"result\":" TDX_STRINGIFY(TDX_JSON_RESULT_USB_ASYNC_FAILED) ",\"message\":\"cast failed\",\"error\":\"worker_failed\"}");
    }

    int64_t send_start_us = esp_timer_get_time();
    esp_err_t send_ret = UsbConsoleHttp_SendResponse(response);
    ESP_LOGI(TAG, "cast worker response ret=%s send=%s send_ms=%lu total_ms=%lu",
             esp_err_to_name(ret),
             esp_err_to_name(send_ret),
             (unsigned long)elapsed_ms_since(send_start_us),
             (unsigned long)elapsed_ms_since(total_start_us));
    free(response);
    free_cast_job(job);
    return ret;
}

static esp_err_t usb_cast_run_command(const void *payload, size_t payload_size)
{
    if (payload == NULL || payload_size != sizeof(usb_cast_worker_command_t)) {
        return ESP_ERR_INVALID_ARG;
    }
    usb_cast_worker_command_t command = {0};
    memcpy(&command, payload, sizeof(command));
    esp_err_t ret = cast_worker_job(command.job);
    ServerNetworkStaWifiWorkTime_ImageTransferEnd();
    return ret;
}

static void usb_cast_cancel_command(const void *payload, size_t payload_size)
{
    if (payload == NULL || payload_size != sizeof(usb_cast_worker_command_t)) {
        return;
    }
    usb_cast_worker_command_t command = {0};
    memcpy(&command, payload, sizeof(command));
    usb_console_http_response_t *response =
        (usb_console_http_response_t *)calloc(1, sizeof(*response));
    if (response != NULL) {
        UsbConsoleHttp_SetJson(response,
                               200,
                               "OK",
                               "{\"func\":\"cast_result\",\"result\":" TDX_STRINGIFY(TDX_JSON_RESULT_BUSY) ",\"message\":\"cast canceled\",\"error\":\"network_priority\"}");
        (void)UsbConsoleHttp_SendResponse(response);
        free(response);
    } else {
        ESP_LOGE(TAG, "cast cancel response alloc failed reason=network_priority");
    }
    free_cast_job(command.job);
    ServerNetworkStaWifiWorkTime_ImageTransferEnd();
}

esp_err_t UsbConsoleCast_SubmitAsync(const usb_console_http_request_t *request,
                                     usb_console_http_response_t *response)
{
    usb_console_cast_job_t *job = NULL;
    int64_t copy_start_us = esp_timer_get_time();

    if (request == NULL || response == NULL || request->body == NULL || request->body_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    job = (usb_console_cast_job_t *)calloc(1, sizeof(*job));
    if (job == NULL) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"cast_result\",\"result\":%d,\"message\":\"cast failed\",\"error\":\"alloc_job_failed\"}",
                                         TDX_JSON_RESULT_NO_MEMORY);
    }

    job->body = (char *)malloc(request->body_len + 1);
    if (job->body == NULL) {
        free_cast_job(job);
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"cast_result\",\"result\":%d,\"message\":\"cast failed\",\"error\":\"alloc_body_failed\"}",
                                         TDX_JSON_RESULT_NO_MEMORY);
    }

    memcpy(job->body, request->body, request->body_len);
    job->body[request->body_len] = '\0';
    job->request = *request;
    job->request.body = job->body;
    int64_t queued_us = esp_timer_get_time();
    job->queued_us = queued_us;

    ESP_LOGI(TAG, "cast copied body_len=%u content_len=%u copy_ms=%lu",
             (unsigned int)job->request.body_len,
             (unsigned int)job->request.content_length,
             (unsigned long)elapsed_ms_since(copy_start_us));
    usb_cast_worker_command_t command = {
        .job = job,
    };
    uint32_t generation =
        __atomic_add_fetch(&s_usb_cast_generation, 1U, __ATOMIC_ACQ_REL);
    uint32_t replace_mask =
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_DAILY) |
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_SLIDESHOW) |
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_LOCAL_IMAGE);
    uint32_t cast_busy_mask =
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_CAST) |
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_CAST2PIC) |
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_USB_CAST) |
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_USB_CAST2PIC);
    ServerNetworkStaWifiWorkTime_ImageTransferBegin();
    esp_err_t submit_ret = ImageBusinessWorker_SubmitReplacingPendingUnlessBusy(
        IMAGE_BUSINESS_OWNER_USB_CAST,
        usb_cast_run_command,
        usb_cast_cancel_command,
        &command,
        sizeof(command),
        generation,
        replace_mask,
        cast_busy_mask);
    if (submit_ret != ESP_OK) {
        ServerNetworkStaWifiWorkTime_ImageTransferEnd();
        free_cast_job(job);
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"cast_result\",\"result\":%d,\"message\":\"cast failed\",\"error\":\"queue_failed\"}",
                                         TDX_JSON_RESULT_QUEUE_FAILED);
    }

    ImageBusinessWorker_Wake();
    ESP_LOGI(TAG, "cast queued generation=%lu submit_ms=%lu",
             (unsigned long)generation,
             (unsigned long)elapsed_ms_since(queued_us));
    response->status = 0;
    return ESP_OK;
}
