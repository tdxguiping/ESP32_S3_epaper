#include "epd_type_1600_1200_133_DKE.h"

#include <cstring>

#include "display_bsp.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "tdx_shared_spi.h"

namespace {
constexpr const char *kTag = "epd_133_dke";
constexpr size_t kDkeImageSize = 1600U * 1200U / 2U;
constexpr size_t kDkeFrameSize = kDkeImageSize / 2U;
// Use the shared safe chunk so all panel paths have the same DMA allocation margin.
constexpr size_t kDkeSpiChunkSize = USER_EPD_SPI_SAFE_DMA_TX_CHUNK;
constexpr uint32_t kDkeResetBusyTimeoutMs = 2000U;
constexpr uint32_t kDkePonBusyTimeoutMs = 10000U;
constexpr uint32_t kDkeDrfBusyTimeoutMs = 60000U;
constexpr uint32_t kDkePofBusyTimeoutMs = 10000U;
constexpr uint32_t kDkeBusyPollMs = 10U;

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

void LogBusyDone(const char *step, uint32_t polls, int64_t start_us)
{
    int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
    if (step != nullptr && std::strcmp(step, "DRF") == 0) {
        ESP_LOGI(kTag, "EPD 1600x1200 13.3 DKE busy done step=%s polls=%lu elapsed_ms=%lld",
                 step, (unsigned long)polls, (long long)elapsed_ms);
    } else {
        ESP_LOGD(kTag, "EPD 1600x1200 13.3 DKE busy done step=%s polls=%lu elapsed_ms=%lld",
                 step != nullptr ? step : "unknown",
                 (unsigned long)polls,
                 (long long)elapsed_ms);
    }
}
}

void EpdType16001200_133_DKE_Display(ePaperPort &epd,
                                     const uint8_t *display_buf,
                                     size_t display_size)
{
    int64_t start_us = esp_timer_get_time();
    if (display_buf == nullptr || display_size != kDkeImageSize) {
        ESP_LOGE(kTag, "EPD 1600x1200 13.3 DKE rejected input=%u expected=%u",
                 (unsigned int)display_size,
                 (unsigned int)kDkeImageSize);
        EpdType_ReportDisplayFailure(ESP_ERR_INVALID_SIZE);
        return;
    }

    ESP_LOGI(kTag, "EPD 1600x1200 13.3 DKE display start size=%u",
             (unsigned int)display_size);
    esp_err_t ret = epd.EpdType16001200_133_DKE_Init();
    if (ret != ESP_OK) {
        EpdType_ReportDisplayFailure(ret);
        ESP_LOGE(kTag, "EPD 1600x1200 13.3 DKE display failed step=init ret=%s total_ms=%lld",
                 esp_err_to_name(ret),
                 (long long)((esp_timer_get_time() - start_us) / 1000));
        return;
    }
    if (!epd.EpdType16001200_133_DKE_NT61522_DisplayNet(display_buf, display_size)) {
        ESP_LOGE(kTag, "EPD 1600x1200 13.3 DKE display abort because frame write failed");
        EpdType_ReportDisplayFailure(ESP_FAIL);
        return;
    }
    ret = epd.EpdType16001200_133_DKE_Update();
    if (ret != ESP_OK) {
        EpdType_ReportDisplayFailure(ret);
        return;
    }
    ESP_LOGI(kTag, "EPD 1600x1200 13.3 DKE display done result=ESP_OK total_ms=%lld",
             (long long)((esp_timer_get_time() - start_us) / 1000));
}

void ePaperPort::EpdType16001200_133_DKE_Sleep()
{
    ESP_LOGI(kTag, "EPD 1600x1200 13.3 DKE sleep");
    EpdType16001200_133_DKE_WriteCommandData(TARGET_BOTH, R07_DSLP, kDslp, sizeof(kDslp));
    delay_ms(100);
}

esp_err_t ePaperPort::EpdType16001200_133_DKE_Init()
{
    int64_t start_us = esp_timer_get_time();
    ESP_LOGI(kTag, "EPD 1600x1200 13.3 DKE init start");

    EPD_Reset();
    esp_err_t ret = EpdType16001200_133_DKE_WaitBusyUnlockSpi("reset",
                                                              kDkeResetBusyTimeoutMs);
    if (ret != ESP_OK) {
        EpdType_ReportDisplayFailure(ret);
        return ret;
    }

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
    return ESP_OK;
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
        EpdType_ReportDisplayFailure(ESP_FAIL);
        return;
    }
    esp_err_t ret = EpdType16001200_133_DKE_Update();
    if (ret != ESP_OK) {
        EpdType_ReportDisplayFailure(ret);
    }
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

esp_err_t ePaperPort::EpdType16001200_133_DKE_Update()
{
    int64_t start_us = esp_timer_get_time();
    ESP_LOGI(kTag, "EPD 1600x1200 13.3 DKE update start");
    esp_err_t ret = ESP_OK;

    setPinCs(TARGET_BOTH, GPIO_LOW);
    spiTransmitCommand(R04_PON);
    setPinCs(TARGET_BOTH, GPIO_HIGH);
    ret = EpdType16001200_133_DKE_WaitBusyUnlockSpi("PON", kDkePonBusyTimeoutMs);
    if (ret != ESP_OK) {
        goto power_off;
    }

    delay_ms(30);
    EpdType16001200_133_DKE_WriteCommandData(TARGET_BOTH, R12_DRF, kDrf, sizeof(kDrf));
    ret = EpdType16001200_133_DKE_WaitBusyUnlockSpi("DRF", kDkeDrfBusyTimeoutMs);
    if (ret != ESP_OK) {
        goto power_off;
    }

    delay_ms(30);

power_off:
    EpdType16001200_133_DKE_WriteCommandData(TARGET_BOTH, R02_POF, kPof, sizeof(kPof));
    {
        esp_err_t power_off_ret =
            EpdType16001200_133_DKE_WaitBusyUnlockSpi("POF", kDkePofBusyTimeoutMs);
        if (ret == ESP_OK) {
            ret = power_off_ret;
        }
    }
    delay_ms(1000);
    if (ret == ESP_OK) {
        ESP_LOGI(kTag, "EPD 1600x1200 13.3 DKE update done result=ESP_OK elapsed_ms=%lld",
                 (long long)((esp_timer_get_time() - start_us) / 1000));
    }
    return ret;
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

esp_err_t ePaperPort::EpdType16001200_133_DKE_WaitBusy(const char *step,
                                                       uint32_t timeout_ms)
{
    int cs1_level = getGpioLevel(cs_);
    int cs2_level = getGpioLevel(cs_2_);
    int epd2_cs_level = getGpioLevel(EPD2_CS_PIN);
    bool cs_low = (cs1_level == GPIO_LOW) ||
                  (cs2_level == GPIO_LOW) ||
                  (epd2_cs_level == GPIO_LOW);

    if (cs_low) {
        return EpdType16001200_133_DKE_WaitBusyLocked(step, timeout_ms);
    }
    return EpdType16001200_133_DKE_WaitBusyUnlockSpi(step, timeout_ms);
}

esp_err_t ePaperPort::EpdType16001200_133_DKE_WaitBusyLocked(const char *step,
                                                             uint32_t timeout_ms)
{
    int64_t start_us = esp_timer_get_time();
    int cs1_level = getGpioLevel(cs_);
    int cs2_level = getGpioLevel(cs_2_);
    int epd2_cs_level = getGpioLevel(EPD2_CS_PIN);

    ESP_LOGD(kTag, "EPD 1600x1200 13.3 DKE busy path=locked step=%s cs=%d,%d,%d",
             step != nullptr ? step : "unknown",
             cs1_level,
             cs2_level,
             epd2_cs_level);

    uint32_t polls = 0;
    while ((uint64_t)(esp_timer_get_time() - start_us) <
           (uint64_t)timeout_ms * 1000ULL) {
        if (Get_BusyIOLevel()) {
            LogBusyDone(step, polls, start_us);
            return ESP_OK;
        }
        ++polls;
        vTaskDelay(pdMS_TO_TICKS(kDkeBusyPollMs));
    }
    if (Get_BusyIOLevel()) {
        LogBusyDone(step, polls, start_us);
        return ESP_OK;
    }

    ESP_LOGE(kTag, "EPD 1600x1200 13.3 DKE busy timeout step=%s level=%d timeout_ms=%lu elapsed_ms=%lld",
             step != nullptr ? step : "unknown",
             Get_BusyIOLevel(),
             (unsigned long)timeout_ms,
             (long long)((esp_timer_get_time() - start_us) / 1000));
    return ESP_ERR_TIMEOUT;
}

esp_err_t ePaperPort::EpdType16001200_133_DKE_WaitBusyUnlockSpi(const char *step,
                                                                uint32_t timeout_ms)
{
    int64_t start_us = esp_timer_get_time();
    int cs1_level = getGpioLevel(cs_);
    int cs2_level = getGpioLevel(cs_2_);
    int epd2_cs_level = getGpioLevel(EPD2_CS_PIN);

    ESP_LOGD(kTag, "EPD 1600x1200 13.3 DKE busy path=unlock_spi step=%s cs=%d,%d,%d",
             step != nullptr ? step : "unknown",
             cs1_level,
             cs2_level,
             epd2_cs_level);

    auto relock_or_restart = [step]() -> esp_err_t {
        esp_err_t lock_ret = TdxSharedSpi_Lock(pdMS_TO_TICKS(10000));
        if (lock_ret == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGE(kTag, "EPD 1600x1200 13.3 DKE shared SPI relock timeout step=%s ret=%s, restart",
                 step != nullptr ? step : "unknown", esp_err_to_name(lock_ret));
        esp_restart();
        return lock_ret;
    };

    setGpioLevel(cs_, GPIO_HIGH);
    setGpioLevel(cs_2_, GPIO_HIGH);
    setGpioLevel(EPD2_CS_PIN, GPIO_HIGH);
    TdxSharedSpi_Unlock();

    esp_err_t wait_ret = ESP_ERR_TIMEOUT;
    uint32_t polls = 0;
    while ((uint64_t)(esp_timer_get_time() - start_us) <
           (uint64_t)timeout_ms * 1000ULL) {
        if (Get_BusyIOLevel()) {
            wait_ret = ESP_OK;
            break;
        }
        ++polls;
        vTaskDelay(pdMS_TO_TICKS(kDkeBusyPollMs));
    }
    if (wait_ret != ESP_OK && Get_BusyIOLevel()) {
        wait_ret = ESP_OK;
    }

    esp_err_t lock_ret = relock_or_restart();
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }
    if (wait_ret == ESP_OK) {
        LogBusyDone(step, polls, start_us);
        return ESP_OK;
    }

    ESP_LOGE(kTag, "EPD 1600x1200 13.3 DKE busy timeout step=%s level=%d timeout_ms=%lu elapsed_ms=%lld",
             step != nullptr ? step : "unknown",
             Get_BusyIOLevel(),
             (unsigned long)timeout_ms,
             (long long)((esp_timer_get_time() - start_us) / 1000));
    return ESP_ERR_TIMEOUT;
}
