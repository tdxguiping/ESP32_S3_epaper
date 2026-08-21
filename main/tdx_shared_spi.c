#include "tdx_shared_spi.h"

#include "esp_log.h"
#include "epd_sd_power_test.h"
#include "freertos/semphr.h"
#include "tdx_cfg.h"
#include <stdbool.h>

static const char *TAG = "tdx_shared_spi";
static SemaphoreHandle_t s_shared_spi_mutex;
static StaticSemaphore_t s_shared_spi_mutex_buffer;
static uint32_t s_normal_spi_request_count;

esp_err_t TdxSharedSpi_Init(void)
{
    if (s_shared_spi_mutex == NULL) {
        s_shared_spi_mutex = xSemaphoreCreateRecursiveMutexStatic(&s_shared_spi_mutex_buffer);
        if (s_shared_spi_mutex == NULL) {
            ESP_LOGE(TAG, "create shared SPI mutex failed");
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

esp_err_t TdxSharedSpi_Lock(TickType_t ticks_to_wait)
{
    // Count before the power-ready hook. This closes the scheduling window where a
    // caller has requested SPI but has not reached the mutex take operation yet.
    (void)__atomic_add_fetch(&s_normal_spi_request_count, 1U, __ATOMIC_ACQ_REL);
#if USER_EPD_SD_POWER_TEST_ENABLE
    // Every normal shared-SPI request must wait for the EPD/SD rail before the caller
    // can touch either device. The dedicated lock below lets the rail-cycle task
    // perform its final idle check without reporting itself as normal activity.
    esp_err_t power_ret = EpdSdPowerTest_PrepareForSharedSpi();
    if (power_ret != ESP_OK) {
        (void)__atomic_sub_fetch(&s_normal_spi_request_count, 1U, __ATOMIC_ACQ_REL);
        return power_ret;
    }
#endif

    esp_err_t lock_ret = TdxSharedSpi_LockForEpdSdPowerTest(ticks_to_wait);
    if (lock_ret != ESP_OK) {
        (void)__atomic_sub_fetch(&s_normal_spi_request_count, 1U, __ATOMIC_ACQ_REL);
    }
    return lock_ret;
}

esp_err_t TdxSharedSpi_LockForEpdSdPowerTest(TickType_t ticks_to_wait)
{
    esp_err_t init_ret = TdxSharedSpi_Init();
    if (init_ret != ESP_OK) {
        return init_ret;
    }
    if (xSemaphoreTakeRecursive(s_shared_spi_mutex, ticks_to_wait) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void TdxSharedSpi_Unlock(void)
{
    if (s_shared_spi_mutex != NULL) {
        if (xSemaphoreGiveRecursive(s_shared_spi_mutex) != pdTRUE) {
            ESP_LOGE(TAG, "release normal shared SPI mutex failed");
            return;
        }
        uint32_t count = __atomic_load_n(&s_normal_spi_request_count, __ATOMIC_ACQUIRE);
        if (count == 0U) {
            ESP_LOGE(TAG, "normal shared SPI request count underflow");
        } else {
            (void)__atomic_sub_fetch(&s_normal_spi_request_count, 1U, __ATOMIC_ACQ_REL);
        }
    }
}

void TdxSharedSpi_UnlockForEpdSdPowerTest(void)
{
    if (s_shared_spi_mutex != NULL) {
        if (xSemaphoreGiveRecursive(s_shared_spi_mutex) != pdTRUE) {
            ESP_LOGE(TAG, "release EPD/SD rail-cycle SPI mutex failed");
        }
    }
}

bool TdxSharedSpi_HasNormalRequests(void)
{
    return __atomic_load_n(&s_normal_spi_request_count, __ATOMIC_ACQUIRE) > 0U;
}

bool TdxSharedSpi_IsBusy(void)
{
    return TdxSharedSpi_HasNormalRequests();
}
