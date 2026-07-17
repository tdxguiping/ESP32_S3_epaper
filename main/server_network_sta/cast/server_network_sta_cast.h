#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ServerNetworkStaCast_Process(httpd_req_t *req,
                                       char *body,
                                       size_t body_len,
                                       const char *content_type,
                                       const char *base_path,
                                       bool *body_taken);

#ifdef __cplusplus
}
#endif
