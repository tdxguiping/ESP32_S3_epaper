#include "server_network_sta_cast.h"

#include <stdio.h>
#include <string.h>

#include "cast_core.h"
#include "esp_log.h"
#include "tdx_cfg.h"

static const char *TAG = "server_sta_cast";

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
                                       const char *body,
                                       size_t body_len,
                                       const char *content_type,
                                       const char *base_path)
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

    httpd_resp_set_type(req, "application/x-ndjson");
    esp_err_t resp_ret = send_cast_received(req, &cast);
    if (resp_ret != ESP_OK) {
        ESP_LOGW(TAG, "cast received response failed ret=%s", esp_err_to_name(resp_ret));
        return resp_ret;
    }

    (void)TdxCastCore_ProcessValidated(&cast, base_path, "network cast", &result);
    (void)send_cast_result(req, &result);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}
