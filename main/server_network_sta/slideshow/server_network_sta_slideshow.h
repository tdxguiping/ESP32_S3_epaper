#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ServerNetworkStaSlideshow_ProcessJson(httpd_req_t *req,
                                                const char *body,
                                                size_t body_len,
                                                const char *base_path);
esp_err_t ServerNetworkStaSlideshow_ShowFirst(const char *base_path);
esp_err_t ServerNetworkStaSlideshow_StartSaved(const char *base_path);
void ServerNetworkStaSlideshow_Stop(void);

#ifdef __cplusplus
}
#endif
