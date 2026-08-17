/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* HTTP File Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "esp_event.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_err.h"
#include "esp_partition.h"
#include "esp_pm.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "app_persistent_state.h"
#include "ch583_uart_app.h"
#include "cast_core.h"
#include "debug_output.h"
#include "epd_display_app.h"
#include "epd_display_mode.h"
#include "epd_sd_power_test.h"
#include "factory_reset.h"
#include "file_serving_example_common.h"
#include "gpio_test.h"
#include "image_business_worker.h"
#include "led_status.h"
#include "local_image_browsing.h"
#include "server_network_sta.h"
#include "server_network_sta_daily_image.h"
#include "server_network_sta_slideshow.h"
#include "server_network_sta_time.h"
#include "server_network_sta_wifi_work_time.h"
#include "network_ota_boot.h"
#include "tdx_zlib_epd_test.h"
#include "tdx_cfg.h"
#include "tdx_shared_spi.h"
#include "usb_console_echo.h"
#include "user_app.h"

#include <string.h>
#include <stdlib.h>

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_app_desc.h"

static const char *TAG = "example";
int g_app_reset_reason = ESP_RST_low_power_No_Disp;



#define PM_DIAG_PERIOD_MS      10000
#define PM_DIAG_MAX_DUMPS      10

static void pm_diag_dump_once(const char *reason)
{
#if CONFIG_PM_ENABLE && CONFIG_PM_PROFILING
    ESP_LOGW(TAG, "PMDBG dump begin: %s", reason);
    esp_pm_dump_locks(stdout);
    fflush(stdout);
    ESP_LOGW(TAG, "PMDBG dump end: %s", reason);
#else
    ESP_LOGW(TAG, "PMDBG disabled: CONFIG_PM_ENABLE=%d CONFIG_PM_PROFILING=%d",
             CONFIG_PM_ENABLE, CONFIG_PM_PROFILING);
#endif
}

static void __attribute__((unused)) pm_diag_task(void *arg)
{
    (void)arg;

#if CONFIG_PM_ENABLE && CONFIG_PM_PROFILING
    for (int i = 0; i < PM_DIAG_MAX_DUMPS; i++) {
        vTaskDelay(pdMS_TO_TICKS(PM_DIAG_PERIOD_MS));
        pm_diag_dump_once("periodic");
    }
#endif

    vTaskDelete(NULL);
}

static void usb_console_ansi_color_test(void)
{
#if USER_USB_CONSOLE_ANSI_COLOR_TEST_ENABLE
    // Print ANSI color samples so the PC USB console can verify xterm_256color rendering.
    // 打印 ANSI 颜色样例，用于 PC 端 USB 串口窗口验证 xterm_256color 渲染。
    UserDebugOutput_Printf("\033[0mANSI color test begin\r\n");
    UserDebugOutput_Printf("\033[30mblack\033[0m \033[31mred\033[0m \033[32mgreen\033[0m \033[33myellow\033[0m\r\n");
    UserDebugOutput_Printf("\033[34mblue\033[0m \033[35mmagenta\033[0m \033[36mcyan\033[0m \033[37mwhite\033[0m\r\n");
    UserDebugOutput_Printf("\033[90mbright black\033[0m \033[91mbright red\033[0m \033[92mbright green\033[0m \033[94mbright blue\033[0m\r\n");
    UserDebugOutput_Printf("\033[1m\033[38;5;202m256 orange bold fg\033[0m \033[38;5;45m256 cyan fg\033[0m \033[48;5;24m256 blue bg\033[0m\r\n");
    UserDebugOutput_Printf("\033[0mANSI color test end\r\n");
    fflush(stdout);
#endif
}

/* This example demonstrates how to create file server
 * using esp_http_server. This file has only startup code.
 * Look in file_server.c for the implementation.
 */

static void app_auto_light_sleep_init(void)
{
#if CONFIG_PM_ENABLE
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 80,
        .min_freq_mhz = 80,
        .light_sleep_enable = false,
    };

    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    //  test power only
    //pm_diag_dump_once("after_pm_config");
    //  test power only over

    ESP_LOGI(TAG, "Power policy: low_power=%d max=%uMHz min=%uMHz light_sleep=%d",
             0,
             (unsigned int)pm_config.max_freq_mhz,
             (unsigned int)pm_config.min_freq_mhz,
             pm_config.light_sleep_enable ? 1 : 0);
#else
    ESP_LOGW(TAG, "Auto Light-sleep not enabled because CONFIG_PM_ENABLE is off");
#endif
}

static const char *reset_reason_to_str(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_UNKNOWN:
        return "unknown";
    case ESP_RST_POWERON:
        return "poweron";
    case ESP_RST_EXT:
        return "external";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "int_wdt";
    case ESP_RST_TASK_WDT:
        return "task_wdt";
    case ESP_RST_WDT:
        return "wdt";
    case ESP_RST_DEEPSLEEP:
        return "deepsleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_SDIO:
        return "sdio";
    case ESP_RST_USB:
        return "usb";
    case ESP_RST_JTAG:
        return "jtag";
    case ESP_RST_EFUSE:
        return "efuse";
    case ESP_RST_PWR_GLITCH:
        return "pwr_glitch";
    case ESP_RST_CPU_LOCKUP:
        return "cpu_lockup";
    default:
        return "invalid";
    }
}

void print_base_info(void)
{
    uint32_t flash_size = 0;
    esp_reset_reason_t reason = esp_reset_reason();
    g_app_reset_reason = (int)reason;

    size_t ram_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        flash_size = 0;
    }

    ESP_LOGI(TAG, "boot reset=%d(%s) app=%d flash=%u ram_free=%u internal_free=%u psram_free=%u",
             (int)reason,
             reset_reason_to_str(reason),
             g_app_reset_reason,
             (unsigned int)flash_size,
             (unsigned int)ram_free,
             (unsigned int)internal_free,
             (unsigned int)psram_free);

    nvs_stats_t short_nvs_stats = {0};
    esp_err_t short_nvs_ret = nvs_get_stats(NULL, &short_nvs_stats);
    if (short_nvs_ret == ESP_OK) {
        ESP_LOGI(TAG, "nvs entries used=%u free=%u available=%u total=%u namespace=%u",
                 (unsigned int)short_nvs_stats.used_entries,
                 (unsigned int)short_nvs_stats.free_entries,
                 (unsigned int)short_nvs_stats.available_entries,
                 (unsigned int)short_nvs_stats.total_entries,
                 (unsigned int)short_nvs_stats.namespace_count);
    } else {
        ESP_LOGW(TAG, "nvs stats failed ret=%s", esp_err_to_name(short_nvs_ret));
    }

    ESP_LOGI(TAG, "work state sleep=%u working=%lu continue=%lu standby=%lu",
             (unsigned int)sleep_time,
             (unsigned long)working_time,
             (unsigned long)server_required_continue_work_time,
             (unsigned long)wifi_standby_time_s);
    // 中文：当前项目没有保留 User_PrintWorkStateNvs()，这里直接打印已恢复的工作状态全局变量。
}

static bool startup_wifi_status_is_progressing(
    const server_network_sta_status_t *status)
{
    if (status == NULL) {
        return false;
    }
    return status->state == SERVER_NETWORK_STA_STATE_CONNECTING ||
           status->state == SERVER_NETWORK_STA_STATE_DISCONNECTING ||
           status->state == SERVER_NETWORK_STA_STATE_WAITING_IP ||
           status->state == SERVER_NETWORK_STA_STATE_RETRY_WAIT ||
           status->state == SERVER_NETWORK_STA_STATE_GOT_IP ||
           status->state == SERVER_NETWORK_STA_STATE_STARTING_SERVICES;
}

static void reconcile_persistent_slideshow_mode(void)
{
    app_persistent_slideshow_control_t control = {0};
    uint32_t control_generation = 0;
    esp_err_t control_ret = AppPersistentState_LoadSlideshowControl(
        &control, &control_generation);
    uint8_t mode = EpdDisplayMode_Get();

    if (control_ret == ESP_ERR_NVS_NOT_FOUND) {
        if (mode == USER_EPD_DISPLAY_MODE_SLIDESHOW) {
            ESP_LOGW(TAG,
                     "startup slideshow mode has no NVS control; reset mode to NORMAL");
            (void)EpdDisplayMode_SetBySlideshowSwitch(false);
        }
        return;
    }
    if (control_ret != ESP_OK) {
        ESP_LOGE(TAG, "startup slideshow control read failed ret=%s",
                 esp_err_to_name(control_ret));
        if (mode == USER_EPD_DISPLAY_MODE_SLIDESHOW) {
            (void)EpdDisplayMode_SetBySlideshowSwitch(false);
        }
        return;
    }

    bool effective_enabled = control.enabled;
    if (control.enabled) {
        app_persistent_slideshow_config_t *config =
            (app_persistent_slideshow_config_t *)calloc(1, sizeof(*config));
        uint32_t config_generation = 0;
        esp_err_t config_ret = config != NULL ?
                               AppPersistentState_LoadSlideshowConfig(
                                   config, &config_generation) : ESP_ERR_NO_MEM;
        free(config);
        if (config_ret != ESP_OK || config_generation != control_generation) {
            effective_enabled = false;
            ESP_LOGE(TAG,
                     "startup slideshow state invalid config_ret=%s config_generation=%lu control_generation=%lu",
                     esp_err_to_name(config_ret),
                     (unsigned long)config_generation,
                     (unsigned long)control_generation);
        }
    }

    bool scheduled_mode_owns_display =
        mode == USER_EPD_DISPLAY_MODE_DAILY ||
        mode == USER_EPD_DISPLAY_MODE_LOCAL_IMAGE_BROWSING;
    if (control.enabled && (!effective_enabled || scheduled_mode_owns_display)) {
        control.enabled = false;
        esp_err_t save_ret = AppPersistentState_SaveSlideshowControl(&control);
        if (save_ret != ESP_OK) {
            ESP_LOGE(TAG, "startup slideshow disable save failed ret=%s",
                     esp_err_to_name(save_ret));
        } else {
            ESP_LOGI(TAG,
                     "startup slideshow control disabled for mode=%u(%s)",
                     (unsigned int)mode,
                     EpdDisplayMode_ToString(mode));
        }
        effective_enabled = false;
    }

    if (!scheduled_mode_owns_display) {
        uint8_t desired_mode = effective_enabled ?
                               USER_EPD_DISPLAY_MODE_SLIDESHOW :
                               USER_EPD_DISPLAY_MODE_NORMAL;
        if (mode != desired_mode) {
            esp_err_t mode_ret =
                EpdDisplayMode_SetBySlideshowSwitch(effective_enabled);
            if (mode_ret != ESP_OK) {
                ESP_LOGE(TAG, "startup slideshow mode reconcile failed ret=%s",
                         esp_err_to_name(mode_ret));
            }
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(UserDebugOutput_Init());

    /* 关闭 ESP-IDF WiFi 内部 INFO 日志，只保留警告和错误。 */

    // /* 关闭 net80211 ROM 版本等 INFO 日志。 */

    // /* 关闭大部分 WiFi 驱动 INFO 日志。 */
    esp_log_level_set("wifi_init", ESP_LOG_WARN);
    esp_log_level_set("net80211", ESP_LOG_WARN);
    esp_log_level_set("wifi", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("pp", ESP_LOG_WARN);
    esp_log_level_set("phy_init", ESP_LOG_WARN);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_WARN);
    esp_log_level_set("mdns_mem", ESP_LOG_WARN);
    esp_log_level_set("ch583_uart", ESP_LOG_WARN);

    ESP_LOGI(TAG, "app start");
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS recover erase ret=%s", esp_err_to_name(nvs_ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);
    ESP_ERROR_CHECK(AppPersistentState_Init());
    esp_err_t ota_boot_ret = NetworkOtaBoot_Init();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(ServerNetworkStaTime_Init());
    ESP_ERROR_CHECK(TdxSharedSpi_Init());
    ESP_ERROR_CHECK(ServerNetworkStaWifiWorkTime_Init());
    if (ota_boot_ret != ESP_OK) {
        /*
         * Retry before optional first-boot modules so a transient early otadata
         * read failure cannot bypass pending power protection or OTA-only recovery.
         */
        (void)NetworkOtaBoot_Init();
    }
    NetworkOtaBoot_EnablePendingProtection();
    ESP_ERROR_CHECK(EpdDisplayMode_Init());
    ESP_LOGI(TAG, "EPD display mode=%u(%s)",
             (unsigned int)EpdDisplayMode_Get(),
             EpdDisplayMode_ToString(EpdDisplayMode_Get()));
    ESP_ERROR_CHECK(ImageBusinessWorker_Init());
    const char *base_path = "/data";
    esp_err_t daily_ret = ServerNetworkStaDailyImage_Init(base_path);
    if (daily_ret != ESP_OK) {
        ESP_LOGE(TAG, "daily image early init failed ret=%s",
                 esp_err_to_name(daily_ret));
    }
    print_base_info();
    if (NetworkOtaBoot_WasPendingVerify()) {
        esp_err_t gpio_test_ret = GpioTest_Init();
        if (gpio_test_ret != ESP_OK) {
            ESP_LOGE(TAG,
                     "optional GPIO test init failed during OTA first boot ret=%s",
                     esp_err_to_name(gpio_test_ret));
        }
    } else {
        ESP_ERROR_CHECK(GpioTest_Init());
    }
    ESP_ERROR_CHECK(ServerNetworkSta_Init());
    // Start CH583 UART before LED status because C5 status LEDs are controlled by CH583 GPIO.
    // 先启动 CH583 串口再初始化 LED 状态，因为 C5 状态灯由 CH583 GPIO 控制。
    ESP_ERROR_CHECK(Ch583UartApp_Init());
    reconcile_persistent_slideshow_mode();
    ESP_ERROR_CHECK(UsbConsoleEcho_Init());

    if (NetworkOtaBoot_IsPendingVerify()) {
        /*
         * Confirm the new image after local critical services are ready. Wi-Fi,
         * DHCP, HTTP, SD, and SNTP are intentionally excluded so a slow network
         * cannot consume the CH583 external power-cut window.
         */
        /* The OTA module owns the single stage-specific error log on failure. */
        (void)NetworkOtaBoot_ConfirmAfterLocalInit();
    }

    vTaskDelay(pdMS_TO_TICKS(500)); // for CH583 ask delay 500ms
    (void)ServerNetworkStaTime_RequestCh583Backup();


    ESP_ERROR_CHECK(UserLedStatus_Init());
#if USER_BLE_ENABLE
    // Start BLE after NVS/event loop so BT state and callbacks have the required system services.
    // 中文：在 NVS 和事件循环初始化之后启动 BLE，保证蓝牙状态和回调依赖的系统服务已经就绪。
    Init_Bl();
#endif
    /* English: Initialize the migrated EPD driver at startup before network cast/upload can request display. */
    /* 中文：启动时初始化移植过来的 EPD 驱动，保证网络 cast/upload 请求显示前屏幕已经就绪。 */
    ESP_ERROR_CHECK(ServerNetworkStaEpdDisplay_Init());

    /* Initialize file storage */
    esp_err_t storage_ret = example_mount_storage(base_path);
    if (storage_ret != ESP_OK) {
        UserLedStatus_SetStorageFailed(true);
        ESP_LOGE(TAG, "storage mount failed ret=%s",
                 esp_err_to_name(storage_ret));
    } else {
        UserLedStatus_SetStorageFailed(false);
        if (NetworkOtaBoot_WasPendingVerify()) {
            esp_err_t factory_reset_ret = FactoryReset_Init(base_path);
            if (factory_reset_ret != ESP_OK) {
                ESP_LOGE(TAG,
                         "optional factory reset init failed during OTA first boot ret=%s",
                         esp_err_to_name(factory_reset_ret));
            }
        } else {
            ESP_ERROR_CHECK(FactoryReset_Init(base_path));
        }

        /* Display the pending Factory Reset welcome only after storage has
         * mounted, so EPD traffic cannot disturb the initial SD handshake. */
        (void)FactoryReset_HandleStartupWelcome();
    }
    esp_err_t power_test_ret = EpdSdPowerTest_Init();
    if (power_test_ret != ESP_OK) {
        // The test is optional and must never prevent the existing application from starting.
        ESP_LOGE(TAG, "EPD/SD independent power test init failed ret=%s",
                 esp_err_to_name(power_test_ret));
    }
#if 0
    /*
     * Keep the verified zlib EPD test available for future diagnostics.
     * Change this local test switch to 1 only while running the test.
     */
    if (storage_ret == ESP_OK && example_storage_is_sd_card()) {
        esp_err_t zlib_test_ret = TdxZlibEpdTest_Run(base_path);
        if (zlib_test_ret != ESP_OK) {
            ESP_LOGE(TAG, "optional zlib EPD test failed ret=%s",
                     esp_err_to_name(zlib_test_ret));
        }
    } else {
        ESP_LOGW(TAG, "zlib EPD test skipped because SD card is not mounted");
    }
#endif

    /* The unified image business worker was created before optional services. */
    esp_err_t local_browsing_ret = LocalImageBrowsing_Init(base_path);
    if (local_browsing_ret != ESP_OK) {
        ESP_LOGE(TAG, "local image browsing init failed ret=%s",
                 esp_err_to_name(local_browsing_ret));
    }
    // Force the old read_value=0x02 path here: Server Network STA only, then start the HTTP file server.
    // 中文：在这里固定旧工程 read_value=0x02 路径：只进入 Server Network STA，然后启动 HTTP 文件服务器。
    (void)ServerNetworkStaWifiWorkTime_StartWifiConnectGuardIfInactive(
        WIFI_CONNECT_POWER_GUARD_MAX_MS);
    uint8_t network_ret = User_Network_mode_app_init(base_path);
    server_network_sta_status_t startup_wifi_status = {0};
    esp_err_t startup_status_ret =
        ServerNetworkSta_GetStatus(&startup_wifi_status);
    if (network_ret == SERVER_NETWORK_STA_OK) {
        ServerNetworkStaWifiWorkTime_ClearWifiConnectGuard(
            "startup_ready");
    } else if (startup_status_ret == ESP_OK &&
               !startup_wifi_status_is_progressing(&startup_wifi_status)) {
        ServerNetworkStaWifiWorkTime_ClearWifiConnectGuard(
            "startup_terminal");
    } else if (startup_status_ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "startup WiFi status read failed; power guard remains bounded ret=%s",
                 esp_err_to_name(startup_status_ret));
    }
    if (network_ret != SERVER_NETWORK_STA_OK) {
        ESP_LOGE(TAG, "network init failed ret=0x%02x", network_ret);
        //return;
    }
    else    {
        ESP_LOGI(TAG, "network ready ret=0x%02x", network_ret);
    }

    if (daily_ret == ESP_OK) {
        daily_ret = ServerNetworkStaDailyImage_StartSaved();
        if (daily_ret != ESP_OK) {
            ESP_LOGE(TAG, "daily image saved start failed ret=%s",
                     esp_err_to_name(daily_ret));
        }
    }

    uint8_t startup_mode = EpdDisplayMode_Get();
    if (startup_mode == USER_EPD_DISPLAY_MODE_SLIDESHOW && storage_ret == ESP_OK) {
        esp_err_t slideshow_ret = ServerNetworkStaSlideshow_StartSavedDelayed(base_path);
        if (slideshow_ret == ESP_OK) {
            ESP_LOGI(TAG, "slideshow delayed start ret=%s", esp_err_to_name(slideshow_ret));
        } else {
            ESP_LOGW(TAG, "slideshow delayed start failed ret=%s", esp_err_to_name(slideshow_ret));
        }
    } else if (startup_mode == USER_EPD_DISPLAY_MODE_SLIDESHOW) {
        ESP_LOGE(TAG, "slideshow startup blocked because storage is not ready");
    } else if (startup_mode == USER_EPD_DISPLAY_MODE_LOCAL_IMAGE_BROWSING) {
        ESP_LOGI(TAG,
                 "local image browsing startup restored, waiting for PB2 event storage_ready=%d",
                 storage_ret == ESP_OK ? 1 : 0);
    }

    /*
     * Restore saved scheduled modes before releasing deferred BLE_DATA. A new
     * BLE command can then supersede startup state without being started twice.
     */
    Ch583UartApp_SetBleDataBusinessReady();

    if (daily_ret == ESP_OK) {
        ESP_LOGI(TAG, "startup mode selected=%u(%s)",
                 (unsigned int)EpdDisplayMode_Get(),
                 EpdDisplayMode_ToString(EpdDisplayMode_Get()));
    }

    if (NetworkOtaBoot_IsPendingVerify()) {
        ESP_LOGW(TAG,
                 "OTA image is pending verification; keep light sleep disabled");
    }
    app_auto_light_sleep_init();

    //  test power only
    // pm_diag_dump_once("after_network_init");
    // #if CONFIG_PM_ENABLE && CONFIG_PM_PROFILING
    // xTaskCreate(pm_diag_task, "pm_diag", 4096, NULL, 1, NULL);
    // #endif
    //  test power only over


    usb_console_ansi_color_test();
    //  ESP_LOGI(TAG, "Server Version=2.2.5");    
    const esp_app_desc_t *app = esp_app_get_description();    
    ESP_LOGI(TAG, "ver=%s", app != NULL ? app->version : "");

    char ble_mac[13] = {0};
    get_ble_mac_no_colon(ble_mac, sizeof(ble_mac));
#if USER_BLE_ENABLE
    ESP_LOGI(TAG, "ble_mac source=ESP32 value=%s",
             ble_mac[0] != '\0' ? ble_mac : "<empty>");
#else
    ESP_LOGI(TAG, "ble_mac source=CH583 value=%s",
             ble_mac[0] != '\0' ? ble_mac : "<empty>");
#endif
     //  test_epd_display();
}
// vTaskDelay(pdMS_TO_TICKS(1000));
