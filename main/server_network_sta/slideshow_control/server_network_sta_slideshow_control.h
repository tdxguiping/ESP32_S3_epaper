#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int result;
    char message[64];
    int sw;
    uint32_t interval;
    bool random;
    int64_t timestamp;
    char time_source[16];
    int64_t time_diff;
    int64_t anchor_epoch;
    int64_t now_epoch;
    int64_t next_epoch;
    uint32_t remain;
} server_network_sta_slideshow_control_result_t;

esp_err_t ServerNetworkStaSlideshowControl_ProcessJson(httpd_req_t *req,
                                                       const char *body,
                                                       size_t body_len,
                                                       const char *base_path);
esp_err_t ServerNetworkStaSlideshowControl_ApplyJson(const char *body,
                                                     const char *base_path,
                                                     server_network_sta_slideshow_control_result_t *result);
esp_err_t ServerNetworkStaSlideshowControl_Disable(const char *base_path);

#ifdef __cplusplus
}
#endif
