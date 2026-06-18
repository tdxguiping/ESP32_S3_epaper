#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "usb_console_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char file_name[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX];
    bool save;
    bool show;
    size_t bin_size;
    size_t image_size;
    usb_console_multipart_part_t bin_part;
    usb_console_multipart_part_t image_part;
} tdx_cast_core_request_t;

typedef struct {
    int result;
    char message[64];
    char error[64];
    char file_name[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX];
} tdx_cast_core_result_t;

typedef struct {
    char save_name[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX];
    bool save;
    bool show;
    bool record_last_cast;
    uint8_t epd_target;
    usb_console_multipart_part_t bin_part;
    usb_console_multipart_part_t image_part;
} tdx_image_transfer_item_t;

void TdxCastCore_ResultOk(tdx_cast_core_result_t *result, const char *file_name, const char *message);
esp_err_t TdxImageTransfer_ParseSingle(const char *body,
                                       size_t body_len,
                                       const char *content_type,
                                       const char *expected_func,
                                       bool default_show,
                                       bool require_save,
                                       tdx_cast_core_request_t *cast,
                                       tdx_cast_core_result_t *result);
esp_err_t TdxImageTransfer_ProcessItems(const tdx_image_transfer_item_t *items,
                                        size_t item_count,
                                        const char *base_path,
                                        const char *log_prefix,
                                        tdx_cast_core_result_t *result);
esp_err_t TdxCastCore_ParseAndValidate(const char *body,
                                       size_t body_len,
                                       const char *content_type,
                                       tdx_cast_core_request_t *cast,
                                       tdx_cast_core_result_t *result);
esp_err_t TdxCastCore_ProcessValidated(const tdx_cast_core_request_t *cast,
                                       const char *base_path,
                                       const char *log_prefix,
                                       tdx_cast_core_result_t *result);

#ifdef __cplusplus
}
#endif
