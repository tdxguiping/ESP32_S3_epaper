#include "tdx_shared_spi.h"

#include "esp_log.h"
#include "freertos/semphr.h"

static const char *TAG = "tdx_shared_spi";
static SemaphoreHandle_t s_shared_spi_mutex;
static StaticSemaphore_t s_shared_spi_mutex_buffer;

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
        xSemaphoreGiveRecursive(s_shared_spi_mutex);
    }
}
