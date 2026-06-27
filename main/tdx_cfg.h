#pragma once

/* -------------------------------------------------------------------------- */
/* 00. Header / Includes                                                       */
/* -------------------------------------------------------------------------- */

#include <stdint.h>
#include <stddef.h>

#include "esp_bit_defs.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* 01. Board / Product / Global Startup Policy                                 */
/* -------------------------------------------------------------------------- */

// Select ESP32-C5 as the only supported board for this project build.
// 选择 ESP32-C5 作为当前工程唯一支持的板级配置。
#define USER_BOARD_ESP32C5 1

// Keep old reset markers here so startup display policy can be changed without touching main.c.
#define ESP_RST_low_power_No_Disp 0xFE
#define ESP_RST_need_Disp_EPD 0xFD

// Keep large buffer fallback limit here so HTTP and EPD avoid exhausting internal RAM.
#define USER_INTERNAL_RAM_FALLBACK_MAX_SIZE (128 * 1024)

// Enable Auto Light-sleep and WiFi modem-sleep after STA gets IP.
// Set to 0 to keep CPU at 240 MHz and WiFi PS disabled without changing sdkconfig.
#ifndef TDX_AUTO_LIGHT_SLEEP_ENABLE
#define TDX_AUTO_LIGHT_SLEEP_ENABLE 0
#endif


/* -------------------------------------------------------------------------- */
/* 02. Common JSON Result Codes                                                */
/* -------------------------------------------------------------------------- */

#define TDX_STRINGIFY_INNER(value) #value
#define TDX_STRINGIFY(value) TDX_STRINGIFY_INNER(value)

// Keep JSON API result codes centralized so every response follows README_Result_Code.md.
// 将 JSON API 返回码集中在这里，保证所有响应都按 README_Result_Code.md 统一维护。
#define TDX_JSON_RESULT_OK 0
#define TDX_JSON_RESULT_JSON_INVALID 1001
#define TDX_JSON_RESULT_FUNC_UNSUPPORTED 1002
#define TDX_JSON_RESULT_FIELD_MISSING 1003
#define TDX_JSON_RESULT_PARAM_INVALID 1004
#define TDX_JSON_RESULT_METHOD_UNSUPPORTED 1005
#define TDX_JSON_RESULT_BODY_TOO_LARGE 1006
#define TDX_JSON_RESULT_BUSY 1007
#define TDX_JSON_RESULT_TIMEOUT 1008
#define TDX_JSON_RESULT_INTERNAL_ERROR 1009
#define TDX_JSON_RESULT_JSON_TOO_LONG 1010
#define TDX_JSON_RESULT_NO_MEMORY 1011
#define TDX_JSON_RESULT_STORAGE_NOT_READY 1012
#define TDX_JSON_RESULT_STORAGE_NO_SPACE 1013
#define TDX_JSON_RESULT_NOT_FOUND 1014
#define TDX_JSON_RESULT_PATH_UNSAFE 1015
#define TDX_JSON_RESULT_QUEUE_FAILED 1016

/* -------------------------------------------------------------------------- */
/* 03. USB / BLE / WiFi JSON Result Codes                                      */
/* -------------------------------------------------------------------------- */

// Keep USB JSON result codes separate so serial transport errors are easy to diagnose.
// 将 USB JSON 返回码单独分组，便于定位串口传输和路由错误。
#define TDX_JSON_RESULT_USB_REQUEST_TOO_LARGE 1101
#define TDX_JSON_RESULT_USB_REQUEST_TIMEOUT 1102
#define TDX_JSON_RESULT_USB_BAD_REQUEST 1103
#define TDX_JSON_RESULT_USB_ROUTE_NOT_FOUND 1104
#define TDX_JSON_RESULT_USB_HANDLER_FAILED 1105
#define TDX_JSON_RESULT_USB_ASYNC_FAILED 1106

// Keep BLE and CH583 JSON result codes here so BLE_DATA replies share one contract.
// 将 BLE 和 CH583 JSON 返回码集中在这里，保证 BLE_DATA 回复格式一致。
#define TDX_JSON_RESULT_BLE_JSON_EMPTY 1201
#define TDX_JSON_RESULT_BLE_FUNC_UNSUPPORTED 1202
#define TDX_JSON_RESULT_BLE_JSON_PARSE_FAILED 1203
#define TDX_JSON_RESULT_BLE_SEND_FAILED 1204
#define TDX_JSON_RESULT_BLE_NO_SAVED_WIFI 1205

// Keep WiFi JSON result codes here so USB, BLE, and network paths use the same meanings.
// 将 WiFi JSON 返回码集中在这里，保证 USB、BLE 和网络路径含义一致。
#define TDX_JSON_RESULT_WIFI_SSID_MISSING 1301
#define TDX_JSON_RESULT_WIFI_KEY_MISSING 1302
#define TDX_JSON_RESULT_WIFI_SSID_INVALID 1303
#define TDX_JSON_RESULT_WIFI_KEY_INVALID 1304
#define TDX_JSON_RESULT_WIFI_SAVE_FAILED 1305
#define TDX_JSON_RESULT_WIFI_CONNECT_SUBMIT_FAILED 1306
#define TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT 1307
#define TDX_JSON_RESULT_WIFI_AUTH_FAILED 1308
#define TDX_JSON_RESULT_WIFI_GOT_IP_FAILED 1309
#define TDX_JSON_RESULT_WIFI_WORK_TIME_MISSING 1351
#define TDX_JSON_RESULT_WIFI_WORK_TIME_RANGE 1352
#define TDX_JSON_RESULT_WIFI_WORK_TIME_SAVE_FAILED 1353
#define TDX_JSON_RESULT_WIFI_WORK_TIME_APPLY_FAILED 1354

/* -------------------------------------------------------------------------- */
/* 04. Image / Delete / Slideshow / Upload / OTA / EPD Result Codes            */
/* -------------------------------------------------------------------------- */

// Keep image, slideshow, upload, OTA, and EPD result codes here for feature-local response updates.
// 将图片、轮播、上传、OTA 和 EPD 返回码集中在这里，便于按功能小步修改响应。
#define TDX_JSON_RESULT_IMAGES_READ_FAILED 1401
#define TDX_JSON_RESULT_THUMB_NAME_INVALID 1402
#define TDX_JSON_RESULT_THUMB_NOT_FOUND 1403
#define TDX_JSON_RESULT_SNAPSHOT_BUILD_FAILED 1404
#define TDX_JSON_RESULT_BLE_MAC_EMPTY 1405

#define TDX_JSON_RESULT_FILE_NAMES_MISSING 1501
#define TDX_JSON_RESULT_FILE_NAME_INVALID 1502
#define TDX_JSON_RESULT_DELETE_FAILED 1503
#define TDX_JSON_RESULT_SLIDESHOW_CONFIG_SAVE_FAILED 1504
#define TDX_JSON_RESULT_SLIDESHOW_START_FAILED 1505
#define TDX_JSON_RESULT_SLIDESHOW_RUNTIME_FAILED 1506
#define TDX_JSON_RESULT_SLIDESHOW_INTERVAL_INVALID 1507
#define TDX_JSON_RESULT_SLIDESHOW_FILE_NOT_FOUND 1508
#define TDX_JSON_RESULT_SLIDESHOW_CONTROL_SAVE_FAILED 1509

#define TDX_JSON_RESULT_UPLOAD_BOUNDARY_MISSING 1601
#define TDX_JSON_RESULT_UPLOAD_FUNC_MISSING 1602
#define TDX_JSON_RESULT_UPLOAD_INVALID 1603
#define TDX_JSON_RESULT_UPLOAD_BIN_MISSING 1604
#define TDX_JSON_RESULT_UPLOAD_IMAGE_MISSING 1605
#define TDX_JSON_RESULT_UPLOAD_SIZE_MISMATCH 1606
#define TDX_JSON_RESULT_SAVE_BIN_FAILED 1607
#define TDX_JSON_RESULT_SAVE_IMAGE_FAILED 1608
#define TDX_JSON_RESULT_DISPLAY_QUEUE_FAILED 1609
#define TDX_JSON_RESULT_LAST_CAST_SAVE_FAILED 1610
#define TDX_JSON_RESULT_SAVE_REQUIRED_FOR_LAST_CAST 1611
#define TDX_JSON_RESULT_UPLOAD_FILE_NAME_INVALID 1612
#define TDX_JSON_RESULT_UPLOAD_RAW_PATH_MISSING 1613
#define TDX_JSON_RESULT_UPLOAD_RAW_PATH_INVALID 1614
#define TDX_JSON_RESULT_UPLOAD_RAW_SAVE_FAILED 1615
#define TDX_JSON_RESULT_CAST2PIC_SCREEN_INVALID 1616
#define TDX_JSON_RESULT_CAST2PIC_SCREEN_UNSUPPORTED 1617

#define TDX_JSON_RESULT_OTA_BOUNDARY_MISSING 1701
#define TDX_JSON_RESULT_OTA_META_MISSING 1702
#define TDX_JSON_RESULT_OTA_META_INVALID 1703
#define TDX_JSON_RESULT_OTA_FIRMWARE_MISSING 1704
#define TDX_JSON_RESULT_OTA_FIRMWARE_SIZE_INVALID 1705
#define TDX_JSON_RESULT_OTA_BEGIN_FAILED 1706
#define TDX_JSON_RESULT_OTA_WRITE_FAILED 1707
#define TDX_JSON_RESULT_OTA_END_FAILED 1708
#define TDX_JSON_RESULT_OTA_VERIFY_FAILED 1709
#define TDX_JSON_RESULT_OTA_SET_BOOT_FAILED 1710
#define TDX_JSON_RESULT_OTA_VERSION_MISMATCH 1711
#define TDX_JSON_RESULT_OTA_PARTITION_TOO_SMALL 1712
#define TDX_JSON_RESULT_OTA_BUSY 1713

#define TDX_JSON_RESULT_EPD_TYPE_INVALID 1801
#define TDX_JSON_RESULT_EPD_TYPE_SAVE_FAILED 1802
#define TDX_JSON_RESULT_EPD_TEST_DISPLAY_FAILED 1803
#define TDX_JSON_RESULT_EPD_DISPLAY_FAILED 1804

/* -------------------------------------------------------------------------- */
/* 05. Server Network STA / WiFi / mDNS                                        */
/* -------------------------------------------------------------------------- */

// Keep STA connection debug logs here so WiFi failure checks can be enabled without changing STA logic.
// 将 STA 连接调试日志开关放在这里，便于不修改 STA 逻辑就排查 WiFi 失败原因。
#define SERVER_NETWORK_STA_DEBUG_LOG_ENABLE 1

// Keep Server Network STA return codes here so main.c and the STA module share one result contract.
#define SERVER_NETWORK_STA_OK 1
#define SERVER_NETWORK_STA_CONNECT_FAIL 3
#define SERVER_NETWORK_STA_NO_SAVED_WIFI 0xA1

// Keep STA wait bits here so future connection policy changes do not require editing the STA implementation.
#define SERVER_NETWORK_STA_CONNECTED_BIT BIT0
#define SERVER_NETWORK_STA_FAIL_BIT BIT1
#define SERVER_NETWORK_STA_DISCONNECTED_BIT BIT2

// Keep the STA connection timeout configurable from one header for board bring-up tuning.
#define SERVER_NETWORK_STA_CONNECT_TIMEOUT_MS 10000

// Hint the known AP channel to reduce WiFi scan time without binding to a fixed BSSID.
#define SERVER_NETWORK_STA_WIFI_CHANNEL_HINT 11

// Keep the ping URI here so heartbeat routing can change without touching GET resource handlers.
#define SERVER_NETWORK_STA_PING_URI "/ping"

// Keep the mDNS host name here so board/product naming does not leak into network code.
// 中文：将 mDNS 主机名集中在这里，避免板级/产品命名散落到网络代码里。
#define USER_MDNS_HOSTNAME "esp32-c5-photopainter"

// Keep the mDNS instance name here so logs and discovery identify the C5 build correctly.
// 中文：将 mDNS 实例名集中在这里，确保日志和发现服务正确标识 C5 版本。
#define USER_MDNS_INSTANCE_NAME "ESP32-C5-WebServer"

/* -------------------------------------------------------------------------- */
/* 06. Storage / HTTP Upload / Multipart Parser                                */
/* -------------------------------------------------------------------------- */

// Print the /data file tree during startup. Keep enabled by default for bring-up visibility.
#define USER_STORAGE_LIST_ON_STARTUP_ENABLE 0

// Enable SD card probing before SPIFFS. Set to 0 when the board has no SD card to boot faster.
#define USER_STORAGE_SD_CARD_ENABLE 1

// Keep the migrated /dataUP upload body limit here so browser upload behavior can be tuned in one place.
#define SERVER_NETWORK_STA_DATAUP_MAX_BODY_SIZE (2 * 1024 * 1024)

// Keep /dataUP parser string limits here because they must match the old web page form field sizes.
#define SERVER_NETWORK_STA_DATAUP_FIELD_NAME_MAX 32
#define SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX 96
#define SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX 32
#define SERVER_NETWORK_STA_UPLOAD_RESULT_JSON_MAX 768

// Keep cast save reserve here so SPIFFS writes leave room for temp files and metadata.
#define SERVER_NETWORK_STA_CAST_SAVE_RESERVE_BYTES (128 * 1024)

// Keep the migrated HTTP receive dispatcher limits here so request routing can be tuned without touching parser code.
#define SERVER_NETWORK_STA_HTTP_HEADER_VALUE_MAX 256
#define SERVER_NETWORK_STA_SMALL_JSON_BODY_MAX 4096

/* -------------------------------------------------------------------------- */
/* 07. USB Console / USB HTTP Text Transport                                   */
/* -------------------------------------------------------------------------- */

// Keep USB console HTTP-text limits here so the serial entry can be tuned without touching feature modules.
// 将 USB 串口 HTTP 文本限制放在这里，方便以后不改功能模块就调整串口入口。
#define USB_CONSOLE_ENABLE 1
#define USB_CONSOLE_RX_BUF_SIZE 4096
#define USB_CONSOLE_TX_BUF_SIZE 1024
#define USB_CONSOLE_HTTP_HEADER_MAX 2048
#define USB_CONSOLE_HTTP_PATH_MAX 128
#define USB_CONSOLE_HTTP_METHOD_MAX 8
#define USB_CONSOLE_HTTP_CONTENT_TYPE_MAX 128
#define USB_CONSOLE_BASE_PATH "/data"
#define USB_CONSOLE_HTTP_BODY_MAX SERVER_NETWORK_STA_DATAUP_MAX_BODY_SIZE
#define USB_CONSOLE_HTTP_RESPONSE_MAX 8192
#define USB_CONSOLE_TASK_STACK_SIZE (12 * 1024)
#define USB_CONSOLE_TASK_PRIORITY 3
#define USB_CONSOLE_WORKER_QUEUE_LENGTH 4
#define USB_CONSOLE_WORKER_TASK_STACK_SIZE (8 * 1024)
#define USB_CONSOLE_WORKER_TASK_PRIORITY 4
#define CAST_SAVE_TASK_QUEUE_LENGTH 2
#define CAST_SAVE_TASK_STACK_SIZE (8 * 1024)
#define CAST_SAVE_TASK_PRIORITY 4

// Use a longer idle USB read wait so the console task does not wake CPU too often without traffic.
// USB 空闲时使用较长读等待，避免没有数据时频繁唤醒 CPU。
#define USB_CONSOLE_READ_IDLE_TIMEOUT_MS 1000

// Use a short active USB read wait once a request starts so large multipart bodies are received quickly.
// 一旦请求开始接收就使用较短读等待，让大 multipart 数据尽快收完。
#define USB_CONSOLE_READ_ACTIVE_TIMEOUT_MS 1
#define USB_CONSOLE_WRITE_TIMEOUT_MS 100
#define USB_CONSOLE_REQUEST_TIMEOUT_MS 30000
#define USB_CONSOLE_START_DELAY_MS 3000
#define USB_CONSOLE_FEATURE_PENDING_STATUS 501
#define USB_CONSOLE_VERBOSE_LOG_ENABLE 0
#define USB_CONSOLE_FRAME_HEAD "@#$\r\n"
#define USB_CONSOLE_FRAME_TAIL "\r\n%^&\r\n"

// Log USB receive progress every fixed byte step so serial upload bottlenecks can be located.
// 按固定字节步进打印 USB 接收进度，便于定位串口上传瓶颈。
#define USB_CONSOLE_RX_PROGRESS_STEP_BYTES (20 * 1024)

// Keep file save stdio buffering configurable without touching USB feature code.
// 将文件保存 stdio 缓冲大小集中配置，便于以后优化 SD/FATFS 写入。
#define USB_CONSOLE_FILE_SAVE_STREAM_BUF_SIZE (64 * 1024)

#define USER_USB_CONSOLE_ANSI_COLOR_TEST_ENABLE 1

/* -------------------------------------------------------------------------- */
/* 08. OTA Upload                                                              */
/* -------------------------------------------------------------------------- */

// Keep OTA upload limits here so the partition size and HTTP body policy can be checked together.
// 中文：OTA 上传限制集中放在这里，便于同时检查 HTTP body 和 OTA 分区容量。
#define SERVER_NETWORK_STA_OTA_UPLOAD_MAX_BODY_SIZE (6 * 1024 * 1024)

// Reserve multipart header room above the firmware partition size when rejecting oversize OTA bodies early.
// 中文：提前拒绝超大 OTA body 时，为 multipart 头部和 meta 字段预留这部分空间。
#define SERVER_NETWORK_STA_OTA_MULTIPART_OVERHEAD_BYTES (64 * 1024)
#define SERVER_NETWORK_STA_OTA_BOUNDARY_MAX 96
#define SERVER_NETWORK_STA_OTA_VERSION_MAX 40

/* -------------------------------------------------------------------------- */
/* 09. Saved Images / Cast / Snapshot / Delete                                 */
/* -------------------------------------------------------------------------- */

// Keep the last-cast record name here so reboot recovery and cast saving use the same file.
#define SERVER_NETWORK_STA_LAST_CAST_FILE "last_cast.txt"

// Keep saved-image listing limits here so JSON response size can be tuned without touching scan logic.
#define SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX 8192
#define SERVER_NETWORK_STA_THUMB_URI_PREFIX "/thumb/"

// Keep delete request limits here so file removal cannot grow unbounded from one JSON request.
#define SERVER_NETWORK_STA_DELETE_MAX_FILES 50

/* -------------------------------------------------------------------------- */
/* 10. Slideshow                                                               */
/* -------------------------------------------------------------------------- */

// Keep slideshow run modes here so software and future deep-sleep behavior share one switch.
#define TDX_SLIDESHOW_RUN_MODE_SOFTWARE 0
#define TDX_SLIDESHOW_RUN_MODE_DEEP_SLEEP 1

// Default to software slideshow so WiFi and HTTP server remain available during playback.
#ifndef TDX_SLIDESHOW_RUN_MODE
#define TDX_SLIDESHOW_RUN_MODE TDX_SLIDESHOW_RUN_MODE_SOFTWARE
#endif

// Keep slideshow limits and state file names here so JSON parsing and saved config stay in sync.
#define TDX_SLIDESHOW_FILE_NAME_MAX_LEN 48
#define TDX_SLIDESHOW_MAX_FILES 50
#define TDX_SLIDESHOW_INTERVAL_MIN_SECONDS 60
#define TDX_SLIDESHOW_INTERVAL_MAX_SECONDS (7U * 24U * 60U * 60U) // 7 days
#define TDX_SLIDESHOW_CONFIG_FILE "slideshow_config.txt"
#define TDX_SLIDESHOW_CONTROL_FILE "show_control.txt"
#define TDX_SLIDESHOW_AFTER_DISPLAY_WAIT_MS 12000
#define TDX_SLIDESHOW_DEEP_SLEEP_FLAG_VALUE 0xA5
#define TDX_SLIDESHOW_NVS_FLAG_KEY "slide_ds"
#define TDX_SLIDESHOW_NVS_LAST_FILE_KEY "slide_last"
#define TDX_SLIDESHOW_NVS_PROGRESS_KEY "slide_progress"
#define TDX_SLIDESHOW_RANDOM_NVS_KEY "slide_random"

/* -------------------------------------------------------------------------- */
/* 11. WiFi Work Time / Sleep Runtime State / NVS Keys                         */
/* -------------------------------------------------------------------------- */

// Keep WiFi keep-alive limits here so phone commands cannot request an unbounded online window.
#define SERVER_NETWORK_STA_WIFI_WORK_TIME_MIN_SECONDS 60   // 秒
#define SERVER_NETWORK_STA_WIFI_WORK_TIME_MAX_SECONDS 3600 // 秒
#define SERVER_NETWORK_STA_WIFI_WORK_TIME_DEFAULT_SECONDS 300 // 秒

#define SERVER_REQUIRED_CONTINUE_WORK_TIME_NVS_KEY "work_continue"
#define WIFI_STANDBY_TIME_S_NVS_KEY "wifi_standby"
#define CH583_BLE_MAC_NVS_KEY "ch583_ble_mac"

// Keep sleep/work-state NVS keys here so BLE, HTTP, and network timers share one saved runtime state.
#define USER_WORK_STATE_NVS_NAMESPACE "work_state"
#define USER_WORK_STATE_NVS_KEY "runtime"
#define USER_WORK_STATE_DEFAULT_CONTINUE_SECONDS SERVER_NETWORK_STA_WIFI_WORK_TIME_DEFAULT_SECONDS
#define USER_WORK_STATE_DEFAULT_STANDBY_SECONDS 15
#define USER_WORK_STATE_MIN_CONTINUE_SECONDS SERVER_NETWORK_STA_WIFI_WORK_TIME_MIN_SECONDS
#define USER_WORK_STATE_MAX_CONTINUE_SECONDS SERVER_NETWORK_STA_WIFI_WORK_TIME_MAX_SECONDS
#define USER_WORK_STATE_TASK_STACK_SIZE (8 * 1024)
#define USER_WORK_STATE_TASK_PRIORITY 3
#define USER_WORK_STATE_TASK_INTERVAL_MS 1000

/* -------------------------------------------------------------------------- */
/* 12. BLE / GATT Legacy Compatibility                                         */
/* -------------------------------------------------------------------------- */

// Keep BLE optional so board bring-up can disable Bluetooth without editing BLE source files.
#ifndef USER_BLE_ENABLE
#define USER_BLE_ENABLE 0
#endif

#if USER_BLE_ENABLE
#include "esp_bt_defs.h"
#endif

// Keep all migrated BLE identifiers here so future app/protocol changes do not touch user_app.cpp.
#define TDX_BLE_LOG_TAG "BLE"
#define TDX_BLE_PROFILE_NUM 1
#define TDX_BLE_PROFILE_APP_IDX 0
#define TDX_BLE_APP_ID 0x56
#define TDX_BLE_DEVICE_NAME "Tdx_6_color"
#define TDX_BLE_SERVICE_INST_ID 0
#define TDX_BLE_ATT_UUID_SIZE 16
#define TDX_BLE_DATA_MAX_LEN 512

#if USER_BLE_ENABLE
#define TDX_BLE_TX_POWER_LOWEST ESP_PWR_LVL_N24
#else
#define TDX_BLE_TX_POWER_LOWEST 0
#endif

// Keep BLE JSON queue limits here so BLE write parsing can be tuned without touching the GATT callback.
#define USER_BLE_JSON_BUF_SIZE 1024
#define USER_BLE_WRITE_QUEUE_LENGTH 4
#define USER_BLE_WRITE_TASK_STACK_SIZE (12 * 1024)
#define USER_BLE_WRITE_TASK_PRIORITY 5

// Keep the original source project's attribute alias visible for protocol mapping checks.
#define TDX_BLE_SWITCH_MODE_VALUE_INDEX TDX_IDX_14_VAL

// Keep declaration length in one place because every characteristic declaration depends on it.
#define TDX_BLE_CHAR_DECLARATION_SIZE (sizeof(uint8_t))

/* -------------------------------------------------------------------------- */
/* 13. CH583 UART / CH583 WiFi Protocol                                        */
/* -------------------------------------------------------------------------- */

// Keep CH583 UART receive enabled from one switch so board bring-up can disable it without touching task code.
#define USER_CH583_UART_ENABLE 1

// Keep CH583 UART pins and baud rate here so protocol TX and RX always use the same physical port.
// 将 CH583 串口引脚和波特率集中在这里，保证协议发送和接收使用同一个物理串口。
#define USER_CH583_UART_PORT UART_NUM_1
#define USER_CH583_UART_TX_PIN GPIO_NUM_24
#define USER_CH583_UART_RX_PIN GPIO_NUM_23
#define USER_CH583_UART_BAUD_RATE 115200
#define CH583_WIFI_UART_PORT USER_CH583_UART_PORT

// Keep CH583 UART buffer and task sizes here so receive pressure can be tuned without changing task logic.
#define USER_CH583_UART_RECEIVE_BUF_SIZE 256
#define USER_CH583_UART_DRIVER_RX_BUF_SIZE 8192
#define USER_CH583_UART_DRIVER_TX_BUF_SIZE 0
#define USER_CH583_UART_EVENT_QUEUE_SIZE 20
#define USER_CH583_UART_EVENT_TASK_STACK_SIZE (8 * 1024)
#define USER_CH583_UART_RECEIVE_TASK_STACK_SIZE (8 * 1024)
#define USER_CH583_UART_WAKEUP_THRESHOLD 3

// Keep CH583 protocol debug flags here so frame parsing logs can be enabled without editing the copied protocol file.
#define CH583_WIFI_UART_DEBUG_PRINT_ENABLE 0
#define CH583_WIFI_UART_DIRECTION_PRINT_ENABLE 1
#define CH583_WIFI_UART_TX_SILENCE_MS 10
#define CH583_WIFI_UART_BAD_CRC_RETRY_MAX 5
#define CH583_WIFI_NFC_JSON_MAX_LEN 220
#define CH583_WIFI_NFC_BASE64URL_MAX_LEN 300
#define CH583_WIFI_NFC_TEST_ENABLE 1
#define CH583_WIFI_NFC_TEST_START_DELAY_SECONDS 5

/* -------------------------------------------------------------------------- */
/* 13.1 Runtime Debug Output                                                   */
/* -------------------------------------------------------------------------- */

// SDK console / bootloader logs are still controlled by sdkconfig. These macros
// only route application logs after UserDebugOutput_Init() is called.
#define USER_DEBUG_OUTPUT_USB_SERIAL_JTAG 1
#define USER_DEBUG_OUTPUT_UART0 2
#define USER_DEBUG_OUTPUT_BOTH 3

#ifndef USER_DEBUG_OUTPUT_TARGET
#define USER_DEBUG_OUTPUT_TARGET USER_DEBUG_OUTPUT_BOTH
#endif


// 1. USB Serial/JTAG
// 2. UART0
//    TX = GPIO11
//    RX = GPIO12
//    baud = 921600

#define USER_DEBUG_UART_PORT UART_NUM_0
#define USER_DEBUG_UART_TX_PIN GPIO_NUM_11
#define USER_DEBUG_UART_RX_PIN GPIO_NUM_12
#define USER_DEBUG_UART_BAUD_RATE 921600
#define USER_DEBUG_UART_RX_BUF_SIZE 256
#define USER_DEBUG_UART_TX_BUF_SIZE 4096
#define USER_DEBUG_UART_LOG_LINE_MAX 512

#if (USER_DEBUG_OUTPUT_TARGET == USER_DEBUG_OUTPUT_UART0) || \
    (USER_DEBUG_OUTPUT_TARGET == USER_DEBUG_OUTPUT_BOTH)
#define USER_DEBUG_UART0_ENABLED 1
#else
#define USER_DEBUG_UART0_ENABLED 0
#endif

/* -------------------------------------------------------------------------- */
/* 14. EPD Display / Panel Geometry / SPI Pins                                 */
/* -------------------------------------------------------------------------- */

// Keep EPD display enable here so network cast/upload can be tested without editing receive code.
#define USER_EPD_ENABLE 1

// Keep EPD panel geometry here because the copied display driver and network bin size must match.
#define USER_EPD_WIDTH 1600
#define USER_EPD_HEIGHT 1200
#define USER_EPD_SCALE_MAX_WIDTH 1350
#define USER_EPD_SCALE_MAX_HEIGHT 1350
#define USER_EPD_TYPE 2

// Keep EPD SPI pins here so board pin changes do not require editing display_bsp.cpp.
// 将墨水屏 SPI 引脚集中在这里，后续改板时不用修改 display_bsp.cpp。
#define USER_EPD_MOSI_PIN GPIO_NUM_1
#define USER_EPD_MISO_PIN GPIO_NUM_25
#define USER_EPD_SCK_PIN GPIO_NUM_6
#define USER_EPD_DC_PIN GPIO_NUM_8
#define USER_EPD_CS_PIN GPIO_NUM_7
#define USER_EPD_CS2_PIN GPIO_NUM_0
#define USER_EPD_RST_PIN GPIO_NUM_10
#define USER_EPD_BUSY_PIN GPIO_NUM_9
#define USER_EPD_SPI_HOST SPI2_HOST

// Keep the second EPD target mapped to the shared C5 EPD control lines plus CS2.
// 将第二路墨水屏目标映射到 C5 共用控制线和 CS2，避免使用旧 S3 的独立 EPD2 引脚。
#define USER_EPD2_DC_PIN USER_EPD_DC_PIN
#define USER_EPD2_CS_PIN USER_EPD_CS2_PIN
#define USER_EPD2_RST_PIN USER_EPD_RST_PIN
#define USER_EPD2_BUSY_PIN USER_EPD_BUSY_PIN

/* -------------------------------------------------------------------------- */
/* 15. SD Card SPI Pins                                                        */
/* -------------------------------------------------------------------------- */

// Keep SD SPI pins here because the C5 board shares MOSI and CLK with the EPD bus.
// 将 SD SPI 引脚集中在这里，因为 C5 板上 SD 与墨水屏共用 MOSI 和 CLK。
#define USER_SD_SPI_MOSI_PIN GPIO_NUM_1
#define USER_SD_SPI_MISO_PIN GPIO_NUM_25
#define USER_SD_SPI_CLK_PIN GPIO_NUM_6
#define USER_SD_SPI_CS_PIN GPIO_NUM_26
#define USER_SD_SPI_HOST SPI2_HOST

/* -------------------------------------------------------------------------- */
/* 16. EPD Display Task / EPD USB Control API                                  */
/* -------------------------------------------------------------------------- */

// Keep EPD task settings here so display latency and stack pressure can be tuned in one place.
#define USER_EPD_DISPLAY_QUEUE_LENGTH 2
#define USER_EPD_DISPLAY_TASK_STACK_SIZE (8 * 1024)
#define USER_EPD_DISPLAY_TASK_PRIORITY 5
// Bound synchronous display waits; completion lifetime remains owned by both waiter and EPD task.
#define USER_EPD_DISPLAY_WAIT_TIMEOUT_MS (5 * 60 * 1000)

#define USER_EPD_TYPE_NVS_KEY "epd_type"
#define USER_EPD_TYPE_DEFAULT 4
#define USB_CONSOLE_EPD_TYPE_DEBUG_LOG_ENABLE 1
#define USB_CONSOLE_EPD_TYPE_LIST_URI "/epd_type_list"
#define USB_CONSOLE_EPD_TYPE_URI "/epd_type"
#define USB_CONSOLE_EPD_TEST_URI "/epd_test"

/* -------------------------------------------------------------------------- */
/* 17. Display Log Compatibility Macros                                        */
/* -------------------------------------------------------------------------- */

// Map the copied display driver's colored logs to ESP-IDF logs for this project.
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

/* -------------------------------------------------------------------------- */
/* 18. GPIO Test Output                                                        */
/* -------------------------------------------------------------------------- */

// Enable local ESP32-C5 GPIO test output. GPIO11 toggles at 50% duty; GPIO12 is fixed output.
#if USER_DEBUG_UART0_ENABLED
#define USER_GPIO_TEST_OUTPUT_ENABLE 0
#else
#define USER_GPIO_TEST_OUTPUT_ENABLE 1
#endif
#define USER_GPIO_TEST_PIN_11 GPIO_NUM_11
#define USER_GPIO_TEST_PIN_12 GPIO_NUM_12
#define USER_GPIO_TEST_PIN_12_LEVEL 0
#define USER_GPIO_TEST_PERIOD_MS 500
#define USER_GPIO_TEST_TASK_STACK_SIZE (2 * 1024)
#define USER_GPIO_TEST_TASK_PRIORITY 1

/* -------------------------------------------------------------------------- */
/* 19. LED Status / CH583 LED Backend                                          */
/* -------------------------------------------------------------------------- */

// Keep LED status enable here so bring-up can disable indicators without changing business code.
#define USER_LED_STATUS_ENABLE 1

// Route status LEDs through CH583 GPIO because the ESP32-C5 board has no local LEDs.
// C5 板载没有本机 LED，状态灯通过 CH583 GPIO 控制。
#define USER_LED_BACKEND_CH583 1

// Keep CH583 LED pins and active levels here so status behavior can change without touching LED logic.
// 将 CH583 LED 引脚和有效电平集中在这里，后续调整状态灯不用修改 LED 逻辑。
#define USER_LED_CH583_GREEN_PORT "PB"
#define USER_LED_CH583_GREEN_PIN 6
#define USER_LED_CH583_RED_PORT "PB"
#define USER_LED_CH583_RED_PIN 5
#define USER_LED_CH583_ON_LEVEL "LOW"
#define USER_LED_CH583_OFF_LEVEL "HIGH"

// CH583 owns the blink clock. Each level adds 600 ms to the LED toggle interval.
#define USER_LED_BLINK_LEVEL_1_MS 600
#define USER_LED_BLINK_LEVEL_2_MS 1200
#define USER_LED_BLINK_LEVEL_3_MS 1800
#define USER_LED_BLINK_LEVEL_4_MS 2400
#define USER_LED_BLINK_LEVEL_5_MS 3000
#define USER_LED_BLINK_LEVEL_6_MS 3600
#define USER_LED_BLINK_LEVEL_7_MS 4200
#define USER_LED_BLINK_LEVEL_8_MS 4800
#define USER_LED_BLINK_LEVEL_9_MS 5400
#define USER_LED_BLINK_LEVEL_10_MS 6000
#define USER_LED_FAST_BLINK_MS USER_LED_BLINK_LEVEL_1_MS
#define USER_LED_MID_BLINK_MS USER_LED_BLINK_LEVEL_2_MS
#define USER_LED_SLOW_BLINK_MS USER_LED_BLINK_LEVEL_3_MS
#define USER_LED_READY_BLINK_MS USER_LED_BLINK_LEVEL_4_MS
#define USER_LED_WORK_BLINK_MS USER_LED_BLINK_LEVEL_2_MS
#define USER_LED_ACTIVITY_BLINK_DELAY_MS 300
#define USER_LED_ACTIVITY_QUEUE_LENGTH 16
#define USER_LED_UART_LARGE_DATA_THRESHOLD 256
#define USER_LED_SUCCESS_HOLD_MS 1000
#define USER_LED_STATUS_TASK_STACK_SIZE (4 * 1024)
#define USER_LED_STATUS_TASK_PRIORITY 3

/* -------------------------------------------------------------------------- */
/* 20. Global Runtime Variables / Shared APIs                                  */
/* -------------------------------------------------------------------------- */

extern uint16_t sleep_time;
extern uint32_t working_time;
extern uint32_t server_required_continue_work_time;
extern uint32_t wifi_standby_time_s;
extern int g_app_reset_reason;
extern uint8_t g_slideshow_random_enable;

void print_base_info(void);

esp_err_t app_nvs_read_u8(const char *key, uint8_t *value, uint8_t default_value);
esp_err_t app_nvs_write_u8(const char *key, uint8_t value);
esp_err_t app_nvs_read_str(const char *key, char *value, size_t value_size, const char *default_value);
esp_err_t app_nvs_write_str(const char *key, const char *value);
esp_err_t app_nvs_read_blob(const char *key, void *value, size_t value_size);
esp_err_t app_nvs_write_blob(const char *key, const void *value, size_t value_size);

#ifdef __cplusplus
}
#endif
