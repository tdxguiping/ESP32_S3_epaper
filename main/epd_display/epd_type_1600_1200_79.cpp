#include "epd_type_1600_1200_79.h"
#include "display_bsp.h"
#include "epd_type_1600_1200_common.h"
#include "esp_timer.h"

namespace {
const unsigned char PSR_V_79[2] = {	0xDF, 0x6B};
const unsigned char PWR_V_79[6] = {	0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38};
const unsigned char POF_V_79[1] = {	0x01};
const unsigned char POFS_MV_79[4] = {	0x00, 0xC0, 0x03, 0xA8};
const unsigned char POFS_SV_79[4] = {	0x00, 0xC0, 0x03, 0x9A};
const unsigned char DRF_V_79[1] = {	0x00};
const unsigned char PLL_V_79[1] = {	0x08 };
const unsigned char CDI_V_79[1] = {	0x37};
const unsigned char TCON_V_79[2] = {	0x03, 0x03};
const unsigned char TRES_V_79[4] = {	0x04, 0xB0, 0x03, 0x20};
const unsigned char CMD66_V_79[6] = {	0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};
const unsigned char EN_BUF_V_79[1] = {	0x07};
const unsigned char CCSET_V_79[1] = {	0x01};
const unsigned char PWS_V_79[1] = {	0x22};
const unsigned char AN_TM_V_79[9] = {	0x00, 0x0C, 0x0C, 0xD9, 0xDD, 0xDD, 0x15, 0x15, 0x55};
const unsigned char AGID_V_79[1] = {	0x10};
const unsigned char CMDA4_V_79[9] = {	0x03, 0x00, 0x01, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00};
const unsigned char DCDC_V_79[3] = {	0x44, 0x54 ,0x00};
const unsigned char BTST_P_V_79[2] = {	0xE0, 0x20};
const unsigned char BOOST_VDDP_EN_V_79[1] = {	0x01};
const unsigned char BTST_N_V_79[2] = {	0xE0, 0x20};
const unsigned char BUCK_BOOST_VDDN_V_79[1] = {	0x01};
const unsigned char TFT_VCOM_POWER_V_79[1] = {	0x02};
}

void ePaperPort::EPD_Check_Busy_79(uint16_t loop_counter)
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
            printf("Check Busy over\r\n");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        i++;
        printf("~%d.", i);

        if (i > loop_counter) {
            int elapsed_ms = (int)((esp_timer_get_time() - start_us) / 1000);
            ESP_LOGE(TAG, "EPD busy timeout level=%d loops=%ld elapsed_ms=%d",
                     Get_BusyIOLevel(), (long)i, elapsed_ms);
            EpdType_ReportDisplayFailure(ESP_ERR_TIMEOUT);
            return;
        }
    }
}

void EpdType16001200_79_Display(ePaperPort &epd, const uint8_t *display_buf, size_t display_size)
{
    static const size_t expected_image_size = 1600U * 1200U / 2U;
    if (display_buf == nullptr || display_size != expected_image_size) {
        ESP_LOGE("Display", "EPD 1600x1200 7.9 rejected input=%u expected=%u",
                 (unsigned int)display_size,
                 (unsigned int)expected_image_size);
        return;
    }

    epd.EPD_Init();
    epd.NT61522_Init_display();
    epd.NT61522_Display_net(display_buf, display_size);
    epd.NT61522_Display();
}

void ePaperPort::EpdType16001200_79_Sleep()
{
    NT61522_Sleep();
}

void ePaperPort::EpdType16001200_79_Init()
{
    // LOG_Purple("1600x1200 %s>%d",__func__,__LINE__);
    NT61522_Init();
}

void ePaperPort::EpdType16001200_79_Display()
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

void ePaperPort::EpdType16001200_79_NT61522_Init()
{
    // static const uint8_t an_tm_v[] = {0x00, 0x0C, 0x0C, 0xD9, 0xDD, 0xDD, 0x15, 0x15, 0x55};
    // static const uint8_t cmd66_v[] = {0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};
    // static const uint8_t pll_v[] = {0x08};
    // static const uint8_t tcon_v[] = {0x03, 0x03};
    // static const uint8_t pofs_mv[] = {0x00, 0xC0, 0x03, 0xA8};
    // static const uint8_t pofs_sv[] = {0x00, 0xC0, 0x03, 0x9A};
    // static const uint8_t agid_v[] = {0x10};
    // static const uint8_t cmda4_v[] = {0x03, 0x00, 0x01, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00};
    // static const uint8_t en_buf_v[] = {0x07};
    // static const uint8_t boost_vddp_en_v[] = {0x01};
    // static const uint8_t buck_boost_vddn_v[] = {0x01};
    // static const uint8_t tft_vcom_power_v[] = {0x02};
    // #define Delay_time_709   10

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(0x74, an_tm_v, sizeof(an_tm_v));
    // setPinCs(TARGET_MASTER, 1);
    // delayms(Delay_time_709);   // 这些 10ms 很重要。否则会显示不清楚

    // setPinCsAll(0);
    // delayms(1);
    // spiTransmit(0xF0, cmd66_v, sizeof(cmd66_v));
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCsAll(0);
    // spiTransmit(R00_PSR, NT61522_PSR_V(), 2);
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(0xA5, NT61522_DCDC_V(), 3);
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCsAll(0);
    // spiTransmit(R30_PLL, pll_v, sizeof(pll_v));
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCsAll(0);
    // spiTransmit(R50_CDI, NT61522_CDI_V(), 1);
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCsAll(0);
    // spiTransmit(R60_TCON, tcon_v, sizeof(tcon_v));
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(R03_POFS, pofs_mv, sizeof(pofs_mv));
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_SLAVE, 0);
    // spiTransmit(R03_POFS, pofs_sv, sizeof(pofs_sv));
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCsAll(0);
    // spiTransmit(R86_AGID, agid_v, sizeof(agid_v));
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCsAll(0);
    // spiTransmit(RE3_PWS, NT61522_PWS_V(), 1);
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCsAll(0);
    // spiTransmit(RE0_CCSET, NT61522_CCSET_V_CUR(), 1);
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCsAll(0);
    // spiTransmit(R61_TRES, NT61522_TRES_V(), 4);
    // setPinCsAll(1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(0xA4, cmda4_v, sizeof(cmda4_v));
    // setPinCs(TARGET_MASTER, 1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(R01_PWR, NT61522_PWR_V(), 6);
    // setPinCs(TARGET_MASTER, 1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(0xB6, en_buf_v, sizeof(en_buf_v));
    // setPinCs(TARGET_MASTER, 1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(0x06, NT61522_BTST_P_V(), 2);
    // setPinCs(TARGET_MASTER, 1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(0xB7, boost_vddp_en_v, sizeof(boost_vddp_en_v));
    // setPinCs(TARGET_MASTER, 1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(0x05, NT61522_BTST_N_V(), 2);
    // setPinCs(TARGET_MASTER, 1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(0xB0, buck_boost_vddn_v, sizeof(buck_boost_vddn_v));
    // setPinCs(TARGET_MASTER, 1);
    // delayms(Delay_time_709);

    // setPinCs(TARGET_MASTER, 0);
    // spiTransmit(0xB1, tft_vcom_power_v, sizeof(tft_vcom_power_v));
    // setPinCs(TARGET_MASTER, 1);
    // delayms(Delay_time_709);  
    // LOG_Purple("%s>%d  over",__func__,__LINE__);




    EPD_Reset();  
    EPD_Check_Busy_79(2);

    NT61522_ReadTemperature();    
    EPD_Reset();  
    EPD_Check_Busy_79(2);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(AN_TM, AN_TM_V_79, sizeof(AN_TM_V_79));
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	spiTransmit(CMD66, CMD66_V_79, sizeof(CMD66_V_79));
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	spiTransmit(PSR, PSR_V_79, sizeof(PSR_V_79));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(DCDC, DCDC_V_79, sizeof(DCDC_V_79));
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	spiTransmit(PLL, PLL_V_79, sizeof(PLL_V_79));
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	spiTransmit(CDI, CDI_V_79, sizeof(CDI_V_79));
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	spiTransmit(TCON, TCON_V_79, sizeof(TCON_V_79));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(POFS, POFS_MV_79, sizeof(POFS_MV_79));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_SLAVE,GPIO_LOW);
	spiTransmit(POFS, POFS_SV_79, sizeof(POFS_SV_79));
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	spiTransmit(AGID, AGID_V_79, sizeof(AGID_V_79));
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	spiTransmit(PWS, PWS_V_79, sizeof(PWS_V_79));
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	spiTransmit(CCSET, CCSET_V_79, sizeof(CCSET_V_79));
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	spiTransmit(TRES, TRES_V_79, sizeof(TRES_V_79));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(CMDA4, CMDA4_V_79, sizeof(CMDA4_V_79));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(PWR, PWR_V_79, sizeof(PWR_V_79));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(EN_BUF, EN_BUF_V_79, sizeof(EN_BUF_V_79));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(BTST_P, BTST_P_V_79, sizeof(BTST_P_V_79));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(BOOST_VDDP_EN, BOOST_VDDP_EN_V_79, sizeof(BOOST_VDDP_EN_V_79));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(BTST_N, BTST_N_V_79, sizeof(BTST_N_V_79));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(BUCK_BOOST_VDDN, BUCK_BOOST_VDDN_V_79, sizeof(BUCK_BOOST_VDDN_V_79));
	setPinCsAll(GPIO_HIGH);

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(TFT_VCOM_POWER, TFT_VCOM_POWER_V_79, sizeof(TFT_VCOM_POWER_V_79));
	setPinCsAll(GPIO_HIGH);
}

void ePaperPort::EpdType16001200_79_NT61522_Display()
{
    ESP_LOGI(TAG, "EPD 1600x1200 7.9 refresh start");

	setPinCsAll(GPIO_LOW);
	spiTransmitCommand(PON);
	delayms(30);
    printf("---1---\r\n");
	EPD_Check_Busy_79(2);
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	delayms(30);
	spiTransmit(DRF, DRF_V_79, sizeof(DRF_V_79));
	delayms(30);
    printf("---2---\r\n");
	EPD_Check_Busy_79(31);
	setPinCsAll(GPIO_HIGH);

	setPinCsAll(GPIO_LOW);
	spiTransmit(POF, POF_V_79, sizeof(POF_V_79));
	delayms(30);
    printf("---3---\r\n");
	EPD_Check_Busy_79(2);
	setPinCsAll(GPIO_HIGH);
    ESP_LOGI(TAG, "EPD 1600x1200 7.9 refresh done");


    // LOG_Purple("1600-7.09  %s>%d",__func__,__LINE__);
    // setPinCsAll(0);
    // spiTransmitCommand(R04_PON);
    // EPD_Check_Busy();
    // setPinCsAll(1);
    // delayms(1);

    // ESP_LOGI(TAG, "NT61522_Display stage: R12_DRF start");
    // setPinCsAll(0);
    // delayms(30);
    // spiTransmit(R12_DRF, NT61522_DRF_V(), 1);
    // EPD_Check_Busy();
    // setPinCsAll(1);
    // delayms(1);
    // ESP_LOGI(TAG, "NT61522_Display stage: R12_DRF done");

    // setPinCsAll(0);
    // spiTransmit(R02_POF, NT61522_POF_V(), 1);
    // EPD_Check_Busy();
    // setPinCsAll(1);
    // delayms(1);
    // LOG_Purple("%s>%d over",__func__,__LINE__);
}

void ePaperPort::EpdType16001200_79_NT61522_InitDisplay()
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
	EPD_Check_Busy_79(2);
	spiReceiveData(&dataBuff[0], 2);
	setPinCs(TARGET_MASTER,GPIO_HIGH);
	delayms(30);
	EPD_Check_Busy_79(2);
	

    //Temptr[0] =  WHT20_Temp+10;
    //Temptr[0] =  WHT20_Temp;
	//temptr_fill = Temptr[0]<<1;

    temptr_fill = Temptr[0]+5;
    temptr_fill = Temptr[0];
    temptr_fill = temptr_fill<<1;

	setPinCs(TARGET_MASTER,GPIO_LOW);
	spiTransmit(RE5_TSSET, &temptr_fill, 1);
	setPinCs(TARGET_MASTER,GPIO_HIGH);
	delayms(30);

    EPD_Select_Master();
    spiTransmitCommand(R10_DTM);    
}

void ePaperPort::EpdType16001200_79_NT61522_DisplayNet(const uint8_t *imageData, size_t imageSize)
{
    // (void)imageData;
     //#define  max_l_dat  300   
     //uint8_t u8dat[max_l_dat];

     uint32_t u32posi;
     static const size_t expected_image_size = 1600U * 1200U / 2U;

#if 0
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
    /* 中文注释：       主屏区域总长度边界       0 ~ (1600*300 - 1) 属于 MASTER       >= 1600*300 属于 SLAVE    */
    const uint32_t master_limit = 1600U * 300U;//=480000;
    //const uint32_t master_limit = 16U * 30000U;//=480000;
    /* 中文注释：       参数保护       按你的描述 imageSize 范围为 1 ~ 900    */
    if (imageData == nullptr || imageSize != expected_image_size) {
        ESP_LOGE(TAG, "EPD 1600x1200 7.9 image size invalid input=%u expected=%u",
                 (unsigned int)imageSize,
                 (unsigned int)expected_image_size);
        return;
    }
    if(image_countger_ >=(480000+480000)){
        ESP_LOGE(TAG, "EPD 1600x1200 7.9 image exceeds display size");
        return;
    }

    size_t remain = imageSize;
    u32posi = 0;
    // memset(u8dat, epd_black,max_l_dat);
    while (remain > 0) {
        /* 中文注释：           单次最多处理 300        */
        //uint32_t chunk = (remain > 300) ? 300U : (uint32_t)remain;
        uint32_t chunk = (remain > 30000) ? 30000U : (uint32_t)remain;
        /* 中文注释：           情况1：当前还在 MASTER 区域        */

        //LOG_Cyan("image_countger_ %ld, master_limit %ld",image_countger_, master_limit);
        if (image_countger_ < master_limit) {
            /* 中文注释：               计算 MASTER 区域还剩多少空间            */
            uint32_t master_remain = master_limit - image_countger_;

            if (chunk <= master_remain) {
                /* 中文注释：                   当前这一段完全落在 MASTER 区域                */
               // printf("M1:chunk=%ld,image_countger_=%ld\r\n",chunk,image_countger_);
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
                    // printf("M2:master_remain=%ld,image_countger_=%ld\r\n",master_remain,image_countger_);
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
                       // printf("S2:slave_part=%ld,image_countger_=%ld\r\n",slave_part,image_countger_);
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
            //printf("S1:chunk=%ld,image_countger_=%ld\r\n",chunk,image_countger_);
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

uint8_t ePaperPort::NT61522_ReadTemperature() {

    setPinCsAll(0);
    spiTransmit(RE0_CCSET, NT61522_CCSET_V_CUR(), 1);
    setPinCsAll(1);
    delayms(10);

    NT61522_ReadRevision();

    setPinCs(TARGET_MASTER, 0);
    spiTransmitCommand(R40_TSC);
    delayms(10);
    EPD_Check_Busy_79(2);
    spiReceiveData(&Temptr[0], 2);
    setPinCs(TARGET_MASTER, 1);
    EPD_Check_Busy_79(2);

    Temptr[0] = Temptr[0] > 50 ? 48 : Temptr[0];
    return Temptr[0];
}
