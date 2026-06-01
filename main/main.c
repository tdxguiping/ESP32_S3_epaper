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
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "ch583_uart_app.h"
#include "epd_display_app.h"
#include "file_serving_example_common.h"
#include "led_status.h"
#include "server_network_sta.h"
#include "server_network_sta_wifi_work_time.h"
#include "tdx_cfg.h"
#include "user_app.h"

/* This example demonstrates how to create file server
 * using esp_http_server. This file has only startup code.
 * Look in file_server.c for the implementation.
 */

static const char *TAG = "example";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting example");
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(ServerNetworkStaWifiWorkTime_Init());
    ESP_ERROR_CHECK(UserLedStatus_Init());
#if USER_BLE_ENABLE
    // Start BLE after NVS/event loop so BT state and callbacks have the required system services.
    // 在 NVS 和事件循环初始化之后启动 BLE，保证蓝牙状态和回调依赖的系统服务已经就绪。
    Init_Bl();
#endif
    ESP_ERROR_CHECK(Ch583UartApp_Init());
    /* English: Initialize the migrated EPD driver at startup before network cast/upload can request display. */
    /* 中文：启动时初始化移植过来的 EPD 驱动，保证网络 cast/upload 请求显示前屏幕已经就绪。 */
    ESP_ERROR_CHECK(ServerNetworkStaEpdDisplay_Init());

    /* Initialize file storage */
    const char* base_path = "/data";
    ESP_ERROR_CHECK(example_mount_storage(base_path));

    // Force the old read_value=0x02 path here: Server Network STA only, then start the HTTP file server.
    // 在这里固定旧工程 read_value=0x02 路径：只进入 Server Network STA，然后启动 HTTP 文件服务器。
    uint8_t network_ret = User_Network_mode_app_init(base_path);
    ESP_LOGI(TAG, "Server Network STA init result=0x%02x", network_ret);
    if (network_ret != SERVER_NETWORK_STA_OK) {
        UserLedStatus_Set(USER_LED_STATE_WIFI_FAIL);
        ESP_LOGE(TAG, "Server Network STA failed, file server not started");
        return;
    }
    UserLedStatus_Set(USER_LED_STATE_SERVER_READY);
    ESP_LOGI(TAG, "Server Network STA file server started");
}
