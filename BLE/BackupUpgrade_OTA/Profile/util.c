#include <stdio.h>
#include <stdlib.h>
#include "CONFIG.h"
#include "app_cfg.h"
#include "commoninfo.h"

char hexToChar(int hex) {
    if (hex < 10) return hex + '0'; // 0-9转换为'0'-'9'
    return hex - 10 + 'A'; // 10-15转换为'A'-'F'
}

void convert_number(int num, char *char1, char *char2) {
    // 分离百位和后两位
    int hundreds = num / 100;       // 获取百位数字
    int remainder = num % 100;      // 获取后两位数字
    
    // 转换百位数字为字符
    *char1 = '0' + hundreds;        // 例如：3 → '3'
    
    // 根据后两位数字进行转换
    if (remainder >= 0 && remainder <= 99) {
        if (remainder >= 10 && remainder <= 99) {
            // 情况1：后两位是10~99，直接转换为对应的两位字符
            //sprintf(char2, "%02d", remainder);  // 确保输出两位数字
            *char2 = 'a' + (remainder - 10);
        } else if (remainder == 0) {
            // 情况2：后两位是00，转换为字符'0'
            *char2 = '0';
        } else {
            // 情况3：后两位是01~09，转换为对应的字母 a~i
            *char2 = '0' + (remainder - 0);
        }
    } else {
        // 处理非法输入
        *char2 = '?';
    }
}

unsigned char getAdcAndWorkMode(unsigned char adc, unsigned char workmode)
{
	unsigned char ret;
	
	if(workmode == DEVICE_MODE_HIGH)	{
		if(adc == Is_Yes) {
			return 'X';
		}else{
			return 'P';
		}
	}else{
		if(adc == Is_Yes) {
			return 'H';
		}else{
			return '@';
		}
	}
}

// 倒序函数（同上）
void reverseMac(uint8_t Mac[6]) {
    int left = 0;
    int right = 5;
    uint8_t temp;
    while (left < right) {
        temp = Mac[left];
        Mac[left] = Mac[right];
        Mac[right] = temp;
        left++;
        right--;
    }
}

void reverseData(uint8_t *data, uint8_t len) {
    int left = 0;
    int right = len-1;
    uint8_t temp;
    while (left < right) {
        temp = data[left];
        data[left] = data[right];
        data[right] = temp;
        left++;
        right--;
    }
}


// 读取R8_CHIP_ID寄存器的值
uint8_t Read_ChipID_R8(void)
{
    // 直接访问寄存器地址，返回8位芯片ID值
    return R8_CHIP_ID;
}


