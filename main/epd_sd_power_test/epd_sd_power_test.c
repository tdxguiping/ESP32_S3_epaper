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
#include "freertos/portmacro.h"
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
static bool s_power_cycle_restore_pending;
static bool s_initialized;
static bool s_armed;
static bool s_cycle_required_after_restore;
static bool s_post_display_decision_pending;
static bool s_immediate_power_off_committed;
static portMUX_TYPE s_decision_mux = portMUX_INITIALIZER_UNLOCKED;

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
    if (power_was_off) {
        int64_t off_us = esp_timer_get_time() - s_power_off_start_us;
        int64_t required_us =
            (int64_t)USER_EPD_SD_POWER_TEST_OFF_TIME_MS * 1000LL;
        if (off_us < required_us) {
            ESP_LOGE(TAG,
                     "EPD/SD restore blocked because mandatory off time is short actual_us=%lld required_us=%lld",
                     (long long)off_us,
                     (long long)required_us);
            return ESP_ERR_INVALID_STATE;
        }
        s_last_power_off_ms = (uint32_t)(off_us / 1000LL);
    }
    esp_err_t power_ret = ServerNetworkStaEpdDisplay_SetPower(true);
    if (power_ret != ESP_OK) {
        ESP_LOGE(TAG, "EPD/SD power restore failed reason=%s ret=%s",
                 wake_reason(wake_events), esp_err_to_name(power_ret));
        return power_ret;
    }

    if (power_was_off) {
        s_power_off_start_us = 0;
        s_power_cycle_restore_pending = true;
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
    bool retry_cycle = __atomic_exchange_n(&s_cycle_required_after_restore,
                                            false,
                                            __ATOMIC_ACQ_REL);
    xEventGroupSetBits(s_power_ready_event_group, USER_EPD_SD_POWER_TEST_READY_BIT);
    __atomic_store_n(&s_armed, retry_cycle, __ATOMIC_RELEASE);
    __atomic_store_n(&s_state,
                     retry_cycle ? USER_EPD_SD_POWER_TEST_STATE_ARMED
                                 : USER_EPD_SD_POWER_TEST_STATE_IDLE,
                     __ATOMIC_RELEASE);
    if (s_power_cycle_restore_pending) {
        ESP_LOGI(TAG, "EPD/SD mandatory power cycle complete reason=%s actual_off_ms=%lu",
                 wake_reason(wake_events), (unsigned long)s_last_power_off_ms);
    } else if (retry_cycle) {
        ESP_LOGW(TAG, "EPD/SD preparation restored; mandatory power cycle will retry");
    }
    s_power_cycle_restore_pending = false;
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

static uint32_t wait_for_mandatory_power_off_interval(void)
{
    uint32_t all_events = 0U;
    bool activity_logged = false;
    const int64_t required_us =
        (int64_t)USER_EPD_SD_POWER_TEST_OFF_TIME_MS * 1000LL;

    for (;;) {
        int64_t elapsed_us = esp_timer_get_time() - s_power_off_start_us;
        if (elapsed_us >= required_us) {
            return all_events;
        }

        int64_t remaining_us = required_us - elapsed_us;
        uint32_t remaining_ms = (uint32_t)((remaining_us + 999LL) / 1000LL);
        TickType_t wait_ticks = pdMS_TO_TICKS(remaining_ms);
        if (wait_ticks == 0) {
            wait_ticks = 1;
        }

        uint32_t events = 0U;
        (void)xTaskNotifyWait(0U, UINT32_MAX, &events, wait_ticks);
        all_events |= events;
        if (events != 0U && !activity_logged) {
            activity_logged = true;
            ESP_LOGW(TAG,
                     "activity waits for mandatory EPD/SD off interval reason=%s target_ms=%u",
                     wake_reason(events),
                     (unsigned int)USER_EPD_SD_POWER_TEST_OFF_TIME_MS);
        }
    }
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
        __atomic_store_n(&s_cycle_required_after_restore, true, __ATOMIC_RELEASE);
        __atomic_store_n(&s_armed, false, __ATOMIC_RELEASE);
        ESP_LOGE(TAG, "EPD/SD power cycle canceled because SD unmount failed ret=%s",
                 esp_err_to_name(storage_ret));
        // The IDF unmount API may already have consumed the SD card handle before
        // reporting a VFS cleanup error. Keep normal users blocked and enter the
        // existing remount retry path instead of exposing a possibly stale handle.
        (void)restore_power_and_storage(0U);
        TdxSharedSpi_UnlockForEpdSdPowerTest();
        return;
    }

    if (!final_conditions_ready(generation)) {
        ESP_LOGW(TAG, "EPD/SD power cycle postponed by activity during final check");
        __atomic_store_n(&s_cycle_required_after_restore, true, __ATOMIC_RELEASE);
        __atomic_store_n(&s_armed, false, __ATOMIC_RELEASE);
        (void)restore_power_and_storage(USER_EPD_SD_POWER_TEST_EVENT_SPI_BIT);
        TdxSharedSpi_UnlockForEpdSdPowerTest();
        return;
    }

    esp_err_t io_ret =
        ServerNetworkStaEpdDisplay_PrepareRailIoForPowerTestOff();
    if (io_ret != ESP_OK) {
        ESP_LOGE(TAG, "EPD/SD power cycle canceled because rail IO isolation failed ret=%s",
                 esp_err_to_name(io_ret));
        __atomic_store_n(&s_cycle_required_after_restore, true, __ATOMIC_RELEASE);
        __atomic_store_n(&s_armed, false, __ATOMIC_RELEASE);
        esp_err_t restore_ret = restore_power_and_storage(0U);
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
        ESP_LOGW(TAG, "EPD/SD power cycle postponed by activity after IO isolation");
        __atomic_store_n(&s_cycle_required_after_restore, true, __ATOMIC_RELEASE);
        __atomic_store_n(&s_armed, false, __ATOMIC_RELEASE);
        (void)restore_power_and_storage(0U);
        TdxSharedSpi_UnlockForEpdSdPowerTest();
        return;
    }

    esp_err_t power_ret = ServerNetworkStaEpdDisplay_SetPower(false);
    if (power_ret != ESP_OK) {
        ESP_LOGE(TAG, "EPD/SD power off failed ret=%s", esp_err_to_name(power_ret));
        __atomic_store_n(&s_cycle_required_after_restore, true, __ATOMIC_RELEASE);
        __atomic_store_n(&s_armed, false, __ATOMIC_RELEASE);
        (void)restore_power_and_storage(0U);
        TdxSharedSpi_UnlockForEpdSdPowerTest();
        return;
    }

    s_power_off_start_us = esp_timer_get_time();
    __atomic_store_n(&s_armed, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_state, USER_EPD_SD_POWER_TEST_STATE_POWER_OFF, __ATOMIC_RELEASE);
    TdxSharedSpi_UnlockForEpdSdPowerTest();
    ESP_LOGI(TAG, "EPD/SD mandatory power cycle started target_off_ms=%u",
             (unsigned int)USER_EPD_SD_POWER_TEST_OFF_TIME_MS);

    uint32_t wake_events = wait_for_mandatory_power_off_interval();
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
        events = 0U;
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
        ESP_LOGE(TAG, "mandatory EPD/SD power cycle unavailable because storage type is unknown");
        return ESP_ERR_INVALID_STATE;
    }

    s_power_ready_event_group = xEventGroupCreate();
    if (s_power_ready_event_group == NULL) {
        ESP_LOGE(TAG, "create EPD/SD power-cycle event group failed");
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
        ESP_LOGE(TAG, "create EPD/SD power-cycle task failed");
        vEventGroupDelete(s_power_ready_event_group);
        s_power_ready_event_group = NULL;
        return ESP_ERR_NO_MEM;
    }

    __atomic_store_n(&s_initialized, true, __ATOMIC_RELEASE);
    ESP_LOGI(TAG, "mandatory EPD/SD stay-awake power cycle initialized off_ms=%u storage=%s",
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
    if (!armed &&
        (state == USER_EPD_SD_POWER_TEST_STATE_IDLE ||
         state == USER_EPD_SD_POWER_TEST_STATE_WAIT_DECISION ||
         state == USER_EPD_SD_POWER_TEST_STATE_POWER_OFF_COMMITTED)) {
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
           state == USER_EPD_SD_POWER_TEST_STATE_ARMED ||
           state == USER_EPD_SD_POWER_TEST_STATE_WAIT_DECISION ||
           state == USER_EPD_SD_POWER_TEST_STATE_POWER_OFF_COMMITTED;
#else
    return true;
#endif
}

bool EpdSdPowerTest_IsMandatoryCyclePending(void)
{
#if USER_EPD_SD_POWER_TEST_ENABLE
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return false;
    }
    uint32_t state = __atomic_load_n(&s_state, __ATOMIC_ACQUIRE);
    return __atomic_load_n(&s_armed, __ATOMIC_ACQUIRE) ||
           __atomic_load_n(&s_cycle_required_after_restore, __ATOMIC_ACQUIRE) ||
           state == USER_EPD_SD_POWER_TEST_STATE_PREPARING ||
           state == USER_EPD_SD_POWER_TEST_STATE_POWER_OFF ||
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
        ESP_LOGE(TAG, "network activity rail-cycle count underflow");
        return;
    }
    (void)__atomic_sub_fetch(&s_network_activity_count, 1U, __ATOMIC_ACQ_REL);
    notify_activity(USER_EPD_SD_POWER_TEST_EVENT_NETWORK_ACTIVITY_BIT);
#endif
}

esp_err_t EpdSdPowerTest_NetworkTryBegin(void)
{
#if USER_EPD_SD_POWER_TEST_ENABLE
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return ESP_OK;
    }
    (void)__atomic_add_fetch(&s_network_activity_count, 1U, __ATOMIC_ACQ_REL);
    notify_activity(USER_EPD_SD_POWER_TEST_EVENT_NETWORK_ACTIVITY_BIT);
    if (!EpdSdPowerTest_IsReadyForImmediateSharedSpi()) {
        EpdSdPowerTest_NetworkEnd();
        return ESP_ERR_INVALID_STATE;
    }
#endif
    return ESP_OK;
}

esp_err_t EpdSdPowerTest_NetworkTryBeginNoWake(void)
{
#if USER_EPD_SD_POWER_TEST_ENABLE
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return ESP_OK;
    }

    // Publish the reservation generation without waking the rail task. This keeps
    // the final pre-off race closed while repeated HTTP BUSY probes cannot force
    // early restore retries or shorten the mandatory GPIO4-low interval.
    (void)__atomic_add_fetch(&s_network_activity_count, 1U, __ATOMIC_ACQ_REL);
    (void)__atomic_add_fetch(&s_event_generation, 1U, __ATOMIC_ACQ_REL);
    if (!EpdSdPowerTest_IsReadyForImmediateSharedSpi()) {
        (void)__atomic_sub_fetch(&s_network_activity_count, 1U, __ATOMIC_ACQ_REL);
        return ESP_ERR_INVALID_STATE;
    }
#endif
    return ESP_OK;
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
    portENTER_CRITICAL(&s_decision_mux);
    __atomic_store_n(&s_post_display_decision_pending, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_immediate_power_off_committed, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_armed, true, __ATOMIC_RELEASE);
    __atomic_store_n(&s_state, USER_EPD_SD_POWER_TEST_STATE_ARMED, __ATOMIC_RELEASE);
    portEXIT_CRITICAL(&s_decision_mux);
    notify_activity(USER_EPD_SD_POWER_TEST_EVENT_EPD_DONE_BIT);
#endif
}

void EpdSdPowerTest_OnEpdJobDoneAwaitDecision(void)
{
#if USER_EPD_SD_POWER_TEST_ENABLE
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return;
    }
    portENTER_CRITICAL(&s_decision_mux);
    __atomic_store_n(&s_armed, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_post_display_decision_pending, true, __ATOMIC_RELEASE);
    __atomic_store_n(&s_immediate_power_off_committed, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_state,
                     USER_EPD_SD_POWER_TEST_STATE_WAIT_DECISION,
                     __ATOMIC_RELEASE);
    portEXIT_CRITICAL(&s_decision_mux);
    notify_activity(USER_EPD_SD_POWER_TEST_EVENT_EPD_DONE_BIT);
    ESP_LOGI(TAG, "EPD completed, waiting for stay-awake or immediate-power-off decision");
#endif
}

esp_err_t EpdSdPowerTest_CommitImmediatePowerOff(void)
{
#if USER_EPD_SD_POWER_TEST_ENABLE
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_decision_mux);
    bool pending =
        __atomic_load_n(&s_post_display_decision_pending, __ATOMIC_ACQUIRE);
    uint32_t state = __atomic_load_n(&s_state, __ATOMIC_ACQUIRE);
    if (!pending) {
        bool cycle_active =
            __atomic_load_n(&s_armed, __ATOMIC_ACQUIRE) ||
            state == USER_EPD_SD_POWER_TEST_STATE_PREPARING ||
            state == USER_EPD_SD_POWER_TEST_STATE_POWER_OFF ||
            state == USER_EPD_SD_POWER_TEST_STATE_RESTORING;
        portEXIT_CRITICAL(&s_decision_mux);
        return cycle_active ? ESP_ERR_INVALID_STATE : ESP_OK;
    }
    if (state != USER_EPD_SD_POWER_TEST_STATE_WAIT_DECISION) {
        portEXIT_CRITICAL(&s_decision_mux);
        return ESP_ERR_INVALID_STATE;
    }

    __atomic_store_n(&s_post_display_decision_pending, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_immediate_power_off_committed, true, __ATOMIC_RELEASE);
    __atomic_store_n(&s_state,
                     USER_EPD_SD_POWER_TEST_STATE_POWER_OFF_COMMITTED,
                     __ATOMIC_RELEASE);
    portEXIT_CRITICAL(&s_decision_mux);
    ESP_LOGI(TAG, "post-display decision is immediate POWER_OFF; independent cycle skipped");
#endif
    return ESP_OK;
}

void EpdSdPowerTest_CommitStayAwake(const char *reason)
{
#if USER_EPD_SD_POWER_TEST_ENABLE
    if (!__atomic_load_n(&s_initialized, __ATOMIC_ACQUIRE)) {
        return;
    }

    portENTER_CRITICAL(&s_decision_mux);
    bool unresolved =
        __atomic_load_n(&s_post_display_decision_pending, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&s_immediate_power_off_committed, __ATOMIC_ACQUIRE);
    if (!unresolved) {
        portEXIT_CRITICAL(&s_decision_mux);
        return;
    }
    __atomic_store_n(&s_post_display_decision_pending, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_immediate_power_off_committed, false, __ATOMIC_RELEASE);
    __atomic_store_n(&s_armed, true, __ATOMIC_RELEASE);
    __atomic_store_n(&s_state, USER_EPD_SD_POWER_TEST_STATE_ARMED, __ATOMIC_RELEASE);
    portEXIT_CRITICAL(&s_decision_mux);
    notify_activity(USER_EPD_SD_POWER_TEST_EVENT_EPD_DONE_BIT);
    ESP_LOGI(TAG,
             "post-display decision is stay awake; mandatory cycle armed reason=%s",
             reason != NULL ? reason : "unspecified");
#else
    (void)reason;
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
        ESP_LOGE(TAG, "image transfer rail-cycle count underflow");
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
        ESP_LOGE(TAG, "slideshow follow-up rail-cycle count underflow");
        return;
    }
    (void)__atomic_sub_fetch(&s_slideshow_followup_count, 1U, __ATOMIC_ACQ_REL);
    notify_activity(USER_EPD_SD_POWER_TEST_EVENT_SLIDESHOW_FOLLOWUP_BIT);
#endif
}
