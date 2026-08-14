#include <stdlib.h>

#include "base64.h"
#include "app_cfg.h"
#include "commoninfo.h"
#include "adc_api.h"

// 宏定义：消除魔法数字，提升可维护性
#define ADC_INIT_SAMPLE_COUNT    15    // ADC初始化采样次数（用于滤波）
#define LOW_BATTERY_THRESHOLD    10    // 低电量阈值（%）
#define FULL_BATTERY_THRESHOLD   100   // 调整为100%作为满电阈值
#define CHARGE_TIME_BASE         150   // 充电时间基数（秒）
#define NORMAL_CHARGE_DURATION   50    // 非满电阶段充电时长系数
#define HIGH_LEVEL_CHARGE_RATE   120   // 高电量（≥70%）每1%充电时间（秒，2分钟）
#define FULL_CHARGE_MIN_CONFIRM_COUNT 5

// 全局变量：命名更清晰，明确用途
UINT8  gIsCharging = Is_No;           // 当前是否正在充电
UINT8  gIsChargeFull = Is_No;         // 当前是否充电满
UINT8  gCurrentBattery = 0;           // 当前电池电量（%）
UINT32 gChargeCheckCounter = 0;       // 充电满检测计数器
UINT8  gStartChargeBattery = 0;       // 开始充电时的初始电量（新增）

/**
 * @brief 获取当前ADC采样值（电池电量百分比）
 * @return 电池电量百分比（0-100）
 */
unsigned char GetCurrentAdcValue()
{
	// 初始化ADC采样（多次采样求平均，用于滤波）
    UINT32 adcSum = 0;
    for (int i = 0; i < ADC_INIT_SAMPLE_COUNT; i++) {
        adcSum += ADC();
    }
    unsigned char adcvalue = adcSum / ADC_INIT_SAMPLE_COUNT;  // 平均值滤波

	return adcvalue;
}

/**
 * @brief 获取当前充电状态
 * @return Is_Yes-正在充电；Is_No-未充电
 */
unsigned char GetCurrentChargeStatus()
{
    UINT32 led_status = GPIOB_ReadPort();
    // 通过充电LED的GPIO状态判断充电状态
    return ((led_status & CHARGE_LED) != 0) ? Is_Yes : Is_No;
}

/**
 * @brief 计算充满电所需剩余时间（秒）
 * @note 新增逻辑：充电前电量≥70%时，每充1%需要2分钟（120秒）
 * @return 剩余充电时间（秒）
 */
int GetNeedTimeToFull()
{
    int needTime;

    // Charge level >= 70%: estimate by 2 minutes per remaining 1%.
    if (gStartChargeBattery >= 70 && gStartChargeBattery <= 100) {
        needTime = (100 - gStartChargeBattery) * HIGH_LEVEL_CHARGE_RATE;
    }
    else if (gCurrentBattery >= 90 && gCurrentBattery <= 100) {
        needTime = CHARGE_TIME_BASE * (101 - gCurrentBattery);
    }
    else {
        needTime = CHARGE_TIME_BASE * NORMAL_CHARGE_DURATION;
    }

    if (needTime < FULL_CHARGE_MIN_CONFIRM_COUNT) {
        needTime = FULL_CHARGE_MIN_CONFIRM_COUNT;
    }

    return needTime;
}

/**
 * @brief 设置LED状态（根据充电状态和电量）
 * @param isCharging 是否充电中
 * @param isFull 是否已充满
 * @param batteryLevel 当前电量
 */
static void RestoreLedPowerOffIo(void)
{
    GPIOB_SetBits(LED5_Power | LED6_Power);
    GPIOB_ModeCfg(LED5_Power | LED6_Power, GPIO_ModeOut_PP_5mA);
}

static void SetLedStatus(UINT8 isCharging, UINT8 isFull, UINT8 batteryLevel)
{
    // 先关闭所有LED，避免状态冲突
    RestoreLedPowerOffIo();

    if (isCharging) {
        if (isFull) {
            GREEN_Power_on;  // 充电满：绿灯亮
        } else {
            RED_Power_on;    // 充电中（未充满）：红灯亮
        }
    } else {
        // 未充电时，低电量亮红灯
        if (batteryLevel <= LOW_BATTERY_THRESHOLD) {
            RED_Power_on;
        }
    }
}

/**
 * @brief ADC模块初始化
 * @note 初始化ADC状态、读取历史电量、设置初始LED状态
 */
void AdcInit()
{
    unsigned char adcValue[ADC_Len] = {0x01};
    UINT8 currentChargeStatus = GetCurrentChargeStatus();

    global_DEVICE_STATUS.fIsCharg = currentChargeStatus;
    gIsCharging = currentChargeStatus;
    gStartChargeBattery = 0;  // 初始化充电起始电量

    global_DEVICE_STATUS.fAdcValue = GetCurrentAdcValue();  // 平均值滤波

    P_scanAdcRspData(0);

    // 读取EEPROM中保存的电量信息
    Get_EEPROM_Flag(adcValue, ADC_Position, ADC_Len);
    gCurrentBattery = adcValue[0];
    // Init full state from current ADC so the LED matches the APP battery level at boot.
    gIsChargeFull = (global_DEVICE_STATUS.fAdcValue >= FULL_BATTERY_THRESHOLD) ? Is_Yes : Is_No;

    // 设置初始LED状态
    SetLedStatus(gIsCharging, gIsChargeFull, global_DEVICE_STATUS.fAdcValue);

    // 若未充电，保存当前ADC值到EEPROM
    if (currentChargeStatus == Is_No) {
        Print_I3("adc init");
        adcValue[0] = global_DEVICE_STATUS.fAdcValue;
        Save_EEPROM_Flag(adcValue, ADC_Position, ADC_Len);
    }
}

/**
 * @brief ADC任务处理函数（周期性执行）
 * @note 处理充电状态检测、电量更新、LED状态控制等，新增高电量充电计时逻辑
 */
void AdcTask()
{
    // 缓存当前充电状态和ADC值，减少重复调用
    UINT8 currentChargeStatus = GetCurrentChargeStatus();
	UINT8 currentAdcValue;

    if (currentChargeStatus == Is_Yes) {
        // 充电中逻辑
        currentAdcValue = ADC();
        global_DEVICE_STATUS.fIsCharg = Is_Yes;

        // 刚插入充电时，记录初始电量（新增逻辑）
        if (gIsCharging == Is_No) {
            gStartChargeBattery = gCurrentBattery;  // 保存充电前的电量
            Print_I3("chg st:%d%%", gStartChargeBattery);
            gChargeCheckCounter = 0;
            if (currentAdcValue < FULL_BATTERY_THRESHOLD) {
                gIsChargeFull = Is_No;
            }
        }

        // 检查是否充满
        if (gIsChargeFull != Is_Yes) {
            global_DEVICE_STATUS.fAdcValue = currentAdcValue;

            // 当电量达到满电阈值时，启动计数器检测
            if (currentAdcValue >= FULL_BATTERY_THRESHOLD) {
                gChargeCheckCounter++;
				//Print_I3("Battery fully charged! Cuerrent time: %d seconds", gChargeCheckCounter);
                // 计数器达到所需时间，判定为充满
                if (gChargeCheckCounter >= GetNeedTimeToFull()) {
                    Print_I3("chg full:%d", gChargeCheckCounter);
                    gIsChargeFull = Is_Yes;
                    gChargeCheckCounter = 0;  // 重置计数器
                }
            } else {
                gChargeCheckCounter = 0;
                gIsChargeFull = Is_No;
            }
        }

        // 充电状态变化时，更新缓存并通知
        if (gIsCharging != global_DEVICE_STATUS.fIsCharg) {
            unsigned char adcv[ADC_Len];
            Get_EEPROM_Flag(adcv, ADC_Position, ADC_Len);
            gCurrentBattery = adcv[0];
            Print_I3("chg bat:%d", gCurrentBattery);
            gIsCharging = global_DEVICE_STATUS.fIsCharg;
            P_scanIsChargeRspData();
        }

        // 更新LED状态
        SetLedStatus(Is_Yes, gIsChargeFull, currentAdcValue);

    } else {
        // 未充电逻辑
        if (global_DEVICE_STATUS.fIsCharg == Is_Yes) {
			currentAdcValue = GetCurrentAdcValue();
			Print_I3("1111 adc:%d", currentAdcValue);
            // 从充电状态切换到未充电
            global_DEVICE_STATUS.fIsCharg = Is_No;
            global_DEVICE_STATUS.fAdcValue = currentAdcValue;
            gIsCharging = Is_No;
            gStartChargeBattery = 0;  // 重置充电起始电量（新增）

            // 通知状态变化
            P_scanAdcRspData(0);
            P_scanIsChargeRspData();
            Print_I3("adc:%d", currentAdcValue);

            // 更新满电状态（未充电时若电量低于阈值，判定为未充满）
            if (currentAdcValue < FULL_BATTERY_THRESHOLD) {
                gIsChargeFull = Is_No;
            }

            // 保存当前电量到EEPROM
            unsigned char adcValue[ADC_Len] = {0x01};
            adcValue[0] = currentAdcValue;
            Save_EEPROM_Flag(adcValue, ADC_Position, ADC_Len);
			// 更新LED状态
        	SetLedStatus(Is_No, gIsChargeFull, currentAdcValue);
        }
    }
}

