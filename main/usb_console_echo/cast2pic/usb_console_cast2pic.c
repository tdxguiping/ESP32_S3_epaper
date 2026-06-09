#include "usb_console_cast2pic.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "epd_display_app.h"
#include "tdx_cfg.h"
#include "usb_console_common.h"

static const char *TAG = "usb_console_cast2pic";

static uint32_t cast2pic_elapsed_ms_since(int64_t start_us)
{
    return (uint32_t)((esp_timer_get_time() - start_us) / 1000);
}

static esp_err_t set_cast2pic_error(usb_console_http_response_t *response, const char *error)
{
    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"cast2pic_result\",\"result\":1,\"message\":\"cast2pic failed\",\"error\":\"%s\"}",
                                     error != NULL ? error : "unknown");
}

static esp_err_t cast2pic_process_one(const usb_console_http_request_t *request,
                                      usb_console_http_response_t *response,
                                      const char *boundary,
                                      const char *suffix,
                                      const char *save_name,
                                      uint8_t screen_number,
                                      bool save,
                                      bool show,
                                      int64_t total_start_us)
{
    char user_file_name[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX] = {0};
    char bin_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    char jpg_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
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
        return set_cast2pic_error(response, "invalid_upload");
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

    if (save) {
        snprintf(bin_dir, sizeof(bin_dir), "%s/bin_img", USB_CONSOLE_BASE_PATH);
        snprintf(jpg_dir, sizeof(jpg_dir), "%s/jpg_img", USB_CONSOLE_BASE_PATH);

        stage_start_us = esp_timer_get_time();
        esp_err_t save_bin_ret = UsbConsoleCommon_SavePartFile(bin_dir, save_name, ".bin", bin_part);
        ESP_LOGI(TAG,
                 "cast2pic save bin suffix=%s ret=%s elapsed_ms=%lu total_ms=%lu",
                 suffix,
                 esp_err_to_name(save_bin_ret),
                 (unsigned long)cast2pic_elapsed_ms_since(stage_start_us),
                 (unsigned long)cast2pic_elapsed_ms_since(total_start_us));
        if (save_bin_ret != ESP_OK) {
            return set_cast2pic_error(response, "save_bin_failed");
        }

        stage_start_us = esp_timer_get_time();
        esp_err_t save_image_ret = UsbConsoleCommon_SavePartFile(jpg_dir, save_name, ".jpg", image_part);
        ESP_LOGI(TAG,
                 "cast2pic save image suffix=%s ret=%s elapsed_ms=%lu total_ms=%lu",
                 suffix,
                 esp_err_to_name(save_image_ret),
                 (unsigned long)cast2pic_elapsed_ms_since(stage_start_us),
                 (unsigned long)cast2pic_elapsed_ms_since(total_start_us));
        if (save_image_ret != ESP_OK) {
            return set_cast2pic_error(response, "save_image_failed");
        }
    }

    if (show) {
        stage_start_us = esp_timer_get_time();
        esp_err_t display_ret = ServerNetworkStaEpdDisplay_QueueToScreen((const uint8_t *)bin_part->data,
                                                                         bin_part->len,
                                                                         screen_number);
        ESP_LOGI(TAG,
                 "cast2pic display suffix=%s ret=%s elapsed_ms=%lu total_ms=%lu",
                 suffix,
                 esp_err_to_name(display_ret),
                 (unsigned long)cast2pic_elapsed_ms_since(stage_start_us),
                 (unsigned long)cast2pic_elapsed_ms_since(total_start_us));
        if (display_ret != ESP_OK) {
            return set_cast2pic_error(response, "display_queue_failed");
        }
    }

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

    if (request == NULL || response == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!UsbConsoleCommon_ExtractBoundary(request->content_type, boundary, sizeof(boundary))) {
        return set_cast2pic_error(response, "missing_boundary");
    }
    if (!UsbConsoleCommon_MultipartParts(request->body,
                                         request->body_len,
                                         boundary,
                                         part_names,
                                         parts,
                                         sizeof(parts) / sizeof(parts[0])) ||
        !func_part->present) {
        return set_cast2pic_error(response, "missing_func");
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
        return set_cast2pic_error(response, "invalid_screen");
    }

    ESP_LOGI(TAG,
             "cast2pic request screen=%s save=%d show=%d body_len=%u",
             screen,
             save ? 1 : 0,
             show ? 1 : 0,
             (unsigned int)request->body_len);

    if (strcasecmp(screen, "b") != 0) {
        esp_err_t ret = cast2pic_process_one(request, response, boundary, "A", "screen_a", 1, save, show, total_start_us);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    if (strcasecmp(screen, "a") != 0) {
        esp_err_t ret = cast2pic_process_one(request, response, boundary, "B", "screen_b", 2, save, show, total_start_us);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    ESP_LOGI(TAG,
             "cast2pic result ready screen=%s total_ms=%lu",
             screen,
             (unsigned long)cast2pic_elapsed_ms_since(total_start_us));
    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"cast2pic_result\",\"result\":0,\"message\":\"ok\",\"screen\":\"%s\"}",
                                     screen);
}
