#include "usb_console_snapshot.h"

#include <string.h>

#include "esp_log.h"
#include "tdx_cfg.h"
#include "usb_console_common.h"

static const char *TAG = "usb_console_snapshot";

esp_err_t UsbConsoleSnapshot_Handle(const usb_console_http_request_t *request,
                                    usb_console_http_response_t *response)
{
    return UsbConsoleCommon_SubmitAsyncRequest(request, response, "snapshot", UsbConsoleSnapshot_Process);
}

esp_err_t UsbConsoleSnapshot_Process(const usb_console_http_request_t *request,
                                    usb_console_http_response_t *response)
{
    if (request == NULL || response == NULL ||
        !UsbConsoleCommon_JsonFuncEquals(request->body, "get_snapshot")) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    size_t used = 0;
    esp_err_t ret = UsbConsoleCommon_SetJsonf(response,
                                             200,
                                             "OK",
                                             "{\"func\":\"get_snapshot_result\",\"result\":0,");
    if (ret != ESP_OK) {
        return ret;
    }
    used = strlen(response->body);
    ret = UsbConsoleCommon_ListSavedImages(response->body, sizeof(response->body), &used);
    if (ret == ESP_OK) {
        ret = UsbConsoleCommon_AppendSnapshot(response->body, sizeof(response->body), &used);
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "get_snapshot failed ret=%s", esp_err_to_name(ret));
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"get_snapshot_result\",\"result\":%d,\"message\":\"snapshot build failed\"}",
                                         TDX_JSON_RESULT_SNAPSHOT_BUILD_FAILED);
    }
    return ESP_OK;
}
