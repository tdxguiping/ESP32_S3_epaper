#include "usb_console_cast.h"

#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "tdx_cfg.h"
#include "usb_console_common.h"
#include "usb_console_http_text.h"
#include "usb_console_worker.h"

typedef struct {
    usb_console_http_request_t request;
    char *body;
    int64_t queued_us;
} usb_console_cast_job_t;

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

static void cast_worker_job(void *ctx)
{
    usb_console_cast_job_t *job = (usb_console_cast_job_t *)ctx;
    usb_console_http_response_t *response = NULL;

    if (job == NULL) {
        return;
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
        return;
    }

    esp_err_t ret = UsbConsoleCast_Process(&job->request, response);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "cast worker failed ret=%s", esp_err_to_name(ret));
        UsbConsoleHttp_SetJson(response,
                               200,
                               "OK",
                               "{\"func\":\"cast_result\",\"result\":1106,\"message\":\"cast failed\",\"error\":\"worker_failed\"}");
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
    esp_err_t submit_ret = UsbConsoleWorker_SubmitJob("cast", cast_worker_job, job);
    if (submit_ret != ESP_OK) {
        free_cast_job(job);
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"cast_result\",\"result\":%d,\"message\":\"cast failed\",\"error\":\"queue_failed\"}",
                                         TDX_JSON_RESULT_QUEUE_FAILED);
    }

    ESP_LOGI(TAG, "cast queued submit_ms=%lu",
             (unsigned long)elapsed_ms_since(queued_us));
    response->status = 0;
    return ESP_OK;
}
