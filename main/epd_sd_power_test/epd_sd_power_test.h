#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t EpdSdPowerTest_Init(void);
esp_err_t EpdSdPowerTest_PrepareForSharedSpi(void);
bool EpdSdPowerTest_IsReadyForImmediateSharedSpi(void);
void EpdSdPowerTest_NetworkBegin(void);
esp_err_t EpdSdPowerTest_NetworkTryBegin(void);
void EpdSdPowerTest_NetworkEnd(void);
void EpdSdPowerTest_OnCh583BleDataReceived(void);
void EpdSdPowerTest_OnEpdTaskRequested(void);
void EpdSdPowerTest_OnEpdJobDone(void);
void EpdSdPowerTest_ImageTransferBegin(void);
void EpdSdPowerTest_ImageTransferEnd(void);
void EpdSdPowerTest_SlideshowFollowupBegin(void);
void EpdSdPowerTest_SlideshowFollowupEnd(void);

#ifdef __cplusplus
}
#endif
