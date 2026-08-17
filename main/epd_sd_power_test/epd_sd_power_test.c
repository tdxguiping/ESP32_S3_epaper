#include "epd_sd_power_test.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "epd_display_app.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "file_serving_example_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "tdx_cfg.h"
#include "tdx_shared_spi.h"

static const char *TAG = "epd_sd_power_test";

#if USER_EPD_SD_POWER_TEST_ENABLE

static TaskHandle_t s_power_test_task;
static EventGroupHandle_t s_power_ready_event_group;
static uint32_t s_state = USER_EPD_SD_POWER_TEST_STATE_IDLE;
static uint32_t s_event_generation;
static uint32_t s_image_transfer_count;
static uint32_t s_slideshow_followup_count;
static uint32_t s_network_activity_count;
static uint32_t s_restore_failure_count;
static int64_t s_power_off_start_us;
static uint32_t s_last_power_off_ms;
static bool s_initialized;
static bool s_armed;

static void notify_activity(uint32_t event_bit)
{
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return;
    }
    (void)__atomic_add_fetch(&s_event_generation, 1U, __ATOMIC_ACQ_REL);
    TaskHandle_t task = __atomic_load_n(&s_power_test_task, __ATOMIC_ACQUIRE);
    if (task != NULL) {
        xTaskNotify(task, event_bit, eSetBits);
    }
}

static const char *wake_reason(uint32_t events)
{
    if ((events & USER_EPD_SD_POWER_TEST_EVENT_NETWORK_ACTIVITY_BIT) != 0U) {
        return "network";
    }
    if ((events & USER_EPD_SD_POWER_TEST_EVENT_CH583_BLE_DATA_BIT) != 0U) {
        return "ch583_ble_data";
    }
    if ((events & USER_EPD_SD_POWER_TEST_EVENT_EPD_REQUEST_BIT) != 0U) {
        return "epd_request";
    }
    if ((events & USER_EPD_SD_POWER_TEST_EVENT_SPI_BIT) != 0U) {
        return "shared_spi";
    }
    if ((events & USER_EPD_SD_POWER_TEST_EVENT_IMAGE_TRANSFER_BIT) != 0U) {
        return "image_transfer";
    }
    if ((events & USER_EPD_SD_POWER_TEST_EVENT_SLIDESHOW_FOLLOWUP_BIT) != 0U) {
        return "slideshow_followup";
    }
    return "timeout";
}

static esp_err_t restore_power_and_storage(uint32_t wake_events)
{
    bool power_was_off = s_power_off_start_us > 0;
    __atomic_store_n(&s_state, USER_EPD_SD_POWER_TEST_STATE_RESTORING, __ATOMIC_RELEASE);
    esp_err_t power_ret = ServerNetworkStaEpdDisplay_SetPower(true);
    if (power_ret != ESP_OK) {
        ESP_LOGE(TAG, "EPD/SD power restore failed reason=%s ret=%s",
                 wake_reason(wake_events), esp_err_to_name(power_ret));
        return power_ret;
    }

    if (s_power_off_start_us > 0) {
        s_last_power_off_ms =
            (uint32_t)((esp_timer_get_time() - s_power_off_start_us) / 1000LL);
        s_power_off_start_us = 0;
    }

    if (power_was_off) {
        vTaskDelay(pdMS_TO_TICKS(USER_EPD_SD_POWER_TEST_POWER_STABLE_MS));
    }

    esp_err_t lock_ret = TdxSharedSpi_LockForEpdSdPowerTest(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        ESP_LOGE(TAG, "EPD/SD storage restore SPI lock failed ret=%s",
                 esp_err_to_name(lock_ret));
        return lock_ret;
    }
    esp_err_t io_ret =
        ServerNetworkStaEpdDisplay_RestoreRailIoAfterPowerTestOn();
    if (io_ret != ESP_OK) {
        TdxSharedSpi_UnlockForEpdSdPowerTest();
        ESP_LOGE(TAG, "EPD/SD rail IO restore failed ret=%s",
                 esp_err_to_name(io_ret));
        return io_ret;
    }
    esp_err_t storage_ret = example_storage_remount_sd_for_epd_power_test();
    TdxSharedSpi_UnlockForEpdSdPowerTest();
    if (storage_ret != ESP_OK) {
        uint32_t failures = ++s_restore_failure_count;
        if (failures == 1U ||
            (failures % USER_EPD_SD_POWER_TEST_RESTORE_LOG_EVERY_COUNT) == 0U) {
            ESP_LOGE(TAG, "EPD/SD storage restore failed reason=%s ret=%s retry=%lu",
                     wake_reason(wake_events),
                     esp_err_to_name(storage_ret),
                     (unsigned long)failures);
        }
        return storage_ret;
    }

    s_restore_failure_count = 0U;
    xEventGroupSetBits(s_power_ready_event_group, USER_EPD_SD_POWER_TEST_READY_BIT);
    __atomic_store_n(&s_state, USER_EPD_SD_POWER_TEST_STATE_IDLE, __ATOMIC_RELEASE);
    if (power_was_off) {
        ESP_LOGI(TAG, "EPD/SD power restored reason=%s actual_off_ms=%lu",
                 wake_reason(wake_events), (unsigned long)s_last_power_off_ms);
    }
    s_last_power_off_ms = 0U;
    return ESP_OK;
}

static bool final_conditions_ready(uint32_t generation)
{
    return __atomic_load_n(&s_armed, __ATOMIC_ACQUIRE) &&
           __atomic_load_n(&s_image_transfer_count, __ATOMIC_ACQUIRE) == 0U &&
           __atomic_load_n(&s_slideshow_followup_count, __ATOMIC_ACQUIRE) == 0U &&
           __atomic_load_n(&s_network_activity_count, __ATOMIC_ACQUIRE) == 0U &&
           !ServerNetworkStaEpdDisplay_IsBusy() &&
           !TdxSharedSpi_HasNormalRequests() &&
           __atomic_load_n(&s_event_generation, __ATOMIC_ACQUIRE) == generation;
}

static void try_start_power_test(void)
{
    if (!__atomic_load_n(&s_armed, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&s_image_transfer_count, __ATOMIC_ACQUIRE) != 0U ||
        __atomic_load_n(&s_slideshow_followup_count, __ATOMIC_ACQUIRE) != 0U ||
        __atomic_load_n(&s_network_activity_count, __ATOMIC_ACQUIRE) != 0U ||
        ServerNetworkStaEpdDisplay_IsBusy()) {
        return;
    }

    uint32_t generation = __atomic_load_n(&s_event_generation, __ATOMIC_ACQUIRE);
    esp_err_t lock_ret = TdxSharedSpi_LockForEpdSdPowerTest(0);
    if (lock_ret != ESP_OK) {
        return;
    }

    if (!final_conditions_ready(generation)) {
        TdxSharedSpi_UnlockForEpdSdPowerTest();
        return;
    }

    __atomic_store_n(&s_state, USER_EPD_SD_POWER_TEST_STATE_PREPARING, __ATOMIC_RELEASE);
    xEventGroupClearBits(s_power_ready_event_group, USER_EPD_SD_POWER_TEST_READY_BIT);

    esp_err_t storage_ret = example_storage_unmount_sd_for_epd_power_test();
    if (storage_ret != ESP_OK) {
        __atomic_store_n(&s_armed, false, __ATOMIC_RELEASE);
        ESP_LOGE(TAG, "EPD/SD power test canceled because SD unmount failed ret=%s",
                 esp_err_to_name(storage_ret));
        // The IDF unmount API may already have consumed the SD card handle before
        // reporting a VFS cleanup error. Keep normal users blocked and enter the
        // existing remount retry path instead of exposing a possibly stale handle.
        (void)restore_power_and_storage(0U);
        TdxSharedSpi_UnlockForEpdSdPowerTest();
        return;
    }

    if (!final_conditions_ready(generation)) {
        ESP_LOGW(TAG, "EPD/SD power test canceled by activity during final check");
        esp_err_t restore_ret =
            restore_power_and_storage(USER_EPD_SD_POWER_TEST_EVENT_SPI_BIT);
        if (restore_ret == ESP_OK) {
            __atomic_store_n(&s_state, USER_EPD_SD_POWER_TEST_STATE_ARMED, __ATOMIC_RELEASE);
        } else {
            __atomic_store_n(&s_armed, false, __ATOMIC_RELEASE);
        }
        TdxSharedSpi_UnlockForEpdSdPowerTest();
        return;
    }

    esp_err_t io_ret =
        ServerNetworkStaEpdDisplay_PrepareRailIoForPowerTestOff();
    if (io_ret != ESP_OK) {
        ESP_LOGE(TAG, "EPD/SD power test canceled because rail IO isolation failed ret=%s",
                 esp_err_to_name(io_ret));
        esp_err_t restore_ret = restore_power_and_storage(0U);
        __atomic_store_n(&s_armed, false, __ATOMIC_RELEASE);
        if (restore_ret != ESP_OK) {
            ESP_LOGE(TAG, "EPD/SD rail IO isolation rollback failed ret=%s",
                     esp_err_to_name(restore_ret));
        }
        TdxSharedSpi_UnlockForEpdSdPowerTest();
        return;
    }

    // Activity can arrive while SD is being unmounted or the SPI pins are being
    // isolated. Recheck immediately before GPIO4 goes low so a pending request
    // restores the prepared resources without briefly removing rail power.
    if (!final_conditions_ready(generation)) {
        ESP_LOGW(TAG, "EPD/SD power test canceled by activity after IO isolation");
        esp_err_t restore_ret = restore_power_and_storage(0U);
        if (restore_ret == ESP_OK) {
            __atomic_store_n(&s_state, USER_EPD_SD_POWER_TEST_STATE_ARMED, __ATOMIC_RELEASE);
        } else {
            __atomic_store_n(&s_armed, false, __ATOMIC_RELEASE);
        }
        TdxSharedSpi_UnlockForEpdSdPowerTest();
        return;
    }

    esp_err_t power_ret = ServerNetworkStaEpdDisplay_SetPower(false);
    if (power_ret != ESP_OK) {
        ESP_LOGE(TAG, "EPD/SD power off failed ret=%s", esp_err_to_name(power_ret));
        (void)restore_power_and_storage(0U);
        __atomic_store_n(&s_armed, false, __ATOMIC_RELEASE);
        TdxSharedSpi_UnlockForEpdSdPowerTest();
        return;
    }

    __atomic_store_n(&s_armed, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_state, USER_EPD_SD_POWER_TEST_STATE_POWER_OFF, __ATOMIC_RELEASE);
    s_power_off_start_us = esp_timer_get_time();
    TdxSharedSpi_UnlockForEpdSdPowerTest();
    ESP_LOGI(TAG, "EPD/SD power off test started duration_ms=%u",
             (unsigned int)USER_EPD_SD_POWER_TEST_OFF_TIME_MS);

    uint32_t wake_events = 0U;
    (void)xTaskNotifyWait(0U,
                          UINT32_MAX,
                          &wake_events,
                          pdMS_TO_TICKS(USER_EPD_SD_POWER_TEST_OFF_TIME_MS));
    (void)restore_power_and_storage(wake_events);
}

static void epd_sd_power_test_task(void *arg)
{
    (void)arg;
    uint32_t events = 0U;

    for (;;) {
        uint32_t state = __atomic_load_n(&s_state, __ATOMIC_ACQUIRE);
        TickType_t wait_ticks = portMAX_DELAY;
        if (__atomic_load_n(&s_armed, __ATOMIC_ACQUIRE)) {
            wait_ticks = pdMS_TO_TICKS(USER_EPD_SD_POWER_TEST_RECHECK_MS);
        } else if (state == USER_EPD_SD_POWER_TEST_STATE_RESTORING) {
            wait_ticks = pdMS_TO_TICKS(USER_EPD_SD_POWER_TEST_RESTORE_RETRY_MS);
        }
        (void)xTaskNotifyWait(0U, UINT32_MAX, &events, wait_ticks);
        if (__atomic_load_n(&s_state, __ATOMIC_ACQUIRE) ==
            USER_EPD_SD_POWER_TEST_STATE_RESTORING) {
            (void)restore_power_and_storage(events);
            continue;
        }
        try_start_power_test();
    }
}

#endif

esp_err_t EpdSdPowerTest_Init(void)
{
#if USER_EPD_SD_POWER_TEST_ENABLE
    if (__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return ESP_OK;
    }

    example_storage_type_t storage_type = example_storage_get_type();
    if (storage_type == EXAMPLE_STORAGE_TYPE_UNKNOWN) {
        ESP_LOGW(TAG, "EPD/SD power test disabled because storage type is unknown");
        return ESP_ERR_INVALID_STATE;
    }

    s_power_ready_event_group = xEventGroupCreate();
    if (s_power_ready_event_group == NULL) {
        ESP_LOGE(TAG, "create EPD/SD power test event group failed");
        return ESP_ERR_NO_MEM;
    }
    xEventGroupSetBits(s_power_ready_event_group, USER_EPD_SD_POWER_TEST_READY_BIT);

    BaseType_t task_ret = xTaskCreate(epd_sd_power_test_task,
                                      "epd_sd_power",
                                      USER_EPD_SD_POWER_TEST_TASK_STACK_SIZE,
                                      NULL,
                                      USER_EPD_SD_POWER_TEST_TASK_PRIORITY,
                                      &s_power_test_task);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "create EPD/SD power test task failed");
        vEventGroupDelete(s_power_ready_event_group);
        s_power_ready_event_group = NULL;
        return ESP_ERR_NO_MEM;
    }

    __atomic_store_n(&s_initialized, true, __ATOMIC_RELEASE);
    ESP_LOGI(TAG, "independent EPD/SD power test initialized off_ms=%u storage=%s",
             (unsigned int)USER_EPD_SD_POWER_TEST_OFF_TIME_MS,
             storage_type == EXAMPLE_STORAGE_TYPE_SD_CARD ? "sd" : "spiffs");
#endif
    return ESP_OK;
}

esp_err_t EpdSdPowerTest_PrepareForSharedSpi(void)
{
#if USER_EPD_SD_POWER_TEST_ENABLE
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE) ||
        xTaskGetCurrentTaskHandle() == __atomic_load_n(&s_power_test_task, __ATOMIC_ACQUIRE)) {
        return ESP_OK;
    }

    bool armed = __atomic_load_n(&s_armed, __ATOMIC_ACQUIRE);
    uint32_t state = __atomic_load_n(&s_state, __ATOMIC_ACQUIRE);
    if (!armed && state == USER_EPD_SD_POWER_TEST_STATE_IDLE) {
        return ESP_OK;
    }

    notify_activity(USER_EPD_SD_POWER_TEST_EVENT_SPI_BIT);
    EventBits_t ready = xEventGroupWaitBits(s_power_ready_event_group,
                                             USER_EPD_SD_POWER_TEST_READY_BIT,
                                             pdFALSE,
                                             pdTRUE,
                                             pdMS_TO_TICKS(USER_EPD_SD_POWER_TEST_READY_TIMEOUT_MS));
    if ((ready & USER_EPD_SD_POWER_TEST_READY_BIT) == 0U) {
        ESP_LOGE(TAG, "shared SPI wait for EPD/SD power restore timed out state=%lu",
                 (unsigned long)__atomic_load_n(&s_state, __ATOMIC_ACQUIRE));
        return ESP_ERR_TIMEOUT;
    }
#endif
    return ESP_OK;
}

bool EpdSdPowerTest_IsReadyForImmediateSharedSpi(void)
{
#if USER_EPD_SD_POWER_TEST_ENABLE
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return true;
    }
    uint32_t state = __atomic_load_n(&s_state, __ATOMIC_ACQUIRE);
    return state == USER_EPD_SD_POWER_TEST_STATE_IDLE ||
           state == USER_EPD_SD_POWER_TEST_STATE_ARMED;
#else
    return true;
#endif
}

bool EpdSdPowerTest_IsTransitionBusy(void)
{
#if USER_EPD_SD_POWER_TEST_ENABLE
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return false;
    }
    uint32_t state = __atomic_load_n(&s_state, __ATOMIC_ACQUIRE);
    return state == USER_EPD_SD_POWER_TEST_STATE_PREPARING ||
           state == USER_EPD_SD_POWER_TEST_STATE_RESTORING;
#else
    return false;
#endif
}

void EpdSdPowerTest_NetworkBegin(void)
{
#if USER_EPD_SD_POWER_TEST_ENABLE
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return;
    }
    (void)__atomic_add_fetch(&s_network_activity_count, 1U, __ATOMIC_ACQ_REL);
    notify_activity(USER_EPD_SD_POWER_TEST_EVENT_NETWORK_ACTIVITY_BIT);
    EventBits_t ready = xEventGroupWaitBits(
        s_power_ready_event_group,
        USER_EPD_SD_POWER_TEST_READY_BIT,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(USER_EPD_SD_POWER_TEST_READY_TIMEOUT_MS));
    if ((ready & USER_EPD_SD_POWER_TEST_READY_BIT) == 0U) {
        ESP_LOGE(TAG, "network wait for EPD/SD power restore timed out state=%lu",
                 (unsigned long)__atomic_load_n(&s_state, __ATOMIC_ACQUIRE));
    }
#endif
}

void EpdSdPowerTest_NetworkEnd(void)
{
#if USER_EPD_SD_POWER_TEST_ENABLE
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return;
    }
    uint32_t count = __atomic_load_n(&s_network_activity_count, __ATOMIC_ACQUIRE);
    if (count == 0U) {
        ESP_LOGE(TAG, "network activity power-test count underflow");
        return;
    }
    (void)__atomic_sub_fetch(&s_network_activity_count, 1U, __ATOMIC_ACQ_REL);
    notify_activity(USER_EPD_SD_POWER_TEST_EVENT_NETWORK_ACTIVITY_BIT);
#endif
}

void EpdSdPowerTest_OnCh583BleDataReceived(void)
{
#if USER_EPD_SD_POWER_TEST_ENABLE
    notify_activity(USER_EPD_SD_POWER_TEST_EVENT_CH583_BLE_DATA_BIT);
#endif
}

void EpdSdPowerTest_OnEpdTaskRequested(void)
{
#if USER_EPD_SD_POWER_TEST_ENABLE
    notify_activity(USER_EPD_SD_POWER_TEST_EVENT_EPD_REQUEST_BIT);
#endif
}

void EpdSdPowerTest_OnEpdJobDone(void)
{
#if USER_EPD_SD_POWER_TEST_ENABLE
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return;
    }
    __atomic_store_n(&s_armed, true, __ATOMIC_RELEASE);
    __atomic_store_n(&s_state, USER_EPD_SD_POWER_TEST_STATE_ARMED, __ATOMIC_RELEASE);
    notify_activity(USER_EPD_SD_POWER_TEST_EVENT_EPD_DONE_BIT);
#endif
}

void EpdSdPowerTest_ImageTransferBegin(void)
{
#if USER_EPD_SD_POWER_TEST_ENABLE
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return;
    }
    (void)__atomic_add_fetch(&s_image_transfer_count, 1U, __ATOMIC_ACQ_REL);
    notify_activity(USER_EPD_SD_POWER_TEST_EVENT_IMAGE_TRANSFER_BIT);
#endif
}

void EpdSdPowerTest_ImageTransferEnd(void)
{
#if USER_EPD_SD_POWER_TEST_ENABLE
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return;
    }
    uint32_t count = __atomic_load_n(&s_image_transfer_count, __ATOMIC_ACQUIRE);
    if (count == 0U) {
        ESP_LOGE(TAG, "image transfer power-test count underflow");
        return;
    }
    (void)__atomic_sub_fetch(&s_image_transfer_count, 1U, __ATOMIC_ACQ_REL);
    notify_activity(USER_EPD_SD_POWER_TEST_EVENT_IMAGE_TRANSFER_BIT);
#endif
}

void EpdSdPowerTest_SlideshowFollowupBegin(void)
{
#if USER_EPD_SD_POWER_TEST_ENABLE
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return;
    }
    // A slideshow immediately saves progress and preloads the next SD image after
    // synchronous EPD completion. Reserve that follow-up before the EPD can finish
    // so the test task cannot power-cycle the rail in the short scheduling gap.
    (void)__atomic_add_fetch(&s_slideshow_followup_count, 1U, __ATOMIC_ACQ_REL);
    notify_activity(USER_EPD_SD_POWER_TEST_EVENT_SLIDESHOW_FOLLOWUP_BIT);
#endif
}

void EpdSdPowerTest_SlideshowFollowupEnd(void)
{
#if USER_EPD_SD_POWER_TEST_ENABLE
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return;
    }
    uint32_t count = __atomic_load_n(&s_slideshow_followup_count, __ATOMIC_ACQUIRE);
    if (count == 0U) {
        ESP_LOGE(TAG, "slideshow follow-up power-test count underflow");
        return;
    }
    (void)__atomic_sub_fetch(&s_slideshow_followup_count, 1U, __ATOMIC_ACQ_REL);
    notify_activity(USER_EPD_SD_POWER_TEST_EVENT_SLIDESHOW_FOLLOWUP_BIT);
#endif
}
