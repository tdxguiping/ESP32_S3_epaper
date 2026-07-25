#include "epd_display_mode.h"

#include "ch583_wifi_uart_protocol.h"
#include "esp_log.h"
#include "tdx_cfg.h"

static const char *TAG = "epd_display_mode";
static uint8_t s_epd_mode = USER_EPD_DISPLAY_MODE_DEFAULT;

static bool epd_display_mode_is_valid(uint8_t mode)
{
    return mode == USER_EPD_DISPLAY_MODE_NORMAL ||
           mode == USER_EPD_DISPLAY_MODE_SLIDESHOW ||
           mode == USER_EPD_DISPLAY_MODE_DAILY;
}

const char *EpdDisplayMode_ToString(uint8_t mode)
{
    switch (mode) {
    case USER_EPD_DISPLAY_MODE_NORMAL:
        return "NORMAL";
    case USER_EPD_DISPLAY_MODE_SLIDESHOW:
        return "SLIDESHOW";
    case USER_EPD_DISPLAY_MODE_DAILY:
        return "DAILY";
    default:
        return "INVALID";
    }
}

uint8_t EpdDisplayMode_Get(void)
{
    return s_epd_mode;
}

esp_err_t EpdDisplayMode_Set(uint8_t mode)
{
    if (!epd_display_mode_is_valid(mode)) {
        ESP_LOGW(TAG, "reject invalid mode=%u", (unsigned int)mode);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t old_mode = s_epd_mode;
    if (old_mode == mode) {
        ESP_LOGI(TAG, "mode unchanged value=%u(%s)",
                 (unsigned int)mode,
                 EpdDisplayMode_ToString(mode));
        return ESP_OK;
    }

    esp_err_t ret = app_nvs_write_u8(USER_EPD_DISPLAY_MODE_NVS_KEY, mode);
    if (ret == ESP_OK) {
        uint8_t verified_mode = USER_EPD_DISPLAY_MODE_DEFAULT;
        ret = app_nvs_read_u8(USER_EPD_DISPLAY_MODE_NVS_KEY,
                              &verified_mode,
                              USER_EPD_DISPLAY_MODE_DEFAULT);
        if (ret != ESP_OK || verified_mode != mode) {
            ESP_LOGE(TAG,
                     "mode NVS verify failed old=%u new=%u read=%u ret=%s",
                     (unsigned int)old_mode,
                     (unsigned int)mode,
                     (unsigned int)verified_mode,
                     esp_err_to_name(ret));
            return ret != ESP_OK ? ret : ESP_FAIL;
        }

        s_epd_mode = mode;
        ESP_LOGI(TAG, "mode changed old=%u(%s) new=%u(%s)",
                 (unsigned int)old_mode,
                 EpdDisplayMode_ToString(old_mode),
                 (unsigned int)mode,
                 EpdDisplayMode_ToString(mode));
        int provision_ret = ch583_wifi_uart_send_current_wifi_provision_status();
        if (provision_ret != 0) {
            ESP_LOGW(TAG, "notify WIFI_PROVISION after mode set failed ret=%d", provision_ret);
        }
    } else {
        ESP_LOGE(TAG, "set mode failed mode=%u(%s) ret=%s",
                 (unsigned int)mode,
                 EpdDisplayMode_ToString(mode),
                 esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t EpdDisplayMode_SetBySlideshowSwitch(bool sw)
{
    return EpdDisplayMode_Set(sw ? USER_EPD_DISPLAY_MODE_SLIDESHOW : USER_EPD_DISPLAY_MODE_NORMAL);
}

esp_err_t EpdDisplayMode_Init(void)
{
    uint8_t mode = USER_EPD_DISPLAY_MODE_DEFAULT;
    esp_err_t ret = app_nvs_read_u8(USER_EPD_DISPLAY_MODE_NVS_KEY,
                                    &mode,
                                    USER_EPD_DISPLAY_MODE_DEFAULT);
    if (ret != ESP_OK) {
        s_epd_mode = USER_EPD_DISPLAY_MODE_DEFAULT;
        ESP_LOGW(TAG, "init read failed ret=%s use mode=%u(%s)",
                 esp_err_to_name(ret),
                 (unsigned int)s_epd_mode,
                 EpdDisplayMode_ToString(s_epd_mode));
        return ret;
    }

    if (!epd_display_mode_is_valid(mode)) {
        ESP_LOGE(TAG, "init invalid saved mode=%u, reset to default", (unsigned int)mode);
        mode = USER_EPD_DISPLAY_MODE_DEFAULT;
        ret = app_nvs_write_u8(USER_EPD_DISPLAY_MODE_NVS_KEY, mode);
        if (ret != ESP_OK) {
            s_epd_mode = USER_EPD_DISPLAY_MODE_DEFAULT;
            ESP_LOGE(TAG, "init reset mode failed ret=%s", esp_err_to_name(ret));
            return ret;
        }
        uint8_t verified_mode = UINT8_MAX;
        ret = app_nvs_read_u8(USER_EPD_DISPLAY_MODE_NVS_KEY,
                              &verified_mode,
                              USER_EPD_DISPLAY_MODE_DEFAULT);
        if (ret != ESP_OK || verified_mode != mode) {
            s_epd_mode = USER_EPD_DISPLAY_MODE_DEFAULT;
            ESP_LOGE(TAG,
                     "init reset verify failed expected=%u read=%u ret=%s",
                     (unsigned int)mode,
                     (unsigned int)verified_mode,
                     esp_err_to_name(ret));
            return ret != ESP_OK ? ret : ESP_FAIL;
        }
    }

    s_epd_mode = mode;
    ESP_LOGI(TAG, "init mode=%u(%s)", (unsigned int)mode, EpdDisplayMode_ToString(mode));
    return ESP_OK;
}
