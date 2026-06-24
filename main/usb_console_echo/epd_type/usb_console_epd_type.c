#include "usb_console_epd_type.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "epd_display_app.h"
#include "epd_type.h"
#include "esp_log.h"
#include "tdx_cfg.h"
#include "usb_console_http_text.h"

static const char *TAG = "usb_epd_type";

static esp_err_t ensure_epd_type_loaded(void)
{
    esp_err_t ret = EpdType_LoadSavedOrDefault();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "load saved EPD type failed ret=%s", esp_err_to_name(ret));
    }
    return ret;
}

static const char *color_name_from_type(epd_color_type_t color_type)
{
    switch (color_type) {
    case BWR_3_Color:
        return "BWR_3_Color";
    case BWRY_4_Color:
        return "BWRY_4_Color";
    case BWYRBG_6_Color:
        return "BWYRBG_6_Color";
    default:
        return "unknown";
    }
}

static int color_count_from_type(epd_color_type_t color_type)
{
    switch (color_type) {
    case BWR_3_Color:
        return 3;
    case BWRY_4_Color:
        return 4;
    case BWYRBG_6_Color:
        return 6;
    default:
        return 0;
    }
}

static int append_epd_type_json_item(char *buffer, size_t buffer_size, size_t *offset, const epd_type_config_t *config)
{
    int written = 0;

    if (buffer == NULL || offset == NULL || config == NULL || *offset >= buffer_size) {
        return -1;
    }

    written = snprintf(buffer + *offset,
                       buffer_size - *offset,
                       "{\"type\":%u,\"name\":\"%s\",\"width\":%u,\"height\":%u,"
                       "\"display_size\":%u,\"color_type\":%u,\"color_name\":\"%s\",\"color_count\":%d}",
                       (unsigned int)config->type,
                       config->name,
                       (unsigned int)config->width,
                       (unsigned int)config->height,
                       (unsigned int)config->display_size,
                       (unsigned int)config->color_type,
                       color_name_from_type(config->color_type),
                       color_count_from_type(config->color_type));
    if (written < 0 || (size_t)written >= buffer_size - *offset) {
        return -1;
    }

    *offset += (size_t)written;
    return written;
}

static void set_epd_type_response_json(usb_console_http_response_t *response,
                                       const char *func,
                                       int result,
                                       bool changed,
                                       const epd_type_config_t *config,
                                       const char *message)
{
    char json[768];

    if (config != NULL) {
        snprintf(json,
                 sizeof(json),
                 "{\"func\":\"%s\",\"result\":%d,\"changed\":%s,\"type\":%u,\"name\":\"%s\","
                 "\"width\":%u,\"height\":%u,\"display_size\":%u,\"color_type\":%u,"
                 "\"color_name\":\"%s\",\"color_count\":%d,\"message\":\"%s\"}",
                 func,
                 result,
                 changed ? "true" : "false",
                 (unsigned int)config->type,
                 config->name,
                 (unsigned int)config->width,
                 (unsigned int)config->height,
                 (unsigned int)config->display_size,
                 (unsigned int)config->color_type,
                 color_name_from_type(config->color_type),
                 color_count_from_type(config->color_type),
                 message != NULL ? message : "");
    } else {
        snprintf(json,
                 sizeof(json),
                 "{\"func\":\"%s\",\"result\":%d,\"changed\":false,\"message\":\"%s\"}",
                 func,
                 result,
                 message != NULL ? message : "");
    }

    UsbConsoleHttp_SetJson(response, result == 0 ? 200 : 400, result == 0 ? "OK" : "Bad Request", json);
}

esp_err_t UsbConsoleEpdType_SendCurrent(void)
{
    const epd_type_config_t *config = NULL;
    usb_console_http_response_t *response = NULL;
    char json[768];
    esp_err_t ret = ESP_OK;

    ret = ensure_epd_type_loaded();
    if (ret != ESP_OK) {
        return ret;
    }

    config = EpdType_GetCurrentConfig();
    response = (usb_console_http_response_t *)calloc(1, sizeof(*response));
    if (response == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (config == NULL) {
        UsbConsoleHttp_SetJson(response,
                               500,
                               "Internal Server Error",
                               "{\"func\":\"epd_type\",\"result\":1801,\"message\":\"invalid current EPD type\"}");
        ret = UsbConsoleHttp_SendResponse(response);
        free(response);
        return ret;
    }

    snprintf(json,
             sizeof(json),
             "{\"func\":\"epd_type\",\"result\":%d,\"type\":%u,\"name\":\"%s\","
             "\"width\":%u,\"height\":%u,\"display_size\":%u,\"color_type\":%u,"
             "\"color_name\":\"%s\",\"color_count\":%d}",
             TDX_JSON_RESULT_OK,
             (unsigned int)config->type,
             config->name,
             (unsigned int)config->width,
             (unsigned int)config->height,
             (unsigned int)config->display_size,
             (unsigned int)config->color_type,
             color_name_from_type(config->color_type),
             color_count_from_type(config->color_type));

#if USB_CONSOLE_EPD_TYPE_DEBUG_LOG_ENABLE
    ESP_LOGI(TAG, "send current EPD type=%u name=%s size=%u",
             (unsigned int)config->type,
             config->name,
             (unsigned int)config->display_size);
#endif

    UsbConsoleHttp_SetJson(response, 200, "OK", json);
    ret = UsbConsoleHttp_SendResponse(response);
    free(response);
    return ret;
}

esp_err_t UsbConsoleEpdType_SendList(void)
{
    usb_console_http_response_t *response = NULL;
    size_t offset = 0;
    size_t count = EpdType_GetCount();
    const epd_type_config_t *current = NULL;
    esp_err_t ret = ESP_OK;

    ret = ensure_epd_type_loaded();
    if (ret != ESP_OK) {
        return ret;
    }

    current = EpdType_GetCurrentConfig();
    response = (usb_console_http_response_t *)calloc(1, sizeof(*response));
    if (response == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // Send the EPD type list from the EPD module so PC-side choices stay in sync with firmware.
    // 从 EPD 模块发送类型列表，让 PC 端选项与固件保持一致，后续新增屏幕只改 EPD 列表。
    offset += snprintf(response->body + offset,
                       sizeof(response->body) - offset,
                       "{\"func\":\"epd_type_list\",\"result\":%d,\"current_type\":%u,\"count\":%u,\"types\":[",
                       TDX_JSON_RESULT_OK,
                       current != NULL ? (unsigned int)current->type : 0U,
                       (unsigned int)count);
    for (size_t i = 0; i < count; i++) {
        const epd_type_config_t *config = EpdType_GetConfigByIndex(i);
        if (offset >= sizeof(response->body)) {
            free(response);
            return ESP_ERR_INVALID_SIZE;
        }
        if (i > 0) {
            response->body[offset++] = ',';
            response->body[offset] = '\0';
        }
        if (append_epd_type_json_item(response->body, sizeof(response->body), &offset, config) < 0) {
            free(response);
            return ESP_ERR_INVALID_SIZE;
        }
    }
    if (offset + 2 >= sizeof(response->body)) {
        free(response);
        return ESP_ERR_INVALID_SIZE;
    }
    response->body[offset++] = ']';
    response->body[offset++] = '}';
    response->body[offset] = '\0';
    response->status = 200;
    response->reason = "OK";
    response->content_type = "application/json";

#if USB_CONSOLE_EPD_TYPE_DEBUG_LOG_ENABLE
    ESP_LOGI(TAG, "send EPD type list count=%u current=%u",
             (unsigned int)count,
             current != NULL ? (unsigned int)current->type : 0U);
#endif

    ret = UsbConsoleHttp_SendResponse(response);
    free(response);
    return ret;
}

esp_err_t UsbConsoleEpdType_HandleSet(const usb_console_http_request_t *request,
                                      usb_console_http_response_t *response)
{
    cJSON *root = NULL;
    cJSON *type_item = NULL;
    int type = 0;
    bool changed = false;
    const epd_type_config_t *config = NULL;
    esp_err_t ret = ESP_OK;

    if (request == NULL || response == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ret = ensure_epd_type_loaded();
    if (ret != ESP_OK) {
        set_epd_type_response_json(response, "set_epd_type_result", TDX_JSON_RESULT_EPD_TYPE_INVALID, false, NULL, esp_err_to_name(ret));
        return ESP_OK;
    }
    if (strcasecmp(request->method, "POST") != 0) {
        set_epd_type_response_json(response, "set_epd_type_result", TDX_JSON_RESULT_METHOD_UNSUPPORTED, false, NULL, "method must be POST");
        return ESP_OK;
    }

    root = cJSON_ParseWithLength(request->body, request->body_len);
    if (root == NULL) {
        set_epd_type_response_json(response, "set_epd_type_result", TDX_JSON_RESULT_JSON_INVALID, false, NULL, "invalid json");
        return ESP_OK;
    }

    type_item = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsNumber(type_item)) {
        cJSON_Delete(root);
        set_epd_type_response_json(response, "set_epd_type_result", TDX_JSON_RESULT_FIELD_MISSING, false, NULL, "missing numeric type");
        return ESP_OK;
    }

    type = type_item->valueint;
    config = EpdType_GetConfig((uint8_t)type);
    if (type < 0 || type > 255 || config == NULL) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "reject invalid EPD type=%d", type);
        set_epd_type_response_json(response, "set_epd_type_result", TDX_JSON_RESULT_EPD_TYPE_INVALID, false, NULL, "invalid EPD type");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "set request type=%d current=%u", type, (unsigned int)EPD_type);
    ret = EpdType_SetAndSave((uint8_t)type, &changed);
    cJSON_Delete(root);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set EPD type=%d failed ret=%s", type, esp_err_to_name(ret));
        set_epd_type_response_json(response, "set_epd_type_result", TDX_JSON_RESULT_EPD_TYPE_SAVE_FAILED, false, config, esp_err_to_name(ret));
        return ESP_OK;
    }

    ESP_LOGI(TAG, "set EPD type=%u name=%s changed=%d",
             (unsigned int)config->type,
             config->name,
             changed ? 1 : 0);

    set_epd_type_response_json(response,
                               "set_epd_type_result",
                               TDX_JSON_RESULT_OK,
                               changed,
                               config,
                               changed ? "saved" : "unchanged");
    return ESP_OK;
}

esp_err_t UsbConsoleEpdType_HandleTest(const usb_console_http_request_t *request,
                                       usb_console_http_response_t *response)
{
    const epd_type_config_t *config = NULL;
    char json[384];
    esp_err_t ret = ESP_OK;

    if (request == NULL || response == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "epd_test handler enter method=%s body_len=%u body=\"%.*s\"",
             request->method,
             (unsigned int)request->body_len,
             request->body_len > 96 ? 96 : (int)request->body_len,
             request->body != NULL ? request->body : "");
    ret = ensure_epd_type_loaded();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "epd_test load EPD type failed ret=%s", esp_err_to_name(ret));
        UsbConsoleHttp_SetJson(response,
                               500,
                               "Internal Server Error",
                               "{\"func\":\"test_epd_display_result\",\"result\":1801,\"message\":\"load EPD type failed\"}");
        return ESP_OK;
    }
    config = EpdType_GetCurrentConfig();
    if (strcasecmp(request->method, "POST") != 0) {
        ESP_LOGW(TAG, "epd_test reject method=%s", request->method);
        UsbConsoleHttp_SetJson(response,
                               400,
                               "Bad Request",
                               "{\"func\":\"test_epd_display_result\",\"result\":1005,\"message\":\"method must be POST\"}");
        return ESP_OK;
    }
    if (config == NULL) {
        ESP_LOGE(TAG, "epd_test current EPD config is null");
        UsbConsoleHttp_SetJson(response,
                               500,
                               "Internal Server Error",
                               "{\"func\":\"test_epd_display_result\",\"result\":1801,\"message\":\"invalid current EPD type\"}");
        return ESP_OK;
    }

    // Run the existing EPD test entry only after the PC explicitly requests it.
    // 只有 PC 明确请求时才调用现有 EPD 测试入口，避免影响正常显示流程。
    ESP_LOGI(TAG, "test current EPD type=%u name=%s",
             (unsigned int)config->type,
             config->name);
    ret = test_epd_display_and_wait();
    ESP_LOGI(TAG, "epd_test display completed type=%u name=%s ret=%s",
             (unsigned int)config->type,
             config->name,
             esp_err_to_name(ret));
    if (ret != ESP_OK) {
        snprintf(json,
                 sizeof(json),
                 "{\"func\":\"test_epd_display_result\",\"result\":%d,\"message\":\"test display failed\",\"type\":%u,\"name\":\"%s\"}",
                 TDX_JSON_RESULT_EPD_TEST_DISPLAY_FAILED,
                 (unsigned int)config->type,
                 config->name);
        UsbConsoleHttp_SetJson(response, 500, "Internal Server Error", json);
        return ESP_OK;
    }

    snprintf(json,
             sizeof(json),
             "{\"func\":\"test_epd_display_result\",\"result\":%d,\"type\":%u,\"name\":\"%s\"}",
             TDX_JSON_RESULT_OK,
             (unsigned int)config->type,
             config->name);
    UsbConsoleHttp_SetJson(response, 200, "OK", json);
    return ESP_OK;
}
