#include "usb_console_common.h"

#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "usb_console_http_text.h"
#include "usb_console_worker.h"

typedef struct {
    usb_console_http_request_t request;
    usb_console_process_fn_t process;
    const char *name;
    char *body;
} usb_console_async_request_t;

static const char *TAG = "usb_console_async";

static void free_async_request(usb_console_async_request_t *job)
{
    if (job == NULL) {
        return;
    }
    free(job->body);
    free(job);
}

static void async_request_job(void *ctx)
{
    usb_console_async_request_t *job = (usb_console_async_request_t *)ctx;
    usb_console_http_response_t *response = NULL;

    if (job == NULL || job->process == NULL) {
        free_async_request(job);
        return;
    }

    response = (usb_console_http_response_t *)calloc(1, sizeof(*response));
    if (response == NULL) {
        ESP_LOGE(TAG, "response alloc failed name=%s body_len=%u",
                 job->name != NULL ? job->name : "unknown",
                 (unsigned int)job->request.body_len);
        free_async_request(job);
        return;
    }

    ESP_LOGI(TAG, "process start name=%s body_len=%u",
             job->name != NULL ? job->name : "unknown",
             (unsigned int)job->request.body_len);
    esp_err_t ret = job->process(&job->request, response);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "process failed name=%s ret=%s",
                 job->name != NULL ? job->name : "unknown",
                 esp_err_to_name(ret));
        UsbConsoleHttp_SetJson(response,
                               500,
                               "Internal Server Error",
                               "{\"func\":\"usb_async_result\",\"result\":1,\"message\":\"handler failed\"}");
    }

    esp_err_t send_ret = ESP_OK;
    if (response->status != 0) {
        send_ret = UsbConsoleHttp_SendResponse(response);
    }
    ESP_LOGI(TAG, "process done name=%s ret=%s send=%s",
             job->name != NULL ? job->name : "unknown",
             esp_err_to_name(ret),
             esp_err_to_name(send_ret));
    free(response);
    free_async_request(job);
}

esp_err_t UsbConsoleCommon_SubmitAsyncRequest(const usb_console_http_request_t *request,
                                              usb_console_http_response_t *response,
                                              const char *name,
                                              usb_console_process_fn_t process)
{
    usb_console_async_request_t *job = NULL;

    if (request == NULL || response == NULL || process == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    job = (usb_console_async_request_t *)calloc(1, sizeof(*job));
    if (job == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (request->body_len > 0) {
        job->body = (char *)malloc(request->body_len + 1);
        if (job->body == NULL) {
            free_async_request(job);
            return ESP_ERR_NO_MEM;
        }
        memcpy(job->body, request->body, request->body_len);
        job->body[request->body_len] = '\0';
    }

    job->request = *request;
    job->request.body = job->body != NULL ? job->body : "";
    job->process = process;
    job->name = name;

    ESP_LOGI(TAG, "queued name=%s body_len=%u content_len=%u",
             name != NULL ? name : "unknown",
             (unsigned int)job->request.body_len,
             (unsigned int)job->request.content_length);
    esp_err_t submit_ret = UsbConsoleWorker_SubmitJob(name, async_request_job, job);
    if (submit_ret != ESP_OK) {
        free_async_request(job);
        return submit_ret;
    }

    response->status = 0;
    return ESP_OK;
}
