#include "epd_type_1208_1600_1243_boe.h"

#include <cstring>

#include "display_bsp.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "tdx_shared_spi.h"

namespace {
constexpr const char *kTag = "epd_1243_boe";
constexpr size_t kBytesPerLine = 1208U / 2U;
constexpr size_t kLineCount = 1600U;
constexpr size_t kImageSize = kBytesPerLine * kLineCount;
static_assert(kBytesPerLine == 604U, "BOE 12.43 bytes per line must remain 604");
static_assert(kImageSize == 966400U, "BOE 12.43 frame size must remain 966400");
constexpr uint32_t kResetBusyTimeoutMs = 2000U;
constexpr uint32_t kPowerOnBusyTimeoutMs = 10000U;
constexpr uint32_t kRefreshBusyTimeoutMs = 60000U;
constexpr uint32_t kPowerOffBusyTimeoutMs = 10000U;
constexpr uint32_t kBusyPollMs = 10U;
constexpr uint32_t kSharedSpiRelockTimeoutMs = 10000U;

constexpr uint8_t kCmdReadId = 0x70;
constexpr uint8_t kCmdDataStart = 0x10;
constexpr uint8_t kCmdPowerOn = 0x04;
constexpr uint8_t kCmdRefresh = 0x12;
constexpr uint8_t kCmdPowerOff = 0x02;
constexpr uint8_t kCmdDeepSleep = 0x07;
constexpr uint8_t kRefreshData[] = {0x00};
constexpr uint8_t kPowerOffData[] = {0x00};
constexpr uint8_t kDeepSleepData[] = {0xA5};
constexpr uint8_t kExpectedChipId[] = {0x12, 0x82, 0x01};

const char *TargetName(EP_Target_t target)
{
    return target == TARGET_MASTER ? "CSB-M" : "CSB-S";
}
}  // namespace

class Boe1243Bl79703Driver {
public:
    explicit Boe1243Bl79703Driver(ePaperPort &epd) : epd_(epd) {}

    esp_err_t Display(const uint8_t *image, size_t image_size)
    {
        if (image == nullptr || image_size != kImageSize) {
            ESP_LOGE(kTag, "BOE 12.43 rejected input=%u expected=%u",
                     (unsigned int)image_size,
                     (unsigned int)kImageSize);
            return ESP_ERR_INVALID_SIZE;
        }

        const int64_t start_us = esp_timer_get_time();
        ESP_LOGI(kTag,
                 "BOE 12.43 display start size=%u mode=vendor_full_frame line_bytes=%u lines=%u",
                 (unsigned int)image_size,
                 (unsigned int)kBytesPerLine,
                 (unsigned int)kLineCount);

        esp_err_t ret = Initialize();
        if (ret == ESP_OK) {
            // Follow the BOE sample: select CSB-M once, send 0x10 once, then
            // stream all 604 bytes per line for 1600 lines without splitting.
            ret = WriteVendorFullFrame(image, kImageSize);
        }
        if (ret == ESP_OK) {
            ret = RefreshAndSleep();
        }

        if (ret != ESP_OK) {
            ESP_LOGE(kTag, "BOE 12.43 display failed ret=%s elapsed_ms=%lld",
                     esp_err_to_name(ret),
                     (long long)((esp_timer_get_time() - start_us) / 1000));
            return ret;
        }

        ESP_LOGI(kTag, "BOE 12.43 display done elapsed_ms=%lld",
                 (long long)((esp_timer_get_time() - start_us) / 1000));
        return ESP_OK;
    }

private:
    void SelectNone()
    {
        epd_.setGpioLevel(epd_.cs_, GPIO_HIGH);
        epd_.setGpioLevel(epd_.cs_2_, GPIO_HIGH);
    }

    void SelectTarget(EP_Target_t target)
    {
        // Drive both chip selects explicitly, matching the DKE selection rule.
        switch (target) {
        case TARGET_MASTER:
            epd_.setGpioLevel(epd_.cs_, GPIO_LOW);
            epd_.setGpioLevel(epd_.cs_2_, GPIO_HIGH);
            break;
        case TARGET_SLAVE:
            epd_.setGpioLevel(epd_.cs_, GPIO_HIGH);
            epd_.setGpioLevel(epd_.cs_2_, GPIO_LOW);
            break;
        case TARGET_BOTH:
            epd_.setGpioLevel(epd_.cs_, GPIO_LOW);
            epd_.setGpioLevel(epd_.cs_2_, GPIO_LOW);
            break;
        default:
            SelectNone();
            break;
        }
    }

    esp_err_t Initialize()
    {
        esp_err_t ret = epd_.Set_Power(1);
        if (ret != ESP_OK) {
            return ret;
        }

        // BL79703 requires a 200 ms rail stabilization before reset and SPI access.
        epd_.delay_ms(200);
        SelectNone();
        epd_.setGpioLevel(epd_.rst_, GPIO_HIGH);
        epd_.delay_ms(20);
        epd_.setGpioLevel(epd_.rst_, GPIO_LOW);
        epd_.delay_ms(20);
        epd_.setGpioLevel(epd_.rst_, GPIO_HIGH);
        epd_.delay_ms(20);

        ret = WaitBusyUnlockSpi("reset", kResetBusyTimeoutMs);
        if (ret != ESP_OK) {
            return ret;
        }
        epd_.delay_ms(10);

        ret = ReadAndVerifyChipId(TARGET_MASTER, true);
        if (ret != ESP_OK) {
            return ret;
        }

        // Read CSB-S for diagnostics, but keep refresh available because the
        // vendor sample documents only the mandatory master ID verification.
        (void)ReadAndVerifyChipId(TARGET_SLAVE, false);
        return ESP_OK;
    }

    esp_err_t ReadAndVerifyChipId(EP_Target_t target, bool required)
    {
        uint8_t chip_id[sizeof(kExpectedChipId)] = {};
        SelectTarget(target);
        esp_err_t ret = epd_.spiTransmitCommand(kCmdReadId);
        if (ret == ESP_OK) {
            ret = epd_.spiReceiveData(chip_id, sizeof(chip_id));
        }
        SelectNone();
        if (ret != ESP_OK) {
            if (required) {
                ESP_LOGE(kTag, "BOE 12.43 chip ID read failed target=%s ret=%s, abort",
                         TargetName(target), esp_err_to_name(ret));
            } else {
                ESP_LOGW(kTag, "BOE 12.43 chip ID read failed target=%s ret=%s, continue",
                         TargetName(target), esp_err_to_name(ret));
            }
            return ret;
        }
        if (std::memcmp(chip_id, kExpectedChipId, sizeof(kExpectedChipId)) != 0) {
            if (required) {
                ESP_LOGE(kTag,
                         "BOE 12.43 chip ID mismatch target=%s id=%02X,%02X,%02X expected=12,82,01, abort",
                         TargetName(target), chip_id[0], chip_id[1], chip_id[2]);
            } else {
                ESP_LOGW(kTag,
                         "BOE 12.43 chip ID mismatch target=%s id=%02X,%02X,%02X expected=12,82,01, continue",
                         TargetName(target), chip_id[0], chip_id[1], chip_id[2]);
            }
            return ESP_ERR_INVALID_RESPONSE;
        }

        ESP_LOGI(kTag, "BOE 12.43 chip ID target=%s id=%02X,%02X,%02X",
                 TargetName(target), chip_id[0], chip_id[1], chip_id[2]);
        return ESP_OK;
    }

    esp_err_t WriteVendorFullFrame(const uint8_t *data, size_t length)
    {
        if (data == nullptr || length != kImageSize) {
            ESP_LOGE(kTag, "BOE 12.43 full frame rejected size=%u expected=%u",
                     (unsigned int)length,
                     (unsigned int)kImageSize);
            return ESP_ERR_INVALID_SIZE;
        }

        ESP_LOGI(kTag, "BOE 12.43 full frame write start target=CSB-M size=%u",
                 (unsigned int)length);
        SelectTarget(TARGET_MASTER);
        esp_err_t ret = epd_.spiTransmitCommand(kCmdDataStart);
        if (ret == ESP_OK) {
            // Reuse the common SPI DMA path; it performs the configured safe chunking.
            ret = epd_.spiTransmitData(data, length);
        }
        SelectNone();
        if (ret != ESP_OK) {
            ESP_LOGE(kTag, "BOE 12.43 full frame write failed target=CSB-M ret=%s",
                     esp_err_to_name(ret));
            return ret;
        }

        ESP_LOGI(kTag, "BOE 12.43 full frame write done target=CSB-M");
        return ESP_OK;
    }

    esp_err_t RefreshAndSleep()
    {
        esp_err_t ret = WriteCommandData(TARGET_BOTH, kCmdPowerOn, nullptr, 0);
        if (ret == ESP_OK) {
            ret = WaitBusyUnlockSpi("PON", kPowerOnBusyTimeoutMs);
        }
        if (ret == ESP_OK) {
            ret = WriteCommandData(TARGET_BOTH,
                                   kCmdRefresh,
                                   kRefreshData,
                                   sizeof(kRefreshData));
        }
        if (ret == ESP_OK) {
            ret = WaitBusyUnlockSpi("DRF", kRefreshBusyTimeoutMs);
        }

        // Always attempt the vendor power-off sequence after power-on/refresh.
        esp_err_t power_off_ret = WriteCommandData(TARGET_BOTH,
                                                    kCmdPowerOff,
                                                    kPowerOffData,
                                                    sizeof(kPowerOffData));
        if (power_off_ret == ESP_OK) {
            power_off_ret = WaitBusyUnlockSpi("POF", kPowerOffBusyTimeoutMs);
        }
        if (ret == ESP_OK) {
            ret = power_off_ret;
        }

        esp_err_t sleep_ret = WriteCommandData(TARGET_BOTH,
                                               kCmdDeepSleep,
                                               kDeepSleepData,
                                               sizeof(kDeepSleepData));
        epd_.delay_ms(50);
        if (ret == ESP_OK) {
            ret = sleep_ret;
        }
        return ret;
    }

    esp_err_t WriteCommandData(EP_Target_t target,
                               uint8_t command,
                               const uint8_t *data,
                               size_t length)
    {
        SelectTarget(target);
        esp_err_t ret = epd_.spiTransmitCommand(command);
        if (ret == ESP_OK && data != nullptr && length > 0) {
            ret = epd_.spiTransmitData(data, length);
        }
        SelectNone();
        return ret;
    }

    esp_err_t WaitBusyUnlockSpi(const char *step, uint32_t timeout_ms)
    {
        const int64_t start_us = esp_timer_get_time();
        SelectNone();
        TdxSharedSpi_Unlock();

        esp_err_t wait_ret = ESP_ERR_TIMEOUT;
        uint32_t polls = 0;
        while ((uint64_t)(esp_timer_get_time() - start_us) <
               (uint64_t)timeout_ms * 1000ULL) {
            if (epd_.Get_BusyIOLevel() == GPIO_HIGH) {
                wait_ret = ESP_OK;
                break;
            }
            ++polls;
            vTaskDelay(pdMS_TO_TICKS(kBusyPollMs));
        }
        if (wait_ret != ESP_OK && epd_.Get_BusyIOLevel() == GPIO_HIGH) {
            wait_ret = ESP_OK;
        }

        esp_err_t lock_ret = TdxSharedSpi_Lock(pdMS_TO_TICKS(kSharedSpiRelockTimeoutMs));
        if (lock_ret != ESP_OK) {
            ESP_LOGE(kTag, "BOE 12.43 shared SPI relock failed step=%s ret=%s, restart",
                     step, esp_err_to_name(lock_ret));
            esp_restart();
            return lock_ret;
        }
        if (wait_ret != ESP_OK) {
            ESP_LOGE(kTag,
                     "BOE 12.43 busy timeout step=%s level=%u timeout_ms=%lu polls=%lu",
                     step,
                     (unsigned int)epd_.Get_BusyIOLevel(),
                     (unsigned long)timeout_ms,
                     (unsigned long)polls);
        }
        return wait_ret;
    }

    ePaperPort &epd_;
};

bool EpdType12081600_1243_BOE_FillTestPattern(uint8_t *display_buf,
                                              size_t display_size)
{
    static constexpr uint8_t kSolidColorBytes[] = {
        0x00, 0x11, 0x22, 0x33, 0x55, 0x66
    };
    if (display_buf == nullptr || display_size != kImageSize) {
        ESP_LOGE(kTag, "BOE 12.43 test pattern rejected size=%u expected=%u",
                 (unsigned int)display_size,
                 (unsigned int)kImageSize);
        return false;
    }

    // Fill the complete native frame directly and keep every color boundary
    // aligned to a 604-byte display line.
    for (size_t i = 0; i < sizeof(kSolidColorBytes); ++i) {
        const size_t start_line = (kLineCount * i) / sizeof(kSolidColorBytes);
        const size_t end_line = (kLineCount * (i + 1U)) / sizeof(kSolidColorBytes);
        std::memset(display_buf + (start_line * kBytesPerLine),
                    kSolidColorBytes[i],
                    (end_line - start_line) * kBytesPerLine);
    }

    ESP_LOGI(kTag,
             "BOE 12.43 test pattern ready size=%u line_bytes=%u lines=%u colors=00,11,22,33,55,66",
             (unsigned int)kImageSize,
             (unsigned int)kBytesPerLine,
             (unsigned int)kLineCount);
    return true;
}

void EpdType12081600_1243_BOE_Display(ePaperPort &epd,
                                      const uint8_t *display_buf,
                                      size_t display_size)
{
    Boe1243Bl79703Driver driver(epd);
    const esp_err_t ret = driver.Display(display_buf, display_size);
    if (ret != ESP_OK) {
        EpdType_ReportDisplayFailure(ret);
    }
}
