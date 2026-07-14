#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#include "tdx_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* Keep the first four numeric values stable for existing firmware clients. */
    SERVER_NETWORK_STA_STATE_IDLE = 0,
    SERVER_NETWORK_STA_STATE_CONNECTING,
    SERVER_NETWORK_STA_STATE_GOT_IP,
    SERVER_NETWORK_STA_STATE_FAILED,
    SERVER_NETWORK_STA_STATE_WAITING_IP,
    SERVER_NETWORK_STA_STATE_RETRY_WAIT,
    SERVER_NETWORK_STA_STATE_STARTING_SERVICES,
    SERVER_NETWORK_STA_STATE_READY,
    SERVER_NETWORK_STA_STATE_AUTH_FAILED,
    SERVER_NETWORK_STA_STATE_NO_CONFIG,
    SERVER_NETWORK_STA_STATE_DISCONNECTING,
} server_network_sta_state_t;

typedef enum {
    SERVER_NETWORK_RETRY_NONE = 0,
    SERVER_NETWORK_RETRY_WIFI,
    SERVER_NETWORK_RETRY_HTTP,
    SERVER_NETWORK_RETRY_MDNS,
} server_network_retry_type_t;

typedef enum {
    SERVER_NETWORK_STA_DISCONNECT_NONE = 0,
    SERVER_NETWORK_STA_DISCONNECT_RECONFIGURE,
    SERVER_NETWORK_STA_DISCONNECT_CONNECT_RECOVERY,
    SERVER_NETWORK_STA_DISCONNECT_DHCP_RECOVERY,
    SERVER_NETWORK_STA_DISCONNECT_PROVISIONING,
    SERVER_NETWORK_STA_DISCONNECT_NO_CONFIG,
} server_network_sta_disconnect_purpose_t;

typedef struct {
    server_network_sta_state_t state;
    int last_result;
    int disconnect_reason;
    int rssi;
    server_network_retry_type_t retry_type;
    uint32_t wifi_retry_count;
    uint32_t wifi_retry_after_ms;
    uint32_t http_retry_count;
    uint32_t http_retry_after_ms;
    uint32_t mdns_retry_count;
    uint32_t mdns_retry_after_ms;
    uint32_t connection_generation;
    uint32_t credential_generation;
    uint32_t ready_stable_remaining_ms;
    server_network_sta_disconnect_purpose_t disconnect_purpose;
    bool ap_connected;
    bool has_ip;
    bool http_running;
    bool http_ready;
    bool mdns_ready;
    char ip[16];
} server_network_sta_status_t;

esp_err_t ServerNetworkSta_Init(void);
void ServerNetworkSta_RequestProvisioning(void);
uint8_t User_Network_mode_app_init(const char *base_path);
uint8_t User_Network_mode_app_init_force(const char *base_path);
uint8_t User_Network_mode_app_new_credential(const char *base_path);
int ServerNetworkSta_GetLastConnectResult(void);
esp_err_t ServerNetworkSta_GetStatus(server_network_sta_status_t *status);
const char *ServerNetworkSta_StateName(server_network_sta_state_t state);
const char *ServerNetworkSta_DisconnectPurposeName(server_network_sta_disconnect_purpose_t purpose);

#ifdef __cplusplus
}
#endif
