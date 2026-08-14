#include <stdlib.h>

#include "base64.h"
#include "app_cfg.h"
#include "commoninfo.h"
#include "adc_api.h"
#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
#include "uart1_wifi_passthrough.h"
#endif

#define ADC_NORMAL_SAMPLE_COUNT                 8
#define ADC_CHARGING_SAMPLE_COUNT               3
#define LOW_BATTERY_THRESHOLD                  10
#define FULL_DISPLAY_PERCENT                  100
#define FULL_TIMER_START_PERCENT               70
#define FULL_TIMER_RESET_PERCENT               91
#define CHARGE_DISPLAY_BELOW_70_SECONDS_PER_PERCENT  120UL
#define CHARGE_DISPLAY_70_80_SECONDS_PER_PERCENT     210UL
#define CHARGE_DISPLAY_80_90_SECONDS_PER_PERCENT     360UL
#define CHARGE_DISPLAY_90_100_SECONDS_PER_PERCENT    720UL
#define CHARGE_DISPLAY_MAX_BEFORE_FULL         99
#define BATTERY_CHARGE_REMOVED_MAX_DROP         5
#define CHARGE_STATUS_DEBOUNCE_COUNT            2

#define BATTERY_ADC_CHECK_INTERVAL_SECONDS_TEST   5UL
#define BATTERY_ADC_CHECK_INTERVAL_SECONDS_PROD   (12UL * 60UL * 60UL)
#ifdef ENABLE_BATTERY_ADC_TEST_INTERVAL
#define BATTERY_ADC_CHECK_INTERVAL_SECONDS        BATTERY_ADC_CHECK_INTERVAL_SECONDS_TEST
#else
#define BATTERY_ADC_CHECK_INTERVAL_SECONDS        BATTERY_ADC_CHECK_INTERVAL_SECONDS_PROD
#endif

#ifndef ADC_TASK_PERIOD_SECONDS
#define ADC_TASK_PERIOD_SECONDS                1UL
#endif

UINT8  gIsCharging = Is_No;
UINT8  gIsChargeFull = Is_No;
UINT8  gCurrentBattery = 0;
UINT32 gChargeCheckCounter = 0;
UINT8  gStartChargeBattery = 0;
static UINT32 sBatteryAdcCheckCounter = 0;
static UINT32 sChargeDisplayRiseCounter = 0;
static UINT16 sLastFilteredVoltageMv = 0;
static UINT8 sLastFilteredStaticPercent = 0;
static UINT8 sLastFilteredChargeCurvePercent = 0;
static UINT8 sChargeStatusStable = Is_No;
static UINT8 sChargeStatusLastRaw = Is_No;
static UINT8 sChargeStatusDebounceCount = 0;
static UINT8 sChargeStatusDebounceInitialized = Is_No;
extern UINT16 gAdcLastVoltageMv;
extern UINT8 gAdcLastStaticCurvePercent;
extern UINT8 gAdcLastChargeCurvePercent;

static UINT8 ClampBatteryPercent(UINT8 batteryPercent)
{
    return (batteryPercent > FULL_DISPLAY_PERCENT) ? FULL_DISPLAY_PERCENT : batteryPercent;
}

static UINT32 GetChargeDisplayStepSeconds(UINT8 displayPercent)
{
    if(displayPercent < FULL_TIMER_START_PERCENT) {
        return CHARGE_DISPLAY_BELOW_70_SECONDS_PER_PERCENT;
    }

    if(displayPercent < 80) {
        return CHARGE_DISPLAY_70_80_SECONDS_PER_PERCENT;
    }

    if(displayPercent < 90) {
        return CHARGE_DISPLAY_80_90_SECONDS_PER_PERCENT;
    }

    if(displayPercent < FULL_DISPLAY_PERCENT) {
        return CHARGE_DISPLAY_90_100_SECONDS_PER_PERCENT;
    }

    return 0;
}

static UINT32 GetChargeRemainingSeconds(UINT8 displayPercent)
{
    UINT32 seconds = 0;
    UINT8 percent = ClampBatteryPercent(displayPercent);

    while(percent < FULL_DISPLAY_PERCENT) {
        UINT32 stepSeconds = GetChargeDisplayStepSeconds(percent);

        if(stepSeconds == 0) {
            break;
        }

        if(percent == displayPercent) {
            seconds += (sChargeDisplayRiseCounter < stepSeconds) ? (stepSeconds - sChargeDisplayRiseCounter) : 0;
        } else {
            seconds += stepSeconds;
        }

        percent++;
    }

    return seconds;
}

int GetNeedTimeToFull()
{
    return (int)GetChargeRemainingSeconds(gCurrentBattery);
}

static void AdcLogSample(const char *reason, UINT8 adcValue, UINT8 chargeStatus)
{
    if(reason == NULL) {
        reason = "unknown";
    }

    Print_I3("[ADC] reason=%s value=%d charge=%d current=%d full=%d check_seconds=%lu full_seconds=%lu",
             reason,
             adcValue,
             chargeStatus,
             gCurrentBattery,
             gIsChargeFull,
             (unsigned long)sBatteryAdcCheckCounter,
             (unsigned long)gChargeCheckCounter);
    Print_I3("[ADC_DETAIL] start=%d step_seconds=%lu step_elapsed=%lu remain_seconds=%lu voltage=%lu.%03luV static=%d charge_curve=%d",
             gStartChargeBattery,
             (unsigned long)GetChargeDisplayStepSeconds(adcValue),
             (unsigned long)sChargeDisplayRiseCounter,
             (unsigned long)GetNeedTimeToFull(),
             (unsigned long)(sLastFilteredVoltageMv / 1000),
             (unsigned long)(sLastFilteredVoltageMv % 1000),
             sLastFilteredStaticPercent,
             sLastFilteredChargeCurvePercent);
}

static unsigned char GetFilteredAdcValue(UINT8 sampleCount)
{
    UINT32 adcSum = 0;
    UINT32 voltageSum = 0;
    UINT32 staticPercentSum = 0;
    UINT32 chargeCurvePercentSum = 0;
    UINT8 i;
    unsigned char adcValue;
    UINT32 voltageMv;

    if(sampleCount == 0) {
        sampleCount = 1;
    }

    for(i = 0; i < sampleCount; i++) {
        adcSum += ADC();
        voltageSum += gAdcLastVoltageMv;
        staticPercentSum += gAdcLastStaticCurvePercent;
        chargeCurvePercentSum += gAdcLastChargeCurvePercent;
    }

    adcValue = (unsigned char)((adcSum + (sampleCount / 2)) / sampleCount);
    voltageMv = (voltageSum + (sampleCount / 2)) / sampleCount;
    sLastFilteredVoltageMv = (UINT16)voltageMv;
    sLastFilteredStaticPercent = (UINT8)((staticPercentSum + (sampleCount / 2)) / sampleCount);
    sLastFilteredChargeCurvePercent = (UINT8)((chargeCurvePercentSum + (sampleCount / 2)) / sampleCount);
    Print_I3("[ADC_VOLT] samples=%d voltage=%lu.%03luV percent=%d static=%d charge_curve=%d",
             sampleCount,
             (unsigned long)(voltageMv / 1000),
             (unsigned long)(voltageMv % 1000),
             adcValue,
             sLastFilteredStaticPercent,
             sLastFilteredChargeCurvePercent);

    return adcValue;
}

unsigned char GetCurrentAdcValue()
{
    return GetFilteredAdcValue(ADC_NORMAL_SAMPLE_COUNT);
}

static unsigned char GetChargingAdcValue(void)
{
    return GetFilteredAdcValue(ADC_CHARGING_SAMPLE_COUNT);
}

static UINT8 ReadChargeStatusRaw(void)
{
    UINT32 led_status = GPIOB_ReadPort();
    return ((led_status & CHARGE_LED) != 0) ? Is_Yes : Is_No;
}

static void ResetChargeStatusDebounce(UINT8 chargeStatus)
{
    sChargeStatusStable = chargeStatus;
    sChargeStatusLastRaw = chargeStatus;
    sChargeStatusDebounceCount = CHARGE_STATUS_DEBOUNCE_COUNT;
    sChargeStatusDebounceInitialized = Is_Yes;
}

unsigned char GetCurrentChargeStatus()
{
    UINT8 rawStatus = ReadChargeStatusRaw();

    if(sChargeStatusDebounceInitialized != Is_Yes) {
        ResetChargeStatusDebounce(rawStatus);
        return sChargeStatusStable;
    }

    if(rawStatus == sChargeStatusStable) {
        sChargeStatusLastRaw = rawStatus;
        sChargeStatusDebounceCount = CHARGE_STATUS_DEBOUNCE_COUNT;
        return sChargeStatusStable;
    }

    if(rawStatus != sChargeStatusLastRaw) {
        sChargeStatusLastRaw = rawStatus;
        sChargeStatusDebounceCount = 1;
    } else if(sChargeStatusDebounceCount < CHARGE_STATUS_DEBOUNCE_COUNT) {
        sChargeStatusDebounceCount++;
    }

    if(sChargeStatusDebounceCount >= CHARGE_STATUS_DEBOUNCE_COUNT) {
        sChargeStatusStable = rawStatus;
        sChargeStatusDebounceCount = CHARGE_STATUS_DEBOUNCE_COUNT;
    }

    return sChargeStatusStable;
}

static void SetLedStatus(UINT8 isCharging, UINT8 isFull, UINT8 batteryLevel)
{
    RED_Power_off;
    GREEN_Power_off;

    if (isCharging) {
        if (isFull) {
            GREEN_Power_on;
        } else {
            RED_Power_on;
        }
    } else {
        if (batteryLevel <= LOW_BATTERY_THRESHOLD) {
            RED_Power_on;
        }
    }
}

static UINT8 LimitChargeRemovedDrop(UINT8 previousValue, UINT8 currentValue)
{
    if((previousValue > currentValue) && ((previousValue - currentValue) > BATTERY_CHARGE_REMOVED_MAX_DROP)) {
        return (UINT8)(previousValue - BATTERY_CHARGE_REMOVED_MAX_DROP);
    }

    return currentValue;
}

static UINT8 UpdateChargingDisplayPercent(UINT8 chargeCurveTargetPercent)
{
    UINT8 displayPercent = ClampBatteryPercent(gCurrentBattery);

    if((gIsChargeFull != Is_Yes) && (displayPercent >= FULL_DISPLAY_PERCENT)) {
        displayPercent = CHARGE_DISPLAY_MAX_BEFORE_FULL;
    }

    if(displayPercent < FULL_TIMER_START_PERCENT) {
        if(chargeCurveTargetPercent <= displayPercent) {
            return displayPercent;
        }

        sChargeDisplayRiseCounter += ADC_TASK_PERIOD_SECONDS;

        while((sChargeDisplayRiseCounter >= CHARGE_DISPLAY_BELOW_70_SECONDS_PER_PERCENT) &&
              (displayPercent < chargeCurveTargetPercent) &&
              (displayPercent < FULL_TIMER_START_PERCENT)) {
            displayPercent++;
            sChargeDisplayRiseCounter -= CHARGE_DISPLAY_BELOW_70_SECONDS_PER_PERCENT;
        }

        return displayPercent;
    }

    if(displayPercent < FULL_DISPLAY_PERCENT) {
        sChargeDisplayRiseCounter += ADC_TASK_PERIOD_SECONDS;

        while(displayPercent < FULL_DISPLAY_PERCENT) {
            UINT32 stepSeconds = GetChargeDisplayStepSeconds(displayPercent);

            if((stepSeconds == 0) || (sChargeDisplayRiseCounter < stepSeconds)) {
                break;
            }

            displayPercent++;
            sChargeDisplayRiseCounter -= stepSeconds;
        }
    }

    return displayPercent;
}

void AdcUpdateLedStatus(void)
{
    SetLedStatus(gIsCharging, gIsChargeFull, global_DEVICE_STATUS.fAdcValue);
}

void AdcInit()
{
    unsigned char adcValue[ADC_Len] = {0x01};
    UINT8 currentChargeStatus;

    global_DEVICE_STATUS.fIsCharg = Is_No;
    gIsCharging = Is_No;
    gStartChargeBattery = 0;
    gChargeCheckCounter = 0;
    sBatteryAdcCheckCounter = 0;
    sChargeDisplayRiseCounter = 0;

    global_DEVICE_STATUS.fAdcValue = GetCurrentAdcValue();
    currentChargeStatus = ReadChargeStatusRaw();
    ResetChargeStatusDebounce(currentChargeStatus);
#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
    WifiPassthrough_OnUsbPowerChanged(currentChargeStatus);
#endif

    Get_EEPROM_Flag(adcValue, ADC_Position, ADC_Len);
    gCurrentBattery = adcValue[0];
    if(gCurrentBattery > FULL_DISPLAY_PERCENT) {
        gCurrentBattery = FULL_DISPLAY_PERCENT;
    }

    if (currentChargeStatus == Is_Yes) {
        global_DEVICE_STATUS.fAdcValue = gCurrentBattery;
    } else {
        gCurrentBattery = global_DEVICE_STATUS.fAdcValue;
    }

    gIsChargeFull = (gCurrentBattery >= FULL_DISPLAY_PERCENT) ? Is_Yes : Is_No;
    P_scanAdcRspData(0);
    AdcLogSample("init", global_DEVICE_STATUS.fAdcValue, currentChargeStatus);
    SetLedStatus(currentChargeStatus, gIsChargeFull, global_DEVICE_STATUS.fAdcValue);

    if (currentChargeStatus == Is_No) {
        Print_I3("adc charg init 000000000000000000000000");
        adcValue[0] = global_DEVICE_STATUS.fAdcValue;
        Save_EEPROM_Flag(adcValue, ADC_Position, ADC_Len);
    }
}

void AdcRefreshBatteryForWifiDone(void)
{
    unsigned char adcValue[ADC_Len] = {0x01};
    UINT8 currentChargeStatus = GetCurrentChargeStatus();
    UINT8 measuredAdcValue;
    UINT8 displayAdcValue;

    if(currentChargeStatus == Is_Yes) {
        measuredAdcValue = GetChargingAdcValue();
        (void)measuredAdcValue;
        displayAdcValue = (gIsChargeFull == Is_Yes) ? FULL_DISPLAY_PERCENT : ClampBatteryPercent(gCurrentBattery);
    } else {
        displayAdcValue = GetCurrentAdcValue();
    }

    global_DEVICE_STATUS.fIsCharg = currentChargeStatus;
    global_DEVICE_STATUS.fAdcValue = displayAdcValue;
    gCurrentBattery = displayAdcValue;
    sBatteryAdcCheckCounter = 0;

    if((currentChargeStatus == Is_No) && (displayAdcValue < FULL_TIMER_RESET_PERCENT)) {
        gIsChargeFull = Is_No;
        gChargeCheckCounter = 0;
        sChargeDisplayRiseCounter = 0;
    }

    P_scanAdcRspData(0);
    P_scanIsChargeRspData();

    if(currentChargeStatus == Is_No) {
        adcValue[0] = displayAdcValue;
        Save_EEPROM_Flag(adcValue, ADC_Position, ADC_Len);
    }

    SetLedStatus(currentChargeStatus, gIsChargeFull, displayAdcValue);
    AdcLogSample("wifi_done", displayAdcValue, currentChargeStatus);
}

void AdcTask()
{
    UINT8 currentChargeStatus = GetCurrentChargeStatus();
    UINT8 currentAdcValue;

    if (currentChargeStatus == Is_Yes) {
        UINT8 measuredChargeCurvePercent;
        UINT8 previousDisplayPercent = global_DEVICE_STATUS.fAdcValue;
        UINT8 displayBatteryPercent;

        (void)GetChargingAdcValue();
        measuredChargeCurvePercent = sLastFilteredChargeCurvePercent;

        global_DEVICE_STATUS.fIsCharg = Is_Yes;

        if (gIsCharging == Is_No) {
            gCurrentBattery = ClampBatteryPercent(gCurrentBattery);
            global_DEVICE_STATUS.fAdcValue = gCurrentBattery;
            previousDisplayPercent = gCurrentBattery;
            gStartChargeBattery = gCurrentBattery;
            gChargeCheckCounter = 0;
            sChargeDisplayRiseCounter = 0;
#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
            WifiPassthrough_OnUsbPowerChanged(Is_Yes);
#endif
            Print_I3("Start charging, initial battery: %d%%", gStartChargeBattery);
        }

        if (gIsChargeFull != Is_Yes) {
            displayBatteryPercent = UpdateChargingDisplayPercent(measuredChargeCurvePercent);
            global_DEVICE_STATUS.fAdcValue = displayBatteryPercent;
            gCurrentBattery = displayBatteryPercent;

            if (displayBatteryPercent >= FULL_TIMER_START_PERCENT) {
                gChargeCheckCounter += ADC_TASK_PERIOD_SECONDS;
                if (displayBatteryPercent >= FULL_DISPLAY_PERCENT) {
                    Print_I3("Battery fully charged! Total time: %lu seconds", (unsigned long)gChargeCheckCounter);
                    gIsChargeFull = Is_Yes;
                    gChargeCheckCounter = 0;
                    global_DEVICE_STATUS.fAdcValue = FULL_DISPLAY_PERCENT;
                    gCurrentBattery = FULL_DISPLAY_PERCENT;
                    sChargeDisplayRiseCounter = 0;
                    P_scanAdcRspData(0);
                }
            } else {
                gChargeCheckCounter = 0;
            }
        } else {
            displayBatteryPercent = FULL_DISPLAY_PERCENT;
            global_DEVICE_STATUS.fAdcValue = FULL_DISPLAY_PERCENT;
            gCurrentBattery = FULL_DISPLAY_PERCENT;
        }

        currentAdcValue = global_DEVICE_STATUS.fAdcValue;

        if((gIsChargeFull != Is_Yes) && (currentAdcValue != previousDisplayPercent)) {
            P_scanAdcRspData(0);
        }

        AdcLogSample("charging", currentAdcValue, currentChargeStatus);

        if (gIsCharging != global_DEVICE_STATUS.fIsCharg) {
            Print_I3("adc charg gCurrentBattery:%d", gCurrentBattery);
            gIsCharging = global_DEVICE_STATUS.fIsCharg;
            P_scanIsChargeRspData();
        }

        SetLedStatus(Is_Yes, gIsChargeFull, global_DEVICE_STATUS.fAdcValue);
    } else {
        if (global_DEVICE_STATUS.fIsCharg == Is_Yes) {
            UINT8 previousAdcValue = global_DEVICE_STATUS.fAdcValue;
            currentAdcValue = GetCurrentAdcValue();
            currentAdcValue = LimitChargeRemovedDrop(previousAdcValue, currentAdcValue);
            AdcLogSample("charge_removed", currentAdcValue, currentChargeStatus);
#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
            WifiPassthrough_OnUsbPowerChanged(Is_No);
#endif
            global_DEVICE_STATUS.fIsCharg = Is_No;
            global_DEVICE_STATUS.fAdcValue = currentAdcValue;
            gCurrentBattery = currentAdcValue;
            gIsCharging = Is_No;
            gStartChargeBattery = 0;
            gChargeCheckCounter = 0;
            sChargeDisplayRiseCounter = 0;

            P_scanAdcRspData(0);
            P_scanIsChargeRspData();

            if (currentAdcValue < FULL_TIMER_RESET_PERCENT) {
                gIsChargeFull = Is_No;
                gChargeCheckCounter = 0;
                sChargeDisplayRiseCounter = 0;
            }

            unsigned char adcValue[ADC_Len] = {0x01};
            adcValue[0] = currentAdcValue;
            Save_EEPROM_Flag(adcValue, ADC_Position, ADC_Len);
            sBatteryAdcCheckCounter = 0;
            SetLedStatus(Is_No, gIsChargeFull, currentAdcValue);
        } else {
            sBatteryAdcCheckCounter += ADC_TASK_PERIOD_SECONDS;
            if(sBatteryAdcCheckCounter >= BATTERY_ADC_CHECK_INTERVAL_SECONDS) {
                sBatteryAdcCheckCounter = 0;
                currentAdcValue = GetCurrentAdcValue();
                global_DEVICE_STATUS.fAdcValue = currentAdcValue;
                gCurrentBattery = currentAdcValue;
                P_scanAdcRspData(0);
                AdcLogSample("periodic", currentAdcValue, currentChargeStatus);
                if (currentAdcValue < FULL_TIMER_RESET_PERCENT) {
                    gIsChargeFull = Is_No;
                    gChargeCheckCounter = 0;
                    sChargeDisplayRiseCounter = 0;
                }
                SetLedStatus(Is_No, gIsChargeFull, currentAdcValue);
            }
        }
    }
}
