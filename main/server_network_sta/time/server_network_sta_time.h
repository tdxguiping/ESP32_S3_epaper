#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SERVER_NETWORK_STA_TIME_SOURCE_NONE = 0,
    SERVER_NETWORK_STA_TIME_SOURCE_DEFAULT,
    SERVER_NETWORK_STA_TIME_SOURCE_APP,
    SERVER_NETWORK_STA_TIME_SOURCE_SNTP,
} server_network_sta_time_source_t;

typedef struct {
    bool valid;
    bool sntp_synced;
    server_network_sta_time_source_t source;
    int64_t epoch;
    char local_time[32];
    char utc_time[32];
    char server[32];
    char timezone[16];
} server_network_sta_time_info_t;

esp_err_t ServerNetworkStaTime_Init(void);
esp_err_t ServerNetworkStaTime_SetDefaultIfInvalid(void);
esp_err_t ServerNetworkStaTime_SetTimestamp(int64_t timestamp);
esp_err_t ServerNetworkStaTime_SetAppTime(int64_t epoch);
esp_err_t ServerNetworkStaTime_ProcessGet(httpd_req_t *req);
esp_err_t ServerNetworkStaTime_GetInfo(server_network_sta_time_info_t *info);
bool ServerNetworkStaTime_IsSntpSynced(void);

#ifdef __cplusplus
}
#endif
