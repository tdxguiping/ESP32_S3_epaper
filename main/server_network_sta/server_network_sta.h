#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#include "tdx_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SERVER_NETWORK_STA_STATE_IDLE = 0,
    SERVER_NETWORK_STA_STATE_CONNECTING,
    SERVER_NETWORK_STA_STATE_GOT_IP,
    SERVER_NETWORK_STA_STATE_FAILED,
} server_network_sta_state_t;

typedef struct {
    server_network_sta_state_t state;
    int last_result;
    char ip[16];
} server_network_sta_status_t;

esp_err_t ServerNetworkSta_Init(void);
void ServerNetworkSta_RequestProvisioning(void);
uint8_t User_Network_mode_app_init(const char *base_path);
uint8_t User_Network_mode_app_init_force(const char *base_path);
int ServerNetworkSta_GetLastConnectResult(void);
esp_err_t ServerNetworkSta_GetStatus(server_network_sta_status_t *status);

#ifdef __cplusplus
}
#endif
