#include "usb_console_wifi_work_time.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "server_network_sta_wifi_work_time.h"
#include "tdx_cfg.h"
#include "usb_console_common.h"

static const char *usb_wifi_work_time_find_json_key(const char *body, const char *key)
{
    char pattern[64];
    const char *pos = body;
    if (body == NULL || key == NULL) {
        return NULL;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    while ((pos = strstr(pos, pattern)) != NULL) {
        const char *after = pos + strlen(pattern);
        while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n') {
            after++;
        }
        if (*after == ':') {
            return pos;
        }
        pos += strlen(pattern);
    }
    return NULL;
}

static bool usb_wifi_work_time_json_i64(const char *body, const char *key, int64_t *out)
{
    const char *pos = usb_wifi_work_time_find_json_key(body, key);
    char *end_ptr = NULL;
    long long value = 0;
    if (pos == NULL || out == NULL) {
        return false;
    }

    pos += strlen(key) + 2;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    if (*pos != ':') {
        return false;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    if ((*pos == '-' && (pos[1] < '0' || pos[1] > '9')) ||
        (*pos != '-' && (*pos < '0' || *pos > '9'))) {
        return false;
    }

    errno = 0;
    value = strtoll(pos, &end_ptr, 10);
    if (errno != 0 || end_ptr == pos) {
        return false;
    }
    while (*end_ptr == ' ' || *end_ptr == '\t' || *end_ptr == '\r' || *end_ptr == '\n') {
        end_ptr++;
    }
    if (*end_ptr != ',' && *end_ptr != '}') {
        return false;
    }

    *out = (int64_t)value;
    return true;
}

esp_err_t UsbConsoleWifiWorkTime_Handle(const usb_console_http_request_t *request,
                                        usb_console_http_response_t *response)
{
    return UsbConsoleCommon_SubmitAsyncRequest(request, response, "wifi_work_time", UsbConsoleWifiWorkTime_Process);
}

esp_err_t UsbConsoleWifiWorkTime_Process(const usb_console_http_request_t *request,
                                        usb_console_http_response_t *response)
{
    int64_t seconds_value = 0;

    if (request == NULL || response == NULL ||
        !UsbConsoleCommon_JsonFuncEquals(request->body, "set_wifi_work_time")) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (usb_wifi_work_time_find_json_key(request->body, "time") != NULL) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"set_wifi_work_time_result\",\"result\":%d,\"message\":\"time field is not supported\"}",
                                         TDX_JSON_RESULT_PARAM_INVALID);
    }
    if (!usb_wifi_work_time_json_i64(request->body, "seconds", &seconds_value)) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"set_wifi_work_time_result\",\"result\":%d,\"message\":\"set wifi work time failed\"}",
                                         TDX_JSON_RESULT_WIFI_WORK_TIME_MISSING);
    }
    if (seconds_value < SERVER_NETWORK_STA_WIFI_WORK_TIME_NETWORK_USB_MIN_SECONDS ||
        seconds_value > SERVER_NETWORK_STA_WIFI_WORK_TIME_MAX_SECONDS) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"set_wifi_work_time_result\",\"result\":%d,\"message\":\"set wifi work time failed\"}",
                                         TDX_JSON_RESULT_WIFI_WORK_TIME_RANGE);
    }

    esp_err_t ret = ServerNetworkStaWifiWorkTime_SetAndSave((uint32_t)seconds_value);
    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"set_wifi_work_time_result\",\"result\":%d}",
                                     ret == ESP_OK ? TDX_JSON_RESULT_OK :
                                     ret == ESP_ERR_INVALID_STATE ? TDX_JSON_RESULT_WIFI_WORK_TIME_APPLY_FAILED :
                                     TDX_JSON_RESULT_WIFI_WORK_TIME_SAVE_FAILED);
}
