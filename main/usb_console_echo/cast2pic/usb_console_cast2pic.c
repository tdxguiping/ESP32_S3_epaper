#include "usb_console_cast2pic.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cast_core.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "tdx_cfg.h"
#include "usb_console_common.h"

static const char *TAG = "usb_console_cast2pic";

static uint32_t cast2pic_elapsed_ms_since(int64_t start_us)
{
    return (uint32_t)((esp_timer_get_time() - start_us) / 1000);
}

static esp_err_t set_cast2pic_error(usb_console_http_response_t *response, int result, const char *error)
{
    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"cast2pic_result\",\"result\":%d,\"message\":\"cast2pic failed\",\"error\":\"%s\"}",
                                     result,
                                     error != NULL ? error : "unknown");
}

static esp_err_t cast2pic_parse_one(const usb_console_http_request_t *request,
                                    usb_console_http_response_t *response,
                                    const char *boundary,
                                    const char *suffix,
                                    const char *save_name,
                                    uint8_t screen_number,
                                    bool save,
                                    bool show,
                                    int64_t total_start_us,
                                    tdx_image_transfer_item_t *item)
{
    char user_file_name[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX] = {0};
    size_t bin_size = 0;
    size_t image_size = 0;
    char file_name_field[24];
    char bin_size_field[24];
    char image_size_field[24];
    char bin_field[24];
    char image_field[24];
    const char *part_names[] = {
        file_name_field,
        bin_size_field,
        image_size_field,
        bin_field,
        image_field,
    };
    usb_console_multipart_part_t parts[sizeof(part_names) / sizeof(part_names[0])] = {0};
    usb_console_multipart_part_t *file_name_part = &parts[0];
    usb_console_multipart_part_t *bin_size_part = &parts[1];
    usb_console_multipart_part_t *image_size_part = &parts[2];
    usb_console_multipart_part_t *bin_part = &parts[3];
    usb_console_multipart_part_t *image_part = &parts[4];
    int64_t stage_start_us = esp_timer_get_time();

    snprintf(file_name_field, sizeof(file_name_field), "fileName%s", suffix);
    snprintf(bin_size_field, sizeof(bin_size_field), "bin_size%s", suffix);
    snprintf(image_size_field, sizeof(image_size_field), "image_size%s", suffix);
    snprintf(bin_field, sizeof(bin_field), "bin%s", suffix);
    snprintf(image_field, sizeof(image_field), "image%s", suffix);
    (void)UsbConsoleCommon_MultipartParts(request->body,
                                          request->body_len,
                                          boundary,
                                          part_names,
                                          parts,
                                          sizeof(parts) / sizeof(parts[0]));

    UsbConsoleCommon_CopyPartText(file_name_part, user_file_name, sizeof(user_file_name));
    if (!UsbConsoleCommon_FileNameIsSafe(user_file_name) ||
        !UsbConsoleCommon_FileNameIsSafe(save_name) ||
        !UsbConsoleCommon_ParsePartSize(bin_size_part, &bin_size) ||
        !UsbConsoleCommon_ParsePartSize(image_size_part, &image_size) ||
        !bin_part->present || !image_part->present ||
        bin_part->len != bin_size || image_part->len != image_size) {
        ESP_LOGW(TAG,
                 "cast2pic invalid suffix=%s user_file=%s bin=%u/%u image=%u/%u",
                 suffix,
                 user_file_name,
                 (unsigned int)bin_part->len,
                 (unsigned int)bin_size,
                 (unsigned int)image_part->len,
                 (unsigned int)image_size);
        return set_cast2pic_error(response, TDX_JSON_RESULT_UPLOAD_INVALID, "invalid_upload");
    }

    ESP_LOGI(TAG,
             "cast2pic meta suffix=%s screen=%u user_file=%s save_file=%s save=%d show=%d bin=%u image=%u parse_ms=%lu total_ms=%lu",
             suffix,
             (unsigned int)screen_number,
             user_file_name,
             save_name,
             save ? 1 : 0,
             show ? 1 : 0,
             (unsigned int)bin_part->len,
             (unsigned int)image_part->len,
             (unsigned long)cast2pic_elapsed_ms_since(stage_start_us),
             (unsigned long)cast2pic_elapsed_ms_since(total_start_us));

    if (item == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(item, 0, sizeof(*item));
    snprintf(item->save_name, sizeof(item->save_name), "%s", save_name);
    item->save = save;
    item->show = show;
    item->record_last_cast = false;
    item->epd_target = screen_number;
    item->bin_part = *bin_part;
    item->image_part = *image_part;
    return ESP_OK;
}

esp_err_t UsbConsoleCast2Pic_Handle(const usb_console_http_request_t *request,
                                    usb_console_http_response_t *response)
{
    return UsbConsoleCommon_SubmitAsyncRequest(request, response, "cast2pic", UsbConsoleCast2Pic_Process);
}

esp_err_t UsbConsoleCast2Pic_Process(const usb_console_http_request_t *request,
                                     usb_console_http_response_t *response)
{
    char boundary[SERVER_NETWORK_STA_OTA_BOUNDARY_MAX] = {0};
    char func[16] = {0};
    char screen[8] = {0};
    bool save = true;
    bool show = true;
    const char *part_names[] = {
        "func",
        "screen",
        "save",
        "show",
    };
    usb_console_multipart_part_t parts[sizeof(part_names) / sizeof(part_names[0])] = {0};
    usb_console_multipart_part_t *func_part = &parts[0];
    usb_console_multipart_part_t *screen_part = &parts[1];
    usb_console_multipart_part_t *save_part = &parts[2];
    usb_console_multipart_part_t *show_part = &parts[3];
    int64_t total_start_us = esp_timer_get_time();
    tdx_image_transfer_item_t items[2] = {0};
    size_t item_count = 0;

    if (request == NULL || response == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!UsbConsoleCommon_ExtractBoundary(request->content_type, boundary, sizeof(boundary))) {
        return set_cast2pic_error(response, TDX_JSON_RESULT_UPLOAD_BOUNDARY_MISSING, "missing_boundary");
    }
    if (!UsbConsoleCommon_MultipartParts(request->body,
                                         request->body_len,
                                         boundary,
                                         part_names,
                                         parts,
                                         sizeof(parts) / sizeof(parts[0])) ||
        !func_part->present) {
        return set_cast2pic_error(response, TDX_JSON_RESULT_UPLOAD_FUNC_MISSING, "missing_func");
    }

    UsbConsoleCommon_CopyPartText(func_part, func, sizeof(func));
    if (strcmp(func, "cast2pic") != 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    UsbConsoleCommon_CopyPartText(screen_part, screen, sizeof(screen));
    save = UsbConsoleCommon_ParsePartBool(save_part, true);
    show = UsbConsoleCommon_ParsePartBool(show_part, true);

    if (screen[0] == '\0') {
        snprintf(screen, sizeof(screen), "ab");
    }
    if (strcasecmp(screen, "a") != 0 && strcasecmp(screen, "b") != 0 && strcasecmp(screen, "ab") != 0) {
        return set_cast2pic_error(response, TDX_JSON_RESULT_CAST2PIC_SCREEN_INVALID, "invalid_screen");
    }

    ESP_LOGI(TAG,
             "cast2pic request screen=%s save=%d show=%d body_len=%u",
             screen,
             save ? 1 : 0,
             show ? 1 : 0,
             (unsigned int)request->body_len);

    if (strcasecmp(screen, "b") != 0) {
        esp_err_t ret = cast2pic_parse_one(request, response, boundary, "A", "screen_a", 1, save, show, total_start_us, &items[item_count]);
        if (ret != ESP_OK) {
            return ret;
        }
        item_count++;
    }

    if (strcasecmp(screen, "a") != 0) {
        esp_err_t ret = cast2pic_parse_one(request, response, boundary, "B", "screen_b", 2, save, show, total_start_us, &items[item_count]);
        if (ret != ESP_OK) {
            return ret;
        }
        item_count++;
    }

    tdx_cast_core_result_t result = {0};
    (void)TdxImageTransfer_ProcessItems(items, item_count, USB_CONSOLE_BASE_PATH, "usb cast2pic", &result);
    if (result.result != TDX_JSON_RESULT_OK) {
        return set_cast2pic_error(response,
                                  result.result,
                                  result.error[0] ? result.error : "cast2pic_failed");
    }

    ESP_LOGI(TAG,
             "cast2pic result ready screen=%s total_ms=%lu",
             screen,
             (unsigned long)cast2pic_elapsed_ms_since(total_start_us));
    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"cast2pic_result\",\"result\":%d,\"message\":\"ok\",\"screen\":\"%s\"}",
                                     TDX_JSON_RESULT_OK,
                                     screen);
}
