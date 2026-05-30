#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ServerNetworkStaWifiWorkTime_ProcessJson(httpd_req_t *req,
                                                   const char *body,
                                                   size_t body_len);
void ServerNetworkStaWifiWorkTime_OnNetworkData(void);

#ifdef __cplusplus
}
#endif
