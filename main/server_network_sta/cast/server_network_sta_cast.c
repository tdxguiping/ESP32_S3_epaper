#include "server_network_sta_cast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cast_core.h"
#include "epd_display_mode.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "image_business_worker.h"
#include "local_image_browsing.h"
#include "esp_timer.h"
#include "server_network_sta_slideshow.h"
#include "server_network_sta_wifi_work_time.h"
#include "tdx_cfg.h"

static const char *TAG = "server_sta_cast";

typedef struct {
    char *body;
    size_t body_len;
    char base_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX];
    tdx_cast_core_request_t cast;
} cast_async_job_t;

typedef struct {
    cast_async_job_t *job;
} cast_worker_command_t;

_Static_assert(sizeof(cast_worker_command_t) <= USER_IMAGE_BUSINESS_WORKER_PAYLOAD_SIZE,
               "cast worker command exceeds image worker payload");

static uint32_t s_cast_generation;

static uint32_t elapsed_ms_since(int64_t start_us)
{
    return (uint32_t)((esp_timer_get_time() - start_us) / 1000);
}

static esp_err_t send_cast_result(httpd_req_t *req, const tdx_cast_core_result_t *result)
{
    char json[224];
    if (req == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (result->result == TDX_JSON_RESULT_OK) {
        snprintf(json,
                 sizeof(json),
                 "{\"func\":\"cast_result\",\"result\":%d,\"message\":\"saved\"}\n",
                 TDX_JSON_RESULT_OK);
    } else {
        snprintf(json,
                 sizeof(json),
                 "{\"func\":\"cast_result\",\"result\":%d,\"message\":\"%s\",\"error\":\"%s\"}\n",
                 result->result,
                 result->message[0] ? result->message : "cast failed",
                 result->error[0] ? result->error : "");
    }
    return httpd_resp_send_chunk(req, json, strlen(json));
}

static esp_err_t send_cast_received(httpd_req_t *req, const tdx_cast_core_request_t *cast)
{
    char json[192];
    if (req == NULL || cast == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(json,
             sizeof(json),
             "{\"func\":\"cast_received\",\"result\":%d,\"fileName\":\"%s\"}\n",
             TDX_JSON_RESULT_OK,
             cast->file_name);
    return httpd_resp_send_chunk(req, json, strlen(json));
}

static esp_err_t cast_async_process(cast_async_job_t *job)
{
    int64_t start_us = esp_timer_get_time();
    tdx_cast_core_result_t result = {0};

    if (job == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "cast async process start file=%s", job->cast.file_name);
    esp_err_t ret = TdxCastCore_ProcessValidatedCastDir(&job->cast,
                                                        job->base_path,
                                                        "network cast async",
                                                        &result);
    ESP_LOGI(TAG, "cast async process done file=%s result=%d error=%s elapsed_ms=%lu",
             job->cast.file_name,
             result.result,
             result.error[0] ? result.error : "",
             (unsigned long)elapsed_ms_since(start_us));
    return ret;
}

static void cast_async_cleanup(void *arg)
{
    cast_async_job_t *job = (cast_async_job_t *)arg;
    if (job == NULL) {
        return;
    }
    heap_caps_free(job->body);
    free(job);
}

static esp_err_t cast_run_command(const void *payload, size_t payload_size)
{
    if (payload == NULL || payload_size != sizeof(cast_worker_command_t)) {
        return ESP_ERR_INVALID_ARG;
    }
    cast_worker_command_t command = {0};
    memcpy(&command, payload, sizeof(command));
    esp_err_t ret = cast_async_process(command.job);
    cast_async_cleanup(command.job);
    ServerNetworkStaWifiWorkTime_ImageTransferEnd();
    return ret;
}

static void cast_cancel_command(const void *payload, size_t payload_size)
{
    if (payload == NULL || payload_size != sizeof(cast_worker_command_t)) {
        return;
    }
    cast_worker_command_t command = {0};
    memcpy(&command, payload, sizeof(command));
    cast_async_cleanup(command.job);
    ServerNetworkStaWifiWorkTime_ImageTransferEnd();
}

static esp_err_t start_cast_async(char *body,
                                  size_t body_len,
                                  const char *base_path,
                                  const tdx_cast_core_request_t *cast)
{
    if (body == NULL || base_path == NULL || cast == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    cast_async_job_t *job = (cast_async_job_t *)calloc(1, sizeof(*job));
    if (job == NULL) {
        return ESP_ERR_NO_MEM;
    }
    job->body = body;
    job->body_len = body_len;
    job->cast = *cast;
    snprintf(job->base_path, sizeof(job->base_path), "%s", base_path);

    cast_worker_command_t command = {
        .job = job,
    };
    uint32_t generation =
        __atomic_add_fetch(&s_cast_generation, 1U, __ATOMIC_ACQ_REL);
    uint32_t replace_mask =
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_DAILY) |
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_SLIDESHOW) |
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_LOCAL_IMAGE) |
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_USB_CAST) |
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_USB_CAST2PIC);
    uint32_t cast_busy_mask =
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_CAST) |
        IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_CAST2PIC);
    ServerNetworkStaWifiWorkTime_ImageTransferBegin();
    esp_err_t submit_ret = ImageBusinessWorker_SubmitReplacingPendingUnlessBusy(
        IMAGE_BUSINESS_OWNER_CAST,
        cast_run_command,
        cast_cancel_command,
        &command,
        sizeof(command),
        generation,
        replace_mask,
        cast_busy_mask);
    if (submit_ret != ESP_OK) {
        ServerNetworkStaWifiWorkTime_ImageTransferEnd();
        free(job);
        return submit_ret;
    }

    ImageBusinessWorker_Wake();
    ESP_LOGI(TAG, "cast async accepted file=%s body=%u generation=%lu",
             cast->file_name,
             (unsigned int)body_len,
             (unsigned long)generation);
    return ESP_OK;
}

static esp_err_t send_single_error(httpd_req_t *req, const tdx_cast_core_result_t *result)
{
    char json[224];
    if (req == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(json,
             sizeof(json),
             "{\"func\":\"cast_result\",\"result\":%d,\"message\":\"%s\",\"error\":\"%s\"}",
             result->result,
             result->message[0] ? result->message : "cast failed",
             result->error[0] ? result->error : "");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

esp_err_t ServerNetworkStaCast_Process(httpd_req_t *req,
                                       char *body,
                                       size_t body_len,
                                       const char *content_type,
                                       const char *base_path,
                                       bool *body_taken)
{
    tdx_cast_core_request_t cast = {0};
    tdx_cast_core_result_t result = {0};
    esp_err_t parse_ret = TdxCastCore_ParseAndValidate(body, body_len, content_type, &cast, &result);

    if (parse_ret == ESP_ERR_NOT_SUPPORTED) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (parse_ret != ESP_OK) {
        ESP_LOGW(TAG, "cast parse/validate failed ret=%s result=%d error=%s",
                 esp_err_to_name(parse_ret), result.result, result.error);
        return send_single_error(req, &result);
    }

    LocalImageBrowsing_Stop();
    esp_err_t mode_ret = EpdDisplayMode_Set(USER_EPD_DISPLAY_MODE_NORMAL);
    if (mode_ret != ESP_OK) {
        ESP_LOGE(TAG, "cast mode save failed file=%s ret=%s",
                 cast.file_name, esp_err_to_name(mode_ret));
        result.result = TDX_JSON_RESULT_INTERNAL_ERROR;
        snprintf(result.message, sizeof(result.message), "%s", "cast failed");
        snprintf(result.error, sizeof(result.error), "%s", "mode_save_failed");
        return send_single_error(req, &result);
    }

    ServerNetworkStaSlideshow_Stop();//     ← 新增
    


    if (cast.show) {
        esp_err_t async_ret = start_cast_async(body, body_len, base_path, &cast);
        if (async_ret != ESP_OK) {
            ESP_LOGW(TAG, "cast async start failed file=%s ret=%s",
                     cast.file_name, esp_err_to_name(async_ret));
            result.result = async_ret == ESP_ERR_TIMEOUT ? TDX_JSON_RESULT_TIMEOUT :
                            async_ret == ESP_ERR_INVALID_STATE ? TDX_JSON_RESULT_BUSY :
                            TDX_JSON_RESULT_NO_MEMORY;
            snprintf(result.message, sizeof(result.message), "%s", "cast failed");
            snprintf(result.error,
                     sizeof(result.error),
                     "%s",
                     async_ret == ESP_ERR_TIMEOUT ? "async_timeout" :
                     async_ret == ESP_ERR_INVALID_STATE ? "async_busy" :
                     "async_start_failed");
            return send_single_error(req, &result);
        }
        if (body_taken != NULL) {
            /* The async worker owns and releases the request body after submit succeeds. */
            *body_taken = true;
        }
        httpd_resp_set_type(req, "application/x-ndjson");
        esp_err_t resp_ret = send_cast_received(req, &cast);
        if (resp_ret == ESP_OK) {
            resp_ret = httpd_resp_send_chunk(req, NULL, 0);
        }
        if (resp_ret == ESP_OK) {
            ESP_LOGI(TAG, "cast response sent file=%s", cast.file_name);
        } else {
            ESP_LOGW(TAG, "cast accepted but response failed file=%s ret=%s",
                     cast.file_name,
                     esp_err_to_name(resp_ret));
        }
        return resp_ret;
    }

    httpd_resp_set_type(req, "application/x-ndjson");
    esp_err_t resp_ret = send_cast_received(req, &cast);
    if (resp_ret != ESP_OK) {
        ESP_LOGW(TAG, "cast received response failed ret=%s", esp_err_to_name(resp_ret));
        return resp_ret;
    }
    ESP_LOGI(TAG, "cast received response sent, continue display/save synchronously file=%s", cast.file_name);

    (void)TdxCastCore_ProcessValidatedCastDir(&cast, base_path, "network cast", &result);
    ESP_LOGI(TAG, "cast display/save done, send final result file=%s result=%d", cast.file_name, result.result);
    (void)send_cast_result(req, &result);
    httpd_resp_send_chunk(req, NULL, 0);
    ESP_LOGI(TAG, "cast HTTP handler done file=%s", cast.file_name);
    return ESP_OK;
}
