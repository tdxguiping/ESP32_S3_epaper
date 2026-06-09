#include "usb_console_upload.h"

#include "usb_console_common.h"

esp_err_t UsbConsoleUpload_Handle(const usb_console_http_request_t *request,
                                  usb_console_http_response_t *response)
{
    return UsbConsoleCommon_SubmitAsyncRequest(request, response, "upload", UsbConsoleUpload_Process);
}

esp_err_t UsbConsoleUpload_Process(const usb_console_http_request_t *request,
                                  usb_console_http_response_t *response)
{
    return UsbConsoleCommon_HandleImageTransfer(request, response, "upload", "upload_result");
}
