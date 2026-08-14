#include "app_cfg.h"
#include "CONFIG.h"
#include "epd_driver.h"
#include "commoninfo.h"
#include "flash_api.h"
#include "image_data.h"

#define DATA_MAX_LEN	238

#ifdef EPD_DISPLAY_TEST_ENABLE
#define TEST_GROUP				1
#define TEST_ROOM				1

//直接投屏，没有经过flash
void EPD_Driver_Display_WithOut_Flash_Debug(void)
{
	UINT32   L;
	const unsigned int TOTAL_LEN = sizeof(image_data);  // 数组总长度（378）
	const unsigned int BATCH_LEN = 238; 						  // 每批发送长度
	unsigned int current_idx = 0;								  // 当前发送起始索引
	unsigned int send_len;										  // 每批实际发送长度
	unsigned int ret;

	global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_AB;
	global_DEVICE_STATUS.fIsNeedStandby = 1;
	global_DEVICE_STATUS.fInitDriver = Is_Yes;
	global_DEVICE_STATUS.fisHost = Is_HOST;
	global_DEVICE_STATUS.fImageDataLen = 0;

	// 循环分批发送，直到所有字节发送完成
	while (current_idx < TOTAL_LEN) {
		// 计算当前批次的实际发送长度：
		// 若剩余字节 >=238，按238发送；否则发送剩余所有字节
		if ((TOTAL_LEN - current_idx) >= BATCH_LEN) {
			send_len = BATCH_LEN;
		} else {
			send_len = TOTAL_LEN - current_idx;
		}

		// 调试用：打印批次信息（可选）
		Print_I3("send data：index=%d, len=%d, totallen=%d\n",current_idx, send_len, current_idx + send_len);

		ret = refreshScreenColor(image_data+current_idx,send_len,1);
		Print_I3("send data return: ret=%d\n",ret);
		// 更新起始索引，准备下一批发送
		current_idx += send_len;
	}

#if 0
	mDelaymS(1000);
	current_idx = 0;
	send_len = 0;
	Print_I3("diplay bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n");
	global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_A;
	global_DEVICE_STATUS.fIsNeedStandby = 1;
	global_DEVICE_STATUS.fInitDriver = Is_Yes;
	global_DEVICE_STATUS.fisHost = Is_HOST;
	global_DEVICE_STATUS.fImageDataLen = 0;

	// 循环分批发送，直到所有字节发送完成
	while (current_idx < TOTAL_LEN) {
		// 计算当前批次的实际发送长度：
		// 若剩余字节 >=238，按238发送；否则发送剩余所有字节
		if ((TOTAL_LEN - current_idx) >= BATCH_LEN) {
			send_len = BATCH_LEN;
		} else {
			send_len = TOTAL_LEN - current_idx;
		}

		// 调试用：打印批次信息（可选）
		Print_I3("send data：index=%d, len=%d, totallen=%d\n",current_idx, send_len, current_idx + send_len);

		ret = refreshScreenColor(image_data+current_idx,send_len,1);
		Print_I3("send data return: ret=%d\n",ret);
		// 更新起始索引，准备下一批发送
		current_idx += send_len;
	}
#endif
}

//先预存1会议室1组，然后再切换投屏
void EPD_Driver_Display_From_Flash_Debug(){
	int i, ret, image_index;
	UINT32   L;
	const unsigned int TOTAL_LEN = sizeof(image_data);  // 数组总长度（378）
	const unsigned int BATCH_LEN = 238; 						  // 每批发送长度
	unsigned int current_idx = 0;								  // 当前发送起始索引
	unsigned int send_len;										  // 每批实际发送长度
	
	InitFlashDriver();

	uint16_t complete_batch = TOTAL_LEN / BATCH_LEN;	// 完整分片数
	uint16_t remain_byte = TOTAL_LEN % BATCH_LEN;	   // 剩余字节数
	uint16_t total_batch = complete_batch + (remain_byte > 0 ? 1 : 0);  // 总分片数
	//PRINT("EPD_Driver_Display_From_Flash_Debug total_batch:%d\r\n",total_batch);

	global_EXTERN_FLASH_INFO.fBlockNum = EXTERN_FLASH_BLOCK_FIRST_ADDR;
	global_DEVICE_STATUS.fPackageCnt = 0;
	global_DEVICE_STATUS.fPackageCount = total_batch;
	global_DEVICE_STATUS.fisHost = IS_HOST;
	
	image_index = EraseSaveBlock(0,TEST_GROUP,TEST_ROOM,1);
	if(image_index == -1){
		PRINT("EraseSaveBlock error 00000000000000000000000000000\r\n");
		global_DEVICE_STATUS.fWorked =Is_No;
		tmos_start_task(main_task_ID,EVENT_Low_Power ,500);
		return;
	}
	
	Print_I3("EPD_Driver_Display_From_Flash_Debug 00 image_index=%d\n",image_index);
	// 循环分批发送，直到所有字节发送完成
	while (current_idx < TOTAL_LEN) {
		// 计算当前批次的实际发送长度：
		// 若剩余字节 >=238，按238发送；否则发送剩余所有字节
		if ((TOTAL_LEN - current_idx) >= BATCH_LEN) {
			send_len = BATCH_LEN;
		} else {
			send_len = TOTAL_LEN - current_idx;
		}

		// 调试用：打印批次信息（可选）
		Print_I3("send data：index=%d, len=%d, totallen=%d\n",current_idx, send_len, current_idx + send_len);
		Save256DataToFlash(image_data+current_idx,send_len,image_index,global_EXTERN_FLASH_INFO.fBlockNum);
		global_DEVICE_STATUS.fPackageCnt++;
		// 更新起始索引，准备下一批发送
		current_idx += send_len;
	}
	DeInitFlashDriver();

	global_DEVICE_STATUS.fInitDriver = Is_Yes;
	global_EXTERN_FLASH_INFO.fBlockNum = EXTERN_FLASH_BLOCK_FIRST_ADDR;
	global_DEVICE_STATUS.fRefreshType = DEVICE_OP_COMMON_PRESAVE;
	global_DEVICE_STATUS.fIsNeedStandby = 1;
	global_DEVICE_STATUS.fPackageCnt=0;
	global_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_AB;
	InitFlashDriver();
	mDelayuS(50);

	if(preSaveDisplayColor(TEST_GROUP, TEST_ROOM, DEVICE_PRE_SAVE) == -1){
		PRINT("send pre save  error \r\n");	
		global_DEVICE_STATUS.fWorked =Is_No;
	}
	mDelayuS(50);
	DeInitFlashDriver();
}
#endif

#ifdef EPD_BWSOLID_IMG_CYCLE_TEST_ENABLE
/**
 * @brief 三色墨水屏黑白纯色图片轮播测试（首次调用立即执行，后续按间隔轮播）
 * @note 首次进入直接显示黑色，之后按getRefreshScreenTime()返回的间隔切换黑白
 */
void EPD_BWSolidImg_CycleTest(void)
{
    // 静态变量：计时器、当前颜色状态、首次执行标记
    static uint32_t cycle_timer = 0;
    static enum { SCREEN_BLACK, SCREEN_WHITE } curr_color = SCREEN_BLACK;
    static UINT8 is_first_run = Is_Yes;  // 首次执行标记

    // 1. 首次调用：立即执行清屏（不等待计时器）
    if (is_first_run)
    {
        // 确保驱动已初始化
        global_DEVICE_STATUS.fInitDriver = Is_Yes;
        // 首次直接显示黑色（按初始状态执行）
        cleanDisplayColor(SCREEN_COLOR_RED, Is_No);
        // 标记为非首次，后续进入轮播逻辑
        is_first_run = Is_No;
        return;  // 首次执行后直接返回，不进入计时逻辑
    }

    // 2. 非首次：按计时器判断是否到达轮播间隔
    cycle_timer++;
    if (cycle_timer >= getRefreshScreenTime())
    {
    	global_DEVICE_STATUS.fInitDriver = Is_Yes;
        // 切换黑白显示状态
        if (curr_color == SCREEN_BLACK)
        {
            cleanDisplayColor(SCREEN_COLOR_WHITE, Is_No);
            curr_color = SCREEN_WHITE;
        }
        else
        {
            cleanDisplayColor(SCREEN_COLOR_RED, Is_No);
            curr_color = SCREEN_BLACK;
        }

        // 重置计时器
        cycle_timer = 0;
    }
}
#endif

