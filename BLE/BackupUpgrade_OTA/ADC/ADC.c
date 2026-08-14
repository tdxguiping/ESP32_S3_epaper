
/********************************** (C) COPYRIGHT *******************************
 * File Name          : Main.c
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2021/03/09
 * Description        : adc采样示例，包括温度检测、单通道检测、差分通道检测、TouchKey检测、中断方式采样。
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *******************************************************************************/

#include "CH58x_common.h"
#include "app_cfg.h"
#include "commoninfo.h"
#include "util.h"

//PA8：通用双向数字 I/0 引脚。
//RXD1：UART1 串行数据输入。
//AIN12：ADC 模拟信号输入通道 12。

#define  ADC_VBAT_DETECT_CTRL_PIN      GPIO_Pin_3
#define  ADC_VBAT_DETECT_STABLE_MS     5

#define  ADC_RAW_SAMPLE_COUNT          32
#define  ADC_TRIM_DROP_COUNT            2
#define  ADC_TRIMMED_SAMPLE_COUNT      (ADC_RAW_SAMPLE_COUNT - (ADC_TRIM_DROP_COUNT * 2))
#define  ADC_VOLTAGE_CALIB_OFFSET_MV  (-100)
#define  Temp_number                   ADC_RAW_SAMPLE_COUNT

uint32_t average_adc_value;
UINT16 gAdcLastVoltageMv = 0;
UINT8 gAdcLastStaticCurvePercent = 0;
UINT8 gAdcLastChargeCurvePercent = 0;

/* index 0 = 100%, index 100 = 0%, unit = mV. */
static const uint16_t s_charge_battery_curve_mv[101] = {
    4400, 4365, 4330, 4295, 4260, 4225, 4190, 4180, 4180, 4170,
    4170, 4170, 4160, 4160, 4160, 4150, 4150, 4150, 4150, 4140,
    4140, 4140, 4130, 4120, 4120, 4110, 4100, 4100, 4100, 4090,
    4080, 4080, 4080, 4080, 4070, 4070, 4070, 4070, 4070, 4070,
    4060, 4050, 4040, 4030, 4020, 4000, 3990, 3980, 3970, 3950,
    3940, 3930, 3920, 3920, 3910, 3900, 3890, 3890, 3880, 3870,
    3870, 3860, 3860, 3850, 3850, 3840, 3830, 3830, 3820, 3810,
    3810, 3810, 3800, 3790, 3780, 3770, 3770, 3760, 3750, 3740,
    3730, 3720, 3720, 3710, 3700, 3690, 3680, 3670, 3670, 3660,
    3660, 3650, 3640, 3630, 3600, 3560, 3520, 3470, 3390, 3330,
    2720
};

static const uint16_t s_discharge_battery_curve_mv[101] = {
    4400, 4362, 4323, 4285, 4247, 4208, 4170, 4160, 4150, 4140,
    4140, 4130, 4120, 4110, 4110, 4100, 4100, 4090, 4090, 4080,
    4080, 4070, 4060, 4050, 4030, 4020, 4010, 3990, 3980, 3970,
    3960, 3950, 3940, 3930, 3920, 3910, 3900, 3890, 3890, 3870,
    3860, 3850, 3840, 3820, 3810, 3790, 3780, 3770, 3760, 3750,
    3740, 3730, 3720, 3710, 3700, 3700, 3690, 3680, 3680, 3670,
    3660, 3660, 3650, 3650, 3640, 3640, 3640, 3630, 3630, 3620,
    3610, 3610, 3600, 3590, 3590, 3580, 3570, 3560, 3550, 3540,
    3530, 3520, 3510, 3500, 3490, 3480, 3480, 3470, 3460, 3450,
    3440, 3430, 3420, 3410, 3380, 3340, 3290, 3240, 3160, 3090,
    2680
};
static void AdcVbatDetectPowerOn(void)
{
    GPIOB_ModeCfg(ADC_VBAT_DETECT_CTRL_PIN, GPIO_ModeOut_PP_5mA);
    GPIOB_SetBits(ADC_VBAT_DETECT_CTRL_PIN);
    DelayMs(ADC_VBAT_DETECT_STABLE_MS);
}

static void AdcVbatDetectPowerOff(void)
{
    GPIOB_ResetBits(ADC_VBAT_DETECT_CTRL_PIN);
}


static uint16_t AdcReadTrimmedAverage(signed short roughCalibValue)
{
    UINT8 i;
    uint32_t sum = 0;
    uint16_t min1 = 0xffff;
    uint16_t min2 = 0xffff;
    uint16_t max1 = 0;
    uint16_t max2 = 0;

    for(i = 0; i < ADC_RAW_SAMPLE_COUNT; i++)
    {
        int32_t sample = (int32_t)ADC_ExcutSingleConver() + roughCalibValue;
        uint16_t value;

        if(sample < 0)
        {
            sample = 0;
        }

        value = (sample > 0xffff) ? 0xffff : (uint16_t)sample;
        sum += value;

        if(value <= min1)
        {
            min2 = min1;
            min1 = value;
        }
        else if(value < min2)
        {
            min2 = value;
        }

        if(value >= max1)
        {
            max2 = max1;
            max1 = value;
        }
        else if(value > max2)
        {
            max2 = value;
        }
    }

    sum -= min1;
    sum -= min2;
    sum -= max1;
    sum -= max2;

    return (uint16_t)((sum + (ADC_TRIMMED_SAMPLE_COUNT / 2)) / ADC_TRIMMED_SAMPLE_COUNT);
}

static uint16_t AdcApplyVoltageCalibration(uint16_t voltageMv)
{
    int32_t calibratedMv = (int32_t)voltageMv + ADC_VOLTAGE_CALIB_OFFSET_MV;

    if(calibratedMv < 0)
    {
        calibratedMv = 0;
    }
    else if(calibratedMv > 0xffff)
    {
        calibratedMv = 0xffff;
    }

    return (uint16_t)calibratedMv;
}
static UINT8 BatteryVoltageToPercent(uint16_t voltageMv, const uint16_t *curveMv)
{
    UINT8 index;

    if(voltageMv >= curveMv[0])
    {
        return 100;
    }

    if(voltageMv <= curveMv[100])
    {
        return 0;
    }

    for(index = 0; index < 100; index++)
    {
        uint16_t highMv = curveMv[index];
        uint16_t lowMv = curveMv[index + 1];

        if(highMv == lowMv)
        {
            if(voltageMv == highMv)
            {
                return (UINT8)(100 - index);
            }
            continue;
        }

        if((voltageMv <= highMv) && (voltageMv >= lowMv))
        {
            UINT8 lowPercent = (UINT8)(99 - index);
            uint16_t spanMv = (uint16_t)(highMv - lowMv);
            uint16_t offsetMv = (uint16_t)(voltageMv - lowMv);

            return (UINT8)(lowPercent + (((uint32_t)offsetMv * 2U >= spanMv) ? 1U : 0U));
        }
    }

    return 0;
}

UINT8  ADC(void)
{
    UINT8 i;
    UINT32 u32Dat;
    signed short RoughCalib_Value = 0;
    uint16_t voltageMv;
    UINT8 staticPercent;
    UINT8 chargePercent;

    AdcVbatDetectPowerOn();

    ADC_InterTSSampInit();
    average_adc_value = 0;
    for(i = 0; i < Temp_number; i++)
    {
       average_adc_value += ADC_ExcutSingleConver();
    }
    average_adc_value = average_adc_value / Temp_number;
    u32Dat = average_adc_value / 141;
    (void)u32Dat;

    GPIOA_ModeCfg(GPIO_Pin_7, GPIO_ModeIN_Floating);
    ADC_ExtSingleChSampInit(SampleFreq_8_or_4, ADC_PGA_0);
    RoughCalib_Value = ADC_DataCalib_Rough();
    ADC_ChannelCfg(11);

    average_adc_value = AdcReadTrimmedAverage(RoughCalib_Value);
    voltageMv = (uint16_t)(((uint32_t)3480 * average_adc_value + 816U) / 1633U);
    voltageMv = AdcApplyVoltageCalibration(voltageMv);
    gAdcLastVoltageMv = voltageMv;
    staticPercent = BatteryVoltageToPercent(voltageMv, s_discharge_battery_curve_mv);
    chargePercent = BatteryVoltageToPercent(voltageMv, s_charge_battery_curve_mv);
    gAdcLastStaticCurvePercent = staticPercent;
    gAdcLastChargeCurvePercent = chargePercent;
    i = staticPercent;

    AdcVbatDetectPowerOff();
    return i;
}
#if 0
UINT8  ADC(void)
{
    UINT16 Max_average_adc_value;
    UINT16 Min_average_adc_value;
    UINT8      i;
    UINT32  u32Dat;
    signed short RoughCalib_Value = 0; // ADC粗调偏差值
    float voltage;
	float min_diff = 1000;
	UINT8 closest_index = 0; 

    /* 温度采样并输出 */
    ADC_InterTSSampInit();
    average_adc_value=0;
    for(i = 0; i < Temp_number; i++)
    {
       average_adc_value += ADC_ExcutSingleConver(); // 连续采样20次
    }
    average_adc_value = (average_adc_value>>4); // 2 2 2 2 =16
    //2820 = 20 度 = 141
    u32Dat = average_adc_value / 141;
    //PRINT("chip Temperature=%d(%d°)\n\r",average_adc_value,u32Dat);
   
    /* 单通道采样：选择adc通道0做采样，对应 PA4引脚， 带数据校准功能 */
    GPIOA_ModeCfg(GPIO_Pin_7, GPIO_ModeIN_Floating);  // GPIO_ModeIN_PU   GPIO_ModeIN_Floating
    ADC_ExtSingleChSampInit(SampleFreq_8_or_4, ADC_PGA_0);

    RoughCalib_Value = ADC_DataCalib_Rough(); // 用于计算ADC内部偏差，记录到全局变量 RoughCalib_Value中
    //PRINT("RoughCalib_Value =%d \r\n", RoughCalib_Value);

    ADC_ChannelCfg(11);
    average_adc_value=0;
    for(i = 0; i < Temp_number; i++)
    {
        //abcBuff[i] = ADC_ExcutSingleConver() + RoughCalib_Value; // 连续采样20次
        average_adc_value += ADC_ExcutSingleConver() + RoughCalib_Value; // 连续采样20次
    }
	//printf("00000 average_adc_value = %d \r\n",average_adc_value); 
	int quotient = average_adc_value / 100;

	average_adc_value = quotient * 100;

	//printf("11111 average_adc_value = %d \r\n",average_adc_value); 
    average_adc_value = (average_adc_value>>4); // 2 2 2 2 =16

    Max_average_adc_value=Max_ADC;
    Min_average_adc_value=Min_ADC;    
    
    u32Dat = (340 * average_adc_value)/1633;
    //printf("ADC=%d battle =%dv \r\n",average_adc_value,u32Dat);   

	voltage = (float)u32Dat / 100.0;
	
	float voltage_tmp = (int)(voltage * 10) / 10.0;	// 结果为3.6
	
	for (i = 0; i < 100; i++) {
		float diff;
		if(global_DEVICE_STATUS.fIsCharg == Is_Yes){
			diff = fabs(voltage_tmp - charg_battle_adv[i]);
		}else{
			diff = fabs(voltage_tmp - battle_adv[i]);
		}
		
		if (diff < min_diff) {
			min_diff = diff;
			closest_index = i;
		}
	}

	i = 100 - closest_index;
    //电池 4.2V 时 = 100%
    //电池 3.4V 时 = 0%        
    /*if(average_adc_value >= Max_average_adc_value)
    {
      	i=100;  
    }
    else  if(average_adc_value <= Min_average_adc_value)
    {
      	i=0;  
    }
    else
    {
      	//i = (UINT8) ((100*(average_adc_value-Min_average_adc_value))/(Max_average_adc_value-Min_average_adc_value));
		i = (UINT8) ((25*(average_adc_value-Min_average_adc_value))/(Max_average_adc_value-Min_average_adc_value));
		i = (i<<2);
    }*/
	//printf("ADC=%d, voltage=%.2f, battle_adv=%.4f, precent=%d%%\r\n", 
   //        average_adc_value, voltage, battle_adv[closest_index], i);

	uint16_t voltage_int = (uint16_t)(voltage_tmp * 100);  // 转为整数（保留2位小数）
	uint16_t ref_voltage_int = (uint16_t)(battle_adv[closest_index] * 10000);  // 保留4位小数

	printf("ADC=%d, voltage=%d.%02dV, battle_adv=%d.%04dV, precent=%d%%\r\n", 
       average_adc_value, 
       voltage_int / 100, voltage_int % 100,  // 整数部分和小数部分
       ref_voltage_int / 10000, ref_voltage_int % 10000, 
       i);

	printf("11111 global_DEVICE_STATUS.fIsCharg = %d \r\n",global_DEVICE_STATUS.fIsCharg); 
	if(global_DEVICE_STATUS.fIsCharg == Is_Yes){
		//printf("22 global_DEVICE_STATUS.fIsCharg = %d \r\n",global_DEVICE_STATUS.fIsCharg); 
		if(i>=20){
    		return i - 20;
		}else{
			return i;
		}
	}
	else{
		return i;
	}
}
#endif
#if 0
UINT8  check_Old_New_PCB_type(void)
{
#if 0
    UINT16 Max_average_adc_value;
    UINT16 Min_average_adc_value;
    UINT8      i;
    UINT32  u32Dat;
    signed short RoughCalib_Value = 0; // ADC粗调偏差值


    /* 单通道采样：选择adc通道0做采样，对应 PA4引脚， 带数据校准功能 */
    GPIOA_ModeCfg(GPIO_Pin_7, GPIO_ModeIN_Floating);  // GPIO_ModeIN_PU   GPIO_ModeIN_Floating
    ADC_ExtSingleChSampInit(SampleFreq_8_or_4, ADC_PGA_0);

    RoughCalib_Value = ADC_DataCalib_Rough(); // 用于计算ADC内部偏差，记录到全局变量 RoughCalib_Value中
    //PRINT("RoughCalib_Value =%d \r\n", RoughCalib_Value);

    ADC_ChannelCfg(11);
    average_adc_value=0;
    for(i = 0; i < Temp_number; i++)
    {
        average_adc_value += ADC_ExcutSingleConver() + RoughCalib_Value; // 连续采样20次
    }
    average_adc_value = (average_adc_value>>4); // 2 2 2 2 =16

    Max_average_adc_value=Max_ADC+500;
    Min_average_adc_value=Min_ADC-500;    
    
    u32Dat = (340 * average_adc_value)/1633;
    //printf("ADC=%d 电压=%dv \r\n",average_adc_value,u32Dat);   


    if(average_adc_value >= Max_average_adc_value)
    {
		Print_I3("old board"); 
		return Is_OLD;
    }
    else  if(average_adc_value <= Min_average_adc_value)
    {
		Print_I3("old board");
		return Is_OLD;
    }
    else
    {
		Print_I3("new board");
		return Is_NEW;
    }
#endif
	uint8_t chipid = Read_ChipID_R8();
	Print_I3("check_Old_New_PCB_type chipid=%d(0x%x)",chipid,chipid);

	if(chipid == 0x82){
		return Is_OLD;
	}else{
		return Is_NEW;
	}
}
#endif

