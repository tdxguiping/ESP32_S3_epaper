#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
esp_err_t ServerNetworkStaSlideshow_StartSavedResetInterval(const char *base_path);
esp_err_t ServerNetworkStaSlideshow_StartSavedDelayed(const char *base_path);
bool ServerNetworkStaSlideshow_IsSavedEnabled(const char *base_path,
                                              uint32_t *interval,
                                              bool *random);
bool ServerNetworkStaSlideshow_GetRuntimeTiming(uint32_t *interval,
                                                uint32_t *elapsed,
                                                bool *running);
bool ServerNetworkStaSlideshow_GetScheduleTiming(uint32_t *interval,
                                                 int64_t *now_epoch,
                                                 int64_t *next_epoch,
                                                 uint32_t *remain,
                                                 bool *running);
void ServerNetworkStaSlideshow_Stop(void);

#ifdef __cplusplus
}
#endif
