#include "server_network_sta_cast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cast_core.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "server_network_sta_dataup_async.h"
#include "tdx_cfg.h"

static const char *TAG = "server_sta_cast";

typedef struct {
    char *body;
    size_t body_len;
    char base_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX];
    tdx_cast_core_request_t cast;
} cast_async_job_t;

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

static void cast_async_process(void *arg)
{
    cast_async_job_t *job = (cast_async_job_t *)arg;
    int64_t start_us = esp_timer_get_time();
    tdx_cast_core_result_t result = {0};

    if (job == NULL) {
        return;
    }

    ESP_LOGI(TAG, "cast async process start file=%s", job->cast.file_name);
    (void)TdxCastCore_ProcessValidatedCastDir(&job->cast, job->base_path, "network cast async", &result);
    ESP_LOGI(TAG, "cast async process done file=%s result=%d error=%s elapsed_ms=%lu",
             job->cast.file_name,
             result.result,
             result.error[0] ? result.error : "",
             (unsigned long)elapsed_ms_since(start_us));
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

    esp_err_t submit_ret = ServerNetworkStaDataupAsync_Submit("cast",
                                                              cast_async_process,
                                                              cast_async_cleanup,
                                                              job);
    if (submit_ret != ESP_OK) {
        free(job);
        return submit_ret;
    }

    ESP_LOGI(TAG, "cast async accepted file=%s body=%u", cast->file_name, (unsigned int)body_len);
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
            *body_taken = true;
        }
        httpd_resp_set_type(req, "application/x-ndjson");
        esp_err_t resp_ret = send_cast_received(req, &cast);
        if (resp_ret == ESP_OK) {
            resp_ret = httpd_resp_send_chunk(req, NULL, 0);
        }
        ESP_LOGI(TAG, "cast async response done file=%s ret=%s", cast.file_name, esp_err_to_name(resp_ret));
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
