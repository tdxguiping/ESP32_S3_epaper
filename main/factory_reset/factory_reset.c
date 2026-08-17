#include "factory_reset.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "epd_display_app.h"
#include "epd_display_mode.h"
#include "app_persistent_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_status.h"
#include "local_image_browsing.h"
#include "nvs.h"
#include "ch583_wifi_uart_protocol.h"
#include "server_network_sta_slideshow.h"
#include "server_network_sta_daily_image.h"
#include "server_network_sta_wifi_work_time.h"
#include "tdx_cfg.h"
#include "tdx_shared_spi.h"

static const char *TAG = "factory_reset";

#define FACTORY_RESET_TASK_STACK_SIZE (5 * 1024)
#define FACTORY_RESET_TASK_PRIORITY 3
#define FACTORY_RESET_CH583_WAKE_SECONDS 10U
#define FACTORY_RESET_WELCOME_DISPLAY_SIZE 960000U

extern const uint8_t factory_reset_welcome_start[] asm("_binary_welcome_bin_start");
extern const uint8_t factory_reset_welcome_end[] asm("_binary_welcome_bin_end");

typedef struct {
    char base_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX];
} factory_reset_context_t;

typedef struct {
    int upload_bin_deleted;
    int upload_jpg_deleted;
    int cast_bin_deleted;
    int cast_jpg_deleted;
    int config_deleted;
    int file_delete_failed;
    esp_err_t file_ret;
    esp_err_t nvs_ret;
    esp_err_t welcome_display_ret;
} factory_reset_result_t;

typedef enum {
    FACTORY_RESET_REQUEST_IDLE = 0,
    FACTORY_RESET_REQUEST_PENDING,
    FACTORY_RESET_REQUEST_RUNNING,
    FACTORY_RESET_REQUEST_COMPLETED,
} factory_reset_request_state_t;

typedef struct {
    factory_reset_request_state_t state;
    factory_reset_trigger_t trigger;
    uint16_t protocol_seq;
    bool guard_armed;
} factory_reset_request_t;

static TaskHandle_t s_factory_reset_task = NULL;
static factory_reset_context_t s_factory_reset_ctx;
static portMUX_TYPE s_factory_reset_request_mux = portMUX_INITIALIZER_UNLOCKED;
static factory_reset_request_t s_factory_reset_request;
static bool s_factory_reset_ready;

static const char *factory_reset_trigger_to_string(factory_reset_trigger_t trigger)
{
    switch (trigger) {
    case FACTORY_RESET_TRIGGER_GPIO28:
        return "GPIO28";
    case FACTORY_RESET_TRIGGER_DEVICE_INFO_KEY_PB1:
        return "DEVICE_INFO_KEY_PB1";
    case FACTORY_RESET_TRIGGER_KEY_EVENT_PB1_PRESS:
        return "KEY_EVENT_PB1_PRESS";
    default:
        return "INVALID";
    }
}

static const char *factory_reset_request_state_to_string(
    factory_reset_request_state_t state)
{
    switch (state) {
    case FACTORY_RESET_REQUEST_IDLE:
        return "IDLE";
    case FACTORY_RESET_REQUEST_PENDING:
        return "PENDING";
    case FACTORY_RESET_REQUEST_RUNNING:
        return "RUNNING";
    case FACTORY_RESET_REQUEST_COMPLETED:
        return "COMPLETED";
    default:
        return "INVALID";
    }
}

static bool factory_reset_remote_request_is_pending(void)
{
    bool pending;
    taskENTER_CRITICAL(&s_factory_reset_request_mux);
    pending = s_factory_reset_request.state == FACTORY_RESET_REQUEST_PENDING;
    taskEXIT_CRITICAL(&s_factory_reset_request_mux);
    return pending;
}

static bool factory_reset_claim_remote_request(factory_reset_trigger_t *trigger,
                                               uint16_t *protocol_seq)
{
    bool claimed = false;
    taskENTER_CRITICAL(&s_factory_reset_request_mux);
    if (s_factory_reset_request.state == FACTORY_RESET_REQUEST_PENDING &&
        s_factory_reset_request.guard_armed) {
        s_factory_reset_request.state = FACTORY_RESET_REQUEST_RUNNING;
        if (trigger != NULL) {
            *trigger = s_factory_reset_request.trigger;
        }
        if (protocol_seq != NULL) {
            *protocol_seq = s_factory_reset_request.protocol_seq;
        }
        claimed = true;
    }
    taskEXIT_CRITICAL(&s_factory_reset_request_mux);
    return claimed;
}

static bool factory_reset_claim_gpio_request(void)
{
    bool claimed = false;
    taskENTER_CRITICAL(&s_factory_reset_request_mux);
    if (s_factory_reset_request.state == FACTORY_RESET_REQUEST_IDLE) {
        ServerNetworkStaWifiWorkTime_SetFactoryResetGuard(true);
        s_factory_reset_request.state = FACTORY_RESET_REQUEST_RUNNING;
        s_factory_reset_request.trigger = FACTORY_RESET_TRIGGER_GPIO28;
        s_factory_reset_request.protocol_seq = 0;
        claimed = true;
    }
    taskEXIT_CRITICAL(&s_factory_reset_request_mux);
    return claimed;
}

static void factory_reset_finish_request(bool succeeded)
{
    taskENTER_CRITICAL(&s_factory_reset_request_mux);
    if (s_factory_reset_request.state == FACTORY_RESET_REQUEST_RUNNING) {
        s_factory_reset_request.state = succeeded
                                            ? FACTORY_RESET_REQUEST_COMPLETED
                                            : FACTORY_RESET_REQUEST_IDLE;
        s_factory_reset_request.guard_armed = false;
    }
    taskEXIT_CRITICAL(&s_factory_reset_request_mux);
}

static bool factory_reset_name_has_ext(const char *name, const char *ext)
{
    size_t name_len;
    size_t ext_len;

    if (name == NULL || ext == NULL) {
        return false;
    }
    name_len = strlen(name);
    ext_len = strlen(ext);
    return name_len > ext_len && strcasecmp(name + name_len - ext_len, ext) == 0;
}

static void factory_reset_record_file_failure(factory_reset_result_t *result,
                                              const char *path,
                                              int error_number)
{
    if (result == NULL) {
        return;
    }
    result->file_delete_failed++;
    if (result->file_ret == ESP_OK) {
        result->file_ret = ESP_FAIL;
        ESP_LOGE(TAG,
                 "factory reset file cleanup failed path=%s errno=%d",
                 path != NULL ? path : "",
                 error_number);
    }
}

static bool factory_reset_delete_path_if_exists(const char *path,
                                                factory_reset_result_t *result)
{
    if (unlink(path) == 0) {
        return true;
    }
    if (errno != ENOENT) {
        factory_reset_record_file_failure(result, path, errno);
    }
    return false;
}

static int factory_reset_delete_files_with_ext(const char *dir_path,
                                               const char *ext,
                                               factory_reset_result_t *result)
{
    DIR *dir = opendir(dir_path);
    int deleted = 0;

    if (dir == NULL) {
        if (errno == ENOENT) {
            ESP_LOGW(TAG, "factory reset image dir missing path=%s", dir_path);
        } else {
            factory_reset_record_file_failure(result, dir_path, errno);
        }
        return 0;
    }

    struct dirent *entry = NULL;
    while (true) {
        errno = 0;
        entry = readdir(dir);
        if (entry == NULL) {
            if (errno != 0) {
                factory_reset_record_file_failure(result, dir_path, errno);
            }
            break;
        }
        if (!factory_reset_name_has_ext(entry->d_name, ext)) {
            continue;
        }

        // Factory reset must still remove legacy files whose names predate the 16-byte protocol limit.
        char path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX + 24];
        int len = snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        if (len < 0 || (size_t)len >= sizeof(path)) {
            factory_reset_record_file_failure(result, dir_path, ENAMETOOLONG);
            continue;
        }
        if (factory_reset_delete_path_if_exists(path, result)) {
            deleted++;
        }
    }

    if (closedir(dir) != 0) {
        factory_reset_record_file_failure(result, dir_path, errno);
    }
    return deleted;
}

static esp_err_t factory_reset_save_default_epd_mode(void)
{
    const uint8_t default_mode = USER_EPD_DISPLAY_MODE_DEFAULT;
    if (EpdDisplayMode_Get() != default_mode) {
        return EpdDisplayMode_Set(default_mode);
    }

    esp_err_t ret =
        app_nvs_write_u8(USER_EPD_DISPLAY_MODE_NVS_KEY, default_mode);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t verified_mode = UINT8_MAX;
    ret = app_nvs_read_u8(USER_EPD_DISPLAY_MODE_NVS_KEY,
                          &verified_mode,
                          USER_EPD_DISPLAY_MODE_DEFAULT);
    if (ret != ESP_OK) {
        return ret;
    }
    if (verified_mode != default_mode) {
        ESP_LOGE(TAG,
                 "factory reset default EPD mode verify failed expected=%u read=%u",
                 (unsigned int)default_mode,
                 (unsigned int)verified_mode);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t factory_reset_erase_wifi_namespace(void)
{
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open("wifi", NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "factory reset open WiFi namespace failed ret=%s",
                 esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_erase_all(handle);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "factory reset erase WiFi namespace failed ret=%s",
                 esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t factory_reset_erase_net80211_credentials(void)
{
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open("nvs.net80211", NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "factory reset open nvs.net80211 failed ret=%s",
                 esp_err_to_name(ret));
        return ret;
    }

    esp_err_t ssid_ret = nvs_erase_key(handle, "sta.ssid");
    if (ssid_ret == ESP_ERR_NVS_NOT_FOUND) {
        ssid_ret = ESP_OK;
    }
    esp_err_t password_ret = nvs_erase_key(handle, "sta.pswd");
    if (password_ret == ESP_ERR_NVS_NOT_FOUND) {
        password_ret = ESP_OK;
    }
    if (ssid_ret == ESP_OK && password_ret == ESP_OK) {
        ret = nvs_commit(handle);
    } else {
        ret = ssid_ret != ESP_OK ? ssid_ret : password_ret;
    }
    nvs_close(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "factory reset erase nvs.net80211 credentials failed ret=%s",
                 esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t factory_reset_clear_wifi_credentials(void)
{
    esp_err_t wifi_ret = factory_reset_erase_wifi_namespace();
    esp_err_t net80211_ret = factory_reset_erase_net80211_credentials();
    if (wifi_ret != ESP_OK) {
        return wifi_ret;
    }
    return net80211_ret;
}

static void factory_reset_clear_persistent_state(factory_reset_result_t *result)
{
    esp_err_t ret = ESP_OK;
    esp_err_t one_ret;

    /*
     * English: Do not erase the whole PhotoPainter namespace. EPD type, work
     * timers, and CH583 BLE MAC are production-critical settings that survive.
     * Saved WiFi credentials are cleared separately from their two namespaces.
     */
    one_ret = app_nvs_erase_key(TDX_SLIDESHOW_NVS_PROGRESS_KEY);
    if (one_ret != ESP_OK && ret == ESP_OK) {
        ret = one_ret;
    }
    one_ret = app_nvs_erase_key(TDX_SLIDESHOW_NVS_LAST_FILE_KEY);
    if (one_ret != ESP_OK && ret == ESP_OK) {
        ret = one_ret;
    }
    one_ret = AppPersistentState_EraseImageState();
    if (one_ret != ESP_OK && ret == ESP_OK) {
        ret = one_ret;
    }
    one_ret = LocalImageBrowsing_ResetState();
    if (one_ret != ESP_OK && ret == ESP_OK) {
        ret = one_ret;
    }

    /*
     * Select and persist DEFAULT before erasing the daily configuration. A running daily
     * worker checks this mode under its config lock and cannot restore the
     * erased configuration after factory reset.
     */
    one_ret = factory_reset_save_default_epd_mode();
    if (one_ret != ESP_OK && ret == ESP_OK) {
        ret = one_ret;
    }
    if (one_ret == ESP_OK) {
        one_ret = ServerNetworkStaDailyImage_ResetConfig();
        if (one_ret != ESP_OK && ret == ESP_OK) {
            ret = one_ret;
        }
    } else {
        ESP_LOGE(TAG,
                 "factory reset keeps daily config because DEFAULT mode was not saved");
    }

    one_ret = factory_reset_clear_wifi_credentials();
    if (one_ret != ESP_OK && ret == ESP_OK) {
        ret = one_ret;
    }

    if (result != NULL) {
        result->nvs_ret = ret;
    }
}

static esp_err_t factory_reset_execute(const char *base_path,
                                       factory_reset_result_t *result)
{
    char bin_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    char jpg_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    char cast_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    struct stat st = {0};

    if (base_path == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));
    result->file_ret = ESP_OK;
    result->nvs_ret = ESP_OK;
    result->welcome_display_ret = ESP_OK;

    snprintf(bin_dir, sizeof(bin_dir), "%s/bin_img", base_path);
    snprintf(jpg_dir, sizeof(jpg_dir), "%s/jpg_img", base_path);
    snprintf(cast_dir, sizeof(cast_dir), "%s/cast_img", base_path);

    if (stat(base_path, &st) != 0) {
        ESP_LOGW(TAG, "factory reset skipped storage not ready base=%s", base_path);
        return ESP_ERR_NOT_FOUND;
    }

    /*
     * English: Stop slideshow before deleting image files so the slideshow task
     * does not try to preload a file that is being removed. EPD display itself is
     * not interrupted; cleanup only proceeds when the display queue is idle.
     */
    ServerNetworkStaSlideshow_Stop();

    /*
     * Reserve the idle EPD for the complete cleanup transaction. This closes
     * the check-then-act window where a PB2 event could otherwise start local
     * browsing immediately after a standalone busy check.
    */
    epd_display_reservation_t reset_reservation = {0};
#if USER_EPD_ENABLE
    esp_err_t reserve_ret =
        ServerNetworkStaEpdDisplay_TryReserveIdle(&reset_reservation);
    if (reserve_ret != ESP_OK) {
        ESP_LOGW(TAG, "factory reset aborted because EPD became BUSY");
        return ESP_ERR_INVALID_STATE;
    }
#endif

    esp_err_t lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        ServerNetworkStaEpdDisplay_ReleaseReservation(&reset_reservation);
        return lock_ret;
    }

    /*
     * English: Factory-default image cleanup deletes saved image payloads from
     * both upload/slideshow storage and cast/cast2pic cache. It intentionally
     * keeps device identity, EPD type, work timers, CH583 data, and OTA state.
     * Saved WiFi credentials are cleared after releasing the shared SPI lock.
     */
    result->upload_bin_deleted =
        factory_reset_delete_files_with_ext(bin_dir, ".bin", result);
    result->upload_jpg_deleted =
        factory_reset_delete_files_with_ext(jpg_dir, ".jpg", result);
    result->cast_bin_deleted =
        factory_reset_delete_files_with_ext(cast_dir, ".bin", result);
    result->cast_jpg_deleted =
        factory_reset_delete_files_with_ext(cast_dir, ".jpg", result);

    TdxSharedSpi_Unlock();

    factory_reset_clear_persistent_state(result);
    esp_err_t cleanup_ret =
        result->file_ret != ESP_OK ? result->file_ret : result->nvs_ret;
    if (cleanup_ret == ESP_OK) {
        const epd_type_config_t *epd_config = EpdType_GetCurrentConfig();
        size_t welcome_size =
            (size_t)(factory_reset_welcome_end - factory_reset_welcome_start);
        if (epd_config == NULL ||
            epd_config->display_size != FACTORY_RESET_WELCOME_DISPLAY_SIZE) {
            result->welcome_display_ret = ESP_ERR_INVALID_SIZE;
            ESP_LOGE(TAG,
                     "factory reset welcome display size unsupported epd_type=%u expected=%u",
                     epd_config != NULL ? (unsigned int)epd_config->type : 0U,
                     (unsigned int)FACTORY_RESET_WELCOME_DISPLAY_SIZE);
        } else {
            result->welcome_display_ret =
                ServerNetworkStaEpdDisplay_QueueReservedToScreenAndWait(
                    &reset_reservation,
                    factory_reset_welcome_start,
                    welcome_size,
                    1);
            if (result->welcome_display_ret == ESP_OK) {
                ESP_LOGI(TAG,
                         "factory reset welcome display completed compressed_size=%u",
                         (unsigned int)welcome_size);
            } else {
                ESP_LOGE(TAG,
                         "factory reset welcome display failed ret=%s",
                         esp_err_to_name(result->welcome_display_ret));
            }
        }
    }
    ServerNetworkStaEpdDisplay_ReleaseReservation(&reset_reservation);
    return cleanup_ret != ESP_OK ? cleanup_ret : result->welcome_display_ret;
}

static bool factory_reset_button_is_active(void)
{
    return gpio_get_level(TDX_FACTORY_RESET_GPIO) == TDX_FACTORY_RESET_ACTIVE_LEVEL;
}

static void factory_reset_run(const factory_reset_context_t *ctx,
                              factory_reset_trigger_t trigger,
                              uint16_t protocol_seq)
{
    ESP_LOGW(TAG,
             "factory reset start source=%s seq=%u",
             factory_reset_trigger_to_string(trigger),
             (unsigned int)protocol_seq);

    ServerNetworkStaWifiWorkTime_SetFactoryResetGuard(true);
    UserLedStatus_FactoryResetBegin();
    factory_reset_result_t result = {
        .file_ret = ESP_OK,
        .nvs_ret = ESP_OK,
    };
    esp_err_t ret = factory_reset_execute(ctx->base_path, &result);
    ESP_LOGW(TAG,
             "factory reset done source=%s seq=%u ret=%s upload_bin_deleted=%d upload_jpg_deleted=%d cast_bin_deleted=%d cast_jpg_deleted=%d cfg_deleted=%d file_delete_failed=%d file_ret=%s nvs_ret=%s welcome_display_ret=%s",
             factory_reset_trigger_to_string(trigger),
             (unsigned int)protocol_seq,
             esp_err_to_name(ret),
             result.upload_bin_deleted,
             result.upload_jpg_deleted,
             result.cast_bin_deleted,
             result.cast_jpg_deleted,
             result.config_deleted,
             result.file_delete_failed,
             esp_err_to_name(result.file_ret),
             esp_err_to_name(result.nvs_ret),
             esp_err_to_name(result.welcome_display_ret));

    if (ret == ESP_OK) {
        int provision_ret = ch583_wifi_uart_send_wifi_provision_status(0);
        if (provision_ret < 0) {
            ESP_LOGE(TAG,
                     "factory reset WIFI_PROVISION unconfigured send failed ret=%d",
                     provision_ret);
        }
        ServerNetworkStaWifiWorkTime_RequestFactoryResetPowerCycle(
            FACTORY_RESET_CH583_WAKE_SECONDS);
    }
    if (ret == ESP_OK) {
        factory_reset_finish_request(true);
        ServerNetworkStaWifiWorkTime_SetFactoryResetGuard(false);
    } else {
        /* Clear the old guard before reopening IDLE so a new request cannot
         * have its freshly-set guard cleared by this failed transaction. */
        ServerNetworkStaWifiWorkTime_SetFactoryResetGuard(false);
        factory_reset_finish_request(false);
    }
    UserLedStatus_FactoryResetEnd();
#if TDX_FACTORY_RESET_RESTART_AFTER_DONE
    if (ret == ESP_OK) {
        UserLedStatus_SetRestartPending(true);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    }
#endif
}

static void factory_reset_task(void *arg)
{
    const factory_reset_context_t *ctx = (const factory_reset_context_t *)arg;
    uint32_t held_ms = 0;
    bool waiting_release = false;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(TDX_FACTORY_RESET_CHECK_MS));

        if (factory_reset_remote_request_is_pending()) {
            held_ms = 0;
            if (ServerNetworkStaEpdDisplay_IsBusy()) {
                continue;
            }
            factory_reset_trigger_t trigger;
            uint16_t protocol_seq;
            if (factory_reset_claim_remote_request(&trigger, &protocol_seq)) {
                factory_reset_run(ctx, trigger, protocol_seq);
                continue;
            }
        }

        /* English: Do not sample GPIO28 while EPD is busy; this prevents image cleanup from racing display I/O. */
        if (ServerNetworkStaEpdDisplay_IsBusy()) {
            held_ms = 0;
            continue;
        }

        bool active = factory_reset_button_is_active();
        if (waiting_release) {
            if (!active) {
                waiting_release = false;
                held_ms = 0;
                ESP_LOGI(TAG, "factory reset button released, ready");
            }
            continue;
        }

        if (!active) {
            /* English: Any high-level sample before HOLD_MS cancels the hold and starts timing from zero next press. */
            held_ms = 0;
            continue;
        }

        held_ms += TDX_FACTORY_RESET_CHECK_MS;
        if (held_ms < TDX_FACTORY_RESET_HOLD_MS) {
            continue;
        }

        waiting_release = true;
        ESP_LOGW(TAG,
                 "factory reset button held gpio=%d hold_ms=%lu, start clear images",
                 (int)TDX_FACTORY_RESET_GPIO,
                 (unsigned long)held_ms);

        if (factory_reset_claim_gpio_request()) {
            factory_reset_run(ctx, FACTORY_RESET_TRIGGER_GPIO28, 0);
        }
    }
}

esp_err_t FactoryReset_Request(factory_reset_trigger_t trigger,
                               uint16_t protocol_seq)
{
#if !TDX_FACTORY_RESET_ENABLE
    (void)trigger;
    (void)protocol_seq;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (trigger != FACTORY_RESET_TRIGGER_DEVICE_INFO_KEY_PB1 &&
        trigger != FACTORY_RESET_TRIGGER_KEY_EVENT_PB1_PRESS) {
        return ESP_ERR_INVALID_ARG;
    }

    bool accepted = false;
    factory_reset_request_state_t state;
    taskENTER_CRITICAL(&s_factory_reset_request_mux);
    state = s_factory_reset_request.state;
    if (state == FACTORY_RESET_REQUEST_IDLE) {
        s_factory_reset_request.trigger = trigger;
        s_factory_reset_request.protocol_seq = protocol_seq;
        s_factory_reset_request.guard_armed = false;
        if (s_factory_reset_ready) {
            ServerNetworkStaWifiWorkTime_SetFactoryResetGuard(true);
            s_factory_reset_request.guard_armed = true;
        }
        s_factory_reset_request.state = FACTORY_RESET_REQUEST_PENDING;
        state = FACTORY_RESET_REQUEST_PENDING;
        accepted = true;
    }
    taskEXIT_CRITICAL(&s_factory_reset_request_mux);

    if (accepted) {
        ESP_LOGW(TAG,
                 "factory reset request pending source=%s seq=%u",
                 factory_reset_trigger_to_string(trigger),
                 (unsigned int)protocol_seq);
    } else {
        ESP_LOGI(TAG,
                 "factory reset request coalesced source=%s seq=%u state=%s",
                 factory_reset_trigger_to_string(trigger),
                 (unsigned int)protocol_seq,
                 factory_reset_request_state_to_string(state));
    }
    return ESP_OK;
#endif
}

esp_err_t FactoryReset_Init(const char *base_path)
{
#if !TDX_FACTORY_RESET_ENABLE
    (void)base_path;
    return ESP_OK;
#else
    if (base_path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_factory_reset_task != NULL) {
        return ESP_OK;
    }

    memset(&s_factory_reset_ctx, 0, sizeof(s_factory_reset_ctx));
    strlcpy(s_factory_reset_ctx.base_path, base_path, sizeof(s_factory_reset_ctx.base_path));

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << TDX_FACTORY_RESET_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "factory reset gpio config failed gpio=%d ret=%s",
                 (int)TDX_FACTORY_RESET_GPIO,
                 esp_err_to_name(ret));
        return ret;
    }

    BaseType_t task_ret = xTaskCreate(factory_reset_task,
                                      "factory_reset",
                                      FACTORY_RESET_TASK_STACK_SIZE,
                                      &s_factory_reset_ctx,
                                      FACTORY_RESET_TASK_PRIORITY,
                                      &s_factory_reset_task);
    if (task_ret != pdPASS) {
        s_factory_reset_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    taskENTER_CRITICAL(&s_factory_reset_request_mux);
    s_factory_reset_ready = true;
    if (s_factory_reset_request.state == FACTORY_RESET_REQUEST_PENDING) {
        ServerNetworkStaWifiWorkTime_SetFactoryResetGuard(true);
        s_factory_reset_request.guard_armed = true;
    }
    taskEXIT_CRITICAL(&s_factory_reset_request_mux);

    ESP_LOGI(TAG,
             "factory reset gpio init pin=%d active=%d check_ms=%u hold_ms=%u",
             (int)TDX_FACTORY_RESET_GPIO,
             TDX_FACTORY_RESET_ACTIVE_LEVEL,
             (unsigned int)TDX_FACTORY_RESET_CHECK_MS,
             (unsigned int)TDX_FACTORY_RESET_HOLD_MS);
    return ESP_OK;
#endif
}
