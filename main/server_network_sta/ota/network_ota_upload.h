#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

bool NetworkOtaUpload_IsOtaRequest(httpd_req_t *req, const char *content_type);
// Report the short interval after a successful response and before the OTA-owned restart task runs.
bool NetworkOtaUpload_IsRestartPending(void);
size_t NetworkOtaUpload_GetMaxBodySize(void);
esp_err_t NetworkOtaUpload_SendErrorAndFinish(httpd_req_t *req,
                                              const char *stage,
                                              const char *message,
                                              esp_err_t err);
esp_err_t NetworkOtaUpload_ProcessReceivedBody(httpd_req_t *req,
                                               const char *body,
                                               size_t body_len,
                                               const char *content_type);

#ifdef __cplusplus
}
#endif
