#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rledecode.h"

struct Info
{
	// 是否已经获取到第一位
	unsigned char started : 1;
	// 当前位数据
	unsigned char cur : 1;
	// 当前正在计算0之前的计数还是0之后的计数
	unsigned char status : 1;
	// 0之前的计数
	unsigned int bitCntBeforeZero;
	// 已经读到的0之后的位数
	unsigned int bitCntAfterZero;
	// 实际的重复数据的数量
	size_t cnt;
	// 上一次计数的剩余数据，因为计算的是位数据，可能数量没有正好是8的倍数
	unsigned char bitsLeftByte;
	unsigned char bitsLeftCnt;
} info;

unsigned char sbytes[60];
unsigned int slength;
RleImgDataCallback_t imgCb = NULL;
int callbackReturnValue = 0;

// 输出单个byte
void onByteAnalyzed(unsigned char byte)
{
	//fputc(byte, outFile);
	if(imgCb != NULL)
		callbackReturnValue = imgCb(byte);
	else
		sbytes[slength++] = byte;
}
// 输出多个重复的byte
void onBytesAnalyzed(unsigned char byte, size_t cnt)
{
	for (size_t i = 0; i < cnt; i++)
	{
		//fputc(byte, outFile);
		if(imgCb != NULL)
			callbackReturnValue = imgCb(byte);
		else
			sbytes[slength++] = byte;
	}
}

// 0之后的读取结束
void checkIfPieceComplete()
{
	if (info.bitCntAfterZero < info.bitCntBeforeZero + 2)
	{
		return;
	}
	int cnt = info.cnt + 1;
	int totalCnt = cnt + info.bitsLeftCnt;
	// 不满一个字节的情况
	if (totalCnt < 8)
	{
		if (info.cur == 1)
		{
			info.bitsLeftByte |= (255 >> info.bitsLeftCnt) & (255 << (8 - totalCnt));
		}
		info.bitsLeftCnt = totalCnt;
	}
	// 可输出数据的情况
	else
	{
		// 能组成整的byte的数据
		int byteCnt = totalCnt / 8;
		if (info.bitsLeftCnt > 0)
		{
			byteCnt -= 1;
			if (info.cur == 1)
			{
				info.bitsLeftByte |= 255 >> info.bitsLeftCnt;
			}
			onByteAnalyzed(info.bitsLeftByte);
		}
		if (byteCnt == 1)
		{
			onByteAnalyzed(info.cur == 1 ? 255 : 0);
		}
		else if (byteCnt > 1)
		{
			onBytesAnalyzed(info.cur == 1 ? 255 : 0, byteCnt);
		}
		// 剩余不够一个byte的数据
		info.bitsLeftCnt = totalCnt % 8;
		if (info.bitsLeftCnt > 0 && info.cur == 1)
		{
			info.bitsLeftByte = 255 << (8 - info.bitsLeftCnt);
		}
		else
		{
			info.bitsLeftByte = 0;
		}
		}
	// 重置计数参数
		info.cur = info.cur == 0 ? 1 : 0;
		info.status = 0;
		info.bitCntBeforeZero = 0;
		info.bitCntAfterZero = 0;
}

void checkBit(unsigned char bit)
{
	// 获取第一位
	if (info.started == 0)
	{
		info.started = 1;
		info.cur = bit;
		return;
	}
	// 当前位是0
	if (bit == 0)
	{
		// 0之前的计数完成
		if (info.status == 0)
		{
			info.status = 1;
			info.cnt = 0;
			return;
		}
		// 读取位
		info.bitCntAfterZero++;
		info.cnt <<= 1;
		checkIfPieceComplete();
	}
	// 当前位是1
	else
	{
		// 正在计算0之前的计数
		if (info.status == 0)
		{
			info.bitCntBeforeZero++;
			return;
		}
		// 读取位
		info.bitCntAfterZero++;
		info.cnt = (info.cnt << 1) | 1;
		checkIfPieceComplete();
	}
}

// 填充数据
void feed(unsigned char *bytes, int arrayLen)
{
	// 遍历byte数组
	for (int i = 0; i < arrayLen; i++)
	{
		char c = *(bytes + i);
		checkBit((c >> 7) & 1);
		checkBit((c >> 6) & 1);
		checkBit((c >> 5) & 1);
		checkBit((c >> 4) & 1);
		checkBit((c >> 3) & 1);
		checkBit((c >> 2) & 1);
		checkBit((c >> 1) & 1);
		checkBit(c & 1);
	}
}

void wind_up()
{
	if (info.bitsLeftCnt > 0)
	{
		onByteAnalyzed(info.bitsLeftByte);
	}
	info.started = 0;
	info.cur = 0;
	info.status = 0;
	info.bitCntBeforeZero = 0;
	info.bitCntAfterZero = 0;
	info.bitsLeftCnt = 0;
	info.bitsLeftByte = 0;
}

// 反转数组
void reverse(unsigned char *arr, int start, int end) {
    while (start < end) {
        unsigned char temp = arr[start];
        arr[start] = arr[end];
        arr[end] = temp;
        start++;
        end--;
    }
}

// 解密函数
int rle_reverse_decrypt(unsigned char *input, int length,RleDataCallback_t rCb) {
	//sbytes = malloc(60);
	memset(sbytes,0,60);
	slength = 0;
	imgCb = NULL;
	
    if (length < 3) {
        memcpy(sbytes, input, length);
        reverse(sbytes, 0, length - 1);
    } else {
        int p = input[length - 1];
        //unsigned char rle_data[100];
		unsigned char *rle_data = malloc(length - 1);
        memcpy(rle_data, input, length - 1);

        // 反转 head 和 tail
        reverse(rle_data, 0, p - 1);
        reverse(rle_data, p, length - 2);
		int piece = 10;
		int size = length - 1;
		int len = (size % piece == 0) ? size / piece : (size / piece + 1);
		for (int i = 0; i < len; i++)
		{
			int tmp1 = i == len - 1 ? (size - piece * i) : piece;
			unsigned char *bytes = malloc(tmp1);
			for (int j = 0; j < tmp1; j++)
			{
				int idx = i * piece + j;
				bytes[j] = rle_data[idx];
			}

			feed(bytes, tmp1);
			free(bytes);
		}
		free(rle_data);
		wind_up();
		rCb(sbytes,slength);
    }

	//free(sbytes);
	return 0;
}

int rle_decrypt(unsigned char *input, int length, RleImgDataCallback_t rCb) {	
	imgCb = rCb;
	feed(input, length);

	return callbackReturnValue;
}

void rle_un_decrypt() {
	info.started = 0;
    info.cur = 0;
    info.status = 0;
    info.bitCntBeforeZero = 0;
    info.bitCntAfterZero = 0;
    info.bitsLeftCnt = 0;
    info.bitsLeftByte = 0;
	imgCb = NULL;
}


