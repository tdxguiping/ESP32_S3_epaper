#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SERVER_NETWORK_STA_DATAUP_ASYNC_IDLE = 0,
    SERVER_NETWORK_STA_DATAUP_ASYNC_BUSY,
    SERVER_NETWORK_STA_DATAUP_ASYNC_TIMEOUT,
} server_network_sta_dataup_async_state_t;

typedef void (*server_network_sta_dataup_async_fn_t)(void *ctx);

esp_err_t ServerNetworkStaDataupAsync_Init(void);
server_network_sta_dataup_async_state_t ServerNetworkStaDataupAsync_GetState(void);
const char *ServerNetworkStaDataupAsync_StateName(server_network_sta_dataup_async_state_t state);
esp_err_t ServerNetworkStaDataupAsync_Submit(const char *name,
                                             server_network_sta_dataup_async_fn_t process,
                                             server_network_sta_dataup_async_fn_t cleanup,
                                             void *ctx);

#ifdef __cplusplus
}
#endif
