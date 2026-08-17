#pragma once

#include <stdbool.h>

#include "epd_display_app.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool active;
    bool power_guard_active;
    bool spi_locked;
    epd_display_reservation_t epd_reservation;
} server_network_sta_upload_reservation_t;

bool ServerNetworkStaUploadGate_IsBusy(void);
bool ServerNetworkStaUploadGate_IsReserved(void);
esp_err_t ServerNetworkStaUploadGate_TryReserve(
    server_network_sta_upload_reservation_t *reservation,
    const char **reason);
void ServerNetworkStaUploadGate_Release(
    server_network_sta_upload_reservation_t *reservation);

#ifdef __cplusplus
}
#endif
