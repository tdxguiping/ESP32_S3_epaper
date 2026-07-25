#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "daily_image_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DAILY_IMAGE_SCHEDULE_RUN_NOW = 0,
    DAILY_IMAGE_SCHEDULE_WAIT_WINDOW,
    DAILY_IMAGE_SCHEDULE_POWER_OFF,
} daily_image_schedule_action_t;

typedef struct {
    daily_image_schedule_action_t action;
    bool retry;
    bool initial_run;
    bool interval_delayed;
    int64_t now_epoch;
    int64_t target_epoch;
    int64_t execute_epoch;
    int64_t wake_epoch;
    int64_t difference_seconds;
    uint32_t lead_seconds;
    uint32_t wake_seconds;
} daily_image_schedule_decision_t;

/*
 * Use only the existing SNTP-backed network clock. The APP timestamp is a
 * schedule anchor and must never be used to set the ESP32 system clock.
 */
esp_err_t DailyImageSchedule_GetNetworkNow(int64_t *now_epoch);
esp_err_t DailyImageSchedule_Decide(
    const daily_image_config_t *config,
    daily_image_schedule_decision_t *decision);

#ifdef __cplusplus
}
#endif
