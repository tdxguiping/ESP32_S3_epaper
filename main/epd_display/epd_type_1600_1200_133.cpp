#include "epd_type_1600_1200_133.h"
#include "display_bsp.h"
#include "debug_output.h"
#include "epd_type_1600_1200_common.h"
#include "esp_timer.h"

void ePaperPort::EPD_Check_Busy_133(uint16_t loop_counter)
{
    int16_t i;
    int64_t start_us = esp_timer_get_time();

    if (loop_counter > 31) {
        loop_counter = 31;
    }
    i = 0;
    while (1) {
        int level = Get_BusyIOLevel();
        if (level) {
            UserDebugOutput_Printf("Check Busy over %d-%d \r\n",i,loop_counter);
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(500));

        if (level) {
            UserDebugOutput_Printf("Check Busy over %d-%d \r\n",i,loop_counter);
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(500));

        i++;
        UserDebugOutput_Printf(".%d-%d.", i,loop_counter);

        if (level) {
            UserDebugOutput_Printf("Check Busy over %d-%d \r\n",i,loop_counter);
            return;
        }


        if (i > loop_counter) {
            int elapsed_ms = (int)((esp_timer_get_time() - start_us) / 1000);
            ESP_LOGE(TAG, "EPD-133 busy timeout level=%d loops=%ld elapsed_ms=%d",
                     Get_BusyIOLevel(), (long)i, elapsed_ms);
            EpdType_ReportDisplayFailure(ESP_ERR_TIMEOUT);
            return;
        }
    }
}

void EpdType16001200_133_Display(ePaperPort &epd, const uint8_t *display_buf, size_t display_size)
{
    static const size_t expected_image_size = 1600U * 1200U / 2U;
    if (display_buf == nullptr || display_size != expected_image_size) {
        ESP_LOGE("Display", "EPD 1600x1200 13.3 rejected input=%u expected=%u",
                 (unsigned int)display_size,
                 (unsigned int)expected_image_size);
        return;
    }

    epd.EPD_Init();
    epd.NT61522_Init_display();
    epd.NT61522_Display_net(display_buf, display_size);
    epd.NT61522_Display();
}

void ePaperPort::EpdType16001200_133_Sleep()
{
    NT61522_Sleep();
}

void ePaperPort::EpdType16001200_133_Init()
{
    LOG_Purple("1600x1200 %s>%d",__func__,__LINE__);
    NT61522_Init();
}

void ePaperPort::EpdType16001200_133_Display()
{

    if (!EnsureDispBuffer()) {
        ESP_LOGE(TAG, "EPD_Display 1600x1200 aborted, DispBuffer not ready");
        EpdType_ReportDisplayFailure(ESP_ERR_NO_MEM);
        return;
    }
    
    NT61522_DisplayImage(DispBuffer, DisplayLen);
    ReleaseRotationBuffer();
    ReleaseDispBuffer();

}

void ePaperPort::EpdType16001200_133_NT61522_Init()
{
	unsigned char r74DataBuf[9]={0x00, 0x0C, 0x0C, 0xD9, 0xDD, 0xDD, 0x15, 0x15, 0x55};
	unsigned char rf0DataBuf[6]={0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};
	unsigned char r60DataBuf[2]={0x03, 0x03};
	unsigned char r86DataBuf[1]={0x10};
	unsigned char rb6DataBuf[1]={0x07};
	unsigned char rb7DataBuf[1]={0x01};
	unsigned char rb0DataBuf[1]={0x01};
	unsigned char rb1DataBuf[1]={0x02};
    #define Delay_time_133   9

    EPD_Reset();
    setPinCsAll(GPIO_HIGH);
    Read_Temptr();       //添加锁定当前温度函数(掉电重启时解除)，为了避免屏幕多次运行IC升温导致调取波形温度与实际环境温度不符

	EPD_Reset();
	EPD_Check_Busy_133(1);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(0x74, r74DataBuf, sizeof(r74DataBuf));
	setPinCs(TARGET_MASTER,GPIO_HIGH);delayms(Delay_time_133);

	setPinCsAll(GPIO_LOW);delayms(Delay_time_133);
	spiTransmit(0xF0, rf0DataBuf, sizeof(rf0DataBuf));
	setPinCsAll(GPIO_HIGH);delayms(Delay_time_133);

	setPinCsAll(GPIO_LOW);
	spiTransmit(R00_PSR, PSR_V, sizeof(PSR_V));
	setPinCsAll(GPIO_HIGH);delayms(Delay_time_133);
	
	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(RA5_DCDC, DCDC_V, sizeof(DCDC_V));
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	spiTransmit(R50_CDI, CDI_V, sizeof(CDI_V));
	setPinCsAll(GPIO_HIGH);delayms(Delay_time_133);

	setPinCsAll(GPIO_LOW);
	spiTransmit(0x60, r60DataBuf, sizeof(r60DataBuf));
	setPinCsAll(GPIO_HIGH);delayms(Delay_time_133);

	setPinCsAll(GPIO_LOW);
	spiTransmit(0x86, r86DataBuf, sizeof(r86DataBuf));
	setPinCsAll(GPIO_HIGH);delayms(Delay_time_133);

	setPinCsAll(GPIO_LOW);
	spiTransmit(RE3_PWS, PWS_V, sizeof(PWS_V));
	setPinCsAll(GPIO_HIGH);delayms(Delay_time_133);

	// setPinCsAll(GPIO_LOW);
	// spiTransmit(RE0_CCSET, CCSET_V_LOCK, sizeof(CCSET_V_LOCK));
	// setPinCsAll(GPIO_HIGH);delayms(Delay_time_133);

	setPinCsAll(GPIO_LOW);
	spiTransmit(R61_TRES, TRES_V, sizeof(TRES_V));
	setPinCsAll(GPIO_HIGH);delayms(Delay_time_133);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(R01_PWR, PWR_V, sizeof(PWR_V));
	setPinCs(TARGET_MASTER,GPIO_HIGH);delayms(Delay_time_133);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(0xB6, rb6DataBuf, sizeof(rb6DataBuf));
	setPinCs(TARGET_MASTER,GPIO_HIGH);delayms(Delay_time_133);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(R06_BTST_P, BTST_P_V, sizeof(BTST_P_V));
	setPinCs(TARGET_MASTER,GPIO_HIGH);delayms(Delay_time_133);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(0xB7, rb7DataBuf, sizeof(rb7DataBuf));
	setPinCs(TARGET_MASTER,GPIO_HIGH);delayms(Delay_time_133);
	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(R05_BTST_N, BTST_N_V, sizeof(BTST_N_V));
	setPinCs(TARGET_MASTER,GPIO_HIGH);

	delayms(Delay_time_133);
	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(0xB0, rb0DataBuf, sizeof(rb0DataBuf));
	setPinCs(TARGET_MASTER,GPIO_HIGH);
	delayms(Delay_time_133);
	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(0xB1, rb1DataBuf, sizeof(rb1DataBuf));
	setPinCs(TARGET_MASTER,GPIO_HIGH);
    delayms(Delay_time_133);
}

void ePaperPort::EpdType16001200_133_NT61522_Display()
{

    setPinCsAll(GPIO_LOW);
	spiTransmitCommand(R04_PON);
	setPinCsAll(GPIO_HIGH);
	delayms(30);
    UserDebugOutput_Printf("---1---\r\n");
	EPD_Check_Busy_133(1);
	delayms(30);

    setPinCsAll(GPIO_LOW);
	spiTransmit(R12_DRF,DRF_V,sizeof(DRF_V));
	setPinCsAll(GPIO_HIGH);
	delayms(30);
    UserDebugOutput_Printf("---2---\r\n");
	EPD_Check_Busy_133(31);
	delayms(30);
	setPinCsAll(GPIO_LOW);
	spiTransmit(R02_POF,POF_V,sizeof(POF_V));
	setPinCsAll(GPIO_HIGH);
	delayms(30);
    UserDebugOutput_Printf("---3---\r\n");
	EPD_Check_Busy_133(1);
	delayms(30);


    vTaskDelay(pdMS_TO_TICKS(1000));
    Set_Power(0);
}

void ePaperPort::EpdType16001200_133_NT61522_InitDisplay()
{
	unsigned char temptr_fill = 0;
    unsigned char dataBuff[10];

    image_countger_ =0;
    u8flag_ = 0xAA;

	setPinCsAll(GPIO_LOW);
	spiTransmit(RE0_CCSET, CCSET_V_LOCK, sizeof(CCSET_V_LOCK));
	setPinCsAll(GPIO_HIGH);
	delayms(10);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmitCommand(R40_TSC);
	delayms(10);
	EPD_Check_Busy_133(1);
	spiReceiveData(&dataBuff[0], 2);
	setPinCs(TARGET_MASTER,GPIO_HIGH);
	delayms(30);
	EPD_Check_Busy_133(1);

    //Temptr[0] =  WHT20_Temp+10;
	temptr_fill = Temptr[0]<<1;
    LOG_Blue("Temptr[0]=%d, temptr_fill=%d", Temptr[0], temptr_fill);   

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(RE5_TSSET, &temptr_fill, 1);
	setPinCs(TARGET_MASTER,GPIO_HIGH);
	delayms(30);

    EPD_Select_Master();
    spiTransmitCommand(R10_DTM);
}

void ePaperPort::EpdType16001200_133_NT61522_DisplayNet(const uint8_t *imageData, size_t imageSize)
{
        // //piTransmitCommand(R10_DTM);
        // EPD_WriteMultiData_ToMaster((uint8_t *)imageData, 480000);

        // EPD_Select_Slave();
        // spiTransmitCommand(R10_DTM);
        // EPD_WriteMultiData_ToSlave((uint8_t *)imageData+480000, 480000);     


#if 0
     uint32_t u32posi;
     static const size_t expected_image_size = 1600U * 1200U / 2U;

     uint32_t total;
     uint8_t Data_Buffer[NT61522_BUFFER_SIZE];
    //=======================Master==================================
    memset(Data_Buffer, epd_yellow, NT61522_BUFFER_SIZE);
    EPD_Select_Master();
    spiTransmitCommand(R10_DTM);
    total = 480000;
    while (total >= NT61522_BUFFER_SIZE) {
        EPD_Select_Master();
        spiTransmitData(Data_Buffer, NT61522_BUFFER_SIZE);
        total -= NT61522_BUFFER_SIZE;
    }
    if (total > 0) {
        spiTransmitData(Data_Buffer, total);
    }
    //=========================================================
    //=================Slaver========================================
    memset(Data_Buffer, epd_red, NT61522_BUFFER_SIZE);
    EPD_Select_Slave();
    spiTransmitCommand(R10_DTM);
    total = 480000;
    while (total >= NT61522_BUFFER_SIZE) {
        EPD_Select_Slave();
        spiTransmitData(Data_Buffer, NT61522_BUFFER_SIZE);
        total -= NT61522_BUFFER_SIZE;
    }
    if (total > 0) {
        spiTransmitData(Data_Buffer, total);
    }
    //=========================================================
#endif

#if 1
     uint32_t u32posi;
     static const size_t expected_image_size = 1600U * 1200U / 2U;

    /* 中文注释：       主屏区域总长度边界       0 ~ (1600*300 - 1) 属于 MASTER       >= 1600*300 属于 SLAVE    */
    const uint32_t master_limit = 1600U * 300U;//=480000;

    /* 中文注释：       参数保护       按你的描述 imageSize 范围为 1 ~ 900    */
    if (imageData == nullptr || imageSize != expected_image_size) {
        ESP_LOGE(TAG, "EPD 1600x1200 13.3 image size invalid input=%u expected=%u",
                 (unsigned int)imageSize,
                 (unsigned int)expected_image_size);
        return;
    }
    if(image_countger_ >=(480000+480000)){
        ESP_LOGE(TAG, "EPD 1600x1200 13.3 image exceeds display size");
        return;
    }

    size_t remain = imageSize;
    u32posi = 0;
    // memset(u8dat, epd_black,max_l_dat);
    while (remain > 0) {
        /* 中文注释：           单次最多处理 300        */
        //uint32_t chunk = (remain > 300) ? 300U : (uint32_t)remain;
        uint32_t chunk = (remain > NT61522_SPI_SAFE_DMA_TX_CHUNK) ? (uint32_t)NT61522_SPI_SAFE_DMA_TX_CHUNK : (uint32_t)remain;
        /* 中文注释：           情况1：当前还在 MASTER 区域        */

        //LOG_Cyan("image_countger_ %ld, master_limit %ld",image_countger_, master_limit);
        if (image_countger_ < master_limit) {
            /* 中文注释：               计算 MASTER 区域还剩多少空间            */
            uint32_t master_remain = master_limit - image_countger_;

            if (chunk <= master_remain) {
                /* 中文注释：                   当前这一段完全落在 MASTER 区域                */
               // UserDebugOutput_Printf("M1:chunk=%ld,image_countger_=%ld\r\n",chunk,image_countger_);
                //setPinCs(TARGET_MASTER, chunk);
                EPD_Select_Master();
                //spiTransmitCommand(R10_DTM);
                spiTransmitData(imageData+u32posi, chunk);
                //spiTransmitData(u8dat, chunk);
                u32posi = u32posi + chunk;  
                //setPinCs(TARGET_MASTER, 1);
                //delayms(30);

                image_countger_ += chunk;
                remain -= chunk;
            } else {
                /* 中文注释：                   当前这一段跨越了 MASTER -> SLAVE 边界                   先发送 MASTER 剩余部分                */
                if (master_remain > 0U) {
                    // UserDebugOutput_Printf("M2:master_remain=%ld,image_countger_=%ld\r\n",master_remain,image_countger_);
                    //setPinCs(TARGET_MASTER, master_remain);
                    EPD_Select_Master();
                    //spiTransmitCommand(R10_DTM);
                    spiTransmitData(imageData+u32posi, master_remain);
                    //spiTransmitData(u8dat, master_remain);
                    //setPinCsAll(1);
                    u32posi = u32posi + master_remain;  
                    //setPinCs(TARGET_MASTER, 1);
                    //delayms(30);

                    image_countger_ += master_remain;
                    remain -= master_remain;
                }

                /* 中文注释：                   再发送本次 chunk 剩余的部分到 SLAVE
                   这里不能等下一轮 outer while，                   要在本轮把 chunk 的剩余部分处理掉
                */
                {
                    uint32_t slave_part = chunk - master_remain;
                    if (slave_part > 0U) {
                       // UserDebugOutput_Printf("S2:slave_part=%ld,image_countger_=%ld\r\n",slave_part,image_countger_);
                        //setPinCs(TARGET_SLAVE, slave_part);
                        EPD_Select_Slave();
                        if (u8flag_ == 0xAA)
                        {
                            spiTransmitCommand(R10_DTM);
                            u8flag_ = 0;
                        }                        
                        spiTransmitData(imageData+u32posi, slave_part);
                        //spiTransmitData(u8dat, slave_part);
                        u32posi = u32posi + slave_part;  
                        //setPinCs(TARGET_SLAVE, 1);
                        //delayms(30);

                        image_countger_ += slave_part;
                        remain -= slave_part;
                    }
                }
            }
        } else {
            /* 中文注释：               情况2：当前已经在 SLAVE 区域            */
            //UserDebugOutput_Printf("S1:chunk=%ld,image_countger_=%ld\r\n",chunk,image_countger_);
            //setPinCs(TARGET_SLAVE, chunk);
            EPD_Select_Slave();
            if (u8flag_ == 0xAA)
            {
             spiTransmitCommand(R10_DTM);
             u8flag_ = 0;
            }                        
            spiTransmitData(imageData+u32posi, chunk);
            //spiTransmitData(u8dat, chunk);

            u32posi = u32posi + chunk;  
            //setPinCs(TARGET_SLAVE, 1);
            //delayms(30);

            image_countger_ += chunk;
            remain -= chunk;
        }
    }
#endif
}

unsigned char ePaperPort::Read_Temptr(void)
{
    NT61522_ReadRevision();

	setPinCsAll(GPIO_LOW);
	spiTransmit(RE0_CCSET, CCSET_V_CUR, sizeof(CCSET_V_CUR));
	setPinCsAll(GPIO_HIGH);
	delayms(10);
	
	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmitCommand(R40_TSC);
	delayms(10);
	EPD_Check_Busy_133(1);
	spiReceiveData(&Temptr[0], 2);
	setPinCs(TARGET_MASTER,GPIO_HIGH);
	EPD_Check_Busy_133(1);

	Temptr[0] = Temptr[0] > 50 ? 48 : Temptr[0];    

    if(Temptr[0] <=1) Temptr[0]=0x2A;    
    LOG_INFO(".... Temptr=%d",Temptr[0]);
	return Temptr[0];
}
