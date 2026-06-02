#pragma once

#include <stdint.h>
#include <stddef.h>

#include "esp_bit_defs.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/uart.h"

// Keep old reset markers here so startup display policy can be changed without touching main.c.
#define ESP_RST_low_power_No_Disp 0xFE
#define ESP_RST_need_Disp_EPD 0xFD

// Keep BLE optional so board bring-up can disable Bluetooth without editing BLE source files.
#ifndef USER_BLE_ENABLE
#define USER_BLE_ENABLE  0
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

// Keep Server Network STA return codes here so main.c and the STA module share one result contract.
#define SERVER_NETWORK_STA_OK 1
#define SERVER_NETWORK_STA_CONNECT_FAIL 3
#define SERVER_NETWORK_STA_NO_SAVED_WIFI 0xA1

// Keep STA wait bits here so future connection policy changes do not require editing the STA implementation.
#define SERVER_NETWORK_STA_CONNECTED_BIT BIT0
#define SERVER_NETWORK_STA_FAIL_BIT BIT1

// Keep the STA connection timeout configurable from one header for board bring-up tuning.
#define SERVER_NETWORK_STA_CONNECT_TIMEOUT_MS 20000

// Keep the migrated /dataUP upload body limit here so browser upload behavior can be tuned in one place.
#define SERVER_NETWORK_STA_DATAUP_MAX_BODY_SIZE (2 * 1024 * 1024)

// Keep large buffer fallback limit here so HTTP and EPD avoid exhausting internal RAM.
#define USER_INTERNAL_RAM_FALLBACK_MAX_SIZE (128 * 1024)

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

// Keep OTA upload limits here so the partition size and HTTP body policy can be checked together.
#define SERVER_NETWORK_STA_OTA_UPLOAD_MAX_BODY_SIZE (6 * 1024 * 1024)
#define SERVER_NETWORK_STA_OTA_BOUNDARY_MAX 96
#define SERVER_NETWORK_STA_OTA_VERSION_MAX 40

// Keep the last-cast record name here so reboot recovery and cast saving use the same file.
#define SERVER_NETWORK_STA_LAST_CAST_FILE "last_cast.txt"

// Keep saved-image listing limits here so JSON response size can be tuned without touching scan logic.
#define SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX 8192
#define SERVER_NETWORK_STA_THUMB_URI_PREFIX "/thumb/"

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
#define TDX_SLIDESHOW_INTERVAL_MIN_SECONDS 1
#define TDX_SLIDESHOW_INTERVAL_MAX_SECONDS 3600
#define TDX_SLIDESHOW_CONFIG_FILE "slideshow_config.txt"
#define TDX_SLIDESHOW_CONTROL_FILE "show_control.txt"
#define TDX_SLIDESHOW_AFTER_DISPLAY_WAIT_MS 12000
#define TDX_SLIDESHOW_DEEP_SLEEP_FLAG_VALUE 0xA5
#define TDX_SLIDESHOW_NVS_FLAG_KEY "slide_ds"
#define TDX_SLIDESHOW_NVS_LAST_FILE_KEY "slide_last"
#define TDX_SLIDESHOW_RANDOM_NVS_KEY "slide_random"
#define SERVER_REQUIRED_CONTINUE_WORK_TIME_NVS_KEY "work_continue"
#define WIFI_STANDBY_TIME_S_NVS_KEY "wifi_standby"
#define CH583_BLE_MAC_NVS_KEY "ch583_ble_mac"

// Keep delete request limits here so file removal cannot grow unbounded from one JSON request.
#define SERVER_NETWORK_STA_DELETE_MAX_FILES 50

// Keep WiFi keep-alive limits here so phone commands cannot request an unbounded online window.
#define SERVER_NETWORK_STA_WIFI_WORK_TIME_MIN_SECONDS 1
#define SERVER_NETWORK_STA_WIFI_WORK_TIME_MAX_SECONDS 3600

// Keep sleep/work-state NVS keys here so BLE, HTTP, and network timers share one saved runtime state.
#define USER_WORK_STATE_NVS_NAMESPACE "work_state"
#define USER_WORK_STATE_NVS_KEY "runtime"
#define USER_WORK_STATE_DEFAULT_CONTINUE_SECONDS (5 * 60)
#define USER_WORK_STATE_DEFAULT_STANDBY_SECONDS 15
#define USER_WORK_STATE_MIN_CONTINUE_SECONDS (5 * 60)
#define USER_WORK_STATE_MAX_CONTINUE_SECONDS (60 * 60)
#define USER_WORK_STATE_TASK_STACK_SIZE (3 * 1024)
#define USER_WORK_STATE_TASK_PRIORITY 3

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

// Keep the ping URI here so heartbeat routing can change without touching GET resource handlers.
#define SERVER_NETWORK_STA_PING_URI "/ping"

// Keep CH583 UART receive enabled from one switch so board bring-up can disable it without touching task code.
#define USER_CH583_UART_ENABLE 1

// Keep CH583 UART pins and baud rate here so protocol TX and RX always use the same physical port.
#define USER_CH583_UART_PORT UART_NUM_0
#define USER_CH583_UART_TX_PIN GPIO_NUM_43
#define USER_CH583_UART_RX_PIN GPIO_NUM_44
#define USER_CH583_UART_BAUD_RATE 115200
#define CH583_WIFI_UART_PORT USER_CH583_UART_PORT

// Keep CH583 UART buffer and task sizes here so receive pressure can be tuned without changing task logic.
#define USER_CH583_UART_RECEIVE_BUF_SIZE 256
#define USER_CH583_UART_DRIVER_RX_BUF_SIZE 8192
#define USER_CH583_UART_DRIVER_TX_BUF_SIZE 0
#define USER_CH583_UART_EVENT_QUEUE_SIZE 20
#define USER_CH583_UART_EVENT_TASK_STACK_SIZE (3 * 1024)
#define USER_CH583_UART_RECEIVE_TASK_STACK_SIZE (8 * 1024)
#define USER_CH583_UART_WAKEUP_THRESHOLD 3

// Keep CH583 protocol debug flags here so frame parsing logs can be enabled without editing the copied protocol file.
#define CH583_WIFI_UART_DEBUG_PRINT_ENABLE 0
#define CH583_WIFI_UART_DIRECTION_PRINT_ENABLE 0
#define CH583_WIFI_UART_TX_SILENCE_MS 10
#define CH583_WIFI_UART_BAD_CRC_RETRY_MAX 5

// Keep EPD display enable here so network cast/upload can be tested without editing receive code.
#define USER_EPD_ENABLE 1

// Keep EPD panel geometry here because the copied display driver and network bin size must match.
#define USER_EPD_WIDTH 1600
#define USER_EPD_HEIGHT 1200
#define USER_EPD_SCALE_MAX_WIDTH 1350
#define USER_EPD_SCALE_MAX_HEIGHT 1350
#define USER_EPD_TYPE 2

// Keep EPD SPI pins here so board pin changes do not require editing display_bsp.cpp.
#define USER_EPD_MOSI_PIN 11
#define USER_EPD_SCK_PIN 10
#define USER_EPD_DC_PIN 8
#define USER_EPD_CS_PIN 9
#define USER_EPD_CS2_PIN 46
#define USER_EPD_RST_PIN 12
#define USER_EPD_BUSY_PIN 13
#define USER_EPD_SPI_HOST SPI3_HOST

// Keep EPD task settings here so display latency and stack pressure can be tuned in one place.
#define USER_EPD_DISPLAY_QUEUE_LENGTH 1
#define USER_EPD_DISPLAY_TASK_STACK_SIZE (8 * 1024)
#define USER_EPD_DISPLAY_TASK_PRIORITY 5

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


// Keep LED status enable here so bring-up can disable indicators without changing business code.
#define USER_LED_STATUS_ENABLE 1

// Keep LED pins and active level here because GPIO42/GPIO45 are low-level-on board LEDs.
#define USER_LED_GREEN_PIN GPIO_NUM_42
#define USER_LED_RED_PIN GPIO_NUM_45
#define USER_LED_ON_LEVEL 0
#define USER_LED_OFF_LEVEL 1

// Keep LED blink timing here so status behavior can be tuned without editing the LED task.
#define USER_LED_FAST_BLINK_MS 100
#define USER_LED_MID_BLINK_MS 500
#define USER_LED_SLOW_BLINK_MS 1000
#define USER_LED_SUCCESS_HOLD_MS 1000
#define USER_LED_STATUS_TASK_STACK_SIZE (4 * 1024)
#define USER_LED_STATUS_TASK_PRIORITY 3
