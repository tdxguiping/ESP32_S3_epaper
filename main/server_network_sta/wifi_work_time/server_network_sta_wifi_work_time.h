#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ServerNetworkStaWifiWorkTime_ProcessJson(httpd_req_t *req,
                                                   const char *body,
                                                   size_t body_len);
esp_err_t ServerNetworkStaWifiWorkTime_Init(void);
esp_err_t ServerNetworkStaWifiWorkTime_SetAndSave(uint32_t seconds);
void ServerNetworkStaWifiWorkTime_OnNetworkData(void);
void ServerNetworkStaWifiWorkTime_SetOtaInProgress(bool in_progress);

#ifdef __cplusplus
}
#endif
