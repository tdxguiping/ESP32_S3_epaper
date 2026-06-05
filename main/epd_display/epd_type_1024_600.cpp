#include "epd_type_1024_600.h"
#include "display_bsp.h"
void EpdType1024600_Display(ePaperPort &epd, const uint8_t *display_buf, size_t display_size)
{
    static const size_t expected_image_size = 1024U * 600U / 2U;
    if (display_buf == nullptr || display_size != expected_image_size) {
        ESP_LOGE("Display", "EPD 1024x600 rejected input=%u expected=%u",
                 (unsigned int)display_size,
                 (unsigned int)expected_image_size);
        return;
    }

    epd.EPD_Init();
    epd.NT61522_Init_display();
    epd.NT61522_Display_net(display_buf, display_size);
    epd.NT61522_Display();
}

void ePaperPort::EpdType1024600_Sleep()
{

}

void ePaperPort::EpdType1024600_Init()
{
    EPD_initial();
}

void ePaperPort::EpdType1024600_Display()
{
    if (!EnsureDispBuffer()) {
        ESP_LOGE(TAG, "EPD_Display 1024x600 aborted, DispBuffer not ready");
        return;
    }

    uint32_t i = 0;
    uint8_t j = 0;
    spi_transaction_t t;

    EPD_WriteCMD_ToBoth(0x10);  	
    Set_DCIOLevel(1);
    for (i = 0; i < (uint32_t)DisplayLen; i += 256) {
        if (j == 0) {
            EPD_Select_Master();  
            memset(&t, 0, sizeof(t));
            t.length    = 8 * 256;
            t.tx_buffer = DispBuffer + i;
            spi_device_polling_transmit(spi, &t); //Transmit!
            j = 1;
        } else {
            EPD_Select_Slave();
            memset(&t, 0, sizeof(t));
            t.length    = 8 * 256;
            t.tx_buffer = DispBuffer + i;
            spi_device_polling_transmit(spi, &t); //Transmit!
            j = 0;
        }
    }
    EPD_Select_None();

    EPD_WriteCMD_ToBoth(0x04);
    delay_ms(100);
    EPD_Check_Busy();

    EPD_WriteCMD_ToBoth(0x12);
    EPD_WriteDATA_ToBoth(0x00);
    delay_ms(100);
    EPD_Check_Busy();

    EPD_WriteCMD_ToBoth(R02_POF);
    EPD_WriteDATA_ToBoth(0x00);
    delay_ms(100);
    EPD_Check_Busy();
    ReleaseRotationBuffer();
    ReleaseDispBuffer();
}

void ePaperPort::EpdType1024600_NT61522_Display()
{

    EPD_Select_None();
     //=====================================================
    EPD_WriteCMD_ToBoth(0x04);
    delay_ms(100);
    EPD_Check_Busy();

    EPD_WriteCMD_ToBoth(0x12);
    EPD_WriteDATA_ToBoth(0x00);
    delay_ms(100);
    EPD_Check_Busy();

    EPD_WriteCMD_ToBoth(R02_POF);
    EPD_WriteDATA_ToBoth(0x00);
    delay_ms(100);
    EPD_Check_Busy();
}

void ePaperPort::EpdType1024600_NT61522_InitDisplay()
{
    //EPD_WriteCMD_ToBoth(0x10);  	
    image_countger_ =0;
    u8flag_ = 0xAA;
    EPD_Select_Master();
    spiTransmitCommand(R10_DTM);  // 0x10
    Set_DCIOLevel(1);    

}

void ePaperPort::EpdType1024600_NT61522_DisplayNet(const uint8_t *imageData, size_t imageSize)
{
     #define  max_l_dat  300   
     uint32_t u32posi;
     static const size_t expected_image_size = 1024U * 600U / 2U;

    /* 中文注释：       主屏区域总长度边界       0 ~ (1600*300 - 1) 属于 MASTER       >= 1600*300 属于 SLAVE    */
    const uint32_t master_limit = 1024U * 150U;//=307200;
    /* 中文注释：       参数保护       按你的描述 imageSize 范围为 1 ~ 900    */
    if (imageData == nullptr || imageSize != expected_image_size) {
        ESP_LOGE(TAG, "EPD 1024x600 image size invalid input=%u expected=%u",
                 (unsigned int)imageSize,
                 (unsigned int)expected_image_size);
        return;
    }
    if(image_countger_ >=307200U){
        ESP_LOGE(TAG, "EPD 1024x600 image exceeds display size");
        return;
    }

   //  LOG_Cyan("imageSize=%d",imageSize);

    size_t remain = imageSize;
    u32posi = 0;

    // memset(u8dat, epd_black,max_l_dat);
    while (remain > 0) {
        /* 中文注释：           单次最多处理 300        */
        uint32_t chunk = (remain > 300) ? 300U : (uint32_t)remain;
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
                        spiTransmitCommand(R10_DTM);
                        {
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


  #if 0
    spi_transaction_t t;       
    // LOG_Purple("1024x600 %s>%d  DisplayLen=%d",__func__,__LINE__,DisplayLen);
    
    /* 中文注释：       参数保护    */
    if (imageData == nullptr || imageSize == 0)
    {
        return;
    }

    /* 中文注释：
       统计总输入字节数
    */
    if(image_countger_ >=(1024*300))
    {
        ESP_LOGE(TAG, "EPD 1024x600 data exceeds display size");
        return;
    }

   // printf("\r\n L=%ld =%d  =%d \r\n",image_countger_,imageSize,s_net_buf_len+imageSize);
    //image_countger_ += (uint32_t)imageSize;

    /* 中文注释：
       src_index 表示当前处理到输入数据的哪个位置
    */
    size_t src_index = 0;
    
    while (src_index < imageSize)
    {
        /* 中文注释：
           先计算缓存区还剩多少空间
        */
        size_t free_space = 256 - s_net_buf_len;

        /* 中文注释：
           本轮最多拷贝的数据量
           要么填满缓存，要么把剩余输入拷完
        */
        size_t copy_len = imageSize - src_index;
        if (copy_len > free_space)
        {
            copy_len = free_space;
        }

        /* 中文注释：
           把输入数据拷贝到缓存尾部
        */
        memcpy(&s_net_buf[s_net_buf_len], &imageData[src_index], copy_len);
        s_net_buf_len += copy_len;
        src_index += copy_len;

        /* 中文注释：
           只要缓存满 256，就立刻发送
        */
        if (s_net_buf_len >= 256)
        {
            if (s_send_to_a)
            {                
               // printf("M> ");
               // memset(s_net_buf, epd_red,256);

                EPD_Select_Master();  
                memset(&t, 0, sizeof(t));
                t.length    = 8*256;
                t.tx_buffer = s_net_buf;
                spi_device_polling_transmit(spi, &t); //Transmit!
            }
            else
            {
                //printf("S> ");
                //memset(s_net_buf, epd_red,256);

                EPD_Select_Slave();
                memset(&t, 0, sizeof(t));
                t.length    = 8*256;
                t.tx_buffer = s_net_buf;            
                spi_device_polling_transmit(spi, &t); //Transmit!
            }

            image_countger_ += 256;
            if(image_countger_ >=(1024*300))
            {
                ESP_LOGE(TAG, "EPD 1024x600 data exceeds display size");
                return;
            }
            // else
            // {
            //   printf("L=%ld\r\n",image_countger_);                
            // }

            /* 中文注释：
               一包发完后，缓存清空
               因为这里设计成“凑满 256 就立即发”
            */
            s_net_buf_len = 0;

            /* 中文注释：
               下一包切换到另一边
            */
            s_send_to_a = !s_send_to_a;
        }
    }

 //   printf("over %d\r\n",s_net_buf_len);                

    /* 中文注释：
       如果函数结束时 s_net_buf_len < 256
       说明还有残留数据，继续保留，等下次再凑满 256 后发送
    */
   #endif
}

void ePaperPort::EPD_initial(void) {


    EPD_Reset();
    EPD_WriteCMD_ToBoth(0xAA);
    EPD_WriteDATA_ToBoth(0x49);
    EPD_WriteDATA_ToBoth(0x55);
    EPD_WriteDATA_ToBoth(0x20);
    EPD_WriteDATA_ToBoth(0x08);
    EPD_WriteDATA_ToBoth(0x09);
    EPD_WriteDATA_ToBoth(0x18);

    EPD_WriteCMD_ToBoth(R00_PSR);
    EPD_WriteDATA_ToBoth(0x5F);
    EPD_WriteDATA_ToBoth(0x69);
    EPD_Check_Busy();

    EPD_WriteCMD_ToBoth(0xE0);
    EPD_WriteDATA_ToBoth(0x01);
    EPD_Check_Busy();

    EPD_WriteCMD_ToBoth(R01_PWR);
    EPD_WriteDATA_ToBoth(0x3F);

    EPD_WriteCMD_ToBoth(R03_POFS);
    EPD_WriteDATA_ToBoth(0x00);
    EPD_WriteDATA_ToBoth(0x54);
    EPD_WriteDATA_ToBoth(0x00);
    EPD_WriteDATA_ToBoth(0x44);

    EPD_WriteCMD_ToBoth(R05_BTST1);
    EPD_WriteDATA_ToBoth(0x40);
    EPD_WriteDATA_ToBoth(0x1F);
    EPD_WriteDATA_ToBoth(0x1F);
    EPD_WriteDATA_ToBoth(0x2C);

    EPD_WriteCMD_ToBoth(R06_BTST2);
    EPD_WriteDATA_ToBoth(0x6F);
    EPD_WriteDATA_ToBoth(0x1F);
    EPD_WriteDATA_ToBoth(0x16);
    EPD_WriteDATA_ToBoth(0x25);

    EPD_WriteCMD_ToBoth(R08_BTST3);
    EPD_WriteDATA_ToBoth(0x6F);
    EPD_WriteDATA_ToBoth(0x1F);
    EPD_WriteDATA_ToBoth(0x1F);
    EPD_WriteDATA_ToBoth(0x22);

    EPD_WriteCMD_ToBoth(R13_IPC);
    EPD_WriteDATA_ToBoth(0x00);
    EPD_WriteDATA_ToBoth(0x04);

    EPD_WriteCMD_ToBoth(0x30);
    EPD_WriteDATA_ToBoth(0x08);

    EPD_WriteCMD_ToBoth(R41_TSE);
    EPD_WriteDATA_ToBoth(0x00);

    EPD_WriteCMD_ToBoth(R50_CDI);
    EPD_WriteDATA_ToBoth(0x3F);

    EPD_WriteCMD_ToBoth(R60_TCON);
    EPD_WriteDATA_ToBoth(0x02);
    EPD_WriteDATA_ToBoth(0x00);

    EPD_WriteCMD_ToBoth(R61_TRES);
    EPD_WriteDATA_ToBoth(0x02);
    EPD_WriteDATA_ToBoth(0x00);
    EPD_WriteDATA_ToBoth(0x02);
    EPD_WriteDATA_ToBoth(0x58);
    EPD_Check_Busy();

    EPD_WriteCMD_ToBoth(0x84);
    EPD_WriteDATA_ToBoth(0x01);

    EPD_WriteCMD_ToBoth(R86_AGID);
    EPD_WriteDATA_ToBoth(0x00);

    EPD_WriteCMD_ToBoth(RE3_PWS);
    EPD_WriteDATA_ToBoth(0x2F);

    EPD_WriteCMD_ToBoth(0xE5);
    EPD_WriteDATA_ToBoth(0x00);
}
