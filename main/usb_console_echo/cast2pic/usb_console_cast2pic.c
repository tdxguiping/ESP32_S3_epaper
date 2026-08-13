#include "usb_console_cast2pic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cast_core.h"
#include "epd_display_mode.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "tdx_cfg.h"
#include "usb_console_common.h"
#include "usb_console_worker.h"

static const char *TAG = "usb_console_cast2pic";

typedef struct {
    tdx_image_transfer_item_t item;
    char *bin_data;
    char *image_data;
} usb_cast2pic_background_job_t;

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

    snprintf(file_name_field, sizeof(file_name_field), "fileName");
    snprintf(bin_size_field, sizeof(bin_size_field), "bin_size");
    snprintf(image_size_field, sizeof(image_size_field), "image_size");
    snprintf(bin_field, sizeof(bin_field), "bin");
    snprintf(image_field, sizeof(image_field), "image");
    (void)UsbConsoleCommon_MultipartParts(request->body,
                                          request->body_len,
                                          boundary,
                                          part_names,
                                          parts,
                                          sizeof(parts) / sizeof(parts[0]));

    UsbConsoleCommon_CopyPartText(file_name_part, user_file_name, sizeof(user_file_name));
    if (!UsbConsoleCommon_FileNameIsSafe(user_file_name) ||
        !UsbConsoleCommon_FileNameIsSafe(save_name)) {
        ESP_LOGW(TAG,
                 "cast2pic invalid fileName len=%u max=%u",
                 (unsigned int)strlen(user_file_name),
                 (unsigned int)TDX_IMAGE_BASE_NAME_MAX_BYTES);
        return set_cast2pic_error(response,
                                  TDX_JSON_RESULT_UPLOAD_FILE_NAME_INVALID,
                                  "invalid_fileName");
    }
    if (!UsbConsoleCommon_ParsePartSize(bin_size_part, &bin_size) ||
        !UsbConsoleCommon_ParsePartSize(image_size_part, &image_size) ||
        !bin_part->present || !image_part->present ||
        /*
         * bin_size is the received wire size. It may vary when the BIN part
         * contains zlib data, but it must still match the actual part length.
         */
        bin_part->len != bin_size || image_part->len != image_size) {
        ESP_LOGW(TAG,
                 "cast2pic invalid user_file=%s bin=%u/%u image=%u/%u",
                 user_file_name,
                 (unsigned int)bin_part->len,
                 (unsigned int)bin_size,
                 (unsigned int)image_part->len,
                 (unsigned int)image_size);
        return set_cast2pic_error(response, TDX_JSON_RESULT_UPLOAD_INVALID, "invalid_upload");
    }

    ESP_LOGI(TAG,
             "cast2pic meta screen=%u user_file=%s save_file=%s save=%d show=%d bin=%u image=%u parse_ms=%lu total_ms=%lu",
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
    strlcpy(item->save_name, save_name, sizeof(item->save_name));
    item->save = save;
    item->show = show;
    item->record_last_cast = false;
    item->storage = TDX_IMAGE_TRANSFER_STORAGE_CAST_DIR;
    item->epd_target = screen_number;
    item->bin_part = *bin_part;
    item->image_part = *image_part;
    return ESP_OK;
}

static void cast2pic_background_free(usb_cast2pic_background_job_t *job)
{
    if (job == NULL) {
        return;
    }
    free(job->bin_data);
    free(job->image_data);
    free(job);
}

static void cast2pic_background_process(void *ctx)
{
    usb_cast2pic_background_job_t *job = (usb_cast2pic_background_job_t *)ctx;
    tdx_cast_core_result_t result = {0};

    if (job == NULL) {
        return;
    }
    (void)TdxImageTransfer_ProcessItems(&job->item, 1, USB_CONSOLE_BASE_PATH,
                                        "usb cast2pic", &result);
    if (result.result == TDX_JSON_RESULT_OK) {
        ESP_LOGI(TAG, "cast2pic background done result=0");
    } else {
        ESP_LOGE(TAG, "cast2pic background failed result=%d error=%s",
                 result.result,
                 result.error[0] ? result.error : "cast2pic_failed");
    }
    cast2pic_background_free(job);
}

static void cast2pic_submit_background(const tdx_image_transfer_item_t *item)
{
    usb_cast2pic_background_job_t *job = NULL;

    if (item == NULL || (!item->show && !item->save)) {
        return;
    }
    job = (usb_cast2pic_background_job_t *)calloc(1, sizeof(*job));
    if (job == NULL) {
        ESP_LOGE(TAG, "cast2pic received but background job alloc failed");
        return;
    }
    job->bin_data = (char *)malloc(item->bin_part.len);
    job->image_data = (char *)malloc(item->image_part.len);
    if (job->bin_data == NULL || job->image_data == NULL) {
        ESP_LOGE(TAG, "cast2pic received but background data alloc failed");
        cast2pic_background_free(job);
        return;
    }

    memcpy(job->bin_data, item->bin_part.data, item->bin_part.len);
    memcpy(job->image_data, item->image_part.data, item->image_part.len);
    job->item = *item;
    job->item.bin_part.data = job->bin_data;
    job->item.image_part.data = job->image_data;

    esp_err_t submit_ret = UsbConsoleWorker_SubmitJob("cast2pic_bg",
                                                      cast2pic_background_process,
                                                      job);
    if (submit_ret != ESP_OK) {
        ESP_LOGE(TAG, "cast2pic received but background submit failed ret=%s",
                 esp_err_to_name(submit_ret));
        cast2pic_background_free(job);
    }
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
    tdx_image_transfer_item_t item = {0};

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

    if (screen[0] == '\0' || strcmp(screen, "ab") == 0) {
        return set_cast2pic_error(response,
                                  TDX_JSON_RESULT_CAST2PIC_SCREEN_UNSUPPORTED,
                                  "unsupported_screen");
    }
    if (strcmp(screen, "a") != 0 && strcmp(screen, "b") != 0) {
        return set_cast2pic_error(response, TDX_JSON_RESULT_CAST2PIC_SCREEN_INVALID, "invalid_screen");
    }

    ESP_LOGI(TAG,
             "cast2pic request screen=%s save=%d show=%d body_len=%u",
             screen,
             save ? 1 : 0,
             show ? 1 : 0,
             (unsigned int)request->body_len);

    const bool screen_a = strcmp(screen, "a") == 0;
    esp_err_t parse_ret = cast2pic_parse_one(request,
                                             response,
                                             boundary,
                                             screen_a ? "screen_b" : "screen_a",
                                             screen_a ? 2 : 1,
                                             save,
                                             show,
                                             total_start_us,
                                             &item);
    if (parse_ret != ESP_OK) {
        return parse_ret;
    }

    esp_err_t mode_ret = EpdDisplayMode_Set(USER_EPD_DISPLAY_MODE_NORMAL);
    if (mode_ret != ESP_OK) {
        ESP_LOGE(TAG, "cast2pic mode save failed screen=%s ret=%s",
                 screen, esp_err_to_name(mode_ret));
        return set_cast2pic_error(response,
                                  TDX_JSON_RESULT_INTERNAL_ERROR,
                                  "mode_save_failed");
    }
    cast2pic_submit_background(&item);

    ESP_LOGI(TAG,
             "cast2pic received screen=%s total_ms=%lu",
             screen,
             (unsigned long)cast2pic_elapsed_ms_since(total_start_us));
    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"cast2pic_result\",\"result\":%d}",
                                     TDX_JSON_RESULT_OK);
}
