#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Read the bootloader-owned OTA state once near the start of app_main().
esp_err_t NetworkOtaBoot_Init(void);

// Report whether this runtime started as the first boot of an unconfirmed OTA image.
bool NetworkOtaBoot_WasPendingVerify(void);

// Report whether the current OTA image still requires confirmation.
bool NetworkOtaBoot_IsPendingVerify(void);

// Apply the power-off hold after the Wi-Fi work-time module has initialized.
void NetworkOtaBoot_EnablePendingProtection(void);

// Confirm the current image and release pending-verification protection on success.
esp_err_t NetworkOtaBoot_ConfirmCurrentImage(void);

// Confirm a pending image after local critical initialization, without waiting for Wi-Fi.
esp_err_t NetworkOtaBoot_ConfirmAfterLocalInit(void);

#ifdef __cplusplus
}
#endif
