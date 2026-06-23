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
#include "ch583_uart_app.h"
#include "cast_core.h"
#include "epd_display_app.h"
#include "file_serving_example_common.h"
#include "gpio_test.h"
#include "led_status.h"
#include "server_network_sta.h"
#include "server_network_sta_slideshow.h"
#include "server_network_sta_wifi_work_time.h"
#include "tdx_cfg.h"
#include "usb_console_echo.h"
#include "user_app.h"

#include <string.h>

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


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
    printf("\033[0mANSI color test begin\r\n");
    printf("\033[30mblack\033[0m \033[31mred\033[0m \033[32mgreen\033[0m \033[33myellow\033[0m\r\n");
    printf("\033[34mblue\033[0m \033[35mmagenta\033[0m \033[36mcyan\033[0m \033[37mwhite\033[0m\r\n");
    printf("\033[90mbright black\033[0m \033[91mbright red\033[0m \033[92mbright green\033[0m \033[94mbright blue\033[0m\r\n");
    printf("\033[1m\033[38;5;202m256 orange bold fg\033[0m \033[38;5;45m256 cyan fg\033[0m \033[48;5;24m256 blue bg\033[0m\r\n");
    printf("\033[0mANSI color test end\r\n");
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
#if TDX_AUTO_LIGHT_SLEEP_ENABLE
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 80, // 160
        .min_freq_mhz = 40,
        .light_sleep_enable = true,
    };
#else
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 240,
        .light_sleep_enable = false,
    };
#endif

    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    //  test power only
    //pm_diag_dump_once("after_pm_config");
    //  test power only over

    ESP_LOGI(TAG, "Power policy: low_power=%d max=%uMHz min=%uMHz light_sleep=%d",
             (int)TDX_AUTO_LIGHT_SLEEP_ENABLE,
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

    if (g_app_reset_reason == ESP_RST_POWERON) {
        g_app_reset_reason = ESP_RST_low_power_No_Disp;
    } else {
        g_app_reset_reason = ESP_RST_low_power_No_Disp;
    }

    size_t ram_total = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    size_t ram_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t heap_8bit_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t heap_8bit_min = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    size_t dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA);
    size_t dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    size_t iram_free = heap_caps_get_free_size(MALLOC_CAP_IRAM_8BIT);
    size_t iram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_IRAM_8BIT);
    size_t internal_8bit_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t internal_8bit_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        flash_size = 0;
    }

    LOG_Purple("-----Reset----------- %d (%s) app=%d", reason, reset_reason_to_str(reason), g_app_reset_reason);
    LOG_Purple("Flash total         : %u bytes (%u MB)",
               (unsigned int)flash_size,
               (unsigned int)(flash_size / 1024 / 1024));
    LOG_Purple("RAM total           : total=%u free=%u", (unsigned int)ram_total, (unsigned int)ram_free);
    LOG_Purple("Internal RAM total  : total=%u free=%u min=%u",
               (unsigned int)internal_total,
               (unsigned int)internal_free,
               (unsigned int)internal_min);
    LOG_Purple("Internal heap       : free=%u min=%u", (unsigned int)internal_free, (unsigned int)internal_min);
    LOG_Purple("PSRAM               : total=%u free=%u", (unsigned int)psram_total, (unsigned int)psram_free);
    LOG_Purple("Default 8-bit heap  : free=%u min=%u", (unsigned int)heap_8bit_free, (unsigned int)heap_8bit_min);
    LOG_Purple("DMA heap            : free=%u largest=%u", (unsigned int)dma_free, (unsigned int)dma_largest);
    LOG_Purple("IRAM 8-bit heap     : free=%u largest=%u", (unsigned int)iram_free, (unsigned int)iram_largest);
    LOG_Purple("Internal 8-bit heap : free=%u largest=%u",
               (unsigned int)internal_8bit_free,
               (unsigned int)internal_8bit_largest);

    const esp_partition_t *nvs_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_NVS,
        "nvs");
    if (nvs_part != NULL) {
        LOG_Purple("NVS partition       : label=%s offset=0x%06x size=%u bytes (0x%x)",
                   nvs_part->label,
                   (unsigned int)nvs_part->address,
                   (unsigned int)nvs_part->size,
                   (unsigned int)nvs_part->size);
    } else {
        LOG_Purple("NVS partition       : not found");
    }

    nvs_stats_t nvs_stats = {0};
    esp_err_t nvs_ret = nvs_get_stats(NULL, &nvs_stats);
    if (nvs_ret == ESP_OK) {
        LOG_Purple("NVS entries         : used=%u free=%u available=%u total=%u namespace=%u",
                   (unsigned int)nvs_stats.used_entries,
                   (unsigned int)nvs_stats.free_entries,
                   (unsigned int)nvs_stats.available_entries,
                   (unsigned int)nvs_stats.total_entries,
                   (unsigned int)nvs_stats.namespace_count);
    } else {
        LOG_Purple("NVS entries         : nvs_get_stats failed ret=%d(%s)",
                   nvs_ret,
                   esp_err_to_name(nvs_ret));
    }

    // English: Print the restored work-state globals here because this project does not keep User_PrintWorkStateNvs().
    // 中文：当前项目没有保留 User_PrintWorkStateNvs()，这里直接打印已恢复的工作状态全局变量。
    LOG_Purple("Work state          : sleep=%u working=%lu continue=%lu standby=%lu",
               (unsigned int)sleep_time,
               (unsigned long)working_time,
               (unsigned long)server_required_continue_work_time,
               (unsigned long)wifi_standby_time_s);
}

void app_main(void)
{
    /* Hide ESP-IDF WiFi internal INFO logs, keep warnings and errors. */
    /* 关闭 ESP-IDF WiFi 内部 INFO 日志，只保留警告和错误。 */
    // esp_log_level_set("wifi_init", ESP_LOG_WARN);

    // /* Hide net80211 ROM/version INFO logs. */
    // /* 关闭 net80211 ROM 版本等 INFO 日志。 */
    // esp_log_level_set("net80211", ESP_LOG_WARN);

    // /* Hide most WiFi driver INFO logs. */
    // /* 关闭大部分 WiFi 驱动 INFO 日志。 */
    // esp_log_level_set("wifi", ESP_LOG_ERROR);
    // esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    // esp_log_level_set("pp", ESP_LOG_WARN);
    // esp_log_level_set("phy_init", ESP_LOG_WARN);
    // esp_log_level_set("esp_netif_handlers", ESP_LOG_WARN);
    // esp_log_level_set("mdns_mem", ESP_LOG_WARN);



    ESP_LOGI(TAG, "Starting example");
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(TdxCastCore_Init());
    ESP_ERROR_CHECK(UsbConsoleEcho_Init());
    ESP_ERROR_CHECK(ServerNetworkStaWifiWorkTime_Init());
    char random_value[8] = {0};
    app_nvs_read_str(TDX_SLIDESHOW_RANDOM_NVS_KEY,
                     random_value,
                     sizeof(random_value),
                     "false");
    g_slideshow_random_enable = (strcmp(random_value, "true") == 0) ? 1 : 0;
    app_nvs_write_str(TDX_SLIDESHOW_RANDOM_NVS_KEY,
                      g_slideshow_random_enable ? "true" : "false");
    ESP_LOGI(TAG, "slideshow random config=%s enable=%u",
             random_value, (unsigned int)g_slideshow_random_enable);


    // esp_log_level_set("wifi_init", ESP_LOG_WARN);
    // esp_log_level_set("net80211", ESP_LOG_WARN);
    // esp_log_level_set("wifi", ESP_LOG_ERROR);
    // esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    // esp_log_level_set("pp", ESP_LOG_WARN);
    // esp_log_level_set("phy_init", ESP_LOG_WARN);
    // esp_log_level_set("esp_netif_handlers", ESP_LOG_WARN);
    // esp_log_level_set("mdns_mem", ESP_LOG_WARN);


    print_base_info();
    ESP_ERROR_CHECK(GpioTest_Init());
    // Start CH583 UART before LED status because C5 status LEDs are controlled by CH583 GPIO.
    // 先启动 CH583 串口再初始化 LED 状态，因为 C5 状态灯由 CH583 GPIO 控制。
    ESP_ERROR_CHECK(Ch583UartApp_Init());
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
    const char* base_path = "/data";
    esp_err_t storage_ret = example_mount_storage(base_path);
    if (storage_ret != ESP_OK) {
        ESP_LOGE(TAG, "Storage mount failed ret=%s, continue without startup slideshow",
                 esp_err_to_name(storage_ret));
    }
    // Force the old read_value=0x02 path here: Server Network STA only, then start the HTTP file server.
    // 中文：在这里固定旧工程 read_value=0x02 路径：只进入 Server Network STA，然后启动 HTTP 文件服务器。
    uint8_t network_ret = User_Network_mode_app_init(base_path);
    ESP_LOGI(TAG, "Server Network STA init result=0x%02x", network_ret);
    if (network_ret != SERVER_NETWORK_STA_OK) {
        UserLedStatus_Set(USER_LED_STATE_WIFI_FAIL);
        ESP_LOGE(TAG, "Server Network STA failed, file server not started");
        //return;
    }
    else    {
        UserLedStatus_Set(USER_LED_STATE_SERVER_READY);
    }

    if (storage_ret == ESP_OK) {
        esp_err_t slideshow_ret = ServerNetworkStaSlideshow_StartSaved(base_path);
        ESP_LOGI(TAG, "startup slideshow start ret=%s", esp_err_to_name(slideshow_ret));
    }

    app_auto_light_sleep_init();

    //  test power only
    // pm_diag_dump_once("after_network_init");
    // #if CONFIG_PM_ENABLE && CONFIG_PM_PROFILING
    // xTaskCreate(pm_diag_task, "pm_diag", 4096, NULL, 1, NULL);
    // #endif
    //  test power only over


    usb_console_ansi_color_test();
    ESP_LOGI(TAG, "Server Version=2.2.5");
    char ble_mac[13] = {0};
    get_ble_mac_no_colon(ble_mac, sizeof(ble_mac));
#if USER_BLE_ENABLE
    ESP_LOGI(TAG, "BLE MAC source=ESP32 built-in BLE MAC value=%s",
             ble_mac[0] != '\0' ? ble_mac : "<empty>");
#else
    ESP_LOGI(TAG, "BLE MAC source=CH583 reported BLE MAC value=%s",
             ble_mac[0] != '\0' ? ble_mac : "<empty>");
#endif
     //  test_epd_display();
}
// LOG_ERROR("%d %s %s",__LINE__,__func__,__FILE__);
// LOG_WARN("%s>%d",__func__,__LINE__);
// LOG_INFO("%s>%d",__func__,__LINE__);
// LOG_Purple("%s>%d",__func__,__LINE__);
// LOG_Blue("%s>%d",__func__,__LINE__);
// LOG_Cyan("%s>%d",__func__,__LINE__);
