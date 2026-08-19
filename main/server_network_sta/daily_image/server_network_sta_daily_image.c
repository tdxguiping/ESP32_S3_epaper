#include "server_network_sta_daily_image.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "daily_image_config.h"
#include "daily_image_http.h"
#include "daily_image_schedule.h"
#include "epd_display_app.h"
#include "epd_display_mode.h"
#include "epd_type.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "image_business_worker.h"
#include "local_image_browsing.h"
#include "nvs.h"
#include "server_network_sta.h"
#include "server_network_sta_slideshow_control.h"
#include "server_network_sta_time.h"
#include "server_network_sta_wifi_work_time.h"
#include "tdx_cfg.h"
#include "tdx_zlib_buffer.h"

static const char *TAG = "daily_image";

typedef struct {
    daily_image_config_t config;
    char source[16];
    uint32_t generation;
} daily_image_job_t;

_Static_assert(sizeof(daily_image_job_t) <=
                   USER_IMAGE_BUSINESS_WORKER_PAYLOAD_SIZE,
               "daily image job exceeds shared worker payload");

static SemaphoreHandle_t s_daily_config_mutex;
static StaticSemaphore_t s_daily_config_mutex_control;
static volatile uint32_t s_daily_request_generation;
static volatile uint32_t s_daily_running_generation;
static volatile uint32_t s_daily_relative_wake_seconds;
static volatile bool s_daily_job_active;
static bool s_daily_initialized;
static bool s_daily_app_job_submitted;
static char s_daily_base_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX];

static bool daily_mode_is_active(void)
{
    return EpdDisplayMode_Get() == USER_EPD_DISPLAY_MODE_DAILY &&
           __atomic_load_n(&s_daily_running_generation, __ATOMIC_ACQUIRE) ==
               __atomic_load_n(&s_daily_request_generation, __ATOMIC_ACQUIRE);
}

static bool daily_wait_interruptible(TickType_t timeout_ticks)
{
    if (!daily_mode_is_active()) {
        return false;
    }

    if (!ImageBusinessWorker_IsCurrentTask()) {
        ESP_LOGE(TAG, "daily interruptible wait called outside image worker");
        return false;
    }

    TickType_t start_tick = xTaskGetTickCount();

    for (;;) {
        if (!daily_mode_is_active()) {
            return false;
        }

        TickType_t elapsed = xTaskGetTickCount() - start_tick;

        if (elapsed >= timeout_ticks) {
            return true;
        }

        TickType_t remaining = timeout_ticks - elapsed;

        (void)ImageBusinessWorker_WaitInterruptible(remaining);

        /*
         * Wake may mean:
         *   1. DAILY was cancelled/replaced
         *   2. another image job arrived
         *   3. an unrelated/stale worker notification
         *
         * Therefore do not treat "woken" itself as cancellation.
         * Re-check mode + generation.
         */
        if (!daily_mode_is_active()) {
            return false;
        }
    }
}


static bool daily_mode_is_selected(void)
{
    return EpdDisplayMode_Get() == USER_EPD_DISPLAY_MODE_DAILY;
}

static esp_err_t send_result(httpd_req_t *req,
                             int result,
                             const char *message,
                             const char *error,
                             int sw,
                             const daily_image_config_t *config)
{
    char json[448];
    if (sw == 1 && config != NULL) {
        snprintf(json,
                 sizeof(json),
                 "{\"func\":\"daily_download_file_result\",\"result\":%d,"
                 "\"message\":\"%s\",\"imageHeight\":%lu,\"imageWidth\":%lu,"
                 "\"orientation\":%d,\"timestamp\":%lld,\"sw\":1,\"mode\":%u,"
                 "\"error\":\"%s\"}",
                 result,
                 message != NULL ? message : "",
                 (unsigned long)config->image_height,
                 (unsigned long)config->image_width,
                 (int)config->orientation,
                 (long long)config->timestamp,
                 (unsigned int)EpdDisplayMode_Get(),
                 error != NULL ? error : "");
    } else {
        snprintf(json,
                 sizeof(json),
                 "{\"func\":\"daily_download_file_result\",\"result\":%d,"
                 "\"message\":\"%s\",\"sw\":%d,\"mode\":%u,"
                 "\"error\":\"%s\"}",
                 result,
                 message != NULL ? message : "",
                 sw,
                 (unsigned int)EpdDisplayMode_Get(),
                 error != NULL ? error : "");
    }
    ESP_LOGI(TAG, "response: %s", json);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t stop_slideshow_for_daily(const char *base_path)
{
    static const char stop_json[] = "{\"func\":\"set_slideshow\",\"sw\":0}";
    server_network_sta_slideshow_control_result_t result = {0};
    esp_err_t ret = ServerNetworkStaSlideshowControl_ApplyJson(stop_json,
                                                               base_path,
                                                               &result);
    if (ret != ESP_OK || result.result != TDX_JSON_RESULT_OK || result.sw != 0) {
        ESP_LOGE(TAG,
                 "slideshow stop failed ret=%s result=%d sw=%d message=%s",
                 esp_err_to_name(ret),
                 result.result,
                 result.sw,
                 result.message);
        return ret != ESP_OK ? ret : ESP_FAIL;
    }
    ESP_LOGI(TAG, "slideshow stopped and NVS control disabled");
    return ESP_OK;
}

static void rollback_daily_enable(const daily_image_config_t *previous_config,
                                  bool previous_config_valid,
                                  uint8_t rollback_mode,
                                  const char *reason)
{
    esp_err_t config_ret =
        previous_config_valid
            ? DailyImageConfig_Save(previous_config)
            : DailyImageConfig_Erase();
    esp_err_t mode_ret =
        EpdDisplayMode_Set(rollback_mode);

    if (config_ret != ESP_OK || mode_ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "daily enable rollback failed reason=%s mode=%u config_ret=%s mode_ret=%s",
                 reason != NULL ? reason : "unknown",
                 (unsigned int)rollback_mode,
                 esp_err_to_name(config_ret),
                 esp_err_to_name(mode_ret));
        return;
    }
    ESP_LOGW(TAG,
             "daily enable rolled back reason=%s previous_config=%d mode=%u",
             reason != NULL ? reason : "unknown",
             previous_config_valid ? 1 : 0,
             (unsigned int)rollback_mode);
}

static void invalidate_previous_generation(const char *reason)
{
    uint32_t generation =
        __atomic_add_fetch(&s_daily_request_generation,
                           1U,
                           __ATOMIC_ACQ_REL);
    ESP_LOGI(TAG,
             "previous daily generation invalidated reason=%s generation=%lu",
             reason != NULL ? reason : "unknown",
             (unsigned long)generation);
}

static esp_err_t daily_run_command(const void *payload, size_t payload_size);

static esp_err_t submit_job(const daily_image_config_t *config,
                            const char *source)
{
    if (config == NULL || !s_daily_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    bool replace_local = source == NULL || strcmp(source, "reschedule") != 0;
    if (replace_local) {
        LocalImageBrowsing_InvalidateCurrent();
    }
    daily_image_job_t job = {.config = *config};
    job.generation = __atomic_add_fetch(&s_daily_request_generation,
                                        1U,
                                        __ATOMIC_ACQ_REL);
    strlcpy(job.source, source != NULL ? source : "unknown", sizeof(job.source));

   // (void)ImageBusinessWorker_CancelPending(IMAGE_BUSINESS_OWNER_DAILY);
    uint32_t replace_mask = IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_DAILY);
    if (replace_local) {
        replace_mask |=IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_LOCAL_IMAGE);
    }


    // esp_err_t ret = ImageBusinessWorker_SubmitReplacingPending(
    //     IMAGE_BUSINESS_OWNER_DAILY,
    //     daily_run_command,
    //     NULL,
    //     &job,
    //     sizeof(job),
    //     job.generation,
    //     replace_local
    //         ? IMAGE_BUSINESS_OWNER_MASK(IMAGE_BUSINESS_OWNER_LOCAL_IMAGE)
    //         : 0U);

    esp_err_t ret =
        ImageBusinessWorker_SubmitReplacingPending(
        IMAGE_BUSINESS_OWNER_DAILY,
        daily_run_command,
        NULL,
        &job,
        sizeof(job),
        job.generation,
        replace_mask);



    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "job submit failed source=%s generation=%lu",
                 job.source, (unsigned long)job.generation);
        return ret;
    }
    ESP_LOGI(TAG, "job submitted source=%s generation=%lu",
             job.source, (unsigned long)job.generation);
    return ESP_OK;
}

static esp_err_t resubmit_job_if_current(const daily_image_job_t *job)
{
    if (job == NULL || s_daily_config_mutex == NULL ||
        xSemaphoreTake(s_daily_config_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t requested_generation =
        __atomic_load_n(&s_daily_request_generation, __ATOMIC_ACQUIRE);
    uint32_t running_generation =
        __atomic_load_n(&s_daily_running_generation, __ATOMIC_ACQUIRE);
    if (!daily_mode_is_selected() ||
        job->generation != requested_generation ||
        job->generation != running_generation) {
        ESP_LOGW(TAG,
                 "stale daily job cancelled generation=%lu requested=%lu running=%lu mode=%u",
                 (unsigned long)job->generation,
                 (unsigned long)requested_generation,
                 (unsigned long)running_generation,
                 (unsigned int)EpdDisplayMode_Get());
        xSemaphoreGive(s_daily_config_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = submit_job(&job->config, "reschedule");
    xSemaphoreGive(s_daily_config_mutex);
    return ret;
}

static const char *schedule_action_name(daily_image_schedule_action_t action)
{
    switch (action) {
    case DAILY_IMAGE_SCHEDULE_RUN_NOW:
        return "RUN_NOW";
    case DAILY_IMAGE_SCHEDULE_WAIT_WINDOW:
        return "WAIT_TARGET";
    case DAILY_IMAGE_SCHEDULE_POWER_OFF:
        return "POWER_OFF";
    default:
        return "UNKNOWN";
    }
}

static void request_guarded_power_off(uint32_t wake_seconds);

static esp_err_t wait_until_ready(void)
{
    __atomic_store_n(&s_daily_relative_wake_seconds, 0U, __ATOMIC_RELEASE);
    for (uint32_t attempt = 1U;
         attempt <= USER_DAILY_IMAGE_READY_CHECK_MAX_ATTEMPTS;
         ++attempt) {
        if (!daily_mode_is_active()) {
            return ESP_ERR_INVALID_STATE;
        }

        server_network_sta_status_t status = {0};
        bool has_ip = ServerNetworkSta_GetStatus(&status) == ESP_OK &&
                      status.has_ip && status.ip[0] != '\0';
        bool time_ready = ServerNetworkStaTime_IsSntpSynced();
        if (has_ip && time_ready) {
            __atomic_store_n(&s_daily_relative_wake_seconds,
                             0U,
                             __ATOMIC_RELEASE);
            ESP_LOGI(TAG, "network ready ip=%s SNTP=1", status.ip);
            return ESP_OK;
        }

        if (attempt == 1U ||
            attempt == USER_DAILY_IMAGE_READY_CHECK_MAX_ATTEMPTS) {
            ESP_LOGW(TAG,
                     "daily readiness failed attempt=%lu/%u has_ip=%d state=%s SNTP=%d",
                     (unsigned long)attempt,
                     (unsigned int)USER_DAILY_IMAGE_READY_CHECK_MAX_ATTEMPTS,
                     has_ip ? 1 : 0,
                     ServerNetworkSta_StateName(status.state),
                     time_ready ? 1 : 0);
        }

                if (attempt < USER_DAILY_IMAGE_READY_CHECK_MAX_ATTEMPTS) {
                    if (!daily_wait_interruptible(pdMS_TO_TICKS(
                            USER_DAILY_IMAGE_READY_CHECK_INTERVAL_SECONDS * 1000U))) {
                        return ESP_ERR_INVALID_STATE;
                    }
                }

        // if (attempt < USER_DAILY_IMAGE_READY_CHECK_MAX_ATTEMPTS) {
        //     vTaskDelay(pdMS_TO_TICKS(
        //         USER_DAILY_IMAGE_READY_CHECK_INTERVAL_SECONDS * 1000U));
        // }

    }

    __atomic_store_n(&s_daily_relative_wake_seconds,
                     USER_DAILY_IMAGE_RETRY_WAKE_SECONDS,
                     __ATOMIC_RELEASE);
    ESP_LOGW(TAG,
             "daily readiness exhausted attempts=%u, sleep=%u seconds",
             (unsigned int)USER_DAILY_IMAGE_READY_CHECK_MAX_ATTEMPTS,
             (unsigned int)USER_DAILY_IMAGE_RETRY_WAKE_SECONDS);
    for (;;) {
        if (!daily_mode_is_active()) {
            __atomic_store_n(&s_daily_relative_wake_seconds,
                             0U,
                             __ATOMIC_RELEASE);
            return ESP_ERR_INVALID_STATE;
        }

        request_guarded_power_off(USER_DAILY_IMAGE_RETRY_WAKE_SECONDS);
        if (!daily_wait_interruptible(pdMS_TO_TICKS(
                USER_DAILY_IMAGE_POWER_OFF_RETRY_SECONDS * 1000U))) {
            __atomic_store_n(&s_daily_relative_wake_seconds,0U,__ATOMIC_RELEASE);
            return ESP_ERR_INVALID_STATE;
        }        
        // request_guarded_power_off(USER_DAILY_IMAGE_RETRY_WAKE_SECONDS);
        // vTaskDelay(pdMS_TO_TICKS(USER_DAILY_IMAGE_POWER_OFF_RETRY_SECONDS * 1000U));
    }
}

static esp_err_t validate_runtime_epd(const daily_image_config_t *config,
                                      const epd_type_config_t **epd_config)
{
    if (config == NULL || epd_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const epd_type_config_t *active = EpdType_GetCurrentConfig();
    if (active == NULL ||
        config->image_height != active->width ||
        config->image_width != active->height ||
        active->display_size == 0) {
        ESP_LOGE(TAG,
                 "runtime EPD mismatch imageHeight=%u imageWidth=%u active=%ux%u type=%u bytes=%u",
                 (unsigned int)config->image_height,
                 (unsigned int)config->image_width,
                 active != NULL ? (unsigned int)active->width : 0U,
                 active != NULL ? (unsigned int)active->height : 0U,
                 active != NULL ? (unsigned int)active->type : 0U,
                 active != NULL ? (unsigned int)active->display_size : 0U);
        return ESP_ERR_INVALID_SIZE;
    }
    *epd_config = active;
    return ESP_OK;
}

static esp_err_t download_once(const daily_image_config_t *config,
                               uint8_t **display_buffer,
                               size_t *downloaded_size,
                               const epd_type_config_t **epd_config)
{
    if (display_buffer == NULL || downloaded_size == NULL ||
        epd_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *display_buffer = NULL;
    *downloaded_size = 0;

    esp_err_t ret = validate_runtime_epd(config, epd_config);
    if (ret != ESP_OK) {
        return ret;
    }

    char download_url[USER_DAILY_IMAGE_DOWNLOAD_URL_BUFFER_SIZE] = {0};
    ret = DailyImageHttp_SelectDownloadUrl(config,
                                           download_url,
                                           sizeof(download_url),
                                           daily_mode_is_active);
    if (ret != ESP_OK || !daily_mode_is_active()) {
        return ret != ESP_OK ? ret : ESP_ERR_INVALID_STATE;
    }

    size_t download_capacity = (*epd_config)->display_size;
#if USER_EPD_DISPLAY_DATA_ZLIB_ENABLE
    download_capacity =
        TdxZlibBuffer_GetCompressBound((*epd_config)->display_size);
    if (download_capacity == 0) {
        ESP_LOGE(TAG, "zlib download capacity failed display=%u",
                 (unsigned int)(*epd_config)->display_size);
        return ESP_ERR_INVALID_SIZE;
    }
#endif

    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t required = (*epd_config)->display_size + download_capacity +
                      USER_DAILY_IMAGE_PSRAM_RESERVE_BYTES;
    if (psram_free < required) {
        ESP_LOGE(TAG, "PSRAM not enough free=%u required=%u display=%u",
                 (unsigned int)psram_free,
                 (unsigned int)required,
                 (unsigned int)(*epd_config)->display_size);
        return ESP_ERR_NO_MEM;
    }

    *display_buffer = (uint8_t *)heap_caps_malloc(
        download_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (*display_buffer == NULL) {
        ESP_LOGE(TAG, "display buffer alloc failed bytes=%u",
                 (unsigned int)download_capacity);
        return ESP_ERR_NO_MEM;
    }

    ret = DailyImageHttp_Download(download_url,
                                  *display_buffer,
                                  download_capacity,
                                  USER_EPD_DISPLAY_DATA_ZLIB_ENABLE == 0,
                                  downloaded_size,
                                  daily_mode_is_active);
    if (ret != ESP_OK) {
        heap_caps_free(*display_buffer);
        *display_buffer = NULL;
    }
    return ret;
}

static esp_err_t download_with_retries(const daily_image_config_t *config,
                                       uint8_t **display_buffer,
                                       size_t *downloaded_size,
                                       const epd_type_config_t **epd_config)
{
    esp_err_t ret = ESP_FAIL;
    for (uint32_t attempt = 1U;
         attempt <= USER_DAILY_IMAGE_RETRY_COUNT;
         ++attempt) {
        ServerNetworkStaWifiWorkTime_OnNetworkData();
        ESP_LOGI(TAG, "download attempt=%lu/%u",
                 (unsigned long)attempt,
                 (unsigned int)USER_DAILY_IMAGE_RETRY_COUNT);
        ret = download_once(config,
                            display_buffer,
                            downloaded_size,
                            epd_config);
        if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
            break;
        }
        ESP_LOGW(TAG, "download attempt failed index=%lu ret=%s",
                 (unsigned long)attempt, esp_err_to_name(ret));

        if (attempt < USER_DAILY_IMAGE_RETRY_COUNT) {
            if (!daily_wait_interruptible(
                    pdMS_TO_TICKS(USER_DAILY_IMAGE_RETRY_DELAY_MS))) {
                ret = ESP_ERR_INVALID_STATE;
                break;
            }
        }
        // if (attempt < USER_DAILY_IMAGE_RETRY_COUNT) {
        //     vTaskDelay(pdMS_TO_TICKS(USER_DAILY_IMAGE_RETRY_DELAY_MS));
        //     if (!daily_mode_is_active()) {
        //         ret = ESP_ERR_INVALID_STATE;
        //         break;
        //     }
        // }



    }
    return ret;
}

static esp_err_t display_once(uint8_t *display_buffer,
                              size_t downloaded_size,
                              const epd_type_config_t *epd_config)
{
    if (display_buffer == NULL || epd_config == NULL ||
        !daily_mode_is_active()) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "EPD display start type=%u name=%s bytes=%u",
             (unsigned int)epd_config->type,
             epd_config->name,
             (unsigned int)downloaded_size);
    esp_err_t ret = ServerNetworkStaEpdDisplay_QueueToScreenAndWait(
        display_buffer,
        downloaded_size,
        1);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "EPD display complete type=%u bytes=%u",
                 (unsigned int)epd_config->type,
                 (unsigned int)downloaded_size);
    } else {
        ESP_LOGE(TAG, "EPD display failed ret=%s", esp_err_to_name(ret));
    }
    return ret;
}

static void request_guarded_power_off(uint32_t wake_seconds)
{
    ESP_LOGI(TAG, "request power off wake_seconds=%lu delay=%u",
             (unsigned long)wake_seconds,
             (unsigned int)USER_DAILY_IMAGE_POWER_OFF_DELAY_SECONDS);
    ServerNetworkStaWifiWorkTime_RequestOneShotPowerOffCountdown(
        USER_DAILY_IMAGE_POWER_OFF_DELAY_SECONDS);
}

static esp_err_t wait_for_run_window(daily_image_config_t *config,
                                     daily_image_schedule_decision_t *decision)
{
    bool power_off_requested = false;
    TickType_t last_power_off_request = 0;
    daily_image_schedule_action_t last_action =
        (daily_image_schedule_action_t)-1;

    for (;;) {
        if (!daily_mode_is_active()) {
            return ESP_ERR_INVALID_STATE;
        }

        esp_err_t ret = DailyImageSchedule_Decide(config, decision);
        if (ret != ESP_OK) {
            if (last_action != DAILY_IMAGE_SCHEDULE_WAIT_WINDOW) {
                ESP_LOGW(TAG, "wait schedule because SNTP is unavailable ret=%s",
                         esp_err_to_name(ret));
            }
            last_action = DAILY_IMAGE_SCHEDULE_WAIT_WINDOW;
            //vTaskDelay(pdMS_TO_TICKS(USER_DAILY_IMAGE_READY_POLL_MS));
            if (!daily_wait_interruptible(
                    pdMS_TO_TICKS(USER_DAILY_IMAGE_READY_POLL_MS))) {
                return ESP_ERR_INVALID_STATE;
            }

            continue;
        }

        if (decision->action != last_action) {
            char target_text[32] = {0};
            DailyImageConfig_FormatEpoch(decision->target_epoch,
                                         target_text,
                                         sizeof(target_text));
            ESP_LOGI(TAG,
                     "schedule action=%s retry=%d initial=%d delayed=%d now=%lld target=%lld(%s) execute=%lld diff=%lld lead=%lu wake=%lu",
                     schedule_action_name(decision->action),
                     decision->retry ? 1 : 0,
                     decision->initial_run ? 1 : 0,
                     decision->interval_delayed ? 1 : 0,
                     (long long)decision->now_epoch,
                     (long long)decision->target_epoch,
                     target_text,
                     (long long)decision->execute_epoch,
                     (long long)decision->difference_seconds,
                     (unsigned long)decision->lead_seconds,
                     (unsigned long)decision->wake_seconds);
            last_action = decision->action;
        }

        if (decision->action == DAILY_IMAGE_SCHEDULE_RUN_NOW) {
            if (decision->initial_run && !decision->retry) {
                ESP_LOGI(TAG,
                         "initial daily run pending, execute now anchor=%lld",
                         (long long)config->timestamp);
            }
            ServerNetworkStaWifiWorkTime_SetDailyImageInProgress(true);
            return ESP_OK;
        }
        if (decision->action == DAILY_IMAGE_SCHEDULE_WAIT_WINDOW) {
            /*
             * Stay awake during the final wake-advance interval and enter the
             * workflow exactly at the shared slideshow lead-time boundary.
             */
            //vTaskDelay(pdMS_TO_TICKS(USER_DAILY_IMAGE_READY_POLL_MS));
            if (!daily_wait_interruptible(
                    pdMS_TO_TICKS(USER_DAILY_IMAGE_READY_POLL_MS))) {
                return ESP_ERR_INVALID_STATE;
            }

            continue;
        }

        TickType_t now_tick = xTaskGetTickCount();
        TickType_t power_off_retry_ticks =
            pdMS_TO_TICKS(USER_DAILY_IMAGE_POWER_OFF_RETRY_SECONDS * 1000U);
        if (!power_off_requested ||
            now_tick - last_power_off_request >= power_off_retry_ticks) {
            request_guarded_power_off(decision->wake_seconds);
            power_off_requested = true;
            last_power_off_request = now_tick;
        }
        /*
         * Usually CH583 removes power before this loop runs again. Keeping the
         * worker alive safely covers a delayed/cancelled shutdown: it can still
         * enter the run window instead of losing today's image.
         */
        //vTaskDelay(pdMS_TO_TICKS(USER_DAILY_IMAGE_READY_POLL_MS));
        if (!daily_wait_interruptible(
                pdMS_TO_TICKS(USER_DAILY_IMAGE_READY_POLL_MS))) {
            return ESP_ERR_INVALID_STATE;
        }


    }
}

static esp_err_t record_retry(daily_image_config_t *config,
                              int64_t failed_target_epoch)
{
    int64_t now_epoch = 0;
    esp_err_t ret = DailyImageSchedule_GetNetworkNow(&now_epoch);
    if (ret != ESP_OK) {
        return ret;
    }
    int64_t retry_due = now_epoch +
                        (int64_t)USER_DAILY_IMAGE_RETRY_WAKE_SECONDS;
    if (xSemaphoreTake(s_daily_config_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!daily_mode_is_active()) {
        xSemaphoreGive(s_daily_config_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    ret = DailyImageConfig_SaveRetryState(config,
                                          true,
                                          retry_due,
                                          failed_target_epoch);
    xSemaphoreGive(s_daily_config_mutex);
    if (ret == ESP_OK) {
        ESP_LOGW(TAG, "retry saved due=%lld target=%lld after=%u seconds",
                 (long long)retry_due,
                 (long long)failed_target_epoch,
                 (unsigned int)USER_DAILY_IMAGE_RETRY_WAKE_SECONDS);
    } else {
        ESP_LOGE(TAG, "retry state save failed ret=%s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t save_success_until_done(daily_image_config_t *config,
                                         int64_t completed_target)
{
    for (;;) {
        if (!daily_mode_is_active()) {
            return ESP_ERR_INVALID_STATE;
        }
        if (xSemaphoreTake(s_daily_config_mutex, portMAX_DELAY) != pdTRUE) {
            return ESP_ERR_INVALID_STATE;
        }
        if (!daily_mode_is_active()) {
            xSemaphoreGive(s_daily_config_mutex);
            return ESP_ERR_INVALID_STATE;
        }
        esp_err_t ret = DailyImageConfig_SaveSuccessState(config,
                                                          completed_target);
        xSemaphoreGive(s_daily_config_mutex);
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        if (ret == ESP_ERR_INVALID_STATE) {
            return ret;
        }
        ESP_LOGE(TAG, "success state save failed ret=%s; retrying",
                 esp_err_to_name(ret));
        ServerNetworkStaWifiWorkTime_OnNetworkData();
        //vTaskDelay(pdMS_TO_TICKS(USER_DAILY_IMAGE_RETRY_DELAY_MS));
        if (!daily_wait_interruptible(
                pdMS_TO_TICKS(USER_DAILY_IMAGE_RETRY_DELAY_MS))) {
            return ESP_ERR_INVALID_STATE;
        }

    }
}

static esp_err_t save_initial_success_until_done(
    daily_image_config_t *config,
    int64_t *success_now_epoch)
{
    if (config == NULL || success_now_epoch == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (;;) {
        if (!daily_mode_is_active()) {
            return ESP_ERR_INVALID_STATE;
        }

        int64_t now_epoch = 0;
        esp_err_t ret = DailyImageSchedule_GetNetworkNow(&now_epoch);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG,
                     "initial daily success time unavailable ret=%s; retrying",
                     esp_err_to_name(ret));
            ServerNetworkStaWifiWorkTime_OnNetworkData();
            //vTaskDelay(pdMS_TO_TICKS(USER_DAILY_IMAGE_RETRY_DELAY_MS));
            if (!daily_wait_interruptible(
                    pdMS_TO_TICKS(USER_DAILY_IMAGE_RETRY_DELAY_MS))) {
                return ESP_ERR_INVALID_STATE;
            }

            continue;
        }

        if (xSemaphoreTake(s_daily_config_mutex, portMAX_DELAY) != pdTRUE) {
            return ESP_ERR_INVALID_STATE;
        }
        if (!daily_mode_is_active()) {
            xSemaphoreGive(s_daily_config_mutex);
            return ESP_ERR_INVALID_STATE;
        }
        ret = DailyImageConfig_SaveInitialSuccessState(config);
        xSemaphoreGive(s_daily_config_mutex);
        if (ret == ESP_OK) {
            *success_now_epoch = now_epoch;
            return ESP_OK;
        }
        if (ret == ESP_ERR_INVALID_STATE) {
            return ret;
        }
        ESP_LOGE(TAG,
                 "initial daily success state save failed ret=%s; retrying",
                 esp_err_to_name(ret));
        ServerNetworkStaWifiWorkTime_OnNetworkData();
        //vTaskDelay(pdMS_TO_TICKS(USER_DAILY_IMAGE_RETRY_DELAY_MS));
        if (!daily_wait_interruptible(
                pdMS_TO_TICKS(USER_DAILY_IMAGE_RETRY_DELAY_MS))) {
            return ESP_ERR_INVALID_STATE;
        }

    }
}

static esp_err_t save_display_start_until_done(
    daily_image_config_t *config,
    int64_t *display_start_epoch)
{
    if (config == NULL || display_start_epoch == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (;;) {
        if (!daily_mode_is_active()) {
            return ESP_ERR_INVALID_STATE;
        }

        int64_t now_epoch = 0;
        esp_err_t ret = DailyImageSchedule_GetNetworkNow(&now_epoch);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG,
                     "EPD start time unavailable ret=%s; retrying",
                     esp_err_to_name(ret));
            ServerNetworkStaWifiWorkTime_OnNetworkData();
            //vTaskDelay(pdMS_TO_TICKS(USER_DAILY_IMAGE_RETRY_DELAY_MS));
            if (!daily_wait_interruptible(
                    pdMS_TO_TICKS(USER_DAILY_IMAGE_RETRY_DELAY_MS))) {
                return ESP_ERR_INVALID_STATE;
            }

            continue;
        }

        if (xSemaphoreTake(s_daily_config_mutex, portMAX_DELAY) != pdTRUE) {
            return ESP_ERR_INVALID_STATE;
        }
        if (!daily_mode_is_active()) {
            xSemaphoreGive(s_daily_config_mutex);
            return ESP_ERR_INVALID_STATE;
        }
        ret = DailyImageConfig_SaveDisplayStartState(config, now_epoch);
        xSemaphoreGive(s_daily_config_mutex);
        if (ret == ESP_OK) {
            *display_start_epoch = now_epoch;
            return ESP_OK;
        }
        if (ret == ESP_ERR_INVALID_STATE) {
            return ret;
        }
        ESP_LOGE(TAG, "EPD start state save failed ret=%s; retrying",
                 esp_err_to_name(ret));
        ServerNetworkStaWifiWorkTime_OnNetworkData();
        //vTaskDelay(pdMS_TO_TICKS(USER_DAILY_IMAGE_RETRY_DELAY_MS));
        if (!daily_wait_interruptible(
                pdMS_TO_TICKS(USER_DAILY_IMAGE_RETRY_DELAY_MS))) {
            return ESP_ERR_INVALID_STATE;
        }

    }
}

static esp_err_t save_retry_until_done(daily_image_config_t *config,
                                       int64_t failed_target)
{
    for (;;) {
        if (!daily_mode_is_active()) {
            return ESP_ERR_INVALID_STATE;
        }
        esp_err_t ret = record_retry(config, failed_target);
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        if (ret == ESP_ERR_INVALID_STATE) {
            return ret;
        }
        ESP_LOGE(TAG, "retry state is not durable; keep awake and retry");
        ServerNetworkStaWifiWorkTime_OnNetworkData();
        //vTaskDelay(pdMS_TO_TICKS(USER_DAILY_IMAGE_RETRY_DELAY_MS));
        if (!daily_wait_interruptible(
                pdMS_TO_TICKS(USER_DAILY_IMAGE_RETRY_DELAY_MS))) {
            return ESP_ERR_INVALID_STATE;
        }

    }
}

static esp_err_t daily_run_command(const void *payload, size_t payload_size)
{
    if (payload == NULL || payload_size != sizeof(daily_image_job_t)) {
        return ESP_ERR_INVALID_ARG;
    }
    daily_image_job_t job = {0};
    memcpy(&job, payload, sizeof(job));

    __atomic_store_n(&s_daily_running_generation,
                     job.generation,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&s_daily_job_active, true, __ATOMIC_RELEASE);
    ESP_LOGI(TAG, "job start source=%s generation=%lu mode=%u",
             job.source,
             (unsigned long)job.generation,
             (unsigned int)EpdDisplayMode_Get());

    esp_err_t ret = wait_until_ready();
    daily_image_schedule_decision_t decision = {0};
    if (ret == ESP_OK) {
        ret = wait_for_run_window(&job.config, &decision);
    }

    uint8_t *display_buffer = NULL;
    size_t downloaded_size = 0;
    const epd_type_config_t *epd_config = NULL;
    if (ret == ESP_OK) {
        ret = download_with_retries(&job.config,
                                    &display_buffer,
                                    &downloaded_size,
                                    &epd_config);
    }
    int64_t display_start_epoch = 0;
    if (ret == ESP_OK) {
        ret = save_display_start_until_done(&job.config,
                                            &display_start_epoch);
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG,
                 "daily EPD start saved epoch=%lld initial=%d target=%lld",
                 (long long)display_start_epoch,
                 decision.initial_run ? 1 : 0,
                 (long long)decision.target_epoch);
        /* Display is intentionally attempted once per wake cycle. */
        ret = display_once(display_buffer, downloaded_size, epd_config);
    }
    if (display_buffer != NULL) {
        heap_caps_free(display_buffer);
    }

    esp_err_t command_ret = ret;
    if (ret == ESP_OK) {
        if (decision.initial_run) {
            int64_t success_now = 0;
            ret = save_initial_success_until_done(&job.config,
                                                  &success_now);
            if (ret == ESP_OK) {
                daily_image_schedule_decision_t next = {0};
                esp_err_t next_ret =
                    DailyImageSchedule_Decide(&job.config, &next);
                if (next_ret == ESP_OK) {
                    ESP_LOGI(TAG,
                             "initial daily run complete now=%lld next_target=%lld next_execute=%lld delayed=%d source=%s",
                             (long long)success_now,
                             (long long)next.target_epoch,
                             (long long)next.execute_epoch,
                             next.interval_delayed ? 1 : 0,
                             job.source);
                } else {
                    ESP_LOGI(TAG,
                             "initial daily run complete now=%lld source=%s",
                             (long long)success_now,
                             job.source);
                }
            }
        } else {
            int64_t completed_target =
                decision.retry ? job.config.retry_target_epoch
                               : decision.target_epoch;
            ret = save_success_until_done(&job.config, completed_target);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "job complete target=%lld source=%s",
                         (long long)completed_target, job.source);
            }
        }
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG,
                     "success state save failed initial=%d ret=%s",
                     decision.initial_run ? 1 : 0,
                     esp_err_to_name(ret));
            command_ret = ret;
        }
    } else if (ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "job failed source=%s initial=%d ret=%s",
                 job.source,
                 decision.initial_run ? 1 : 0,
                 esp_err_to_name(ret));
        ret = save_retry_until_done(&job.config,
                                    decision.retry
                                        ? job.config.retry_target_epoch
                                        : decision.target_epoch);
    }

    ServerNetworkStaWifiWorkTime_SetDailyImageInProgress(false);
    if (ret != ESP_ERR_INVALID_STATE && daily_mode_is_active()) {
        /*
         * Success selects the next daily slot. Failure selects the saved
         * one-hour retry. Both use the same guarded CH583 power-off path.
         */
        esp_err_t wait_ret = wait_for_run_window(&job.config, &decision);
        if (wait_ret == ESP_OK) {
            /* A delayed shutdown expired locally; queue the now-due work. */
            if (resubmit_job_if_current(&job) != ESP_OK) {
                ServerNetworkStaWifiWorkTime_SetDailyImageInProgress(false);
            }
        }
    }

    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "job canceled source=%s mode=%u",
                 job.source, (unsigned int)EpdDisplayMode_Get());
    }
    __atomic_store_n(&s_daily_job_active, false, __ATOMIC_RELEASE);
    return command_ret;
}

esp_err_t ServerNetworkStaDailyImage_Init(const char *base_path)
{
#if USER_DAILY_IMAGE_ENABLE
    if (base_path == NULL ||
        strlcpy(s_daily_base_path, base_path, sizeof(s_daily_base_path)) >=
            sizeof(s_daily_base_path)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_daily_config_mutex == NULL) {
        s_daily_config_mutex =
            xSemaphoreCreateMutexStatic(&s_daily_config_mutex_control);
        if (s_daily_config_mutex == NULL) {
            ESP_LOGE(TAG, "static config mutex create failed");
            return ESP_ERR_NO_MEM;
        }
    }
    s_daily_initialized = true;
    esp_err_t ret = ImageBusinessWorker_Init();
    if (ret != ESP_OK) {
        s_daily_initialized = false;
        return ret;
    }
    ESP_LOGI(TAG, "base initialized mode=%u shared_worker=resident",
             (unsigned int)EpdDisplayMode_Get());
    return ret;
#else
    (void)base_path;
    return ESP_OK;
#endif
}

esp_err_t ServerNetworkStaDailyImage_StartSaved(void)
{
#if USER_DAILY_IMAGE_ENABLE
    if (!s_daily_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!daily_mode_is_selected()) {
        ESP_LOGI(TAG, "saved job not started mode=%u",
                 (unsigned int)EpdDisplayMode_Get());
        return ESP_OK;
    }

    if (xSemaphoreTake(s_daily_config_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!daily_mode_is_selected()) {
        ESP_LOGI(TAG, "saved startup job canceled because mode changed to %u",
                 (unsigned int)EpdDisplayMode_Get());
        xSemaphoreGive(s_daily_config_mutex);
        return ESP_OK;
    }
    if (s_daily_app_job_submitted) {
        ESP_LOGI(TAG, "saved startup job superseded by APP request");
        xSemaphoreGive(s_daily_config_mutex);
        return ESP_OK;
    }
    daily_image_config_t config = {0};
    esp_err_t ret = DailyImageConfig_Load(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DAILY startup config invalid, restore NORMAL ret=%s",
                 esp_err_to_name(ret));
        esp_err_t mode_ret = EpdDisplayMode_Set(USER_EPD_DISPLAY_MODE_NORMAL);
        xSemaphoreGive(s_daily_config_mutex);
        return mode_ret != ESP_OK ? mode_ret : ret;
    }
    ret = submit_job(&config, "startup");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "DAILY startup job unavailable, restore NORMAL ret=%s",
                 esp_err_to_name(ret));
        esp_err_t mode_ret = EpdDisplayMode_Set(USER_EPD_DISPLAY_MODE_NORMAL);
        if (mode_ret != ESP_OK) {
            ESP_LOGE(TAG, "restore NORMAL after startup failure ret=%s",
                     esp_err_to_name(mode_ret));
        }
    }
    xSemaphoreGive(s_daily_config_mutex);
    return ret;
#else
    return ESP_OK;
#endif
}

static void stop_daily_work(void)
{
#if USER_DAILY_IMAGE_ENABLE
    bool had_work = __atomic_load_n(&s_daily_job_active, __ATOMIC_ACQUIRE) ||
                    ImageBusinessWorker_IsOwnerBusy(
                        IMAGE_BUSINESS_OWNER_DAILY);
    uint32_t generation = __atomic_add_fetch(&s_daily_request_generation,
                                              1U,
                                              __ATOMIC_ACQ_REL);
    __atomic_store_n(&s_daily_relative_wake_seconds, 0U, __ATOMIC_RELEASE);
    (void)ImageBusinessWorker_CancelPending(IMAGE_BUSINESS_OWNER_DAILY);
    ImageBusinessWorker_Wake();
    if (had_work) {
        ESP_LOGI(TAG, "daily work stopped generation=%lu",
                 (unsigned long)generation);
    }
#endif
}

void ServerNetworkStaDailyImage_Stop(void)
{
    stop_daily_work();
}

esp_err_t ServerNetworkStaDailyImage_StopAndWait(void)
{
    ServerNetworkStaDailyImage_Stop();
#if USER_DAILY_IMAGE_ENABLE
    TickType_t timeout_ticks = pdMS_TO_TICKS(USER_EPD_DISPLAY_WAIT_TIMEOUT_MS + 5000U);
    esp_err_t wait_ret = ImageBusinessWorker_WaitOwnerIdle(
        IMAGE_BUSINESS_OWNER_DAILY, timeout_ticks);
    if (wait_ret != ESP_OK ||
        __atomic_load_n(&s_daily_job_active, __ATOMIC_ACQUIRE)) {
        ESP_LOGE(TAG, "daily job stop timeout");
        return ESP_ERR_TIMEOUT;
    }
#endif
    return ESP_OK;
}

esp_err_t ServerNetworkStaDailyImage_GetPowerOffWakeSeconds(
    uint32_t *wake_seconds,
    bool *keep_awake)
{
    if (wake_seconds == NULL || keep_awake == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *wake_seconds = 0;
    *keep_awake = true;

    uint32_t relative_wake =
        __atomic_load_n(&s_daily_relative_wake_seconds, __ATOMIC_ACQUIRE);
    if (relative_wake != 0U) {
        *wake_seconds = relative_wake;
        *keep_awake = false;
        return ESP_OK;
    }

    if (s_daily_config_mutex == NULL ||
        xSemaphoreTake(s_daily_config_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    daily_image_config_t config = {0};
    esp_err_t ret = DailyImageConfig_Load(&config);
    xSemaphoreGive(s_daily_config_mutex);
    if (ret != ESP_OK) {
        return ret;
    }
    daily_image_schedule_decision_t decision = {0};
    ret = DailyImageSchedule_Decide(&config, &decision);
    if (ret != ESP_OK) {
        return ret;
    }
    if (decision.action == DAILY_IMAGE_SCHEDULE_POWER_OFF) {
        *wake_seconds = decision.wake_seconds;
        *keep_awake = false;
    }
    return ESP_OK;
}

esp_err_t ServerNetworkStaDailyImage_ResetConfig(void)
{
    bool locked = false;
    if (s_daily_config_mutex != NULL) {
        locked =
            xSemaphoreTake(s_daily_config_mutex, portMAX_DELAY) == pdTRUE;
        if (!locked) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    esp_err_t ret = DailyImageConfig_Erase();
    if (locked) {
        xSemaphoreGive(s_daily_config_mutex);
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "factory reset erased NVS key=%s",
                 USER_DAILY_IMAGE_NVS_KEY);
    } else {
        ESP_LOGE(TAG, "factory reset erase failed key=%s ret=%s",
                 USER_DAILY_IMAGE_NVS_KEY, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t ServerNetworkStaDailyImage_ProcessJson(httpd_req_t *req,
                                                 const char *body,
                                                 size_t body_len,
                                                 const char *base_path)
{
#if USER_DAILY_IMAGE_ENABLE
    daily_image_config_t config = {0};
    int sw = -1;
    int result_code = TDX_JSON_RESULT_OK;
    const char *error = "no error";

    esp_err_t ret = DailyImageConfig_Parse(body,
                                           body_len,
                                           &config,
                                           &sw,
                                           &result_code,
                                           &error);
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "request parse failed ret=%s result=%d error=%s",
                 esp_err_to_name(ret), result_code, error);
        return send_result(
            req,
            result_code,
            strcmp(error, "invalid_sw") == 0
                ? "daily image switch rejected"
                : "daily image config rejected",
            error,
            sw,
            &config);
    }
    if (!s_daily_initialized) {
        return send_result(req,
                           TDX_JSON_RESULT_DAILY_JOB_SUBMIT_FAILED,
                           "daily image module is not ready",
                           "module_not_ready",
                           sw,
                           &config);
    }

    if (sw == 1) {
        int64_t now_epoch = 0;
        ret = DailyImageSchedule_GetNetworkNow(&now_epoch);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG,
                     "daily request rejected because current-boot SNTP is unavailable ret=%s",
                     esp_err_to_name(ret));
            return send_result(
                req,
                TDX_JSON_RESULT_DAILY_NETWORK_TIME_UNAVAILABLE,
                "network time is unavailable",
                "network_time_unavailable",
                sw,
                &config);
        }
        if (config.timestamp <= now_epoch) {
            ESP_LOGE(TAG,
                     "daily request timestamp is not future timestamp=%lld now=%lld",
                     (long long)config.timestamp,
                     (long long)now_epoch);
            return send_result(req,
                               TDX_JSON_RESULT_DAILY_TIME_INVALID,
                               "daily image timestamp must be in the future",
                               "timestamp_not_future",
                               sw,
                               &config);
        }
    }

    if (xSemaphoreTake(s_daily_config_mutex, portMAX_DELAY) != pdTRUE) {
        return send_result(req,
                           TDX_JSON_RESULT_DAILY_JOB_SUBMIT_FAILED,
                           "daily image module is busy",
                           "module_busy",
                           sw,
                           &config);
    }

    const char *effective_base_path =
        base_path != NULL ? base_path : s_daily_base_path;
    if (sw == 0) {
        ESP_LOGI(TAG, "daily request sw=0 disable");
        LocalImageBrowsing_Stop();
        ret = stop_slideshow_for_daily(effective_base_path);
        if (ret != ESP_OK) {
            xSemaphoreGive(s_daily_config_mutex);
            return send_result(req,
                               TDX_JSON_RESULT_DAILY_SLIDESHOW_STOP_FAILED,
                               "disable daily image failed",
                               "slideshow_stop_failed",
                               sw,
                               NULL);
        }

        ret = EpdDisplayMode_Set(USER_EPD_DISPLAY_MODE_NORMAL);
        if (ret != ESP_OK ||
            EpdDisplayMode_Get() != USER_EPD_DISPLAY_MODE_NORMAL) {
            ESP_LOGE(TAG, "daily disable NORMAL mode failed ret=%s mode=%u",
                     esp_err_to_name(ret),
                     (unsigned int)EpdDisplayMode_Get());
            xSemaphoreGive(s_daily_config_mutex);
            return send_result(req,
                               TDX_JSON_RESULT_DAILY_MODE_SAVE_FAILED,
                               "disable daily image mode save failed",
                               "mode_save_failed",
                               sw,
                               NULL);
        }

        s_daily_app_job_submitted = true;
        xSemaphoreGive(s_daily_config_mutex);
        ESP_LOGI(TAG, "daily image disabled mode=%u",
                 (unsigned int)EpdDisplayMode_Get());
        return send_result(req,
                           TDX_JSON_RESULT_OK,
                           "daily image disabled",
                           "no error",
                           sw,
                           NULL);
    }

    ESP_LOGI(TAG, "daily request sw=1 enable");
    uint8_t previous_mode = EpdDisplayMode_Get();
    ret = ImageBusinessWorker_Init();
    if (ret != ESP_OK) {
        xSemaphoreGive(s_daily_config_mutex);
        return send_result(req,
                           TDX_JSON_RESULT_DAILY_JOB_SUBMIT_FAILED,
                           "daily image worker initialization failed",
                           "worker_init_failed",
                           sw,
                           &config);
    }

    daily_image_config_t previous_config = {0};
    bool previous_config_valid = false;
    esp_err_t previous_ret = DailyImageConfig_Load(&previous_config);
    if (previous_ret == ESP_OK) {
        previous_config_valid = true;
    } else if (previous_ret != ESP_ERR_NVS_NOT_FOUND &&
               previous_ret != ESP_ERR_INVALID_CRC &&
               previous_ret != ESP_ERR_INVALID_SIZE) {
        ESP_LOGE(TAG, "previous daily config snapshot failed ret=%s",
                 esp_err_to_name(previous_ret));
        xSemaphoreGive(s_daily_config_mutex);
        return send_result(req,
                           TDX_JSON_RESULT_DAILY_CONFIG_SAVE_FAILED,
                           "daily image previous config read failed",
                           "previous_config_read_failed",
                           sw,
                           &config);
    }

    /*
     * Stop a startup-restored or older APP job before replacing its durable
     * configuration. The new queue item receives a later generation.
     */
    invalidate_previous_generation("app_config");

    ret = DailyImageConfig_Save(&config);
    if (ret != ESP_OK) {
        uint8_t rollback_mode =
            previous_mode == USER_EPD_DISPLAY_MODE_DAILY
                ? USER_EPD_DISPLAY_MODE_NORMAL
                : previous_mode;
        rollback_daily_enable(&previous_config,
                              previous_config_valid,
                              rollback_mode,
                              "config_save_failed");
        xSemaphoreGive(s_daily_config_mutex);
        return send_result(req,
                           TDX_JSON_RESULT_DAILY_CONFIG_SAVE_FAILED,
                           "daily image config save failed",
                           "config_save_failed",
                           sw,
                           &config);
    }

    ret = stop_slideshow_for_daily(effective_base_path);
    if (ret != ESP_OK) {
        rollback_daily_enable(&previous_config,
                              previous_config_valid,
                              USER_EPD_DISPLAY_MODE_NORMAL,
                              "slideshow_stop_failed");
        xSemaphoreGive(s_daily_config_mutex);
        return send_result(req,
                           TDX_JSON_RESULT_DAILY_SLIDESHOW_STOP_FAILED,
                           "stop slideshow failed",
                           "slideshow_stop_failed",
                           sw,
                           &config);
    }

    ret = EpdDisplayMode_Set(USER_EPD_DISPLAY_MODE_DAILY);
    if (ret != ESP_OK) {
        rollback_daily_enable(&previous_config,
                              previous_config_valid,
                              USER_EPD_DISPLAY_MODE_NORMAL,
                              "mode_save_failed");
        xSemaphoreGive(s_daily_config_mutex);
        return send_result(req,
                           TDX_JSON_RESULT_DAILY_MODE_SAVE_FAILED,
                           "daily mode save failed",
                           "mode_save_failed",
                           sw,
                           &config);
    }

    ret = submit_job(&config, "app");
    if (ret == ESP_OK) {
        s_daily_app_job_submitted = true;
    }
    if (ret != ESP_OK) {
        rollback_daily_enable(&previous_config,
                              previous_config_valid,
                              USER_EPD_DISPLAY_MODE_NORMAL,
                              "job_submit_failed");
        xSemaphoreGive(s_daily_config_mutex);
        return send_result(req,
                           TDX_JSON_RESULT_DAILY_JOB_SUBMIT_FAILED,
                           "daily image job submit failed",
                           "job_submit_failed",
                           sw,
                           &config);
    }
    xSemaphoreGive(s_daily_config_mutex);

    return send_result(req,
                       TDX_JSON_RESULT_OK,
                       "daily image config saved and accepted",
                       "no error",
                       sw,
                       &config);
#else
    (void)req;
    (void)body;
    (void)body_len;
    (void)base_path;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
