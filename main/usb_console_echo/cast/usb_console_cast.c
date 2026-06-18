#include "usb_console_cast.h"

#include "cast_core.h"
#include "usb_console_common.h"

esp_err_t UsbConsoleCast_Handle(const usb_console_http_request_t *request,
                                usb_console_http_response_t *response)
{
    return UsbConsoleCast_SubmitAsync(request, response);
}

esp_err_t UsbConsoleCast_Process(const usb_console_http_request_t *request,
                                 usb_console_http_response_t *response)
{
    tdx_cast_core_request_t cast = {0};
    tdx_cast_core_result_t result = {0};

    if (request == NULL || response == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t parse_ret = TdxCastCore_ParseAndValidate(request->body,
                                                       request->body_len,
                                                       request->content_type,
                                                       &cast,
                                                       &result);
    if (parse_ret == ESP_ERR_NOT_SUPPORTED) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (parse_ret == ESP_OK) {
        (void)TdxCastCore_ProcessValidated(&cast, USB_CONSOLE_BASE_PATH, "usb cast", &result);
    }

    if (result.result == TDX_JSON_RESULT_OK) {
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"cast_result\",\"result\":%d,\"message\":\"ok\",\"fileName\":\"%s\"}",
                                         TDX_JSON_RESULT_OK,
                                         result.file_name[0] ? result.file_name : cast.file_name);
    }

    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"cast_result\",\"result\":%d,\"message\":\"%s\",\"error\":\"%s\"}",
                                     result.result,
                                     result.message[0] ? result.message : "cast failed",
                                     result.error[0] ? result.error : "");
}
