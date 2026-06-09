#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "usb_console_http_text.h"

typedef struct {
    bool present;
    const char *data;
    size_t len;
    char filename[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX];
} usb_console_multipart_part_t;

typedef esp_err_t (*usb_console_process_fn_t)(const usb_console_http_request_t *request,
                                              usb_console_http_response_t *response);

bool UsbConsoleCommon_JsonFuncEquals(const char *body, const char *func);
bool UsbConsoleCommon_JsonString(const char *body, const char *key, char *out, size_t out_size);
bool UsbConsoleCommon_JsonU32(const char *body, const char *key, uint32_t *out);
bool UsbConsoleCommon_JsonInt(const char *body, const char *key, int *out);
bool UsbConsoleCommon_JsonBool(const char *body, const char *key, bool *out);
bool UsbConsoleCommon_FileNameIsSafe(const char *name);
esp_err_t UsbConsoleCommon_SetJsonf(usb_console_http_response_t *response,
                                    int status,
                                    const char *reason,
                                    const char *fmt,
                                    ...);
esp_err_t UsbConsoleCommon_ListSavedImages(char *json, size_t json_size, size_t *used);
esp_err_t UsbConsoleCommon_AppendSnapshot(char *json, size_t json_size, size_t *used);
bool UsbConsoleCommon_ExtractBoundary(const char *content_type, char *boundary, size_t boundary_size);
bool UsbConsoleCommon_MultipartPart(const char *body,
                                    size_t body_len,
                                    const char *boundary,
                                    const char *name,
                                    usb_console_multipart_part_t *part);
bool UsbConsoleCommon_MultipartParts(const char *body,
                                     size_t body_len,
                                     const char *boundary,
                                     const char *const *names,
                                     usb_console_multipart_part_t *parts,
                                     size_t part_count);
void UsbConsoleCommon_CopyPartText(const usb_console_multipart_part_t *part, char *out, size_t out_size);
bool UsbConsoleCommon_ParsePartSize(const usb_console_multipart_part_t *part, size_t *out);
bool UsbConsoleCommon_ParsePartBool(const usb_console_multipart_part_t *part, bool default_value);
esp_err_t UsbConsoleCommon_SavePartFile(const char *dir,
                                        const char *file_name,
                                        const char *ext,
                                        const usb_console_multipart_part_t *part);
esp_err_t UsbConsoleCommon_RecordLastCast(const char *file_name);
esp_err_t UsbConsoleCommon_SubmitAsyncRequest(const usb_console_http_request_t *request,
                                              usb_console_http_response_t *response,
                                              const char *name,
                                              usb_console_process_fn_t process);
esp_err_t UsbConsoleCommon_HandleImageTransfer(const usb_console_http_request_t *request,
                                               usb_console_http_response_t *response,
                                               const char *expected_func,
                                               const char *result_func);
