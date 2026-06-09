#include "usb_console_cast2pic.h"

#include "usb_console_common.h"

esp_err_t UsbConsoleCast2Pic_Handle(const usb_console_http_request_t *request,
                                    usb_console_http_response_t *response)
{
    return UsbConsoleCommon_SubmitAsyncRequest(request, response, "cast2pic", UsbConsoleCast2Pic_Process);
}

esp_err_t UsbConsoleCast2Pic_Process(const usb_console_http_request_t *request,
                                     usb_console_http_response_t *response)
{
    return UsbConsoleCommon_HandleImageTransfer(request, response, "cast2pic", "cast2pic_result");
}
