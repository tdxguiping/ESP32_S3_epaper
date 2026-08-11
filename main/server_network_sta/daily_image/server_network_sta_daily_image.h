#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Base initialization is intentionally separate from loading a saved job. */
esp_err_t ServerNetworkStaDailyImage_Init(const char *base_path);
esp_err_t ServerNetworkStaDailyImage_StartSaved(void);
// Invalidate queued and running DAILY work without erasing its saved configuration.
esp_err_t ServerNetworkStaDailyImage_StopAndWait(void);
esp_err_t ServerNetworkStaDailyImage_GetPowerOffWakeSeconds(
    uint32_t *wake_seconds,
    bool *keep_awake);
esp_err_t ServerNetworkStaDailyImage_ResetConfig(void);
esp_err_t ServerNetworkStaDailyImage_ProcessJson(httpd_req_t *req,
                                                 const char *body,
                                                 size_t body_len,
                                                 const char *base_path);

#ifdef __cplusplus
}
#endif
