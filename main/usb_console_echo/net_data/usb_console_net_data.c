#include "usb_console_net_data.h"

#include <string.h>

#include "esp_log.h"
#include "tdx_cfg.h"
#include "usb_console_cast.h"
#include "usb_console_cast2pic.h"
#include "usb_console_common.h"
#include "usb_console_delete.h"
#include "usb_console_saved_images.h"
#include "usb_console_slideshow.h"
#include "usb_console_slideshow_control.h"
#include "usb_console_snapshot.h"
#include "usb_console_upload.h"
#include "usb_console_wifi_work_time.h"

static const char *TAG = "usb_console_net_data";

esp_err_t UsbConsoleNetData_Handle(const usb_console_http_request_t *request,
                                   usb_console_http_response_t *response)
{
    return UsbConsoleCommon_SubmitAsyncRequest(request, response, "net_data", UsbConsoleNetData_Process);
}

esp_err_t UsbConsoleNetData_Process(const usb_console_http_request_t *request,
                                   usb_console_http_response_t *response)
{
    esp_err_t ret = ESP_ERR_NOT_SUPPORTED;

    if (request == NULL || response == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strstr(request->content_type, "multipart/form-data") != NULL) {
        ret = UsbConsoleCast2Pic_Process(request, response);
        if (ret == ESP_ERR_NOT_SUPPORTED) {
            ret = UsbConsoleCast_Process(request, response);
        }
        if (ret == ESP_ERR_NOT_SUPPORTED) {
            ret = UsbConsoleUpload_Process(request, response);
        }
        if (ret != ESP_ERR_NOT_SUPPORTED) {
            return ret;
        }
    } else {
        ret = UsbConsoleSnapshot_Process(request, response);
        if (ret == ESP_ERR_NOT_SUPPORTED) {
            ret = UsbConsoleSavedImages_Process(request, response);
        }
        if (ret == ESP_ERR_NOT_SUPPORTED) {
            ret = UsbConsoleSlideshow_Process(request, response);
        }
        if (ret == ESP_ERR_NOT_SUPPORTED) {
            ret = UsbConsoleSlideshowControl_Process(request, response);
        }
        if (ret == ESP_ERR_NOT_SUPPORTED) {
            ret = UsbConsoleDelete_Process(request, response);
        }
        if (ret == ESP_ERR_NOT_SUPPORTED) {
            ret = UsbConsoleWifiWorkTime_Process(request, response);
        }
        if (ret != ESP_ERR_NOT_SUPPORTED) {
            return ret;
        }
    }

    ESP_LOGW(TAG, "unsupported USB net_data content_type=%s", request->content_type);
    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"unknown_result\",\"result\":%d,\"message\":\"unsupported func\"}",
                                     TDX_JSON_RESULT_FUNC_UNSUPPORTED);
}
