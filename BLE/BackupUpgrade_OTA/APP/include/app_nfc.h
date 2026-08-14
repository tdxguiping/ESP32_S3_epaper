#ifndef APP_NFC_H
#define APP_NFC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CONFIG.h"

#define NFC_APP_JSON_MAX_LEN          220U
#define NFC_APP_STATUS_OK             0
#define NFC_APP_STATUS_BAD_ARG        1
#define NFC_APP_STATUS_BAD_LEN        2
#define NFC_APP_STATUS_BUSY           3

void NfcApp_Init(void);
uint8_t NfcApp_StartPicc(void);
void NfcApp_StopPicc(void);
uint8_t NfcApp_StartManualWindow(void);
void NfcApp_StopManualWindow(void);
void NfcApp_FastPoll(void);
void NfcApp_EnterLowPowerIo(void);
uint8_t NfcApp_SetJsonPayload(uint8_t *data, uint16_t len);
void NfcApp_ClearJsonPayload(void);
uint8_t NfcApp_IsSessionBootRequest(void);
void NfcApp_RequestSessionBoot(void);
void NfcApp_RunOnlySession(void);
void NfcApp_HandleBleBootPendingActions(void);
void NfcApp_GetStatus(char *out, uint16_t out_size);
uint16_t NfcApp_GetPayloadLen(void);
char *NfcApp_GetLastAuthText(void);
uint8_t NfcApp_IsPiccRunning(void);
uint8_t NfcApp_IsWindowBusy(void);
uint8_t NfcApp_IsPresent(void);

#ifdef __cplusplus
}
#endif

#endif
