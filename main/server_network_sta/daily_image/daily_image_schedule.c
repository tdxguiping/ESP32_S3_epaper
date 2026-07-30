#include "daily_image_schedule.h"

#include <limits.h>
#include <string.h>

#include "esp_log.h"
#include "server_network_sta_slideshow.h"
#include "server_network_sta_time.h"
#include "tdx_cfg.h"

static const char *TAG = "daily_image_time";

esp_err_t DailyImageSchedule_GetNetworkNow(int64_t *now_epoch)
{
    if (now_epoch == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ServerNetworkStaTime_IsSntpSynced()) {
        return ESP_ERR_INVALID_STATE;
    }

    server_network_sta_time_info_t info = {0};
    esp_err_t ret = ServerNetworkStaTime_GetInfo(&info);
    if (ret != ESP_OK || !info.valid || !info.sntp_synced || info.epoch <= 0) {
        return ret != ESP_OK ? ret : ESP_ERR_INVALID_STATE;
    }

    *now_epoch = info.epoch;
    return ESP_OK;
}

static esp_err_t select_normal_target(const daily_image_config_t *config,
                                      int64_t now_epoch,
                                      int64_t *target_epoch)
{
    if (config == NULL || target_epoch == NULL || config->timestamp <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (now_epoch < config->timestamp) {
        *target_epoch = config->timestamp;
        return ESP_OK;
    }

    int64_t elapsed = now_epoch - config->timestamp;
    int64_t slot = config->timestamp +
                   (elapsed / (int64_t)USER_DAILY_IMAGE_PERIOD_SECONDS) *
                       (int64_t)USER_DAILY_IMAGE_PERIOD_SECONDS;

    /*
     * Keep an unfinished current slot even when boot or SNTP synchronization
     * is late. Only a successfully completed slot advances to the next day.
     */
    if (slot != config->last_completed_target_epoch) {
        *target_epoch = slot;
    } else {
        *target_epoch = slot + (int64_t)USER_DAILY_IMAGE_PERIOD_SECONDS;
    }
    return ESP_OK;
}

esp_err_t DailyImageSchedule_Decide(
    const daily_image_config_t *config,
    daily_image_schedule_decision_t *decision)
{
    if (config == NULL || decision == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(decision, 0, sizeof(*decision));
    esp_err_t ret = DailyImageSchedule_GetNetworkNow(&decision->now_epoch);
    if (ret != ESP_OK) {
        return ret;
    }

    if (config->retry_pending != 0U) {
        decision->retry = true;
        decision->initial_run =
            config->initial_run_pending ==
            USER_DAILY_IMAGE_INITIAL_RUN_PENDING;
        decision->target_epoch = config->retry_due_epoch;
        decision->execute_epoch = decision->target_epoch;
    } else if (config->initial_run_pending ==
               USER_DAILY_IMAGE_INITIAL_RUN_PENDING) {
        /*
         * Every accepted sw=1 request performs one bootstrap display after
         * network time is ready. The APP timestamp remains the later schedule
         * anchor and is not used to delay this first run.
         */
        decision->initial_run = true;
        decision->target_epoch = decision->now_epoch;
        decision->execute_epoch = decision->now_epoch;
    } else {
        ret = select_normal_target(config,
                                   decision->now_epoch,
                                   &decision->target_epoch);
        if (ret != ESP_OK) {
            return ret;
        }
        decision->lead_seconds = slideshow_rtc_display_lead_seconds();
        decision->execute_epoch =
            decision->target_epoch > (int64_t)decision->lead_seconds
                ? decision->target_epoch - (int64_t)decision->lead_seconds
                : decision->target_epoch;
    }

    /*
     * A newly accepted APP request always keeps its immediate first run.
     * Every later EPD attempt, including retries, must respect the persisted
     * five-minute interval without moving the APP's absolute timestamp slots.
     */
    bool initial_run_exempt = decision->initial_run && !decision->retry;
    if (!initial_run_exempt && config->last_daily_epd_epoch != 0U) {
        int64_t minimum_execute =
            (int64_t)config->last_daily_epd_epoch +
            (int64_t)USER_DAILY_IMAGE_MIN_DISPLAY_INTERVAL_SECONDS;
        if (decision->execute_epoch < minimum_execute) {
            decision->execute_epoch = minimum_execute;
            decision->interval_delayed = true;
        }
    }

    decision->difference_seconds =
        decision->execute_epoch - decision->now_epoch;

    /*
     * An unfinished normal slot and an overdue hourly retry both run as soon
     * as their execution time is reached. They are never moved to now + N.
     */
    if (decision->difference_seconds <= 0) {
        decision->action = DAILY_IMAGE_SCHEDULE_RUN_NOW;
        return ESP_OK;
    }

    if (decision->difference_seconds <=
        (int64_t)USER_DAILY_IMAGE_WAKE_ADVANCE_SECONDS) {
        decision->action = DAILY_IMAGE_SCHEDULE_WAIT_WINDOW;
        return ESP_OK;
    }

    int64_t delay = decision->difference_seconds -
                    (int64_t)USER_DAILY_IMAGE_WAKE_ADVANCE_SECONDS;
    if (delay < (int64_t)CH583_WAKE_TIMER_MIN_SECONDS ||
        delay > (int64_t)CH583_WAKE_TIMER_MAX_SECONDS ||
        delay > UINT32_MAX) {
        ESP_LOGE(TAG, "wake delay out of range delay=%lld min=%u max=%lu",
                 (long long)delay,
                 (unsigned int)CH583_WAKE_TIMER_MIN_SECONDS,
                 (unsigned long)CH583_WAKE_TIMER_MAX_SECONDS);
        return ESP_ERR_INVALID_SIZE;
    }

    decision->action = DAILY_IMAGE_SCHEDULE_POWER_OFF;
    decision->wake_seconds = (uint32_t)delay;
    decision->wake_epoch = decision->now_epoch + delay;
    return ESP_OK;
}
