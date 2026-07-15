#include "factory_reset.h"

#include <dirent.h>
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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_status.h"
#include "server_network_sta_slideshow.h"
#include "tdx_cfg.h"
#include "tdx_shared_spi.h"

static const char *TAG = "factory_reset";

#define FACTORY_RESET_TASK_STACK_SIZE (5 * 1024)
#define FACTORY_RESET_TASK_PRIORITY 3

typedef struct {
    char base_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX];
} factory_reset_context_t;

typedef struct {
    int upload_bin_deleted;
    int upload_jpg_deleted;
    int cast_bin_deleted;
    int cast_jpg_deleted;
    int config_deleted;
    esp_err_t nvs_ret;
} factory_reset_result_t;

static TaskHandle_t s_factory_reset_task = NULL;
static factory_reset_context_t s_factory_reset_ctx;

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

static bool factory_reset_delete_path_if_exists(const char *path)
{
    if (unlink(path) == 0) {
        return true;
    }
    return false;
}

static int factory_reset_delete_files_with_ext(const char *dir_path, const char *ext)
{
    DIR *dir = opendir(dir_path);
    int deleted = 0;

    if (dir == NULL) {
        ESP_LOGW(TAG, "factory reset image dir missing path=%s", dir_path);
        return 0;
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (!factory_reset_name_has_ext(entry->d_name, ext)) {
            continue;
        }

        char path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + TDX_SLIDESHOW_FILE_NAME_MAX_LEN + 24];
        int len = snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        if (len < 0 || (size_t)len >= sizeof(path)) {
            ESP_LOGW(TAG, "factory reset skip long path dir=%s name=%s", dir_path, entry->d_name);
            continue;
        }
        if (factory_reset_delete_path_if_exists(path)) {
            deleted++;
        }
    }

    closedir(dir);
    return deleted;
}

static void factory_reset_clear_slideshow_nvs(factory_reset_result_t *result)
{
    esp_err_t ret = ESP_OK;
    esp_err_t one_ret;

    /*
     * English: Only slideshow runtime keys are erased here. Do not erase the whole
     * PhotoPainter namespace, because WiFi credentials, EPD type, work timers, and
     * CH583 BLE MAC are production-critical settings that must survive this reset.
     */
    one_ret = app_nvs_erase_key(TDX_SLIDESHOW_NVS_PROGRESS_KEY);
    if (one_ret != ESP_OK && ret == ESP_OK) {
        ret = one_ret;
    }
    one_ret = app_nvs_erase_key(TDX_SLIDESHOW_NVS_LAST_FILE_KEY);
    if (one_ret != ESP_OK && ret == ESP_OK) {
        ret = one_ret;
    }
    one_ret = app_nvs_write_str(TDX_SLIDESHOW_RANDOM_NVS_KEY, "false");
    if (one_ret != ESP_OK && ret == ESP_OK) {
        ret = one_ret;
    }
    g_slideshow_random_enable = 0;

    if (result != NULL) {
        result->nvs_ret = ret;
    }
}

static esp_err_t factory_reset_clear_images(const char *base_path, factory_reset_result_t *result)
{
    char bin_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    char jpg_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    char cast_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    char slideshow_config[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    char slideshow_control[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    char last_cast[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    struct stat st = {0};

    if (base_path == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));
    result->nvs_ret = ESP_OK;

    snprintf(bin_dir, sizeof(bin_dir), "%s/bin_img", base_path);
    snprintf(jpg_dir, sizeof(jpg_dir), "%s/jpg_img", base_path);
    snprintf(cast_dir, sizeof(cast_dir), "%s/cast_img", base_path);
    snprintf(slideshow_config, sizeof(slideshow_config), "%s/%s", bin_dir, TDX_SLIDESHOW_CONFIG_FILE);
    snprintf(slideshow_control, sizeof(slideshow_control), "%s/%s", bin_dir, TDX_SLIDESHOW_CONTROL_FILE);
    snprintf(last_cast, sizeof(last_cast), "%s/last_cast.txt", cast_dir);

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

    if (ServerNetworkStaEpdDisplay_IsBusy()) {
        ESP_LOGW(TAG, "factory reset aborted because EPD became BUSY");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }

    /*
     * English: Factory-default image cleanup deletes saved image payloads from
     * both upload/slideshow storage and cast/cast2pic cache. It intentionally
     * keeps device identity, WiFi, EPD type, work timers, CH583 data, and OTA state.
     */
    result->upload_bin_deleted = factory_reset_delete_files_with_ext(bin_dir, ".bin");
    result->upload_jpg_deleted = factory_reset_delete_files_with_ext(jpg_dir, ".jpg");
    result->cast_bin_deleted = factory_reset_delete_files_with_ext(cast_dir, ".bin");
    result->cast_jpg_deleted = factory_reset_delete_files_with_ext(cast_dir, ".jpg");
    if (factory_reset_delete_path_if_exists(slideshow_config)) {
        result->config_deleted++;
    }
    if (factory_reset_delete_path_if_exists(slideshow_control)) {
        result->config_deleted++;
    }
    if (factory_reset_delete_path_if_exists(last_cast)) {
        result->config_deleted++;
    }

    TdxSharedSpi_Unlock();

    factory_reset_clear_slideshow_nvs(result);
    return result->nvs_ret;
}

static bool factory_reset_button_is_active(void)
{
    return gpio_get_level(TDX_FACTORY_RESET_GPIO) == TDX_FACTORY_RESET_ACTIVE_LEVEL;
}

static void factory_reset_task(void *arg)
{
    const factory_reset_context_t *ctx = (const factory_reset_context_t *)arg;
    uint32_t held_ms = 0;
    bool waiting_release = false;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(TDX_FACTORY_RESET_CHECK_MS));

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

        UserLedStatus_FactoryResetBegin();
        factory_reset_result_t result;
        esp_err_t ret = factory_reset_clear_images(ctx->base_path, &result);
        ESP_LOGW(TAG,
                 "factory reset done ret=%s upload_bin_deleted=%d upload_jpg_deleted=%d cast_bin_deleted=%d cast_jpg_deleted=%d cfg_deleted=%d nvs_ret=%s",
                 esp_err_to_name(ret),
                 result.upload_bin_deleted,
                 result.upload_jpg_deleted,
                 result.cast_bin_deleted,
                 result.cast_jpg_deleted,
                 result.config_deleted,
                 esp_err_to_name(result.nvs_ret));

#if TDX_FACTORY_RESET_RESTART_AFTER_DONE
        if (ret == ESP_OK) {
            UserLedStatus_SetRestartPending(true);
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        }
#endif
        UserLedStatus_FactoryResetEnd();
    }
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

    ESP_LOGI(TAG,
             "factory reset gpio init pin=%d active=%d check_ms=%u hold_ms=%u",
             (int)TDX_FACTORY_RESET_GPIO,
             TDX_FACTORY_RESET_ACTIVE_LEVEL,
             (unsigned int)TDX_FACTORY_RESET_CHECK_MS,
             (unsigned int)TDX_FACTORY_RESET_HOLD_MS);
    return ESP_OK;
#endif
}
