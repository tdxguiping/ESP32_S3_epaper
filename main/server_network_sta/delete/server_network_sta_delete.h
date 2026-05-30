#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ServerNetworkStaDelete_ProcessJson(httpd_req_t *req,
                                             const char *body,
                                             size_t body_len,
                                             const char *base_path);

#ifdef __cplusplus
}
#endif
