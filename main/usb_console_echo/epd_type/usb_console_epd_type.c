#include "usb_console_epd_type.h"

#include <stdio.h>
#include <stdlib.h>

#include "epd_type.h"
#include "usb_console_http_text.h"

static const char *color_name_from_type(uint8_t color_type)
{
    switch (color_type) {
    case BWR_3_Color:
        return "BWR_3_Color";
    case BWRY_4_Color:
        return "BWRY_4_Color";
    case BWYRBG_6_Color:
        return "BWYRBG_6_Color";
    default:
        return "Unknown_Color";
    }
}

esp_err_t UsbConsoleEpdType_SendCurrent(void)
{
    const epd_type_config_t *config = EpdType_GetCurrentConfig();
    usb_console_http_response_t *response = NULL;

    response = (usb_console_http_response_t *)calloc(1, sizeof(*response));
    if (response == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (config == NULL) {
        snprintf(response->body,
                 sizeof(response->body),
                 "{\"func\":\"epd_type\",\"result\":1,\"message\":\"invalid epd type\",\"type\":%u}",
                 (unsigned int)EPD_type);
    } else {
        snprintf(response->body,
                 sizeof(response->body),
                 "{\"func\":\"epd_type\",\"result\":0,\"type\":%u,\"name\":\"%s\",\"width\":%u,\"height\":%u,\"display_size\":%u,\"color_type\":%u,\"color_name\":\"%s\",\"color_count\":%u}",
                 (unsigned int)config->type,
                 config->name,
                 (unsigned int)config->width,
                 (unsigned int)config->height,
                 (unsigned int)config->display_size,
                 (unsigned int)config->color_type,
                 color_name_from_type(config->color_type),
                 (unsigned int)config->color_type);
    }

    response->status = 200;
    response->reason = "OK";
    response->content_type = "application/json";

    // Send current EPD type with the normal framed USB HTTP-like response format.
    // 使用普通带帧头帧尾的 USB HTTP-like 响应格式发送当前 EPD 类型。
    esp_err_t ret = UsbConsoleHttp_SendResponse(response);
    free(response);
    return ret;
}
