#pragma once

#include "esp_bit_defs.h"
#include "esp_bt_defs.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/uart.h"

// Enable this switch to keep the migrated BLE server isolated from the file server logic.
// 打开这个开关可让移植过来的 BLE 服务独立于文件服务器逻辑，后续调试时只需要改这里。
#define USER_BLE_ENABLE 1

// Keep all migrated BLE identifiers here so future app/protocol changes do not touch user_app.cpp.
// 将移植过来的 BLE 标识集中放在这里，后续修改 APP 协议或设备名时不用改 user_app.cpp。
#define TDX_BLE_LOG_TAG "BLE"
#define TDX_BLE_PROFILE_NUM 1
#define TDX_BLE_PROFILE_APP_IDX 0
#define TDX_BLE_APP_ID 0x56
#define TDX_BLE_DEVICE_NAME "Tdx_6_color"
#define TDX_BLE_SERVICE_INST_ID 0
#define TDX_BLE_ATT_UUID_SIZE 16
#define TDX_BLE_DATA_MAX_LEN 512
#define TDX_BLE_TX_POWER_LOWEST ESP_PWR_LVL_N24

// Keep the original source project's attribute alias visible for protocol mapping checks.
// 保留源工程里的属性别名，方便以后核对手机端写入的是哪一个协议通道。
#define TDX_BLE_SWITCH_MODE_VALUE_INDEX TDX_IDX_14_VAL

// Keep declaration length in one place because every characteristic declaration depends on it.
// 特征声明长度统一放在这里，因为每个 BLE characteristic declaration 都依赖这个长度。
#define TDX_BLE_CHAR_DECLARATION_SIZE (sizeof(uint8_t))

// Keep Server Network STA return codes here so main.c and the STA module share one result contract.
// 将 Server Network STA 返回值集中在这里，保证 main.c 和 STA 模块使用同一套结果约定。
#define SERVER_NETWORK_STA_OK 1
#define SERVER_NETWORK_STA_CONNECT_FAIL 3
#define SERVER_NETWORK_STA_NO_SAVED_WIFI 0xA1

// Keep STA wait bits here so future connection policy changes do not require editing the STA implementation.
// 将 STA 等待事件位集中在这里，后续调整连接策略时不用修改 STA 实现代码。
#define SERVER_NETWORK_STA_CONNECTED_BIT BIT0
#define SERVER_NETWORK_STA_FAIL_BIT BIT1

// Keep the STA connection timeout configurable from one header for board bring-up tuning.
// 将 STA 联网超时时间集中在一个头文件里，方便上板调试时统一调整。
#define SERVER_NETWORK_STA_CONNECT_TIMEOUT_MS 8000

// Keep the migrated /dataUP upload body limit here so browser upload behavior can be tuned in one place.
// 将移植的 /dataUP 上传 body 限制集中在这里，方便统一调整网页上传容量。
#define SERVER_NETWORK_STA_DATAUP_MAX_BODY_SIZE (2 * 1024 * 1024)

// Keep /dataUP parser string limits here because they must match the old web page form field sizes.
// 将 /dataUP 解析字符串限制集中在这里，因为它们需要匹配旧网页表单字段大小。
#define SERVER_NETWORK_STA_DATAUP_FIELD_NAME_MAX 32
#define SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX 96
#define SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX 32
#define SERVER_NETWORK_STA_UPLOAD_RESULT_JSON_MAX 768

// Keep the migrated HTTP receive dispatcher limits here so request routing can be tuned without touching parser code.
// 将移植的 HTTP 接收分发限制集中在这里，后续调整收包和 JSON 分发时不用修改解析代码。
#define SERVER_NETWORK_STA_HTTP_HEADER_VALUE_MAX 256
#define SERVER_NETWORK_STA_SMALL_JSON_BODY_MAX 4096

// Keep OTA upload limits here so the partition size and HTTP body policy can be checked together.
// 将 OTA 上传限制集中在这里，方便和 Flash OTA 分区大小一起核对。
#define SERVER_NETWORK_STA_OTA_UPLOAD_MAX_BODY_SIZE (6 * 1024 * 1024)
#define SERVER_NETWORK_STA_OTA_BOUNDARY_MAX 96
#define SERVER_NETWORK_STA_OTA_VERSION_MAX 40

// Keep the last-cast record name here so reboot recovery and cast saving use the same file.
// 将最后一次 cast 记录文件名集中在这里，保证重启恢复和投图保存使用同一个记录文件。
#define SERVER_NETWORK_STA_LAST_CAST_FILE "last_cast.txt"

// Keep saved-image listing limits here so JSON response size can be tuned without touching scan logic.
// 将已存图片列表响应限制集中在这里，后续调整返回列表容量时不用修改扫描逻辑。
#define SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX 8192
#define SERVER_NETWORK_STA_THUMB_URI_PREFIX "/thumb/"

// Keep slideshow run modes here so software and future deep-sleep behavior share one switch.
// 将轮播运行模式集中在这里，便于软件常驻和后续深睡模式共用同一个开关。
#define TDX_SLIDESHOW_RUN_MODE_SOFTWARE 0
#define TDX_SLIDESHOW_RUN_MODE_DEEP_SLEEP 1

// Default to software slideshow so WiFi and HTTP server remain available during playback.
// 默认使用软件常驻轮播，让 WiFi 和 HTTP Server 在播放期间继续可用。
#ifndef TDX_SLIDESHOW_RUN_MODE
#define TDX_SLIDESHOW_RUN_MODE TDX_SLIDESHOW_RUN_MODE_SOFTWARE
#endif

// Keep slideshow limits and state file names here so JSON parsing and saved config stay in sync.
// 将轮播限制和状态文件名集中在这里，保证 JSON 解析与保存配置保持一致。
#define TDX_SLIDESHOW_FILE_NAME_MAX_LEN 48
#define TDX_SLIDESHOW_MAX_FILES 50
#define TDX_SLIDESHOW_INTERVAL_MIN_SECONDS 1
#define TDX_SLIDESHOW_INTERVAL_MAX_SECONDS 3600
#define TDX_SLIDESHOW_CONFIG_FILE "slideshow_config.txt"
#define TDX_SLIDESHOW_CONTROL_FILE "show_control.txt"
#define TDX_SLIDESHOW_AFTER_DISPLAY_WAIT_MS 12000
#define TDX_SLIDESHOW_DEEP_SLEEP_FLAG_VALUE 0xA5
#define TDX_SLIDESHOW_NVS_FLAG_KEY "slide_ds"
#define TDX_SLIDESHOW_NVS_LAST_FILE_KEY "slide_last"
#define TDX_SLIDESHOW_RANDOM_NVS_KEY "slide_random"

// Keep delete request limits here so file removal cannot grow unbounded from one JSON request.
// 将删除请求限制集中在这里，避免单次 JSON 删除数量无限增长。
#define SERVER_NETWORK_STA_DELETE_MAX_FILES 50

// Keep WiFi keep-alive limits here so phone commands cannot request an unbounded online window.
// 将 WiFi 保活时长限制集中在这里，避免手机指令请求无限制在线时间。
#define SERVER_NETWORK_STA_WIFI_WORK_TIME_MIN_SECONDS 1
#define SERVER_NETWORK_STA_WIFI_WORK_TIME_MAX_SECONDS 3600

// Keep the ping URI here so heartbeat routing can change without touching GET resource handlers.
// 将 ping 路径集中在这里，后续调整心跳路由时不用修改 GET 资源处理函数。
#define SERVER_NETWORK_STA_PING_URI "/ping"

// Keep CH583 UART receive enabled from one switch so board bring-up can disable it without touching task code.
// 将 CH583 串口接收开关集中在这里，方便上板调试时不改任务代码就能关闭。
#define USER_CH583_UART_ENABLE 1

// Keep CH583 UART pins and baud rate here so protocol TX and RX always use the same physical port.
// 将 CH583 串口引脚和波特率集中在这里，保证协议发送和接收始终使用同一个物理串口。
#define USER_CH583_UART_PORT UART_NUM_0
#define USER_CH583_UART_TX_PIN GPIO_NUM_43
#define USER_CH583_UART_RX_PIN GPIO_NUM_44
#define USER_CH583_UART_BAUD_RATE 115200
#define CH583_WIFI_UART_PORT USER_CH583_UART_PORT

// Keep CH583 UART buffer and task sizes here so receive pressure can be tuned without changing task logic.
// 将 CH583 串口缓冲区和任务栈大小集中在这里，后续调接收压力时不用改任务逻辑。
#define USER_CH583_UART_RECEIVE_BUF_SIZE 256
#define USER_CH583_UART_DRIVER_RX_BUF_SIZE 8192
#define USER_CH583_UART_DRIVER_TX_BUF_SIZE 0
#define USER_CH583_UART_EVENT_QUEUE_SIZE 20
#define USER_CH583_UART_EVENT_TASK_STACK_SIZE (3 * 1024)
#define USER_CH583_UART_RECEIVE_TASK_STACK_SIZE (8 * 1024)
#define USER_CH583_UART_WAKEUP_THRESHOLD 3

// Keep CH583 protocol debug flags here so frame parsing logs can be enabled without editing the copied protocol file.
// 将 CH583 协议调试开关集中在这里，后续打开帧解析日志时不用修改复制过来的协议文件。
#define CH583_WIFI_UART_DEBUG_PRINT_ENABLE 1
#define CH583_WIFI_UART_DIRECTION_PRINT_ENABLE 1
#define CH583_WIFI_UART_TX_SILENCE_MS 100
#define CH583_WIFI_UART_BAD_CRC_RETRY_MAX 10

// Keep EPD display enable here so network cast/upload can be tested without editing receive code.
// 中文：将 EPD 显示开关集中在这里，便于不改收包代码就测试 cast/upload 显示。
#define USER_EPD_ENABLE 1

// Keep EPD panel geometry here because the copied display driver and network bin size must match.
// 中文：将 EPD 屏幕尺寸集中在这里，保证复制过来的显示驱动和网络 bin 数据大小匹配。
#define USER_EPD_WIDTH 1600
#define USER_EPD_HEIGHT 1200
#define USER_EPD_SCALE_MAX_WIDTH 1350
#define USER_EPD_SCALE_MAX_HEIGHT 1350
#define USER_EPD_TYPE 2

// Keep EPD SPI pins here so board pin changes do not require editing display_bsp.cpp.
// 中文：将 EPD SPI 引脚集中在这里，后续板级引脚变化不用修改 display_bsp.cpp。
#define USER_EPD_MOSI_PIN 11
#define USER_EPD_SCK_PIN 10
#define USER_EPD_DC_PIN 8
#define USER_EPD_CS_PIN 9
#define USER_EPD_CS2_PIN 46
#define USER_EPD_RST_PIN 12
#define USER_EPD_BUSY_PIN 13
#define USER_EPD_SPI_HOST SPI3_HOST

// Keep EPD task settings here so display latency and stack pressure can be tuned in one place.
// 中文：将 EPD 任务参数集中在这里，方便统一调整显示延迟和栈空间压力。
#define USER_EPD_DISPLAY_QUEUE_LENGTH 1
#define USER_EPD_DISPLAY_TASK_STACK_SIZE (8 * 1024)
#define USER_EPD_DISPLAY_TASK_PRIORITY 5

// Map the copied display driver's colored logs to ESP-IDF logs for this project.
// 中文：把复制过来的显示驱动彩色日志映射到 ESP-IDF 日志，避免依赖旧工程日志宏。
#ifndef LOG_Blue
#define LOG_Blue(fmt, ...) ESP_LOGI("Display", fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_Purple
#define LOG_Purple(fmt, ...) ESP_LOGI("Display", fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_Cyan
#define LOG_Cyan(fmt, ...) ESP_LOGI("Display", fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_INFO
#define LOG_INFO(fmt, ...) ESP_LOGI("Display", fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_WARN
#define LOG_WARN(fmt, ...) ESP_LOGW("Display", fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_ERROR
#define LOG_ERROR(fmt, ...) ESP_LOGE("Display", fmt, ##__VA_ARGS__)
#endif

// Keep the old display driver's board switch defined here so copied code has a stable board path.
// 中文：将旧显示驱动的板级开关放在这里，保证复制代码有稳定的板级分支。
#define Hardware_Version_ 1

// Keep LED status enable here so bring-up can disable indicators without changing business code.
// 中文：将 LED 状态显示开关集中在这里，方便调试时不改业务代码就关闭指示灯。
#define USER_LED_STATUS_ENABLE 1

// Keep LED pins and active level here because GPIO42/GPIO45 are low-level-on board LEDs.
// 中文：将 LED 引脚和有效电平集中在这里，因为 GPIO42/GPIO45 是低电平点亮的板载灯。
#define USER_LED_GREEN_PIN GPIO_NUM_42
#define USER_LED_RED_PIN GPIO_NUM_45
#define USER_LED_ON_LEVEL 0
#define USER_LED_OFF_LEVEL 1

// Keep LED blink timing here so status behavior can be tuned without editing the LED task.
// 中文：将 LED 闪烁时间集中在这里，后续调整状态显示节奏不用修改 LED 任务。
#define USER_LED_FAST_BLINK_MS 100
#define USER_LED_MID_BLINK_MS 500
#define USER_LED_SLOW_BLINK_MS 1000
#define USER_LED_SUCCESS_HOLD_MS 1000
#define USER_LED_STATUS_TASK_STACK_SIZE (2 * 1024)
#define USER_LED_STATUS_TASK_PRIORITY 3
