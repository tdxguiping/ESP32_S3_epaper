#ifndef UART1_WIFI_PASSTHROUGH_H
#define UART1_WIFI_PASSTHROUGH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CONFIG.h"

#define WIFI_FORCE_ALWAYS_ON           0

void WifiPassthrough_Init(void);
void WifiPassthrough_RequestBootWake(void);
void WifiPassthrough_OnBleConnected(uint16_t conn_handle);
void WifiPassthrough_OnBleDisconnected(void);
void WifiPassthrough_BleWrite(uint8_t *data, uint16_t len);
void WifiPassthrough_ProcessRx(void);
void WifiPassthrough_FastPoll(void);
void WifiPassthrough_OnWifiDone(void);
void WifiPassthrough_EnterOtaGuard(void);
void WifiPassthrough_HandleReservedIo(void);
void WifiPassthrough_EnterLowPowerIo(void);
void WifiPassthrough_OnUsbPowerChanged(uint8_t present);
void WifiPassthrough_OnWakeKeyPb1Pressed(void);
void WifiPassthrough_OnWakeKeyPb2Pressed(void);
void WifiPassthrough_OnWakeKeyPressed(void);
void WifiPassthrough_OnNfcAuthorizedWake(void);
uint8_t WifiPassthrough_IsWifiAwake(void);
uint8_t WifiPassthrough_IsUsbPowerPresent(void);
uint8_t WifiPassthrough_IsWakePending(void);
uint8_t WifiPassthrough_IsTimedWakeArmed(void);

#ifdef __cplusplus
}
#endif

#endif
