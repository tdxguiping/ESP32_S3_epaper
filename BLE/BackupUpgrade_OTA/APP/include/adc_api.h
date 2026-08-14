#ifndef ADC_API_H
#define ADC_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CONFIG.h"

// void define
extern unsigned char GetCurrentAdcValue();
extern unsigned char GetCurrentChargeStatus();
extern int GetNeedTimeToFull();
extern void AdcInit();
extern void AdcTask();
extern void AdcUpdateLedStatus();
extern void AdcRefreshBatteryForWifiDone();

#ifdef __cplusplus
}
#endif

#endif

