#include "server_network_sta_upload.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cast_core.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "tdx_cfg.h"

static const char *TAG = "server_sta_upload";

static void log_heap_watermark(const char *point)
{
    ESP_LOGI(TAG,
             "heap %s free=%u min=%u psram=%u internal=%u",
             point,
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

typedef struct {
    char func[16];
    char file_name[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX];
    size_t bin_size;
    size_t image_size;
    bool save;
    bool show;
} upload_meta_t;

static esp_err_t send_upload_result(httpd_req_t *req, bool ok, const char *message,
                                    const char *error, const upload_meta_t *meta)
{
    char json[SERVER_NETWORK_STA_UPLOAD_RESULT_JSON_MAX];
    char bin_file[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX + 4] = {0};
    char image_file[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX + 4] = {0};
    char message_text[128] = {0};
    char error_text[128] = {0};
    const char *file_name = (meta != NULL && meta->file_name[0]) ? meta->file_name : "";
    int result = TDX_JSON_RESULT_OK;

    strlcpy(message_text, message != NULL ? message : "", sizeof(message_text));
    if (ok && (error == NULL || error[0] == '\0')) {
        strlcpy(error_text, "no error", sizeof(error_text));
    } else {
        strlcpy(error_text, error != NULL ? error : "", sizeof(error_text));
    }
    if (!ok) {
        if (strcmp(error_text, "missing_boundary") == 0) {
            result = TDX_JSON_RESULT_UPLOAD_BOUNDARY_MISSING;
        } else if (strcmp(error_text, "missing_func") == 0) {
            result = TDX_JSON_RESULT_UPLOAD_FUNC_MISSING;
        } else if (strcmp(error_text, "invalid_fileName") == 0) {
            result = TDX_JSON_RESULT_UPLOAD_FILE_NAME_INVALID;
        } else if (strcmp(error_text, "missing_bin") == 0) {
            result = TDX_JSON_RESULT_UPLOAD_BIN_MISSING;
        } else if (strcmp(error_text, "missing_image") == 0) {
            result = TDX_JSON_RESULT_UPLOAD_IMAGE_MISSING;
        } else if (strstr(error_text, "size") != NULL) {
            result = TDX_JSON_RESULT_UPLOAD_SIZE_MISMATCH;
        } else if (strcmp(error_text, "show_failed") == 0 || strcmp(error_text, "display_queue_failed") == 0) {
            result = TDX_JSON_RESULT_DISPLAY_QUEUE_FAILED;
        } else if (strcmp(error_text, "sd_not_ready") == 0) {
            result = TDX_JSON_RESULT_STORAGE_NOT_READY;
        } else if (strcmp(error_text, "storage_not_enough") == 0) {
            result = TDX_JSON_RESULT_STORAGE_NO_SPACE;
        } else if (strcmp(error_text, "save_failed") == 0 || strcmp(error_text, "save_bin_failed") == 0) {
            result = TDX_JSON_RESULT_SAVE_BIN_FAILED;
        } else if (strcmp(error_text, "save_image_failed") == 0) {
            result = TDX_JSON_RESULT_SAVE_IMAGE_FAILED;
        } else {
            result = TDX_JSON_RESULT_UPLOAD_INVALID;
        }
    }
    if (file_name[0] != '\0') {
        snprintf(bin_file, sizeof(bin_file), "%s.bin", file_name);
        snprintf(image_file, sizeof(image_file), "%s.jpg", file_name);
    }

    int json_len = snprintf(json, sizeof(json),
                            "{\"func\":\"upload_result\",\"result\":%d,\"message\":\"%s\",\"fileName\":\"%s\","
                            "\"bin_file\":\"%s\",\"image_file\":\"%s\",\"save\":%s,\"show\":%s,\"error\":\"%s\"}",
                            result,
                            message_text,
                            file_name,
                            bin_file,
                            image_file,
                            (meta != NULL && meta->save) ? "true" : "false",
                            (meta != NULL && meta->show) ? "true" : "false",
                            error_text);
    if (json_len < 0 || (size_t)json_len >= sizeof(json)) {
        ESP_LOGE(TAG, "upload response json too long file=%s", file_name);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req,
                                  "{\"func\":\"upload_result\",\"result\":1010,\"message\":\"upload response too long\",\"error\":\"response_too_long\"}");
    }
    ESP_LOGI(TAG, "upload response: %s", json);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

esp_err_t ServerNetworkStaUpload_Process(httpd_req_t *req,
                                         const char *body,
                                         size_t body_len,
                                         const char *content_type,
                                         const char *base_path)
{
    tdx_cast_core_request_t upload = {0};
    tdx_cast_core_result_t result = {0};
    upload_meta_t meta = {0};

    esp_err_t parse_ret = TdxImageTransfer_ParseSingle(body,
                                                       body_len,
                                                       content_type,
                                                       "upload",
                                                       false,
                                                       false,
                                                       &upload,
                                                       &result);
    if (parse_ret == ESP_ERR_NOT_SUPPORTED) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    snprintf(meta.func, sizeof(meta.func), "upload");
    snprintf(meta.file_name, sizeof(meta.file_name), "%s", upload.file_name);
    meta.save = upload.save;
    meta.show = upload.show;
    meta.bin_size = upload.bin_size;
    meta.image_size = upload.image_size;

    if (parse_ret == ESP_OK) {
        tdx_image_transfer_item_t item = {
            .save = upload.save,
            .show = upload.show,
            .record_last_cast = false,
            .epd_target = 1,
            .bin_part = upload.bin_part,
            .image_part = upload.image_part,
        };
        snprintf(item.save_name, sizeof(item.save_name), "%s", upload.file_name);
        (void)TdxImageTransfer_ProcessItems(&item, 1, base_path, "network upload", &result);
        log_heap_watermark("save_done");
    }

    if (result.result == TDX_JSON_RESULT_OK) {
        return send_upload_result(req, true, "upload success", "", &meta);
    }
    return send_upload_result(req, false,
                              result.message[0] ? result.message : "upload failed",
                              result.error[0] ? result.error : "upload_failed",
                              &meta);
}
