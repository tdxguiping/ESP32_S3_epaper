#include "factory_welcome_resource.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epd_type.h"
#include "epd_display_app.h"
#include "epd_sd_power_test.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "file_serving_example_common.h"
#include "factory_welcome_config.h"
#include "factory_welcome_http.h"
#include "factory_welcome_manifest.h"
#include "factory_welcome_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_ota_upload.h"
#include "server_network_sta_wifi_work_time.h"

static const char *TAG = "factory_welcome";

typedef struct {
    uint32_t generation;
    factory_welcome_cancel_fn_t is_canceled;
    TickType_t deadline;
} factory_welcome_run_context_t;

static bool ota_is_busy(void)
{
    return NetworkOtaUpload_IsRestartPending() ||
           ServerNetworkStaWifiWorkTime_IsOtaBusy();
}

static bool run_can_continue(void *opaque)
{
    factory_welcome_run_context_t *context =
        (factory_welcome_run_context_t *)opaque;
    if (context == NULL || ota_is_busy() ||
        (context->is_canceled != NULL &&
         context->is_canceled(context->generation))) {
        return false;
    }
    return (int32_t)(context->deadline - xTaskGetTickCount()) > 0;
}

static bool run_owner_is_active(const factory_welcome_run_context_t *context)
{
    return context != NULL && !ota_is_busy() &&
           (context->is_canceled == NULL ||
           !context->is_canceled(context->generation));
}

static void display_test_pattern_fallback(
    const factory_welcome_run_context_t *context,
    esp_err_t reason)
{
    if (!run_owner_is_active(context)) {
        return;
    }
    ESP_LOGW(TAG, "resource display unavailable ret=%s, queue EPD test pattern",
             esp_err_to_name(reason));
    test_epd_display();
}

static uint32_t run_remaining_ms(const factory_welcome_run_context_t *context)
{
    int32_t remaining_ticks = (int32_t)(context->deadline - xTaskGetTickCount());
    if (remaining_ticks <= 0) {
        return 0U;
    }
    uint32_t remaining_ms = (uint32_t)pdTICKS_TO_MS((TickType_t)remaining_ticks);
    return remaining_ms > 0U ? remaining_ms : 1U;
}

static esp_err_t wait_for_sd(factory_welcome_run_context_t *context)
{
    TickType_t deadline = xTaskGetTickCount() +
        pdMS_TO_TICKS(FACTORY_WELCOME_SD_READY_WAIT_MS);
    while (run_can_continue(context)) {
        example_storage_type_t type = example_storage_get_type();
        if (type == EXAMPLE_STORAGE_TYPE_SD_CARD &&
            EpdSdPowerTest_IsReadyForImmediateSharedSpi()) {
            return ESP_OK;
        }
        if (type == EXAMPLE_STORAGE_TYPE_SPIFFS) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        if ((int32_t)(deadline - xTaskGetTickCount()) <= 0) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(FACTORY_WELCOME_SD_READY_POLL_MS));
    }
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t http_get_with_retry(const char *url,
                                     uint8_t *buffer,
                                     size_t capacity,
                                     size_t expected_size,
                                     size_t *received_size,
                                     factory_welcome_run_context_t *context)
{
    esp_err_t ret = ESP_FAIL;
    for (uint32_t attempt = 1U;
         attempt <= FACTORY_WELCOME_HTTP_RETRY_COUNT && run_can_continue(context);
         ++attempt) {
        uint32_t timeout_ms = run_remaining_ms(context);
        if (timeout_ms > FACTORY_WELCOME_HTTP_TIMEOUT_MS) {
            timeout_ms = FACTORY_WELCOME_HTTP_TIMEOUT_MS;
        }
        ret = FactoryWelcomeHttp_Get(url,
                                     buffer,
                                     capacity,
                                     expected_size,
                                     received_size,
                                     timeout_ms,
                                     run_can_continue,
                                     context);
        if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
            return ret;
        }
        if (attempt < FACTORY_WELCOME_HTTP_RETRY_COUNT) {
            ESP_LOGW(TAG, "HTTP retry attempt=%lu/%u ret=%s",
                     (unsigned long)(attempt + 1U),
                     (unsigned int)FACTORY_WELCOME_HTTP_RETRY_COUNT,
                     esp_err_to_name(ret));
            uint32_t delay_ms = run_remaining_ms(context);
            if (delay_ms > FACTORY_WELCOME_HTTP_RETRY_DELAY_MS) {
                delay_ms = FACTORY_WELCOME_HTTP_RETRY_DELAY_MS;
            }
            if (delay_ms > 0U) {
                vTaskDelay(pdMS_TO_TICKS(delay_ms));
            }
        }
    }
    return run_can_continue(context) ? ret : ESP_ERR_INVALID_STATE;
}

static esp_err_t current_resolution(char *resolution, size_t resolution_size)
{
    const epd_type_config_t *config = EpdType_GetCurrentConfig();
    if (config == NULL || config->width == 0U || config->height == 0U) {
        return ESP_ERR_INVALID_STATE;
    }
    uint16_t short_side = config->width < config->height ?
                          config->width : config->height;
    uint16_t long_side = config->width < config->height ?
                         config->height : config->width;
    int length = snprintf(resolution, resolution_size, "%ux%u",
                          (unsigned int)short_side,
                          (unsigned int)long_side);
    return length > 0 && (size_t)length < resolution_size ?
           ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t download_required_files(
    const char *base_path,
    const factory_welcome_manifest_t *remote,
    bool force_all,
    bool *changed,
    factory_welcome_run_context_t *context)
{
    if (changed == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *changed = false;
    for (size_t i = 0; i < remote->file_count; ++i) {
        const factory_welcome_file_t *file = &remote->files[i];
        bool matches = false;
        esp_err_t ret = FactoryWelcomeStorage_FileHasSize(
            base_path, file->file_name, file->compressed_size, &matches);
        if (ret != ESP_OK) {
            return ret;
        }
        if (!force_all && matches) {
            continue;
        }
        if (!run_can_continue(context)) {
            return ESP_ERR_INVALID_STATE;
        }

        uint8_t *data = (uint8_t *)heap_caps_malloc(
            file->compressed_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (data == NULL) {
            return ESP_ERR_NO_MEM;
        }
        size_t received_size = 0U;
        ret = http_get_with_retry(file->url,
                                  data,
                                  file->compressed_size,
                                  file->compressed_size,
                                  &received_size,
                                  context);
        if (ret == ESP_OK && received_size == file->compressed_size &&
            run_can_continue(context)) {
            ret = FactoryWelcomeStorage_WriteFileAtomic(base_path,
                                                        file->file_name,
                                                        data,
                                                        received_size);
        }
        free(data);
        if (ret != ESP_OK) {
            return ret;
        }
        *changed = true;
        ESP_LOGI(TAG, "file saved name=%s size=%u",
                 file->file_name, (unsigned int)file->compressed_size);
    }
    return ESP_OK;
}

static esp_err_t display_resource_file(
    const char *base_path,
    const char *file_name,
    size_t compressed_size,
    const factory_welcome_run_context_t *context)
{
    if (file_name == NULL || file_name[0] == '\0' || compressed_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!run_owner_is_active(context)) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t *data = (uint8_t *)heap_caps_malloc(
        compressed_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t ret = FactoryWelcomeStorage_ReadFileExact(
        base_path, file_name, data, compressed_size);
    if (ret == ESP_OK && run_owner_is_active(context)) {
        ESP_LOGI(TAG, "display start name=%s size=%u",
                 file_name, (unsigned int)compressed_size);
        ret = ServerNetworkStaEpdDisplay_QueueToScreenAndWait(
            data, compressed_size, 1U);
    } else if (ret == ESP_OK) {
        ret = ESP_ERR_INVALID_STATE;
    }
    free(data);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "display complete name=%s", file_name);
    } else if (ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "display failed name=%s ret=%s",
                 file_name, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t FactoryWelcomeResource_Sync(const char *base_path,
                                      uint32_t generation,
                                      factory_welcome_cancel_fn_t is_canceled)
{
    if (base_path == NULL || base_path[0] == '\0' || generation == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    factory_welcome_run_context_t context = {
        .generation = generation,
        .is_canceled = is_canceled,
        .deadline = xTaskGetTickCount() +
                    pdMS_TO_TICKS(FACTORY_WELCOME_TOTAL_TIMEOUT_MS),
    };
    if (ota_is_busy()) {
        ESP_LOGW(TAG, "sync skipped reason=ota");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = wait_for_sd(&context);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "sync skipped reason=sd_not_ready ret=%s",
                 esp_err_to_name(ret));
        display_test_pattern_fallback(&context, ret);
        return ret;
    }

    char resolution[16] = {0};
    ret = current_resolution(resolution, sizeof(resolution));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "current resolution unavailable ret=%s",
                 esp_err_to_name(ret));
        display_test_pattern_fallback(&context, ret);
        return ret;
    }
    ESP_LOGI(TAG, "sync start resolution=%s", resolution);

    ServerNetworkStaWifiWorkTime_ImageTransferBegin();
    EpdSdPowerTest_ImageTransferBegin();

    bool display_selected = false;
    char display_file_name[FACTORY_WELCOME_FILE_NAME_MAX_SIZE] = {0};
    size_t display_compressed_size = 0U;
    char *remote_json = (char *)heap_caps_malloc(
        FACTORY_WELCOME_MANIFEST_MAX_SIZE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *local_json = (char *)heap_caps_malloc(
        FACTORY_WELCOME_MANIFEST_MAX_SIZE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (remote_json == NULL || local_json == NULL) {
        ret = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "manifest buffer allocation failed");
        goto cleanup;
    }

    size_t remote_size = 0U;
    ret = http_get_with_retry(FACTORY_WELCOME_MANIFEST_URL,
                              (uint8_t *)remote_json,
                              FACTORY_WELCOME_MANIFEST_MAX_SIZE - 1U,
                              0U,
                              &remote_size,
                              &context);
    if (ret != ESP_OK) {
        goto cleanup;
    }
    remote_json[remote_size] = '\0';

    factory_welcome_manifest_t *remote =
        (factory_welcome_manifest_t *)heap_caps_calloc(
            1U,
            sizeof(factory_welcome_manifest_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    factory_welcome_manifest_t *local =
        (factory_welcome_manifest_t *)heap_caps_calloc(
            1U,
            sizeof(factory_welcome_manifest_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (remote == NULL || local == NULL) {
        free(remote);
        free(local);
        ret = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "parsed manifest allocation failed");
        goto cleanup;
    }
    ret = FactoryWelcomeManifest_ParseForResolution(
        remote_json, remote_size, resolution, remote);
    if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "resource not found resolution=%s", resolution);
        goto cleanup_manifests;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "remote manifest invalid ret=%s", esp_err_to_name(ret));
        goto cleanup_manifests;
    }

    ret = FactoryWelcomeStorage_EnsureDirectory(base_path);
    if (ret != ESP_OK) {
        goto cleanup_manifests;
    }

    size_t local_size = 0U;
    esp_err_t local_read_ret = FactoryWelcomeStorage_ReadManifest(
        base_path,
        local_json,
        FACTORY_WELCOME_MANIFEST_MAX_SIZE,
        &local_size);
    bool local_valid = local_read_ret == ESP_OK &&
        FactoryWelcomeManifest_ParseForResolution(
            local_json, local_size, resolution, local) == ESP_OK;
    uint32_t local_version = local_valid ? local->version : 0U;
    free(local_json);
    local_json = NULL;
    if (local_read_ret == ESP_OK && !local_valid) {
        ESP_LOGW(TAG, "local manifest invalid, refresh required");
    }

    if (local_valid && remote->version < local_version) {
        ESP_LOGW(TAG, "remote version older remote=%lu local=%lu",
                 (unsigned long)remote->version,
                 (unsigned long)local_version);
        strlcpy(display_file_name,
                local->files[0].file_name,
                sizeof(display_file_name));
        display_compressed_size = local->files[0].compressed_size;
        display_selected = true;
        ret = ESP_OK;
        goto cleanup_manifests;
    }

    free(local);
    local = NULL;
    bool force_all = !local_valid || remote->version > local_version;
    bool files_changed = false;
    ret = download_required_files(base_path,
                                  remote,
                                  force_all,
                                  &files_changed,
                                  &context);
    if (ret != ESP_OK) {
        goto cleanup_manifests;
    }

    bool manifest_changed = !local_valid || remote->version != local_version ||
                            files_changed;
    if (manifest_changed) {
        if (!run_can_continue(&context)) {
            ret = ESP_ERR_INVALID_STATE;
            goto cleanup_manifests;
        }
        ret = FactoryWelcomeStorage_WriteManifestAtomic(
            base_path, remote_json, remote_size);
        if (ret != ESP_OK) {
            goto cleanup_manifests;
        }
        ESP_LOGI(TAG, "sync complete version=%lu files=%u",
                 (unsigned long)remote->version,
                 (unsigned int)remote->file_count);
    } else {
        ESP_LOGI(TAG, "sync unchanged version=%lu files=%u",
                 (unsigned long)remote->version,
                 (unsigned int)remote->file_count);
    }

    strlcpy(display_file_name,
            remote->files[0].file_name,
            sizeof(display_file_name));
    display_compressed_size = remote->files[0].compressed_size;
    display_selected = true;
    ret = ESP_OK;

cleanup_manifests:
    free(local);
    free(remote);
cleanup:
    free(local_json);
    free(remote_json);
    if (ret == ESP_OK && display_selected) {
        ret = display_resource_file(base_path,
                                    display_file_name,
                                    display_compressed_size,
                                    &context);
    }
    EpdSdPowerTest_ImageTransferEnd();
    ServerNetworkStaWifiWorkTime_ImageTransferEnd();
    if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND &&
        ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "sync failed ret=%s", esp_err_to_name(ret));
    }
    if (ret != ESP_OK) {
        display_test_pattern_fallback(&context, ret);
    }
    return ret;
}
