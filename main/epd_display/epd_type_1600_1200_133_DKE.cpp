#include "epd_type_1600_1200_133_DKE.h"

#include "display_bsp.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace {
constexpr const char *kTag = "epd_133_dke";
constexpr size_t kDkeImageSize = 1600U * 1200U / 2U;
constexpr size_t kDkeFrameSize = kDkeImageSize / 2U;
// English: Match the proven 13.3 XingTai transfer chunk to stay below the ESP-IDF SPI hardware limit.
// 中文：沿用已验证的 13.3 兴泰分包大小，避免超过 ESP-IDF SPI 硬件单次传输上限。
constexpr size_t kDkeSpiChunkSize = 30000U;
constexpr uint16_t kDkeBusyMaxLoops = 3000U;

constexpr uint8_t kCmdAnTm = 0x74;
constexpr uint8_t kCmdCmd66 = 0xF0;
constexpr uint8_t kCmdCmdA4 = 0xA4;
constexpr uint8_t kCmdBuckBoostVddn = 0xB0;
constexpr uint8_t kCmdTftVcomPower = 0xB1;
constexpr uint8_t kCmdEnBuf = 0xB6;
constexpr uint8_t kCmdBoostVddpEn = 0xB7;

constexpr uint8_t kPsr[] = {0xDF, 0x6B};
constexpr uint8_t kPwr[] = {0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38};
constexpr uint8_t kPof[] = {0x01};
constexpr uint8_t kPofsMaster[] = {0x00, 0xC0, 0x03, 0xA4};
constexpr uint8_t kPofsSlave[] = {0x00, 0xC0, 0x03, 0x95};
constexpr uint8_t kDrf[] = {0x00};
constexpr uint8_t kCdi[] = {0x37};
constexpr uint8_t kTcon[] = {0x03, 0x03};
constexpr uint8_t kTres[] = {0x04, 0xB0, 0x03, 0x20};
constexpr uint8_t kCmd66[] = {0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};
constexpr uint8_t kEnBuf[] = {0x07};
constexpr uint8_t kCcset[] = {0x01};
constexpr uint8_t kPws[] = {0x22};
constexpr uint8_t kAnTm[] = {0xC0, 0x1C, 0x1C, 0xCC, 0xCC, 0xCC, 0x15, 0x15, 0x55};
constexpr uint8_t kAgid[] = {0x10};
constexpr uint8_t kCmdA4[] = {0x03, 0x00, 0x01, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00};
constexpr uint8_t kBtstP[] = {0xD8, 0x18};
constexpr uint8_t kBoostVddpEn[] = {0x01};
constexpr uint8_t kBtstN[] = {0xD8, 0x18};
constexpr uint8_t kBuckBoostVddn[] = {0x01};
constexpr uint8_t kTftVcomPower[] = {0x02};
constexpr uint8_t kDcdc[] = {0x44, 0x54, 0x00};
constexpr uint8_t kPll[] = {0x08};
constexpr uint8_t kDslp[] = {0xA5};
}

void EpdType16001200_133_DKE_Display(ePaperPort &epd,
                                     const uint8_t *display_buf,
                                     size_t display_size)
{
    if (display_buf == nullptr || display_size != kDkeImageSize) {
        ESP_LOGE(kTag, "EPD 1600x1200 13.3 DKE rejected input=%u expected=%u",
                 (unsigned int)display_size,
                 (unsigned int)kDkeImageSize);
        return;
    }

    ESP_LOGI(kTag, "EPD 1600x1200 13.3 DKE display start size=%u",
             (unsigned int)display_size);
    epd.EpdType16001200_133_DKE_Init();
    if (!epd.EpdType16001200_133_DKE_NT61522_DisplayNet(display_buf, display_size)) {
        ESP_LOGE(kTag, "EPD 1600x1200 13.3 DKE display abort because frame write failed");
        EpdType_ReportDisplayFailure(ESP_FAIL);
        return;
    }
    epd.EpdType16001200_133_DKE_Update();
    ESP_LOGI(kTag, "EPD 1600x1200 13.3 DKE display done");
}

void ePaperPort::EpdType16001200_133_DKE_Sleep()
{
    ESP_LOGI(kTag, "EPD 1600x1200 13.3 DKE sleep");
    EpdType16001200_133_DKE_WriteCommandData(TARGET_BOTH, R07_DSLP, kDslp, sizeof(kDslp));
    delay_ms(100);
    Set_Power(0);
}

void ePaperPort::EpdType16001200_133_DKE_Init()
{
    int64_t start_us = esp_timer_get_time();
    ESP_LOGI(kTag, "EPD 1600x1200 13.3 DKE init start");

    EPD_Reset();
    EpdType16001200_133_DKE_WaitBusy("reset", 100);//  kDkeBusyMaxLoops

    EpdType16001200_133_DKE_WriteCommandData(TARGET_MASTER, kCmdAnTm, kAnTm, sizeof(kAnTm));
    EpdType16001200_133_DKE_WriteCommandData(TARGET_BOTH, kCmdCmd66, kCmd66, sizeof(kCmd66));
    EpdType16001200_133_DKE_WriteCommandData(TARGET_BOTH, R00_PSR, kPsr, sizeof(kPsr));
    EpdType16001200_133_DKE_WriteCommandData(TARGET_MASTER, RA5_DCDC, kDcdc, sizeof(kDcdc));
    EpdType16001200_133_DKE_WriteCommandData(TARGET_BOTH, R30_PLL, kPll, sizeof(kPll));
    EpdType16001200_133_DKE_WriteCommandData(TARGET_BOTH, R50_CDI, kCdi, sizeof(kCdi));
    EpdType16001200_133_DKE_WriteCommandData(TARGET_BOTH, R60_TCON, kTcon, sizeof(kTcon));
    EpdType16001200_133_DKE_WriteCommandData(TARGET_MASTER, R03_POFS, kPofsMaster, sizeof(kPofsMaster));
    EpdType16001200_133_DKE_WriteCommandData(TARGET_SLAVE, R03_POFS, kPofsSlave, sizeof(kPofsSlave));
    EpdType16001200_133_DKE_WriteCommandData(TARGET_BOTH, R86_AGID, kAgid, sizeof(kAgid));
    EpdType16001200_133_DKE_WriteCommandData(TARGET_BOTH, RE3_PWS, kPws, sizeof(kPws));
    EpdType16001200_133_DKE_WriteCommandData(TARGET_BOTH, RE0_CCSET, kCcset, sizeof(kCcset));
    EpdType16001200_133_DKE_WriteCommandData(TARGET_BOTH, R61_TRES, kTres, sizeof(kTres));
    EpdType16001200_133_DKE_WriteCommandData(TARGET_MASTER, kCmdCmdA4, kCmdA4, sizeof(kCmdA4));
    EpdType16001200_133_DKE_WriteCommandData(TARGET_MASTER, R01_PWR, kPwr, sizeof(kPwr));
    EpdType16001200_133_DKE_WriteCommandData(TARGET_MASTER, kCmdEnBuf, kEnBuf, sizeof(kEnBuf));
    EpdType16001200_133_DKE_WriteCommandData(TARGET_MASTER, R06_BTST_P, kBtstP, sizeof(kBtstP));
    EpdType16001200_133_DKE_WriteCommandData(TARGET_MASTER, kCmdBoostVddpEn, kBoostVddpEn, sizeof(kBoostVddpEn));
    EpdType16001200_133_DKE_WriteCommandData(TARGET_MASTER, R05_BTST_N, kBtstN, sizeof(kBtstN));
    EpdType16001200_133_DKE_WriteCommandData(TARGET_MASTER, kCmdBuckBoostVddn, kBuckBoostVddn, sizeof(kBuckBoostVddn));
    EpdType16001200_133_DKE_WriteCommandData(TARGET_MASTER, kCmdTftVcomPower, kTftVcomPower, sizeof(kTftVcomPower));

    ESP_LOGI(kTag, "EPD 1600x1200 13.3 DKE init done elapsed_ms=%lld",
             (long long)((esp_timer_get_time() - start_us) / 1000));
}

void ePaperPort::EpdType16001200_133_DKE_Display()
{
    if (!EnsureDispBuffer()) {
        ESP_LOGE(kTag, "EPD 1600x1200 13.3 DKE display buffer not ready");
        EpdType_ReportDisplayFailure(ESP_ERR_NO_MEM);
        return;
    }

    bool write_ok = EpdType16001200_133_DKE_NT61522_DisplayNet(DispBuffer, DisplayLen);
    ReleaseRotationBuffer();
    ReleaseDispBuffer();
    if (!write_ok) {
        ESP_LOGE(kTag, "EPD 1600x1200 13.3 DKE display buffer write failed");
        return;
    }
    EpdType16001200_133_DKE_Update();
}

bool ePaperPort::EpdType16001200_133_DKE_NT61522_DisplayNet(const uint8_t *imageData,
                                                            size_t imageSize)
{
    if (imageData == nullptr || imageSize != kDkeImageSize) {
        ESP_LOGE(kTag, "EPD 1600x1200 13.3 DKE image size invalid input=%u expected=%u",
                 (unsigned int)imageSize,
                 (unsigned int)kDkeImageSize);
        return false;
    }

    ESP_LOGI(kTag, "EPD 1600x1200 13.3 DKE master write start size=%u",
             (unsigned int)kDkeFrameSize);
    if (!EpdType16001200_133_DKE_WriteFrame(TARGET_MASTER, imageData, kDkeFrameSize)) {
        return false;
    }
    ESP_LOGI(kTag, "EPD 1600x1200 13.3 DKE master write done");

    ESP_LOGI(kTag, "EPD 1600x1200 13.3 DKE slave write start size=%u",
             (unsigned int)kDkeFrameSize);
    if (!EpdType16001200_133_DKE_WriteFrame(TARGET_SLAVE, imageData + kDkeFrameSize, kDkeFrameSize)) {
        return false;
    }
    ESP_LOGI(kTag, "EPD 1600x1200 13.3 DKE slave write done");
    return true;
}

void ePaperPort::EpdType16001200_133_DKE_Update()
{
    int64_t start_us = esp_timer_get_time();
    ESP_LOGI(kTag, "EPD 1600x1200 13.3 DKE update start");

    setPinCs(TARGET_BOTH, GPIO_LOW);
    spiTransmitCommand(R04_PON);
    setPinCs(TARGET_BOTH, GPIO_HIGH);
    EpdType16001200_133_DKE_WaitBusy("PON", kDkeBusyMaxLoops);
    ESP_LOGI(kTag, "EPD 1600x1200 13.3 DKE PON done");

    delay_ms(30);
    EpdType16001200_133_DKE_WriteCommandData(TARGET_BOTH, R12_DRF, kDrf, sizeof(kDrf));
    EpdType16001200_133_DKE_WaitBusy("DRF", kDkeBusyMaxLoops);
    ESP_LOGI(kTag, "EPD 1600x1200 13.3 DKE DRF done");

    delay_ms(30);
    EpdType16001200_133_DKE_WriteCommandData(TARGET_BOTH, R02_POF, kPof, sizeof(kPof));
    EpdType16001200_133_DKE_WaitBusy("POF", kDkeBusyMaxLoops);
    delay_ms(1000);
    Set_Power(0);

    ESP_LOGI(kTag, "EPD 1600x1200 13.3 DKE update done elapsed_ms=%lld",
             (long long)((esp_timer_get_time() - start_us) / 1000));
}

void ePaperPort::EpdType16001200_133_DKE_WriteCommandData(EP_Target_t target,
                                                          uint8_t command,
                                                          const uint8_t *data,
                                                          size_t len)
{
    setPinCs(target, GPIO_LOW);
    spiTransmit(command, data, len);
    setPinCs(target, GPIO_HIGH);
}

bool ePaperPort::EpdType16001200_133_DKE_WriteFrame(EP_Target_t target,
                                                    const uint8_t *data,
                                                    size_t len)
{
    if (data == nullptr || len == 0) {
        ESP_LOGE(kTag, "EPD 1600x1200 13.3 DKE frame invalid target=%u size=%u",
                 (unsigned int)target,
                 (unsigned int)len);
        return false;
    }

    // English: Keep command and frame data in one selected CS phase to match the DKE sample.
    // 中文：命令和帧数据保持在同一次片选低电平期间，尽量对齐 DKE 工厂例程的时序。
    setPinCs(target, GPIO_LOW);
    spiTransmitCommand(R10_DTM);
    Set_DCIOLevel(1);
    delay_us(1);

    const uint8_t *ptr = data;
    size_t remaining = len;
    while (remaining > 0) {
        size_t chunk = remaining > kDkeSpiChunkSize ? kDkeSpiChunkSize : remaining;
        esp_err_t ret = spiTransmitData(ptr, chunk);
        if (ret != ESP_OK) {
            ESP_LOGE(kTag, "EPD 1600x1200 13.3 DKE frame write failed target=%u chunk=%u remaining=%u ret=%s",
                     (unsigned int)target,
                     (unsigned int)chunk,
                     (unsigned int)remaining,
                     esp_err_to_name(ret));
            setPinCs(target, GPIO_HIGH);
            return false;
        }
        ptr += chunk;
        remaining -= chunk;
    }
    setPinCs(target, GPIO_HIGH);
    return true;
}

void ePaperPort::EpdType16001200_133_DKE_WaitBusy(const char *step, uint16_t max_loops)
{
    int64_t start_us = esp_timer_get_time();
    for (uint16_t i = 0; i <= max_loops; ++i) {
        if (Get_BusyIOLevel()) {
            ESP_LOGI(kTag, "EPD 1600x1200 13.3 DKE busy done step=%s loops=%u elapsed_ms=%lld",
                     step != nullptr ? step : "unknown",
                     (unsigned int)i,
                     (long long)((esp_timer_get_time() - start_us) / 1000));
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGE(kTag, "EPD 1600x1200 13.3 DKE busy timeout step=%s level=%d loops=%u elapsed_ms=%lld",
             step != nullptr ? step : "unknown",
             Get_BusyIOLevel(),
             (unsigned int)max_loops,
             (long long)((esp_timer_get_time() - start_us) / 1000));
    EpdType_ReportDisplayFailure(ESP_ERR_TIMEOUT);
}
