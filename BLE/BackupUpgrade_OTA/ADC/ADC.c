
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

// 3844 = 4.13   -  

// 3600 4024   (424)
//  0% 600，100% 850
#define  Max_ADC       1964//2020  // 4.2v =100%
#define  Min_ADC       1664//1634  // 3.4v = 0%


#define  Temp_number   (16)
uint32_t average_adc_value;
/* Battery voltage in 0.1 mV units; preserve all 101 original entries. */
static const uint16_t battery_voltage_01mv[101] = {
    41400, 41210, 41010, 40950, 40840, 40800, 40770, 40680, 40600, 40510,
    40450, 40410, 40350, 40330, 40310, 40293, 40280, 40240, 40210, 40190,
    40160, 40130, 40100, 40080, 40040, 40000, 39980, 39950, 39920, 39880,
    39850, 39798, 39737, 39676, 39615, 39554, 39493, 39432, 39371, 39310,
    39249, 39188, 39127, 39066, 39005, 38944, 38883, 38822, 38761, 38700,
    38667, 38634, 38601, 38568, 38535, 38502, 38469, 38436, 38403, 38370,
    38309, 38248, 38187, 38126, 38065, 38004, 37943, 37882, 37821, 37760,
    37650, 37540, 37430, 37320, 37210, 37100, 36990, 36880, 36770, 36660,
    36544, 36428, 36312, 36196, 36080, 35964, 35848, 35732, 35616, 35500,
    35381, 35262, 35143, 35024, 34905, 34786, 34667, 34548, 34429, 34310,
    34280
};

/*float charg_battle_adv[100]={
	4.0008, 4.0008, 4.0008, 4.0008, 4.0008, 4.0008, 4.0008, 4.0008, 4.0008, 4.0008,
	4.0008, 4.0008, 4.0008, 4.0008, 4.0008, 4.0008, 4.0008, 4.0008, 4.0008, 4.0008,
	4.0008, 4.0008, 4.0008, 4.0008, 4.0008, 4.0008, 4.0008, 4.0008, 4.0008, 4.0008,
	4.0008, 4.0008, 4.0008, 4.0008, 4.0008,
	3.9942, 3.9856, 3.9760, 3.9670, 3.9574, 3.9487, 3.9394, 3.9307, 3.9223, 3.9133, 
	3.9056, 3.8981, 3.8901, 3.8833, 3.8761, 3.8693, 3.8628, 3.8569, 3.8510, 3.8451, 
	3.8399, 3.8346, 3.8293, 3.8247, 3.8197, 3.8151, 3.8107, 3.8058, 3.8014, 3.7971, 
	3.7927, 3.7884, 3.7841, 3.7800, 3.7757, 3.7717, 3.7676, 3.7633, 3.7596, 3.7596, 
	3.7552, 3.7512, 3.7472, 3.7428, 3.7385, 3.7338, 3.7289, 3.7236, 3.7174, 3.7115, 
	3.7044, 3.6969, 3.6889, 3.6811, 3.6721, 3.6641, 3.6557, 3.6467, 3.6383, 3.6294, 
	3.6197, 3.6098, 3.5990, 3.5856, 3.5593
};*/

UINT8  ADC(void)
{
    UINT16 Max_average_adc_value;
    UINT16 Min_average_adc_value;
    UINT8      i;
    UINT32  u32Dat;
    signed short RoughCalib_Value = 0; // ADC粗调偏差值
    uint32_t voltage_01mv;
    uint32_t min_diff = 0xffffffffUL;
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
   
    /* 单通道采样：选择adc通道0做采样，对应 PA4引脚， 带数据校准功能 */
    GPIOA_ModeCfg(GPIO_Pin_7, GPIO_ModeIN_Floating);  // GPIO_ModeIN_PU   GPIO_ModeIN_Floating
    ADC_ExtSingleChSampInit(SampleFreq_5_33_or_2_67, ADC_PGA_0);

    RoughCalib_Value = ADC_DataCalib_Rough(); // 用于计算ADC内部偏差，记录到全局变量 RoughCalib_Value中

    ADC_ChannelCfg(11);
    average_adc_value=0;
    for(i = 0; i < Temp_number; i++)
    {
        average_adc_value += ADC_ExcutSingleConver() + RoughCalib_Value; // 连续采样20次
    }
	//printf("00000 average_adc_value = %d \r\n",average_adc_value); 
	int quotient = average_adc_value / 100;

	average_adc_value = quotient * 100;
    average_adc_value = (average_adc_value>>4); // 2 2 2 2 =16

    Max_average_adc_value=Max_ADC;
    Min_average_adc_value=Min_ADC;    
    
    u32Dat = (348 * average_adc_value)/1633;

    /* u32Dat is in 10 mV units. Avoid pulling in soft-float helpers. */
    voltage_01mv = u32Dat * 100U;
    for (i = 0; i < sizeof(battery_voltage_01mv) / sizeof(battery_voltage_01mv[0]); i++) {
        uint32_t reference = battery_voltage_01mv[i];
        uint32_t diff = voltage_01mv >= reference ?
                        voltage_01mv - reference : reference - voltage_01mv;
        /* Keep the first (higher charge) entry on an exact tie. */
        if (diff < min_diff) {
            min_diff = diff;
            closest_index = i;
        }
    }
    i = 100 - closest_index;

    ADC_DisableTSPower();
    R8_ADC_CONVERT = 0;
    R8_ADC_CFG = 0;
    GPIOA_ModeCfg(GPIO_Pin_7, GPIO_ModeIN_PU);

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
    ADC_ExtSingleChSampInit(SampleFreq_5_33_or_2_67, ADC_PGA_0);

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
    ADC_ExtSingleChSampInit(SampleFreq_5_33_or_2_67, ADC_PGA_0);

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

