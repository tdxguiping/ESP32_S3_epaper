#include "tdx_cfg.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "app_nvs";

uint8_t g_slideshow_random_enable;

esp_err_t app_nvs_read_u8(const char *key, uint8_t *out_value, uint8_t default_value)
{
    nvs_handle_t handle;
    esp_err_t ret;

    if (key == NULL || out_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = nvs_open("PhotoPainter", NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "open PhotoPainter failed key=%s ret=%s", key, esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_get_u8(handle, key, out_value);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *out_value = default_value;

        ret = nvs_set_u8(handle, key, default_value);
        if (ret == ESP_OK) {
            ret = nvs_commit(handle);
        }
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "read_u8 key=%s value=%u ret=%s", key, (unsigned int)*out_value, esp_err_to_name(ret));

    return ret;
}

esp_err_t app_nvs_write_u8(const char *key, uint8_t value)
{
    nvs_handle_t handle;
    esp_err_t ret;

    if (key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = nvs_open("PhotoPainter", NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "open PhotoPainter failed key=%s ret=%s", key, esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_u8(handle, key, value);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "write_u8 key=%s value=%u ret=%s", key, (unsigned int)value, esp_err_to_name(ret));

    return ret;
}

esp_err_t app_nvs_write_str(const char *key, const char *value)
{
    nvs_handle_t handle;
    esp_err_t ret;

    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = nvs_open("PhotoPainter", NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "open PhotoPainter failed key=%s ret=%s", key, esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_str(handle, key, value);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "write_str key=%s value=%s ret=%s", key, value, esp_err_to_name(ret));

    return ret;
}

esp_err_t app_nvs_read_str(const char *key, char *value, size_t value_size, const char *default_value)
{
    nvs_handle_t handle;
    esp_err_t ret;
    size_t required_size = value_size;

    if (key == NULL || value == NULL || value_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    value[0] = '\0';

    ret = nvs_open("PhotoPainter", NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        if (default_value != NULL) {
            snprintf(value, value_size, "%s", default_value);
        }
        ESP_LOGW(TAG, "open PhotoPainter failed key=%s ret=%s default=%s",
                 key, esp_err_to_name(ret), value);
        return ret;
    }

    ret = nvs_get_str(handle, key, value, &required_size);
    nvs_close(handle);

    if (ret != ESP_OK && default_value != NULL) {
        snprintf(value, value_size, "%s", default_value);
    }

    ESP_LOGI(TAG, "read_str key=%s value=%s ret=%s", key, value, esp_err_to_name(ret));

    return ret;
}

esp_err_t app_nvs_write_blob(const char *key, const void *value, size_t value_size)
{
    nvs_handle_t handle;
    esp_err_t ret;

    if (key == NULL || value == NULL || value_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = nvs_open("PhotoPainter", NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "open PhotoPainter failed key=%s ret=%s", key, esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_blob(handle, key, value, value_size);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);

    ESP_LOGI(TAG, "write_blob key=%s size=%u ret=%s",
             key, (unsigned int)value_size, esp_err_to_name(ret));
    return ret;
}

esp_err_t app_nvs_read_blob(const char *key, void *value, size_t value_size)
{
    nvs_handle_t handle;
    esp_err_t ret;
    size_t required_size = 0;

    if (key == NULL || value == NULL || value_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = nvs_open("PhotoPainter", NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "open PhotoPainter failed key=%s ret=%s", key, esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_get_blob(handle, key, NULL, &required_size);
    if (ret == ESP_OK && required_size != value_size) {
        ret = ESP_ERR_INVALID_SIZE;
    }
    if (ret == ESP_OK) {
        required_size = value_size;
        ret = nvs_get_blob(handle, key, value, &required_size);
    }
    nvs_close(handle);

    ESP_LOGI(TAG, "read_blob key=%s size=%u ret=%s",
             key, (unsigned int)value_size, esp_err_to_name(ret));
    return ret;
}
