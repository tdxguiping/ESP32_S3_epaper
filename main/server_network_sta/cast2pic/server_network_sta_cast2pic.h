#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ServerNetworkStaCast2Pic_Process(httpd_req_t *req,
                                           const char *body,
                                           size_t body_len,
                                           const char *content_type,
                                           const char *base_path);

#ifdef __cplusplus
}
#endif
