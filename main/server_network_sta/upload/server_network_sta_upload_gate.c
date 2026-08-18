#include "server_network_sta_upload_gate.h"

#include <string.h>

#include "epd_sd_power_test.h"
#include "factory_reset.h"
#include "image_business_worker.h"
#include "network_ota_upload.h"
#include "server_network_sta_wifi_work_time.h"
#include "tdx_shared_spi.h"

static bool s_upload_reserved;

static const char *owner_busy_reason(void)
{
    if (ImageBusinessWorker_HasPendingCommand()) {
        return "image_pending";
    }

    image_business_owner_t owner = ImageBusinessWorker_GetCurrentOwner();
    switch (owner) {
    case IMAGE_BUSINESS_OWNER_NONE:
        return NULL;
    case IMAGE_BUSINESS_OWNER_DAILY:
        // DAILY clears this flag before its interruptible wait for the next slot.
        return ServerNetworkStaWifiWorkTime_IsDailyImageInProgress()
                   ? "daily_busy"
                   : NULL;
    case IMAGE_BUSINESS_OWNER_SLIDESHOW:
        // EPD/SPI state distinguishes a slide transaction from its long interval wait.
        return NULL;
    default:
        // All remaining owners are bounded one-shot transactions.
        return "image_worker_busy";
    }
}

static const char *busy_reason(bool include_reservation)
{
    if (include_reservation &&
        __atomic_load_n(&s_upload_reserved, __ATOMIC_ACQUIRE)) {
        return "upload_busy";
    }
    if (FactoryReset_IsBusy()) {
        return "factory_reset";
    }
    if (NetworkOtaUpload_IsRestartPending() ||
        ServerNetworkStaWifiWorkTime_IsOtaBusy()) {
        return "ota_busy";
    }
    const char *owner_reason = owner_busy_reason();
    if (owner_reason != NULL) {
        return owner_reason;
    }
    if (ServerNetworkStaEpdDisplay_IsBusy()) {
        return "epd_busy";
    }
    if (ServerNetworkStaWifiWorkTime_IsImageSaveInProgress()) {
        return "image_save_busy";
    }
    if (TdxSharedSpi_IsBusy()) {
        return "shared_spi_busy";
    }
    // Upload may start only while the shared EPD/SD rail is immediately usable.
    // POWER_OFF, PREPARING and RESTORING all report BUSY; a later ping returns
    // IDLE naturally after the existing power-test state machine restores the rail.
    if (!EpdSdPowerTest_IsReadyForImmediateSharedSpi()) {
        return "sd_power_busy";
    }
    return NULL;
}

bool ServerNetworkStaUploadGate_IsBusy(void)
{
    return busy_reason(true) != NULL;
}

bool ServerNetworkStaUploadGate_IsReserved(void)
{
    return __atomic_load_n(&s_upload_reserved, __ATOMIC_ACQUIRE);
}

static esp_err_t reservation_fail(
    server_network_sta_upload_reservation_t *reservation,
    const char **reason,
    const char *failure_reason)
{
    if (reason != NULL) {
        *reason = failure_reason;
    }
    ServerNetworkStaUploadGate_Release(reservation);
    return ESP_ERR_INVALID_STATE;
}

esp_err_t ServerNetworkStaUploadGate_TryReserve(
    server_network_sta_upload_reservation_t *reservation,
    const char **reason)
{
    if (reservation == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(reservation, 0, sizeof(*reservation));
    if (reason != NULL) {
        *reason = "upload_busy";
    }

    const char *initial_reason = busy_reason(true);
    if (initial_reason != NULL) {
        if (reason != NULL) {
            *reason = initial_reason;
        }
        return ESP_ERR_INVALID_STATE;
    }

    bool expected = false;
    if (!__atomic_compare_exchange_n(&s_upload_reserved,
                                     &expected,
                                     true,
                                     false,
                                     __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE)) {
        return ESP_ERR_INVALID_STATE;
    }
    reservation->active = true;

    EpdSdPowerTest_ImageTransferBegin();
    reservation->power_guard_active = true;
    if (!EpdSdPowerTest_IsReadyForImmediateSharedSpi()) {
        return reservation_fail(reservation, reason, "sd_power_busy");
    }

    esp_err_t epd_ret = ServerNetworkStaEpdDisplay_TryReserveIdle(
        &reservation->epd_reservation);
    if (epd_ret != ESP_OK) {
        return reservation_fail(reservation, reason, "epd_busy");
    }

    esp_err_t spi_ret = TdxSharedSpi_Lock(0);
    if (spi_ret != ESP_OK) {
        return reservation_fail(reservation, reason, "shared_spi_busy");
    }
    reservation->spi_locked = true;

    // Close races with a terminal request or a one-shot image job accepted
    // while the two hardware reservations were being assembled.
    const char *final_reason = owner_busy_reason();
    if (FactoryReset_IsBusy()) {
        final_reason = "factory_reset";
    } else if (NetworkOtaUpload_IsRestartPending() ||
               ServerNetworkStaWifiWorkTime_IsOtaBusy()) {
        final_reason = "ota_busy";
    }
    if (final_reason != NULL) {
        return reservation_fail(reservation, reason, final_reason);
    }

    if (reason != NULL) {
        *reason = NULL;
    }
    return ESP_OK;
}

void ServerNetworkStaUploadGate_Release(
    server_network_sta_upload_reservation_t *reservation)
{
    if (reservation == NULL || !reservation->active) {
        return;
    }
    if (reservation->spi_locked) {
        TdxSharedSpi_Unlock();
        reservation->spi_locked = false;
    }
    ServerNetworkStaEpdDisplay_ReleaseReservation(
        &reservation->epd_reservation);
    if (reservation->power_guard_active) {
        EpdSdPowerTest_ImageTransferEnd();
        reservation->power_guard_active = false;
    }
    reservation->active = false;
    __atomic_store_n(&s_upload_reserved, false, __ATOMIC_RELEASE);
}
