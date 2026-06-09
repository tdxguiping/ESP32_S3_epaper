#include "usb_console_cast.h"

#include "usb_console_common.h"

esp_err_t UsbConsoleCast_Handle(const usb_console_http_request_t *request,
                                usb_console_http_response_t *response)
{
    return UsbConsoleCast_SubmitAsync(request, response);
}

esp_err_t UsbConsoleCast_Process(const usb_console_http_request_t *request,
                                 usb_console_http_response_t *response)
{
    return UsbConsoleCommon_HandleImageTransfer(request, response, "cast", "cast_result");
}
