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
#define USER_BOARD_ESP32C5 1

// Keep old reset markers here so startup display policy can be changed without touching main.c.
#define ESP_RST_low_power_No_Disp 0xFE
// Configuration value for ESP RST need Disp EPD; update local references before changing it.
#define ESP_RST_need_Disp_EPD 0xFD

// Keep large buffer fallback limit here so HTTP and EPD avoid exhausting internal RAM.
#define USER_INTERNAL_RAM_FALLBACK_MAX_SIZE (128 * 1024)

// Auto Light-sleep is disabled for reliable CH583 UART, USB, and HTTP receive paths.
// CPU frequency is controlled by sdkconfig/app_auto_light_sleep_init; keep this at 0.
#ifndef TDX_AUTO_LIGHT_SLEEP_ENABLE
// Feature switch for TDX AUTO LIGHT SLEEP ENABLE; set to 1 to enable and 0 to disable.
#define TDX_AUTO_LIGHT_SLEEP_ENABLE 0
#endif


/* -------------------------------------------------------------------------- */
/* 02. Common JSON Result Codes                                                */
/* -------------------------------------------------------------------------- */

#define TDX_STRINGIFY_INNER(value) #value
// Configuration value for TDX STRINGIFY; update local references before changing it.
#define TDX_STRINGIFY(value) TDX_STRINGIFY_INNER(value)

// Keep JSON API result codes centralized so every response follows README_Result_Code.md.
#define TDX_JSON_RESULT_OK 0
// Public JSON result code for TDX JSON RESULT JSON INVALID; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_JSON_INVALID 1001
// Public JSON result code for TDX JSON RESULT FUNC UNSUPPORTED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_FUNC_UNSUPPORTED 1002
// Public JSON result code for TDX JSON RESULT FIELD MISSING; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_FIELD_MISSING 1003
// Public JSON result code for TDX JSON RESULT PARAM INVALID; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_PARAM_INVALID 1004
// Public JSON result code for TDX JSON RESULT METHOD UNSUPPORTED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_METHOD_UNSUPPORTED 1005
// Public JSON result code for TDX JSON RESULT BODY TOO LARGE; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_BODY_TOO_LARGE 1006
// Public JSON result code for TDX JSON RESULT BUSY; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_BUSY 1007
// Public JSON result code for TDX JSON RESULT TIMEOUT; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_TIMEOUT 1008
// Public JSON result code for TDX JSON RESULT INTERNAL ERROR; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_INTERNAL_ERROR 1009
// Public JSON result code for TDX JSON RESULT JSON TOO LONG; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_JSON_TOO_LONG 1010
// Public JSON result code for TDX JSON RESULT NO MEMORY; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_NO_MEMORY 1011
// Public JSON result code for TDX JSON RESULT STORAGE NOT READY; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_STORAGE_NOT_READY 1012
// Public JSON result code for TDX JSON RESULT STORAGE NO SPACE; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_STORAGE_NO_SPACE 1013
// Public JSON result code for TDX JSON RESULT NOT FOUND; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_NOT_FOUND 1014
// Public JSON result code for TDX JSON RESULT PATH UNSAFE; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_PATH_UNSAFE 1015
// Public JSON result code for TDX JSON RESULT QUEUE FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_QUEUE_FAILED 1016

/* -------------------------------------------------------------------------- */
/* 03. USB / BLE / WiFi JSON Result Codes                                      */
/* -------------------------------------------------------------------------- */

// Keep USB JSON result codes separate so serial transport errors are easy to diagnose.
#define TDX_JSON_RESULT_USB_REQUEST_TOO_LARGE 1101
// Public JSON result code for TDX JSON RESULT USB REQUEST TIMEOUT; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_USB_REQUEST_TIMEOUT 1102
// Public JSON result code for TDX JSON RESULT USB BAD REQUEST; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_USB_BAD_REQUEST 1103
// Public JSON result code for TDX JSON RESULT USB ROUTE NOT FOUND; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_USB_ROUTE_NOT_FOUND 1104
// Public JSON result code for TDX JSON RESULT USB HANDLER FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_USB_HANDLER_FAILED 1105
// Public JSON result code for TDX JSON RESULT USB ASYNC FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_USB_ASYNC_FAILED 1106

// Keep BLE and CH583 JSON result codes here so BLE_DATA replies share one contract.
#define TDX_JSON_RESULT_BLE_JSON_EMPTY 1201
// Public JSON result code for TDX JSON RESULT BLE FUNC UNSUPPORTED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_BLE_FUNC_UNSUPPORTED 1202
// Public JSON result code for TDX JSON RESULT BLE JSON PARSE FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_BLE_JSON_PARSE_FAILED 1203
// Public JSON result code for TDX JSON RESULT BLE SEND FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_BLE_SEND_FAILED 1204
// Public JSON result code for TDX JSON RESULT BLE NO SAVED WIFI; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_BLE_NO_SAVED_WIFI 1205

// Keep WiFi JSON result codes here so USB, BLE, and network paths use the same meanings.
#define TDX_JSON_RESULT_WIFI_SSID_MISSING 1301
// Public JSON result code for TDX JSON RESULT WIFI KEY MISSING; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_WIFI_KEY_MISSING 1302
// Public JSON result code for TDX JSON RESULT WIFI SSID INVALID; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_WIFI_SSID_INVALID 1303
// Public JSON result code for TDX JSON RESULT WIFI KEY INVALID; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_WIFI_KEY_INVALID 1304
// Public JSON result code for TDX JSON RESULT WIFI SAVE FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_WIFI_SAVE_FAILED 1305
// Public JSON result code for TDX JSON RESULT WIFI CONNECT SUBMIT FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_WIFI_CONNECT_SUBMIT_FAILED 1306
// Public JSON result code for TDX JSON RESULT WIFI CONNECT TIMEOUT; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_WIFI_CONNECT_TIMEOUT 1307
// Public JSON result code for TDX JSON RESULT WIFI AUTH FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_WIFI_AUTH_FAILED 1308
// Public JSON result code for TDX JSON RESULT WIFI GOT IP FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_WIFI_GOT_IP_FAILED 1309
// Public JSON result code for TDX JSON RESULT WIFI WORK TIME MISSING; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_WIFI_WORK_TIME_MISSING 1351
// Public JSON result code for TDX JSON RESULT WIFI WORK TIME RANGE; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_WIFI_WORK_TIME_RANGE 1352
// Public JSON result code for TDX JSON RESULT WIFI WORK TIME SAVE FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_WIFI_WORK_TIME_SAVE_FAILED 1353
// Public JSON result code for TDX JSON RESULT WIFI WORK TIME APPLY FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_WIFI_WORK_TIME_APPLY_FAILED 1354

/* -------------------------------------------------------------------------- */
/* 04. Image / Delete / Slideshow / Upload / OTA / EPD Result Codes            */
/* -------------------------------------------------------------------------- */

// Keep image, slideshow, upload, OTA, and EPD result codes here for feature-local response updates.
#define TDX_JSON_RESULT_IMAGES_READ_FAILED 1401
// Public JSON result code for TDX JSON RESULT THUMB NAME INVALID; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_THUMB_NAME_INVALID 1402
// Public JSON result code for TDX JSON RESULT THUMB NOT FOUND; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_THUMB_NOT_FOUND 1403
// Public JSON result code for TDX JSON RESULT SNAPSHOT BUILD FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_SNAPSHOT_BUILD_FAILED 1404
// Public JSON result code for TDX JSON RESULT BLE MAC EMPTY; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_BLE_MAC_EMPTY 1405

// Public JSON result code for TDX JSON RESULT FILE NAMES MISSING; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_FILE_NAMES_MISSING 1501
// Public JSON result code for TDX JSON RESULT FILE NAME INVALID; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_FILE_NAME_INVALID 1502
// Public JSON result code for TDX JSON RESULT DELETE FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_DELETE_FAILED 1503
// Public JSON result code for TDX JSON RESULT SLIDESHOW CONFIG SAVE FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_SLIDESHOW_CONFIG_SAVE_FAILED 1504
// Public JSON result code for TDX JSON RESULT SLIDESHOW START FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_SLIDESHOW_START_FAILED 1505
// Public JSON result code for TDX JSON RESULT SLIDESHOW RUNTIME FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_SLIDESHOW_RUNTIME_FAILED 1506
// Public JSON result code for TDX JSON RESULT SLIDESHOW INTERVAL INVALID; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_SLIDESHOW_INTERVAL_INVALID 1507
// Public JSON result code for TDX JSON RESULT SLIDESHOW FILE NOT FOUND; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_SLIDESHOW_FILE_NOT_FOUND 1508
// Public JSON result code for TDX JSON RESULT SLIDESHOW CONTROL SAVE FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_SLIDESHOW_CONTROL_SAVE_FAILED 1509
// Public JSON result code for TDX JSON RESULT SLIDESHOW TIMESTAMP INVALID; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_SLIDESHOW_TIMESTAMP_INVALID 1510
// Public JSON result code for TDX JSON RESULT SLIDESHOW TIMEZONE DEPRECATED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_SLIDESHOW_TIMEZONE_DEPRECATED 1511
// Public JSON result code for TDX JSON RESULT SLIDESHOW TIME SET FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_SLIDESHOW_TIME_SET_FAILED 1512
// Public JSON result code for TDX JSON RESULT SLIDESHOW TIME DIFF TOO LARGE; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_SLIDESHOW_TIME_DIFF_TOO_LARGE 1513
// Public JSON result code for TDX JSON RESULT FILE NAMES TOO MANY; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_FILE_NAMES_TOO_MANY 1514
// Public JSON result code for missing slideshow start index; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_SLIDESHOW_START_INDEX_MISSING 1515
// Public JSON result code for invalid slideshow start index; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_SLIDESHOW_START_INDEX_INVALID 1516
// Reserved legacy result code for duplicate slideshow file names; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_SLIDESHOW_FILE_DUPLICATE 1517

// Public JSON result code for TDX JSON RESULT UPLOAD BOUNDARY MISSING; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_UPLOAD_BOUNDARY_MISSING 1601
// Public JSON result code for TDX JSON RESULT UPLOAD FUNC MISSING; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_UPLOAD_FUNC_MISSING 1602
// Public JSON result code for TDX JSON RESULT UPLOAD INVALID; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_UPLOAD_INVALID 1603
// Public JSON result code for TDX JSON RESULT UPLOAD BIN MISSING; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_UPLOAD_BIN_MISSING 1604
// Public JSON result code for TDX JSON RESULT UPLOAD IMAGE MISSING; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_UPLOAD_IMAGE_MISSING 1605
// Public JSON result code for TDX JSON RESULT UPLOAD SIZE MISMATCH; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_UPLOAD_SIZE_MISMATCH 1606
// Public JSON result code for TDX JSON RESULT SAVE BIN FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_SAVE_BIN_FAILED 1607
// Public JSON result code for TDX JSON RESULT SAVE IMAGE FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_SAVE_IMAGE_FAILED 1608
// Public JSON result code for TDX JSON RESULT DISPLAY QUEUE FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_DISPLAY_QUEUE_FAILED 1609
// Public JSON result code for TDX JSON RESULT LAST CAST SAVE FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_LAST_CAST_SAVE_FAILED 1610
// Public JSON result code for TDX JSON RESULT SAVE REQUIRED FOR LAST CAST; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_SAVE_REQUIRED_FOR_LAST_CAST 1611
// Public JSON result code for TDX JSON RESULT UPLOAD FILE NAME INVALID; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_UPLOAD_FILE_NAME_INVALID 1612
// Public JSON result code for TDX JSON RESULT UPLOAD RAW PATH MISSING; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_UPLOAD_RAW_PATH_MISSING 1613
// Public JSON result code for TDX JSON RESULT UPLOAD RAW PATH INVALID; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_UPLOAD_RAW_PATH_INVALID 1614
// Public JSON result code for TDX JSON RESULT UPLOAD RAW SAVE FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_UPLOAD_RAW_SAVE_FAILED 1615
// Public JSON result code for TDX JSON RESULT CAST2PIC SCREEN INVALID; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_CAST2PIC_SCREEN_INVALID 1616
// Public JSON result code for TDX JSON RESULT CAST2PIC SCREEN UNSUPPORTED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_CAST2PIC_SCREEN_UNSUPPORTED 1617

// Public JSON result code for TDX JSON RESULT OTA BOUNDARY MISSING; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_OTA_BOUNDARY_MISSING 1701
// Public JSON result code for TDX JSON RESULT OTA META MISSING; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_OTA_META_MISSING 1702
// Public JSON result code for TDX JSON RESULT OTA META INVALID; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_OTA_META_INVALID 1703
// Public JSON result code for TDX JSON RESULT OTA FIRMWARE MISSING; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_OTA_FIRMWARE_MISSING 1704
// Public JSON result code for TDX JSON RESULT OTA FIRMWARE SIZE INVALID; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_OTA_FIRMWARE_SIZE_INVALID 1705
// Public JSON result code for TDX JSON RESULT OTA BEGIN FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_OTA_BEGIN_FAILED 1706
// Public JSON result code for TDX JSON RESULT OTA WRITE FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_OTA_WRITE_FAILED 1707
// Public JSON result code for TDX JSON RESULT OTA END FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_OTA_END_FAILED 1708
// Public JSON result code for TDX JSON RESULT OTA VERIFY FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_OTA_VERIFY_FAILED 1709
// Public JSON result code for TDX JSON RESULT OTA SET BOOT FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_OTA_SET_BOOT_FAILED 1710
// Public JSON result code for TDX JSON RESULT OTA VERSION MISMATCH; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_OTA_VERSION_MISMATCH 1711
// Public JSON result code for TDX JSON RESULT OTA PARTITION TOO SMALL; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_OTA_PARTITION_TOO_SMALL 1712
// Public JSON result code for TDX JSON RESULT OTA BUSY; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_OTA_BUSY 1713

// Daily image dimensions do not match the active EPD profile.
#define TDX_JSON_RESULT_DAILY_IMAGE_SIZE_MISMATCH 1901
// Daily image orientation is not an exact signed 16-bit integer.
#define TDX_JSON_RESULT_DAILY_ORIENTATION_INVALID 1902
// Daily image API URL is missing, too long, or not HTTPS.
#define TDX_JSON_RESULT_DAILY_API_URL_INVALID 1903
// Daily image timestamp is missing, is not an exact integer, or is outside the supported epoch range.
#define TDX_JSON_RESULT_DAILY_TIME_INVALID 1904
// Daily image configuration could not be saved to and verified from NVS.
#define TDX_JSON_RESULT_DAILY_CONFIG_SAVE_FAILED 1905
// Slideshow could not be stopped and confirmed before DAILY mode was entered.
#define TDX_JSON_RESULT_DAILY_SLIDESHOW_STOP_FAILED 1906
// DAILY display mode could not be persisted.
#define TDX_JSON_RESULT_DAILY_MODE_SAVE_FAILED 1907
// The dedicated daily image worker could not accept the request.
#define TDX_JSON_RESULT_DAILY_JOB_SUBMIT_FAILED 1908
// Current-boot SNTP time is unavailable, so a new daily timestamp cannot be validated.
#define TDX_JSON_RESULT_DAILY_NETWORK_TIME_UNAVAILABLE 1909

// Public JSON result code for TDX JSON RESULT EPD TYPE INVALID; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_EPD_TYPE_INVALID 1801
// Public JSON result code for TDX JSON RESULT EPD TYPE SAVE FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_EPD_TYPE_SAVE_FAILED 1802
// Public JSON result code for TDX JSON RESULT EPD TEST DISPLAY FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_EPD_TEST_DISPLAY_FAILED 1803
// Public JSON result code for TDX JSON RESULT EPD DISPLAY FAILED; keep the numeric value stable for clients.
#define TDX_JSON_RESULT_EPD_DISPLAY_FAILED 1804

/* -------------------------------------------------------------------------- */
/* 05. Server Network STA / WiFi / mDNS                                        */
/* -------------------------------------------------------------------------- */

// Keep STA connection debug logs here so WiFi failure checks can be enabled without changing STA logic.
#define SERVER_NETWORK_STA_DEBUG_LOG_ENABLE 1

// Development-only: print the WiFi password in plaintext for bring-up debugging.
// Keep this enabled only during local WiFi debugging and set it to 0 before release testing.

// Keep Server Network STA return codes here so main.c and the STA module share one result contract.
#define SERVER_NETWORK_STA_OK 1
// Configuration value for SERVER NETWORK STA CONNECT FAIL; update local references before changing it.
#define SERVER_NETWORK_STA_CONNECT_FAIL 3
// A newer control request replaced this synchronous wait; it is not a WiFi timeout.
#define SERVER_NETWORK_STA_CONNECT_SUPERSEDED 4
// Configuration value for SERVER NETWORK STA NO SAVED WIFI; update local references before changing it.
#define SERVER_NETWORK_STA_NO_SAVED_WIFI 0xA1

// Keep STA wait bits here so future connection policy changes do not require editing the STA implementation.
#define SERVER_NETWORK_STA_CONNECTED_BIT BIT0
// Configuration value for SERVER NETWORK STA FAIL BIT; update local references before changing it.
#define SERVER_NETWORK_STA_FAIL_BIT BIT1
// Configuration value for SERVER NETWORK STA DISCONNECTED BIT; update local references before changing it.
#define SERVER_NETWORK_STA_DISCONNECTED_BIT BIT2

// Queue length for the WiFi manager task; size it for event bursts without wasting RAM.
#define SERVER_NETWORK_STA_MANAGER_QUEUE_LENGTH 24
// Task stack size for the WiFi manager task; tune with runtime stack high-water data.
#define SERVER_NETWORK_STA_MANAGER_STACK_SIZE 6144
// FreeRTOS task priority for the WiFi manager task; keep scheduler side effects in mind when changing it.
#define SERVER_NETWORK_STA_MANAGER_PRIORITY 5

// Maximum time to wait after STA association for either a usable IP or the next recovery step.
#define SERVER_NETWORK_STA_AP_OR_IP_TIMEOUT_MS 10000

// Fixed delay before retrying HTTP server startup after the network comes up.
#define SERVER_NETWORK_STA_HTTP_RETRY_MS 3000

// Number of synchronous request slots shared by legacy callers waiting for a connect result.
#define SERVER_NETWORK_STA_REQUEST_SLOT_COUNT 2

// Maximum time for one connect-flow attempt, including association, DHCP, and recovery handoff.
#define SERVER_NETWORK_STA_CONNECT_FLOW_TIMEOUT_MS 45000

// Maximum time a legacy synchronous caller waits for the manager to report a connect result.
#define SERVER_NETWORK_STA_SYNC_REQUEST_TIMEOUT_MS 45000
// Maximum absolute lifetime of a BLE/CH583 WiFi connection power guard.
#define WIFI_CONNECT_POWER_GUARD_MAX_MS 45000
// Development only: print the saved WiFi password in plaintext for connection debugging.
#define SERVER_NETWORK_STA_LOG_PASSWORD_PLAINTEXT 1
// Wait this long after an early wifi_wakeup 1307 while the manager is still progressing.
#define WIFI_WAKEUP_EARLY_1307_GRACE_MS 10000
// Polling interval for the isolated wifi_wakeup result observer.
#define WIFI_WAKEUP_RESULT_POLL_DELAY_MS 200
// Internal wifi_wakeup observer outcomes; keep them separate from protocol result codes.
#define WIFI_WAKEUP_WAIT_READY               0
#define WIFI_WAKEUP_WAIT_NOTIFY_TIMEOUT      1
#define WIFI_WAKEUP_WAIT_TERMINAL            2
#define WIFI_WAKEUP_WAIT_CONFIG_QUEUED       3
#define WIFI_WAKEUP_WAIT_STATUS_ERROR        4
#define WIFI_WAKEUP_WAIT_POWER_GUARD_TIMEOUT 5
// Pending credential publication states used only by the BLE/CH583 WiFi worker.
#define WIFI_PENDING_CONFIG_NONE   0
#define WIFI_PENDING_CONFIG_SAVING 1
#define WIFI_PENDING_CONFIG_READY  2
// Results returned when the worker atomically consumes pending credential work.
#define WIFI_PENDING_TAKE_FINISH 0
#define WIFI_PENDING_TAKE_WAIT   1
#define WIFI_PENDING_TAKE_READY  2
#if WIFI_CONNECT_POWER_GUARD_MAX_MS > 45000
#error "WiFi connect power guard must not exceed 45 seconds"
#endif
#if WIFI_CONNECT_POWER_GUARD_MAX_MS < SERVER_NETWORK_STA_SYNC_REQUEST_TIMEOUT_MS
#error "WiFi connect power guard must cover the synchronous request timeout"
#endif
#if WIFI_WAKEUP_EARLY_1307_GRACE_MS > WIFI_CONNECT_POWER_GUARD_MAX_MS
#error "wifi_wakeup grace must not exceed the WiFi connect power guard"
#endif

// Maximum time to wait for an expected disconnect before escalating to recovery handling.
#define SERVER_NETWORK_STA_EXPECTED_DISCONNECT_TIMEOUT_MS 3000

// Stable READY window before retry and auth-failure counters are reset.
#define SERVER_NETWORK_STA_READY_STABLE_RESET_MS 30000

// Use no AP channel preference so routers can change channel without delaying STA discovery.
#define SERVER_NETWORK_STA_WIFI_CHANNEL_HINT 0

// Keep the ping URI here so heartbeat routing can change without touching GET resource handlers.
#define SERVER_NETWORK_STA_PING_URI "/ping"

// Keep the time URI here so RTC/SNTP status routing can change without touching GET resource handlers.
#define SERVER_NETWORK_STA_TIME_URI "/time"

// Keep the mDNS host name here so board/product naming does not leak into network code.
#define USER_MDNS_HOSTNAME "esp32-c5-photopainter"

// Keep the mDNS instance name here so logs and discovery identify the C5 build correctly.
#define USER_MDNS_INSTANCE_NAME "ESP32-C5-WebServer"

/* -------------------------------------------------------------------------- */
/* 06. Storage / HTTP Upload / Multipart Parser                                */
/* -------------------------------------------------------------------------- */

// Print the /data file tree during startup. Keep enabled by default for bring-up visibility.
#define USER_STORAGE_LIST_ON_STARTUP_ENABLE 0

// Print one log line per HTTP directory-list entry only when debugging file browser output.
#define USER_HTTP_FILE_LIST_LOG_ENABLE 0

// Print successful app_nvs read/write logs only when debugging NVS value flow.
#define USER_NVS_VERBOSE_LOG_ENABLE 0

// Keep image-state records in one stable default-NVS namespace across application-only OTA updates.
#define APP_STATE_NVS_NAMESPACE "image_state"
// NVS keys are limited to 15 characters excluding the trailing NUL.
#define APP_STATE_NVS_SLIDESHOW_CONFIG_KEY "slide_cfg"
#define APP_STATE_NVS_SLIDESHOW_CONTROL_KEY "slide_ctl"
#define APP_STATE_NVS_LAST_CAST_KEY "last_cast"
// Persisted records use independent magic values so a key/type mismatch is rejected safely.
#define APP_STATE_SLIDESHOW_CONFIG_MAGIC 0x53434647UL
#define APP_STATE_SLIDESHOW_CONTROL_MAGIC 0x5343544CUL
#define APP_STATE_LAST_CAST_MAGIC 0x4C434153UL
// Increment a record version only when its serialized layout or validation contract changes.
#define APP_STATE_SLIDESHOW_CONFIG_VERSION 1U
#define APP_STATE_SLIDESHOW_CONTROL_VERSION 1U
#define APP_STATE_LAST_CAST_VERSION 1U
// Bound variable-size NVS allocation before reading untrusted or damaged stored length metadata.
#define APP_STATE_SLIDESHOW_CONFIG_BLOB_MAX_SIZE 3072U

// Print multipart fallback parser details only when debugging legacy /dataUP uploads.
#define USER_HTTP_MULTIPART_DETAIL_LOG_ENABLE 0

// Enable SD card probing before SPIFFS. Set to 0 when the board has no SD card to boot faster.
#define USER_STORAGE_SD_CARD_ENABLE 1

// Keep the migrated /dataUP upload body limit here so browser upload behavior can be tuned in one place.
#define SERVER_NETWORK_STA_DATAUP_MAX_BODY_SIZE (2 * 1024 * 1024)

// Keep /dataUP parser string limits here because they must match the old web page form field sizes.
#define SERVER_NETWORK_STA_DATAUP_FIELD_NAME_MAX 32
// Configuration value for SERVER NETWORK STA DATAUP FILE NAME MAX; update local references before changing it.
#define SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX 96
// Configuration value for SERVER NETWORK STA DATAUP BASE PATH MAX; update local references before changing it.
#define SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX 32
// Configuration value for SERVER NETWORK STA UPLOAD RESULT JSON MAX; update local references before changing it.
#define SERVER_NETWORK_STA_UPLOAD_RESULT_JSON_MAX 768

// Keep cast save reserve here so SPIFFS writes leave room for temp files and metadata.
#define SERVER_NETWORK_STA_CAST_SAVE_RESERVE_BYTES (128 * 1024)

// Keep the migrated HTTP receive dispatcher limits here so request routing can be tuned without touching parser code.
#define SERVER_NETWORK_STA_HTTP_HEADER_VALUE_MAX 256
// Configuration value for SERVER NETWORK STA SMALL JSON BODY MAX; update local references before changing it.
#define SERVER_NETWORK_STA_SMALL_JSON_BODY_MAX 4096

/* -------------------------------------------------------------------------- */
/* 07. USB Console / USB HTTP Text Transport                                   */
/* -------------------------------------------------------------------------- */

// Keep USB console HTTP-text limits here so the serial entry can be tuned without touching feature modules.
#define USB_CONSOLE_ENABLE 1
// Buffer or size limit for USB CONSOLE RX BUF SIZE; keep RAM and payload limits aligned.
#define USB_CONSOLE_RX_BUF_SIZE 4096
// Buffer or size limit for USB CONSOLE TX BUF SIZE; keep RAM and payload limits aligned.
#define USB_CONSOLE_TX_BUF_SIZE 1024
// Configuration value for USB CONSOLE HTTP HEADER MAX; update local references before changing it.
#define USB_CONSOLE_HTTP_HEADER_MAX 2048
// Configuration value for USB CONSOLE HTTP PATH MAX; update local references before changing it.
#define USB_CONSOLE_HTTP_PATH_MAX 128
// Configuration value for USB CONSOLE HTTP METHOD MAX; update local references before changing it.
#define USB_CONSOLE_HTTP_METHOD_MAX 8
// Configuration value for USB CONSOLE HTTP CONTENT TYPE MAX; update local references before changing it.
#define USB_CONSOLE_HTTP_CONTENT_TYPE_MAX 128
// Configuration value for USB CONSOLE BASE PATH; update local references before changing it.
#define USB_CONSOLE_BASE_PATH "/data"
// Configuration value for USB CONSOLE HTTP BODY MAX; update local references before changing it.
#define USB_CONSOLE_HTTP_BODY_MAX SERVER_NETWORK_STA_DATAUP_MAX_BODY_SIZE
// Configuration value for USB CONSOLE HTTP RESPONSE MAX; update local references before changing it.
#define USB_CONSOLE_HTTP_RESPONSE_MAX 8192
// Task stack size for USB CONSOLE TASK STACK SIZE; tune with runtime stack high-water data.
#define USB_CONSOLE_TASK_STACK_SIZE (12 * 1024)
// FreeRTOS task priority for USB CONSOLE TASK PRIORITY; keep scheduler side effects in mind when changing it.
#define USB_CONSOLE_TASK_PRIORITY 3
// Use a longer idle USB read wait so the console task does not wake CPU too often without traffic.
#define USB_CONSOLE_READ_IDLE_TIMEOUT_MS 1000

// Use a short active USB read wait once a request starts so large multipart bodies are received quickly.
#define USB_CONSOLE_READ_ACTIVE_TIMEOUT_MS 1
// Timing value for USB CONSOLE WRITE TIMEOUT MS; verify related wake, sleep, and retry behavior if it changes.
#define USB_CONSOLE_WRITE_TIMEOUT_MS 100
// Timing value for USB CONSOLE REQUEST TIMEOUT MS; verify related wake, sleep, and retry behavior if it changes.
#define USB_CONSOLE_REQUEST_TIMEOUT_MS 30000
// Timing value for USB CONSOLE START DELAY MS; verify related wake, sleep, and retry behavior if it changes.
#define USB_CONSOLE_START_DELAY_MS 3000
// Configuration value for USB CONSOLE FEATURE PENDING STATUS; update local references before changing it.
#define USB_CONSOLE_FEATURE_PENDING_STATUS 501
// Feature switch for USB CONSOLE VERBOSE LOG ENABLE; set to 1 to enable and 0 to disable.
#define USB_CONSOLE_VERBOSE_LOG_ENABLE 0
// Configuration value for USB CONSOLE FRAME HEAD; update local references before changing it.
#define USB_CONSOLE_FRAME_HEAD "@#$\r\n"
// Configuration value for USB CONSOLE FRAME TAIL; update local references before changing it.
#define USB_CONSOLE_FRAME_TAIL "\r\n%^&\r\n"

// Log USB receive progress every fixed byte step so serial upload bottlenecks can be located.
#define USB_CONSOLE_RX_PROGRESS_STEP_BYTES (20 * 1024)

// Use a conservative stdio buffer to reduce transient file-save memory while keeping SD/FATFS writes batched.
#define USB_CONSOLE_FILE_SAVE_STREAM_BUF_SIZE (16 * 1024)

// Log internal RAM and PSRAM around file-save operations during development.
#define USER_FILE_SAVE_MEMORY_LOG_ENABLE 1
#if USER_FILE_SAVE_MEMORY_LOG_ENABLE != 0 && USER_FILE_SAVE_MEMORY_LOG_ENABLE != 1
#error "USER_FILE_SAVE_MEMORY_LOG_ENABLE must be 0 or 1"
#endif

// Feature switch for USER USB CONSOLE ANSI COLOR TEST ENABLE; set to 1 to enable and 0 to disable.
#define USER_USB_CONSOLE_ANSI_COLOR_TEST_ENABLE 0

/* -------------------------------------------------------------------------- */
/* 08. OTA Upload                                                              */
/* -------------------------------------------------------------------------- */

// Keep OTA upload limits here so the partition size and HTTP body policy can be checked together.
#define SERVER_NETWORK_STA_OTA_UPLOAD_MAX_BODY_SIZE (6 * 1024 * 1024)

// Reserve multipart header room above the firmware partition size when rejecting oversize OTA bodies early.
#define SERVER_NETWORK_STA_OTA_MULTIPART_OVERHEAD_BYTES (64 * 1024)
// Configuration value for SERVER NETWORK STA OTA BOUNDARY MAX; update local references before changing it.
#define SERVER_NETWORK_STA_OTA_BOUNDARY_MAX 96
// Configuration value for SERVER NETWORK STA OTA VERSION MAX; update local references before changing it.
#define SERVER_NETWORK_STA_OTA_VERSION_MAX 40

// Print OTA low-level multipart and firmware-header details only during OTA parser bring-up.
#define SERVER_NETWORK_STA_OTA_DETAIL_LOG_ENABLE 0

// Allow the HTTP handler to return and close the OTA response before restarting the device.
#define SERVER_NETWORK_STA_OTA_RESTART_DELAY_MS 100U
// Warn when first-boot image confirmation approaches the external power-cut window.
#define USER_OTA_LOCAL_CONFIRM_WARNING_MS 6000U
// Document the maximum CH583 delay after the APP accepts the ESP32-C5 OTA result.
#define USER_OTA_CH583_POWER_CUT_LIMIT_MS 10000U

// Keep the OTA restart worker independent from HTTP server and application worker stacks.
#define SERVER_NETWORK_STA_OTA_RESTART_TASK_STACK_SIZE 3072U

// Run the restart worker above background application tasks without competing with system services.
#define SERVER_NETWORK_STA_OTA_RESTART_TASK_PRIORITY 5U

/* -------------------------------------------------------------------------- */
/* 09. Saved Images / Cast / Snapshot / Delete                                 */
/* -------------------------------------------------------------------------- */

// Keep saved-image listing limits here so JSON response size can be tuned without touching scan logic.
#define SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX 8192
// HTTP/USB route string for SERVER NETWORK STA THUMB URI PREFIX; update registered handlers if it changes.
#define SERVER_NETWORK_STA_THUMB_URI_PREFIX "/thumb/"

// Keep delete request limits here so file removal cannot grow unbounded from one JSON request.
#define TDX_DELETE_MAX_FILES 50

/* -------------------------------------------------------------------------- */
/* 10. Slideshow                                                               */
/* -------------------------------------------------------------------------- */

// Keep slideshow run modes here so software and future deep-sleep behavior share one switch.
#define TDX_SLIDESHOW_RUN_MODE_SOFTWARE 0
// Mode value for TDX SLIDESHOW RUN MODE DEEP SLEEP; keep it consistent with stored/runtime mode decoding.
#define TDX_SLIDESHOW_RUN_MODE_DEEP_SLEEP 1

// Default to software slideshow so WiFi and HTTP server remain available during playback.
#ifndef TDX_SLIDESHOW_RUN_MODE
// Mode value for TDX SLIDESHOW RUN MODE; keep it consistent with stored/runtime mode decoding.
#define TDX_SLIDESHOW_RUN_MODE TDX_SLIDESHOW_RUN_MODE_SOFTWARE
#endif

// APP image base names contain at most 16 ASCII bytes, without extension or trailing NUL.
#define TDX_IMAGE_BASE_NAME_MAX_BYTES 16U
// Internal C-string storage includes one byte for the trailing NUL.
#define TDX_IMAGE_BASE_NAME_BUFFER_SIZE (TDX_IMAGE_BASE_NAME_MAX_BYTES + 1U)
// Keep the existing name as a buffer-size alias so slideshow/delete/snapshot stay in sync.
#define TDX_SLIDESHOW_FILE_NAME_MAX_LEN TDX_IMAGE_BASE_NAME_BUFFER_SIZE
// Configuration value for TDX SLIDESHOW MAX FILES; update local references before changing it.
#define TDX_SLIDESHOW_MAX_FILES 150
// Timing value for TDX SLIDESHOW INTERVAL MIN SECONDS; verify related wake, sleep, and retry behavior if it changes.
#define TDX_SLIDESHOW_INTERVAL_MIN_SECONDS 60
// Minimum computed CH583 wake interval required before slideshow power-off is allowed.
#define TDX_SLIDESHOW_POWER_OFF_MIN_WAKE_INTERVAL_SECONDS 10
// Timing value for TDX SLIDESHOW INTERVAL MAX SECONDS; verify related wake, sleep, and retry behavior if it changes.
#define TDX_SLIDESHOW_INTERVAL_MAX_SECONDS (7U * 24U * 60U * 60U) // 7 days
// Timing value for CH583 WAKE TIMER MIN SECONDS; verify related wake, sleep, and retry behavior if it changes.
#define CH583_WAKE_TIMER_MIN_SECONDS 1
// Timing value for CH583 WAKE TIMER MAX SECONDS; verify related wake, sleep, and retry behavior if it changes.
#define CH583_WAKE_TIMER_MAX_SECONDS TDX_SLIDESHOW_INTERVAL_MAX_SECONDS
// Timing value for TDX SLIDESHOW STARTUP DELAY MS; verify related wake, sleep, and retry behavior if it changes.
#define TDX_SLIDESHOW_STARTUP_DELAY_MS 10000
// Seconds to wait for CH583 time after startup delay before using the saved slideshow anchor as RTC fallback.
#define TDX_SLIDESHOW_STARTUP_TIME_FALLBACK_WAIT_SECONDS 3
// Log throttle for startup time-source waiting messages.
#define TDX_SLIDESHOW_STARTUP_TIME_WAIT_LOG_SECONDS 10
// Timing value for TDX SLIDESHOW RTC DISPLAY LEAD SECONDS; verify related wake, sleep, and retry behavior if it changes.
#define TDX_SLIDESHOW_RTC_DISPLAY_LEAD_SECONDS 2
// Timing value for TDX SLIDESHOW WAKE EXTRA ADVANCE SECONDS; verify related wake, sleep, and retry behavior if it changes.
#define TDX_SLIDESHOW_WAKE_EXTRA_ADVANCE_SECONDS 15
// Timing value for TDX SLIDESHOW AFTER DISPLAY WAIT MS; verify related wake, sleep, and retry behavior if it changes.
#define TDX_SLIDESHOW_AFTER_DISPLAY_WAIT_MS 12000
// Request guarded CH583 power-off shortly after slideshow state becomes durable.
#define TDX_SLIDESHOW_POST_DISPLAY_POWER_OFF_DELAY_SECONDS 1U
// Configuration value for TDX SLIDESHOW DEEP SLEEP FLAG VALUE; update local references before changing it.
#define TDX_SLIDESHOW_DEEP_SLEEP_FLAG_VALUE 0xA5
// NVS key used by TDX SLIDESHOW NVS FLAG KEY; keep storage compatibility before changing it.
#define TDX_SLIDESHOW_NVS_FLAG_KEY "slide_ds"
// NVS key used by TDX SLIDESHOW NVS LAST FILE KEY; keep storage compatibility before changing it.
#define TDX_SLIDESHOW_NVS_LAST_FILE_KEY "slide_last"
// NVS key used by TDX SLIDESHOW NVS PROGRESS KEY; keep storage compatibility before changing it.
#define TDX_SLIDESHOW_NVS_PROGRESS_KEY "slide_progress"
/* -------------------------------------------------------------------------- */
/* 10.1 Factory Reset Button                                                  */
/* -------------------------------------------------------------------------- */

// Enable the GPIO based factory-default image cleanup feature.
#define TDX_FACTORY_RESET_ENABLE 1
// GPIO28 is normally high and becomes low while the physical button is pressed.
#define TDX_FACTORY_RESET_GPIO GPIO_NUM_28
// Active level for the button. Keep this separate so future hardware can invert the input without changing logic.
#define TDX_FACTORY_RESET_ACTIVE_LEVEL 0
// Poll period for the button task. A small task is used so power-off and WiFi timers stay independent.
#define TDX_FACTORY_RESET_CHECK_MS 300
// The button must remain continuously active for 5 seconds. Any high-level sample before this timeout cancels the hold.
#define TDX_FACTORY_RESET_HOLD_MS 5000
// Keep disabled by default: clearing images does not require a reboot, and no WiFi/EPD/CH583 settings are erased.

/* -------------------------------------------------------------------------- */
/* 11. WiFi Work Time / Sleep Runtime State / NVS Keys                         */
/* -------------------------------------------------------------------------- */

// Minimum WiFi online time accepted from runtime configuration, in seconds.
#define SERVER_NETWORK_STA_WIFI_WORK_TIME_MIN_SECONDS 60
// Minimum WiFi online time accepted by the network HTTP and USB interfaces, in seconds.
#define SERVER_NETWORK_STA_WIFI_WORK_TIME_NETWORK_USB_MIN_SECONDS 0
// Maximum WiFi online time accepted from runtime configuration, in seconds.
#define SERVER_NETWORK_STA_WIFI_WORK_TIME_MAX_SECONDS 3600
// Default WiFi online time used when no valid saved value exists, in seconds.
#define SERVER_NETWORK_STA_WIFI_WORK_TIME_DEFAULT_SECONDS 300

// NVS key used by SERVER REQUIRED CONTINUE WORK TIME NVS KEY; keep storage compatibility before changing it.
#define SERVER_REQUIRED_CONTINUE_WORK_TIME_NVS_KEY "work_continue"
// NVS key used by WIFI STANDBY TIME S NVS KEY; keep storage compatibility before changing it.
#define WIFI_STANDBY_TIME_S_NVS_KEY "wifi_standby"
// DEVICE_INFO reuses the old key strings so protocol upgrades keep saved CH583 identity data.
#define CH583_DEVICE_INFO_MAC_NVS_KEY "ch583_ble_mac"
#define CH583_DEVICE_INFO_BLE_VER_NVS_KEY "ch583_ble_ver"
#define CH583_DEVICE_INFO_BLE_VER_DEFAULT 0

// Keep sleep/work-state NVS keys here so BLE, HTTP, and network timers share one saved runtime state.
#define USER_WORK_STATE_NVS_NAMESPACE "work_state"
// NVS key used by USER WORK STATE NVS KEY; keep storage compatibility before changing it.
#define USER_WORK_STATE_NVS_KEY "runtime"
// Timing value for USER WORK STATE DEFAULT CONTINUE SECONDS; verify related wake, sleep, and retry behavior if it changes.
#define USER_WORK_STATE_DEFAULT_CONTINUE_SECONDS SERVER_NETWORK_STA_WIFI_WORK_TIME_DEFAULT_SECONDS
// Timing value for USER WORK STATE DEFAULT STANDBY SECONDS; verify related wake, sleep, and retry behavior if it changes.
#define USER_WORK_STATE_DEFAULT_STANDBY_SECONDS 15
// Timing value for USER WORK STATE MIN CONTINUE SECONDS; verify related wake, sleep, and retry behavior if it changes.
#define USER_WORK_STATE_MIN_CONTINUE_SECONDS SERVER_NETWORK_STA_WIFI_WORK_TIME_NETWORK_USB_MIN_SECONDS
// Timing value for USER WORK STATE MAX CONTINUE SECONDS; verify related wake, sleep, and retry behavior if it changes.
#define USER_WORK_STATE_MAX_CONTINUE_SECONDS SERVER_NETWORK_STA_WIFI_WORK_TIME_MAX_SECONDS
// Task stack size for USER WORK STATE TASK STACK SIZE; tune with runtime stack high-water data.
#define USER_WORK_STATE_TASK_STACK_SIZE (8 * 1024)
// FreeRTOS task priority for USER WORK STATE TASK PRIORITY; keep scheduler side effects in mind when changing it.
#define USER_WORK_STATE_TASK_PRIORITY 3
// Timing value for USER WORK STATE TASK INTERVAL MS; verify related wake, sleep, and retry behavior if it changes.
#define USER_WORK_STATE_TASK_INTERVAL_MS 1000
// Hold WiFi power for this many seconds after the most recent HTTP network activity.
#define USER_WORK_STATE_HTTP_ACTIVITY_HOLD_SECONDS 20
// Hold WiFi power after CH583 startup or validated business activity.
#define USER_WORK_STATE_CH583_ACTIVITY_HOLD_SECONDS 20
// OTA hold bits keep receive, flash-write, and first-boot verification lifetimes independent.
#define USER_WORK_STATE_OTA_HOLD_WRITE_BIT   (1UL << 0)
#define USER_WORK_STATE_OTA_HOLD_RECEIVE_BIT (1UL << 1)
#define USER_WORK_STATE_OTA_HOLD_PENDING_VERIFY_BIT (1UL << 2)
// Guard-log bits ensure short power-off holds produce one useful log without per-second noise.
#define USER_WORK_STATE_GUARD_LOG_HTTP_BIT (1UL << 0)
#define USER_WORK_STATE_GUARD_LOG_CH583_BIT (1UL << 1)
#define USER_WORK_STATE_GUARD_LOG_WIFI_CONNECT_BIT (1UL << 2)
// Runtime state bits keep startup and power-off cancellation guards centralized.
#define USER_WORK_STATE_RUNTIME_CH583_STARTUP_PENDING_BIT (1UL << 0)
#define USER_WORK_STATE_RUNTIME_LED_CANCEL_PENDING_BIT    (1UL << 1)
#define USER_WORK_STATE_RUNTIME_WAKE_TIMER_CANCEL_PENDING_BIT (1UL << 2)
// One-shot state is published atomically so its active and business-owner bits stay consistent.
#define USER_WORK_STATE_ONE_SHOT_ACTIVE_BIT      (1U << 0)
#define USER_WORK_STATE_ONE_SHOT_DAILY_OWNER_BIT (1U << 1)
#define USER_WORK_STATE_ONE_SHOT_SLIDESHOW_OWNER_BIT (1U << 2)
#define USER_WORK_STATE_ONE_SHOT_LOCAL_IMAGE_OWNER_BIT (1U << 3)
#define USER_WORK_STATE_ONE_SHOT_OWNER_MASK \
    (USER_WORK_STATE_ONE_SHOT_DAILY_OWNER_BIT | \
     USER_WORK_STATE_ONE_SHOT_SLIDESHOW_OWNER_BIT | \
     USER_WORK_STATE_ONE_SHOT_LOCAL_IMAGE_OWNER_BIT)
// WIFI_PROVISION low nibble used only for the one-shot standby notification
// immediately before POWER_OFF. It must not be stored as an EPD display mode.
#define CH583_WIFI_PROVISION_MODE_STANDBY 0x0FU
// Keep the local EPD/SD GPIO4 rail on after sending CH583 POWER_OFF.
// CH583 POWER_OFF remains enabled and still controls the ESP32/WiFi supply.
#define USER_POWER_OFF_LOCAL_EPD_SD_CUTOFF_ENABLE 0

// After each EPD display job finishes, request one low-power countdown through work_state_task.
#define USER_EPD_DONE_LOW_POWER_ENABLE   0
// Timing value for USER EPD DONE LOW POWER DELAY SECONDS; verify related wake, sleep, and retry behavior if it changes.
#define USER_EPD_DONE_LOW_POWER_DELAY_SECONDS 5
// Timing value for USER EPD DONE LOW POWER SLIDESHOW MIN REMAIN SECONDS; verify related wake, sleep, and retry behavior if it changes.
#define USER_EPD_DONE_LOW_POWER_SLIDESHOW_MIN_REMAIN_SECONDS 40

// Allow the shared EPD/SD rail to stabilize after startup enables it.
// Startup follows a long hardware power-off period and must not pulse the rail low.
#define USER_EPD_SD_STARTUP_POWER_STABLE_MS 100U

// Mandatory independent GPIO4 power cycle for the shared EPD/SD rail when the
// post-display decision keeps CH583 awake. An immediate committed POWER_OFF skips it.
#define USER_EPD_SD_POWER_TEST_ENABLE 1
#if USER_EPD_SD_POWER_TEST_ENABLE != 1
#error "The production EPD/SD stay-awake power cycle must remain enabled"
#endif
// Isolate every external EPD/SD rail signal during the independent GPIO4 power test.
#define USER_EPD_SD_POWER_TEST_IO_ISOLATION_ENABLE 1
// Keep GPIO4 low for at least this interval. Activity cannot restore the rail early.
#define USER_EPD_SD_POWER_TEST_OFF_TIME_MS 2000U
// Recheck while armed so a shared-SPI user that was already active can finish without extra hooks.
#define USER_EPD_SD_POWER_TEST_RECHECK_MS 20U
// Retry a failed GPIO/SD restore without releasing normal SPI users onto an unavailable rail.
#define USER_EPD_SD_POWER_TEST_RESTORE_RETRY_MS 1000U
// Wait for the shared rail to stabilize before remounting external SD or releasing EPD/SPI users.
#define USER_EPD_SD_POWER_TEST_POWER_STABLE_MS 100U
// Bound callers waiting for the rail and SD mount to recover after an interrupted test.
#define USER_EPD_SD_POWER_TEST_READY_TIMEOUT_MS 10000U
// Log the first restore failure and then one out of this many retries to avoid flooding normal logs.
#define USER_EPD_SD_POWER_TEST_RESTORE_LOG_EVERY_COUNT 10U
#define USER_EPD_SD_POWER_TEST_TASK_STACK_SIZE (4U * 1024U)
#define USER_EPD_SD_POWER_TEST_TASK_PRIORITY 6U

// Task-notification bits are centralized here so future event additions remain collision-free.
#define USER_EPD_SD_POWER_TEST_EVENT_NETWORK_ACTIVITY_BIT (1UL << 0)
#define USER_EPD_SD_POWER_TEST_EVENT_CH583_BLE_DATA_BIT   (1UL << 1)
#define USER_EPD_SD_POWER_TEST_EVENT_EPD_REQUEST_BIT      (1UL << 2)
#define USER_EPD_SD_POWER_TEST_EVENT_EPD_DONE_BIT         (1UL << 3)
#define USER_EPD_SD_POWER_TEST_EVENT_SPI_BIT              (1UL << 4)
#define USER_EPD_SD_POWER_TEST_EVENT_IMAGE_TRANSFER_BIT   (1UL << 5)
#define USER_EPD_SD_POWER_TEST_EVENT_SLIDESHOW_FOLLOWUP_BIT (1UL << 6)
#define USER_EPD_SD_POWER_TEST_READY_BIT               (1UL << 0)

// Runtime state values are definitions only; mutable state remains private to the power module.
#define USER_EPD_SD_POWER_TEST_STATE_IDLE       0U
#define USER_EPD_SD_POWER_TEST_STATE_ARMED      1U
#define USER_EPD_SD_POWER_TEST_STATE_PREPARING  2U
#define USER_EPD_SD_POWER_TEST_STATE_POWER_OFF  3U
#define USER_EPD_SD_POWER_TEST_STATE_RESTORING  4U
#define USER_EPD_SD_POWER_TEST_STATE_WAIT_DECISION 5U
#define USER_EPD_SD_POWER_TEST_STATE_POWER_OFF_COMMITTED 6U

/* -------------------------------------------------------------------------- */
/* 12. BLE / GATT Legacy Compatibility                                         */
/* -------------------------------------------------------------------------- */

// Keep BLE optional so board bring-up can disable Bluetooth without editing BLE source files.
#ifndef USER_BLE_ENABLE
// Feature switch for USER BLE ENABLE; set to 1 to enable and 0 to disable.
#define USER_BLE_ENABLE 0
#endif

#if USER_BLE_ENABLE
#include "esp_bt_defs.h"
#endif

// Keep all migrated BLE identifiers here so future app/protocol changes do not touch user_app.cpp.
#define TDX_BLE_LOG_TAG "BLE"
// Configuration value for TDX BLE PROFILE NUM; update local references before changing it.
#define TDX_BLE_PROFILE_NUM 1
// Configuration value for TDX BLE PROFILE APP IDX; update local references before changing it.
#define TDX_BLE_PROFILE_APP_IDX 0
// Configuration value for TDX BLE APP ID; update local references before changing it.
#define TDX_BLE_APP_ID 0x56
// Configuration value for TDX BLE DEVICE NAME; update local references before changing it.
#define TDX_BLE_DEVICE_NAME "Tdx_6_color"
// Configuration value for TDX BLE SERVICE INST ID; update local references before changing it.
#define TDX_BLE_SERVICE_INST_ID 0
// Configuration value for TDX BLE ATT UUID SIZE; update local references before changing it.
#define TDX_BLE_ATT_UUID_SIZE 16
// Configuration value for TDX BLE DATA MAX LEN; update local references before changing it.
#define TDX_BLE_DATA_MAX_LEN 512

#if USER_BLE_ENABLE
// Configuration value for TDX BLE TX POWER LOWEST; update local references before changing it.
#define TDX_BLE_TX_POWER_LOWEST ESP_PWR_LVL_N24
#else
// Configuration value for TDX BLE TX POWER LOWEST; update local references before changing it.
#define TDX_BLE_TX_POWER_LOWEST 0
#endif

// Keep BLE JSON queue limits here so BLE write parsing can be tuned without touching the GATT callback.
#define USER_BLE_JSON_BUF_SIZE 1024
// Queue length for USER BLE WRITE QUEUE LENGTH; size it for burst traffic without wasting RAM.
#define USER_BLE_WRITE_QUEUE_LENGTH 4
// Task stack size for USER BLE WRITE TASK STACK SIZE; tune with runtime stack high-water data.
#define USER_BLE_WRITE_TASK_STACK_SIZE (12 * 1024)
// FreeRTOS task priority for USER BLE WRITE TASK PRIORITY; keep scheduler side effects in mind when changing it.
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
#define USER_CH583_UART_PORT UART_NUM_1
// GPIO assignment for USER CH583 UART TX PIN; update board wiring and dependent drivers if it changes.
#define USER_CH583_UART_TX_PIN GPIO_NUM_24
// GPIO assignment for USER CH583 UART RX PIN; update board wiring and dependent drivers if it changes.
#define USER_CH583_UART_RX_PIN GPIO_NUM_23
// Configuration value for USER CH583 UART BAUD RATE; update local references before changing it.
#define USER_CH583_UART_BAUD_RATE 115200
// Configuration value for CH583 WIFI UART PORT; update local references before changing it.
#define CH583_WIFI_UART_PORT USER_CH583_UART_PORT

// Keep CH583 UART buffer and task sizes here so receive pressure can be tuned without changing task logic.
#define USER_CH583_UART_RECEIVE_BUF_SIZE 256
// Buffer or size limit for USER CH583 UART DRIVER RX BUF SIZE; keep RAM and payload limits aligned.
#define USER_CH583_UART_DRIVER_RX_BUF_SIZE 8192
// Buffer or size limit for USER CH583 UART DRIVER TX BUF SIZE; keep RAM and payload limits aligned.
#define USER_CH583_UART_DRIVER_TX_BUF_SIZE 0
// Configuration value for USER CH583 UART EVENT QUEUE SIZE; update local references before changing it.
#define USER_CH583_UART_EVENT_QUEUE_SIZE 20
// Task stack size for USER CH583 UART EVENT TASK STACK SIZE; tune with runtime stack high-water data.
#define USER_CH583_UART_EVENT_TASK_STACK_SIZE (8 * 1024)
// Task stack size for USER CH583 UART RECEIVE TASK STACK SIZE; tune with runtime stack high-water data.
#define USER_CH583_UART_RECEIVE_TASK_STACK_SIZE (8 * 1024)
// Configuration value for USER CH583 UART WAKEUP THRESHOLD; update local references before changing it.
#define USER_CH583_UART_WAKEUP_THRESHOLD 3

// Keep CH583 protocol debug flags here so frame parsing logs can be enabled without editing the copied protocol file.
#define CH583_WIFI_UART_DEBUG_PRINT_ENABLE 0
// Feature switch for CH583 WIFI UART DIRECTION PRINT ENABLE; set to 1 to enable and 0 to disable.
#define CH583_WIFI_UART_DIRECTION_PRINT_ENABLE 1
// Timing value for CH583 WIFI UART TX SILENCE MS; verify related wake, sleep, and retry behavior if it changes.
#define CH583_WIFI_UART_TX_SILENCE_MS 10
// Configuration value for CH583 WIFI UART BAD CRC RETRY MAX; update local references before changing it.
#define CH583_WIFI_UART_BAD_CRC_RETRY_MAX 5
// DEVICE_INFO replaces the former BLE_MAC and BLE_VER receive commands.
#define CH583_DEVICE_INFO_CMD "DEVICE_INFO"
#define CH583_KEY_EVENT_CMD "KEY_EVENT"
// Keep early BLE_DATA bounded until storage, display, and network startup finish.
#define CH583_BLE_DATA_STARTUP_QUEUE_LENGTH 4U
// The BLE_DATA worker runs JSON business outside the UART protocol task.
#define CH583_BLE_DATA_BUSINESS_TASK_STACK_SIZE (12U * 1024U)
#define CH583_BLE_DATA_BUSINESS_TASK_PRIORITY 3U
#define CH583_DEVICE_INFO_MAC_HEX_LEN 12
#define CH583_DEVICE_INFO_BLE_VER_TEXT_MAX_LEN 3
// Five-field DEVICE_INFO is bounded by the longest BLE_CONNECT wake reason.
#define CH583_DEVICE_INFO_ARG_MAX_LEN 33
#define CH583_DEVICE_INFO_SCREEN_TYPE_133 'd'
#define CH583_DEVICE_INFO_SCREEN_TYPE_709 'e'
// board_info_hex is the complete visible ASCII byte: 0x40 vendor 0, 0x41 vendor 1.
#define CH583_DEVICE_INFO_BOARD_XINGTAI 0x40
#define CH583_DEVICE_INFO_BOARD_DKE 0x41
#define CH583_DEVICE_INFO_ERR_SAVE_FAILED "DEVICE_INFO_SAVE_FAILED"
// Configuration value for CH583 WIFI NFC JSON MAX LEN; update local references before changing it.
#define CH583_WIFI_NFC_JSON_MAX_LEN 220
// Configuration value for CH583 WIFI NFC BASE64URL MAX LEN; update local references before changing it.
#define CH583_WIFI_NFC_BASE64URL_MAX_LEN 300
// Feature switch for CH583 WIFI NFC TEST ENABLE; set to 1 to enable and 0 to disable.
#define CH583_WIFI_NFC_TEST_ENABLE 0
// Timing value for CH583 WIFI NFC TEST START DELAY SECONDS; verify related wake, sleep, and retry behavior if it changes.
#define CH583_WIFI_NFC_TEST_START_DELAY_SECONDS 5

/* -------------------------------------------------------------------------- */
/* 13.1 Runtime Debug Output                                                   */
/* -------------------------------------------------------------------------- */

// SDK console / bootloader logs are still controlled by sdkconfig. These macros
// only route application logs after UserDebugOutput_Init() is called.
#define USER_DEBUG_OUTPUT_USB_SERIAL_JTAG 1
// Configuration value for USER DEBUG OUTPUT UART0; update local references before changing it.
#define USER_DEBUG_OUTPUT_UART0 2
// Configuration value for USER DEBUG OUTPUT BOTH; update local references before changing it.
#define USER_DEBUG_OUTPUT_BOTH 3

#ifndef USER_DEBUG_OUTPUT_TARGET
// Select where application debug logs are routed after UserDebugOutput_Init() runs.
#define USER_DEBUG_OUTPUT_TARGET USER_DEBUG_OUTPUT_BOTH
#endif


// 1. USB Serial/JTAG
// 2. UART0
//    TX = GPIO11
//    RX = GPIO12
//    baud = 921600

#define USER_DEBUG_UART_PORT UART_NUM_0
// GPIO assignment for USER DEBUG UART TX PIN; update board wiring and dependent drivers if it changes.
#define USER_DEBUG_UART_TX_PIN GPIO_NUM_11
// GPIO assignment for USER DEBUG UART RX PIN; update board wiring and dependent drivers if it changes.
#define USER_DEBUG_UART_RX_PIN GPIO_NUM_12
// Configuration value for USER DEBUG UART BAUD RATE; update local references before changing it.
#define USER_DEBUG_UART_BAUD_RATE 921600
// Buffer or size limit for USER DEBUG UART RX BUF SIZE; keep RAM and payload limits aligned.
#define USER_DEBUG_UART_RX_BUF_SIZE 256
// Buffer or size limit for USER DEBUG UART TX BUF SIZE; keep RAM and payload limits aligned.
#define USER_DEBUG_UART_TX_BUF_SIZE 4096
// Configuration value for USER DEBUG UART LOG LINE MAX; update local references before changing it.
#define USER_DEBUG_UART_LOG_LINE_MAX 512

#if (USER_DEBUG_OUTPUT_TARGET == USER_DEBUG_OUTPUT_UART0) || \
    (USER_DEBUG_OUTPUT_TARGET == USER_DEBUG_OUTPUT_BOTH)
// Feature switch for USER DEBUG UART0 ENABLED; set to 1 to enable and 0 to disable.
#define USER_DEBUG_UART0_ENABLED 1
#else
// Feature switch for USER DEBUG UART0 ENABLED; set to 1 to enable and 0 to disable.
#define USER_DEBUG_UART0_ENABLED 0
#endif

/* -------------------------------------------------------------------------- */
/* 14. EPD Display / Panel Geometry / SPI Pins                                 */
/* -------------------------------------------------------------------------- */

// Keep EPD display enable here so network cast/upload can be tested without editing receive code.
#define USER_EPD_ENABLE 1

// Keep EPD panel geometry here because the copied display driver and network bin size must match.
#define USER_EPD_WIDTH 1600
// Configuration value for USER EPD HEIGHT; update local references before changing it.
#define USER_EPD_HEIGHT 1200
// Configuration value for USER EPD SCALE MAX WIDTH; update local references before changing it.
#define USER_EPD_SCALE_MAX_WIDTH 1350
// Configuration value for USER EPD SCALE MAX HEIGHT; update local references before changing it.
#define USER_EPD_SCALE_MAX_HEIGHT 1350
// Configuration value for USER EPD TYPE; update local references before changing it.
#define USER_EPD_TYPE 2

// Keep EPD SPI pins here so board pin changes do not require editing display_bsp.cpp.
#define USER_EPD_MOSI_PIN GPIO_NUM_1
// GPIO assignment for USER EPD MISO PIN; update board wiring and dependent drivers if it changes.
#define USER_EPD_MISO_PIN GPIO_NUM_25
// GPIO assignment for USER EPD SCK PIN; update board wiring and dependent drivers if it changes.
#define USER_EPD_SCK_PIN GPIO_NUM_6
// GPIO assignment for USER EPD DC PIN; update board wiring and dependent drivers if it changes.
#define USER_EPD_DC_PIN GPIO_NUM_8
// GPIO assignment for USER EPD CS PIN; update board wiring and dependent drivers if it changes.
#define USER_EPD_CS_PIN GPIO_NUM_7
// GPIO assignment for USER EPD CS2 PIN; update board wiring and dependent drivers if it changes.
#define USER_EPD_CS2_PIN GPIO_NUM_0
// GPIO assignment for USER EPD RST PIN; update board wiring and dependent drivers if it changes.
#define USER_EPD_RST_PIN GPIO_NUM_10
// GPIO assignment for USER EPD BUSY PIN; update board wiring and dependent drivers if it changes.
#define USER_EPD_BUSY_PIN GPIO_NUM_9
// Configuration value for USER EPD SPI HOST; update local references before changing it.
#define USER_EPD_SPI_HOST SPI2_HOST

// Keep the second EPD target mapped to the shared C5 EPD control lines plus CS2.
#define USER_EPD2_DC_PIN USER_EPD_DC_PIN
// GPIO assignment for USER EPD2 CS PIN; update board wiring and dependent drivers if it changes.
#define USER_EPD2_CS_PIN USER_EPD_CS2_PIN
// GPIO assignment for USER EPD2 RST PIN; update board wiring and dependent drivers if it changes.
#define USER_EPD2_RST_PIN USER_EPD_RST_PIN
// GPIO assignment for USER EPD2 BUSY PIN; update board wiring and dependent drivers if it changes.
#define USER_EPD2_BUSY_PIN USER_EPD_BUSY_PIN

/* -------------------------------------------------------------------------- */
/* 15. SD Card SPI Pins                                                        */
/* -------------------------------------------------------------------------- */

// Keep SD SPI pins here because the C5 board shares MOSI and CLK with the EPD bus.
#define USER_SD_SPI_MOSI_PIN GPIO_NUM_1
// GPIO assignment for USER SD SPI MISO PIN; update board wiring and dependent drivers if it changes.
#define USER_SD_SPI_MISO_PIN GPIO_NUM_25
// GPIO assignment for USER SD SPI CLK PIN; update board wiring and dependent drivers if it changes.
#define USER_SD_SPI_CLK_PIN GPIO_NUM_6
// GPIO assignment for USER SD SPI CS PIN; update board wiring and dependent drivers if it changes.
#define USER_SD_SPI_CS_PIN GPIO_NUM_26
// Configuration value for USER SD SPI HOST; update local references before changing it.
#define USER_SD_SPI_HOST SPI2_HOST

/* -------------------------------------------------------------------------- */
/* 16. EPD Display Task / EPD USB Control API                                  */
/* -------------------------------------------------------------------------- */

// Keep EPD task settings here so display latency and stack pressure can be tuned in one place.
#define USER_EPD_DISPLAY_QUEUE_LENGTH 2
// Task stack size for USER EPD DISPLAY TASK STACK SIZE; tune with runtime stack high-water data.
#define USER_EPD_DISPLAY_TASK_STACK_SIZE (8 * 1024)
// FreeRTOS task priority for USER EPD DISPLAY TASK PRIORITY; keep scheduler side effects in mind when changing it.
#define USER_EPD_DISPLAY_TASK_PRIORITY 5
// Bound the shared EPD/SDSPI bus and back EPD payload TX with one static DMA buffer.
#define USER_SHARED_SPI_MAX_TRANSFER_SIZE   4096U  // 1024U
#define USER_EPD_SPI_SAFE_DMA_TX_CHUNK USER_SHARED_SPI_MAX_TRANSFER_SIZE
#if USER_SHARED_SPI_MAX_TRANSFER_SIZE < 516U
#error "USER_SHARED_SPI_MAX_TRANSFER_SIZE must hold one SDSPI block transaction"
#endif
#if USER_EPD_SPI_SAFE_DMA_TX_CHUNK == 0
#error "USER_EPD_SPI_SAFE_DMA_TX_CHUNK must be greater than zero"
#endif
// Bound synchronous display waits; completion lifetime remains owned by both waiter and EPD task.
#define USER_EPD_DISPLAY_WAIT_TIMEOUT_MS (5 * 60 * 1000)
// Select zlib-compressed business display data when set to 1, or raw display data when set to 0.
#define USER_EPD_DISPLAY_DATA_ZLIB_ENABLE 1
#if USER_EPD_DISPLAY_DATA_ZLIB_ENABLE != 0 && USER_EPD_DISPLAY_DATA_ZLIB_ENABLE != 1
#error "USER_EPD_DISPLAY_DATA_ZLIB_ENABLE must be 0 or 1"
#endif

// NVS key used by USER EPD TYPE NVS KEY; keep storage compatibility before changing it.
#define USER_EPD_TYPE_NVS_KEY "epd_type"
// Configuration value for USER EPD TYPE DEFAULT; update local references before changing it.
#define USER_EPD_TYPE_DEFAULT 10   // EPD_TYPE_1600_1200_133_DKE = 10,
// Mode value for USER EPD DISPLAY MODE NORMAL; keep it consistent with stored/runtime mode decoding.
#define USER_EPD_DISPLAY_MODE_NORMAL 0
// Mode value for USER EPD DISPLAY MODE SLIDESHOW; keep it consistent with stored/runtime mode decoding.
#define USER_EPD_DISPLAY_MODE_SLIDESHOW 1
// Mode value for USER EPD DISPLAY MODE DAILY; keep it consistent with stored/runtime mode decoding.
#define USER_EPD_DISPLAY_MODE_DAILY 2
// Mode value for USER EPD DISPLAY MODE DEFAULT; keep it consistent with stored/runtime mode decoding.
#define USER_EPD_DISPLAY_MODE_DEFAULT USER_EPD_DISPLAY_MODE_NORMAL
// NVS key used by USER EPD DISPLAY MODE NVS KEY; keep storage compatibility before changing it.
#define USER_EPD_DISPLAY_MODE_NVS_KEY "epd_mode"

/* -------------------------------------------------------------------------- */
/* 16.1 Daily Image                                                            */
/* -------------------------------------------------------------------------- */

// Enable the isolated daily image feature without changing existing display paths.
#define USER_DAILY_IMAGE_ENABLE 1
// Version the NVS blob so future fields can be migrated without guessing its layout.
#define USER_DAILY_IMAGE_NVS_CONFIG_VERSION 2U
// A saved value of zero means the APP-triggered immediate display has completed.
#define USER_DAILY_IMAGE_INITIAL_RUN_DONE 0U
// A saved value of one makes the next valid DAILY worker run immediately after SNTP.
#define USER_DAILY_IMAGE_INITIAL_RUN_PENDING 1U
// Keep the NVS key within the ESP-IDF 15-character key limit.
#define USER_DAILY_IMAGE_NVS_KEY "daily_cfg"
// Store the fixed protocol function name in the versioned NVS blob for full request recovery.
#define USER_DAILY_IMAGE_FUNC_BUFFER_SIZE 24U
// Store at most 499 URL bytes plus the terminating null byte from the APP request.
#define USER_DAILY_IMAGE_API_URL_BUFFER_SIZE 500U
// Bound the URL returned by the daily image selection API.
#define USER_DAILY_IMAGE_DOWNLOAD_URL_BUFFER_SIZE 1024U
// Bound the small JSON body posted to the selection API.
#define USER_DAILY_IMAGE_QUERY_BODY_SIZE 192U
// Bound the selection API response so an unexpected page cannot consume application memory.
#define USER_DAILY_IMAGE_QUERY_RESPONSE_SIZE 4096U
// Bound connect and socket operations for both HTTPS requests.
#define USER_DAILY_IMAGE_HTTP_TIMEOUT_MS 15000U
// One permanent worker serializes scheduled, local, network cast, and USB cast image business.
#define USER_IMAGE_BUSINESS_WORKER_STACK_SIZE (12U * 1024U)
// Keep one bounded inline pending command; large cast bodies are transferred by owned heap pointer.
#define USER_IMAGE_BUSINESS_WORKER_PAYLOAD_SIZE 640U
// Keep image business work below the EPD display task priority.
#define USER_IMAGE_BUSINESS_WORKER_PRIORITY 4U
// Poll target time and mode changes without a busy loop.
#define USER_DAILY_IMAGE_READY_POLL_MS 1000U
// Space WiFi and SNTP readiness checks without holding the CPU in a tight loop.
#define USER_DAILY_IMAGE_READY_CHECK_INTERVAL_SECONDS 5U
// Sleep for one hour after this many consecutive WiFi or SNTP readiness failures.
#define USER_DAILY_IMAGE_READY_CHECK_MAX_ATTEMPTS 10U
// Retry only the API selection and BIN download within the current boot.
#define USER_DAILY_IMAGE_RETRY_COUNT 3U
// Separate retries so a server outage does not create a tight request loop.
#define USER_DAILY_IMAGE_RETRY_DELAY_MS 10000U
// Wake early enough for boot, WiFi association, time restore, and HTTPS setup.
#define USER_DAILY_IMAGE_WAKE_ADVANCE_SECONDS 30U
// Repeat the absolute daily schedule every 24 hours.
#define USER_DAILY_IMAGE_PERIOD_SECONDS (24U * 60U * 60U)
// Keep scheduled and retry EPD starts at least five minutes apart; a new APP-triggered initial run is exempt.
#define USER_DAILY_IMAGE_MIN_DISPLAY_INTERVAL_SECONDS (5U * 60U)
// Retry a failed download or a failed single display after one hour.
#define USER_DAILY_IMAGE_RETRY_WAKE_SECONDS (60U * 60U)
// Reserve PSRAM for TLS, the EPD queue copy, and unrelated running services.
#define USER_DAILY_IMAGE_PSRAM_RESERVE_BYTES (256 * 1024U)
// Four equal steps produce 25/50/75 percent progress plus one completion log.
#define USER_DAILY_IMAGE_PROGRESS_STEP_COUNT 4U
// Request the existing guarded power-off path immediately after daily work terminates.
#define USER_DAILY_IMAGE_POWER_OFF_DELAY_SECONDS 1U
/* Retry a guarded shutdown after temporary HTTP/EPD/CH583 activity clears. */
#define USER_DAILY_IMAGE_POWER_OFF_RETRY_SECONDS 20U
// Feature switch for USB CONSOLE EPD TYPE DEBUG LOG ENABLE; set to 1 to enable and 0 to disable.
#define USB_CONSOLE_EPD_TYPE_DEBUG_LOG_ENABLE 1
// HTTP/USB route string for USB CONSOLE EPD TYPE LIST URI; update registered handlers if it changes.
#define USB_CONSOLE_EPD_TYPE_LIST_URI "/epd_type_list"
// HTTP/USB route string for USB CONSOLE EPD TYPE URI; update registered handlers if it changes.
#define USB_CONSOLE_EPD_TYPE_URI "/epd_type"
// HTTP/USB route string for USB CONSOLE EPD TEST URI; update registered handlers if it changes.
#define USB_CONSOLE_EPD_TEST_URI "/epd_test"

/* -------------------------------------------------------------------------- */
/* 17. Display Log Compatibility Macros                                        */
/* -------------------------------------------------------------------------- */

// Map the copied display driver's colored logs to ESP-IDF logs for this project.
#ifndef LOG_Blue
// Display-driver blue log alias mapped to ESP-IDF info logs.
#define LOG_Blue(fmt, ...) ESP_LOGI("Display", fmt, ##__VA_ARGS__)
#endif

#ifndef LOG_Purple
// Display-driver purple log alias mapped to ESP-IDF info logs.
#define LOG_Purple(fmt, ...) ESP_LOGI("Display", fmt, ##__VA_ARGS__)
#endif

#ifndef LOG_Cyan
// Display-driver cyan log alias mapped to ESP-IDF info logs.
#define LOG_Cyan(fmt, ...) ESP_LOGI("Display", fmt, ##__VA_ARGS__)
#endif

#ifndef LOG_INFO
// Display-driver info log alias mapped to ESP-IDF info logs.
#define LOG_INFO(fmt, ...) ESP_LOGI("Display", fmt, ##__VA_ARGS__)
#endif

#ifndef LOG_WARN
// Display-driver warning log alias mapped to ESP-IDF warning logs.
#define LOG_WARN(fmt, ...) ESP_LOGW("Display", fmt, ##__VA_ARGS__)
#endif

#ifndef LOG_ERROR
// Display-driver error log alias mapped to ESP-IDF error logs.
#define LOG_ERROR(fmt, ...) ESP_LOGE("Display", fmt, ##__VA_ARGS__)
#endif

/* -------------------------------------------------------------------------- */
/* 18. GPIO Test Output                                                        */
/* -------------------------------------------------------------------------- */

// Enable local ESP32-C5 GPIO test output. GPIO11 toggles at 50% duty; GPIO12 is fixed output.
#if USER_DEBUG_UART0_ENABLED
// Disable local GPIO test output while UART0 debug output owns GPIO11/GPIO12.
#define USER_GPIO_TEST_OUTPUT_ENABLE 0
#else
// Enable local GPIO test output when GPIO11/GPIO12 are free.
#define USER_GPIO_TEST_OUTPUT_ENABLE 1
#endif
// GPIO assignment for USER GPIO TEST PIN 11; update board wiring and dependent drivers if it changes.
#define USER_GPIO_TEST_PIN_11 GPIO_NUM_11
// GPIO assignment for USER GPIO TEST PIN 12; update board wiring and dependent drivers if it changes.
#define USER_GPIO_TEST_PIN_12 GPIO_NUM_12
// Fixed output level driven on GPIO12 during the local GPIO test.
#define USER_GPIO_TEST_PIN_12_LEVEL 0
// Toggle period used by the local GPIO11 test output.
#define USER_GPIO_TEST_PERIOD_MS 500
// Task stack size for the local GPIO test task.
#define USER_GPIO_TEST_TASK_STACK_SIZE (2 * 1024)
// FreeRTOS task priority for the local GPIO test task.
#define USER_GPIO_TEST_TASK_PRIORITY 1

/* -------------------------------------------------------------------------- */
/* 19. LED Status / CH583 LED Backend                                          */
/* -------------------------------------------------------------------------- */

// Keep LED status enable here so bring-up can disable indicators without changing business code.
#define USER_LED_STATUS_ENABLE 1

// Route status LEDs through CH583 GPIO because the ESP32-C5 board has no local LEDs.
#define USER_LED_BACKEND_CH583 1

// Keep CH583 LED pins and active levels here so status behavior can change without touching LED logic.
#define USER_LED_CH583_GREEN_PORT "PB"
// GPIO assignment for USER LED CH583 GREEN PIN; update board wiring and dependent drivers if it changes.
#define USER_LED_CH583_GREEN_PIN 6
// Configuration value for USER LED CH583 RED PORT; update local references before changing it.
#define USER_LED_CH583_RED_PORT "PB"
// GPIO assignment for USER LED CH583 RED PIN; update board wiring and dependent drivers if it changes.
#define USER_LED_CH583_RED_PIN 5
// Configuration value for USER LED CH583 ON LEVEL; update local references before changing it.
#define USER_LED_CH583_ON_LEVEL "LOW"
// Configuration value for USER LED CH583 OFF LEVEL; update local references before changing it.
#define USER_LED_CH583_OFF_LEVEL "HIGH"

// CH583 owns the blink clock. Each level adds 600 ms to the LED toggle interval.
#define USER_LED_BLINK_LEVEL_1_MS 600
// Timing value for USER LED BLINK LEVEL 2 MS; verify related wake, sleep, and retry behavior if it changes.
#define USER_LED_BLINK_LEVEL_2_MS 1200
// Timing value for USER LED BLINK LEVEL 3 MS; verify related wake, sleep, and retry behavior if it changes.
#define USER_LED_BLINK_LEVEL_3_MS 1800
// Timing value for USER LED BLINK LEVEL 4 MS; verify related wake, sleep, and retry behavior if it changes.
#define USER_LED_BLINK_LEVEL_4_MS 2400
// Timing value for USER LED BLINK LEVEL 5 MS; verify related wake, sleep, and retry behavior if it changes.
#define USER_LED_BLINK_LEVEL_5_MS 3000
// Timing value for USER LED BLINK LEVEL 6 MS; verify related wake, sleep, and retry behavior if it changes.
#define USER_LED_BLINK_LEVEL_6_MS 3600
// Timing value for USER LED BLINK LEVEL 7 MS; verify related wake, sleep, and retry behavior if it changes.
#define USER_LED_BLINK_LEVEL_7_MS 4200
// Timing value for USER LED BLINK LEVEL 8 MS; verify related wake, sleep, and retry behavior if it changes.
#define USER_LED_BLINK_LEVEL_8_MS 4800
// Timing value for USER LED BLINK LEVEL 9 MS; verify related wake, sleep, and retry behavior if it changes.
#define USER_LED_BLINK_LEVEL_9_MS 5400
// Timing value for USER LED BLINK LEVEL 10 MS; verify related wake, sleep, and retry behavior if it changes.
#define USER_LED_BLINK_LEVEL_10_MS 6000
// Timing value for USER LED FAST BLINK MS; verify related wake, sleep, and retry behavior if it changes.
#define USER_LED_FAST_BLINK_MS USER_LED_BLINK_LEVEL_1_MS
// Timing value for USER LED MID BLINK MS; verify related wake, sleep, and retry behavior if it changes.
#define USER_LED_MID_BLINK_MS USER_LED_BLINK_LEVEL_2_MS
// Slow blink uses a 2400 ms ON/OFF interval, so its full period is 4.8 seconds.
#define USER_LED_SLOW_BLINK_MS USER_LED_BLINK_LEVEL_4_MS
// READY heartbeat uses a 600 ms ON/OFF interval, so its full period is 1.2 seconds.
#define USER_LED_READY_BLINK_MS USER_LED_FAST_BLINK_MS
// WiFi connecting, waiting for IP, and service startup use the medium interval.
#define USER_LED_WIFI_CONNECT_BLINK_MS USER_LED_MID_BLINK_MS
// Network and UART large-data activity use the fast interval.
#define USER_LED_NETWORK_ACTIVITY_BLINK_MS USER_LED_FAST_BLINK_MS
// EPD refresh activity uses the slow interval so it remains distinct from data transfer.
#define USER_LED_EPD_ACTIVITY_BLINK_MS USER_LED_SLOW_BLINK_MS
// WiFi configuration and authentication faults use the slow interval.
#define USER_LED_WIFI_ERROR_BLINK_MS USER_LED_SLOW_BLINK_MS
// HTTP and storage faults use the medium interval.
#define USER_LED_SERVICE_ERROR_BLINK_MS USER_LED_MID_BLINK_MS
// Timing value for USER LED ACTIVITY BLINK DELAY MS; verify related wake, sleep, and retry behavior if it changes.
#define USER_LED_ACTIVITY_BLINK_DELAY_MS 300
// Queue length for all LED state, activity, fault, and power-off events.
#define USER_LED_EVENT_QUEUE_LENGTH 24
// Payload-size threshold for treating UART activity as a large transfer in LED status logic.
#define USER_LED_UART_LARGE_DATA_THRESHOLD 256
// Timing value for USER LED SUCCESS HOLD MS; verify related wake, sleep, and retry behavior if it changes.
#define USER_LED_SUCCESS_HOLD_MS 1000
// Hold time for a recoverable business operation failure before restoring the active state.
#define USER_LED_OPERATION_FAIL_HOLD_MS 3000
// Bound normal LED event posting so LED indication cannot indefinitely block business tasks.
#define USER_LED_EVENT_POST_WAIT_MS 20
// Bound the high-priority power-off event insertion into the LED queue.
#define USER_LED_POWER_OFF_EVENT_POST_WAIT_MS 200
// Maximum time the power-off caller waits for the LED task to physically turn both LEDs off.
#define USER_LED_POWER_OFF_ACK_TIMEOUT_MS 2000
// Delay between bounded retries after a CH583 LED command cannot be written.
#define USER_LED_APPLY_RETRY_MS 500
// Limit retries so a persistent UART fault cannot produce continuous LED logs.
#define USER_LED_APPLY_RETRY_MAX 3

// LED physical modes used only by the LED status module.
#define USER_LED_MODE_OFF 0
#define USER_LED_MODE_SOLID 1
#define USER_LED_MODE_BLINK 2
#define USER_LED_MODE_UNKNOWN 255

// Events accepted by the single LED status task.
#define USER_LED_EVENT_SET_STATE 1
#define USER_LED_EVENT_ACTIVITY_BEGIN 2
#define USER_LED_EVENT_ACTIVITY_END 3
#define USER_LED_EVENT_SET_FAULT 4
#define USER_LED_EVENT_CLEAR_FAULT 5
#define USER_LED_EVENT_SHOW_SUCCESS 6
#define USER_LED_EVENT_SHOW_OPERATION_FAIL 7
#define USER_LED_EVENT_OTA_BEGIN 8
#define USER_LED_EVENT_OTA_END 9
#define USER_LED_EVENT_FACTORY_RESET_BEGIN 10
#define USER_LED_EVENT_FACTORY_RESET_END 11
#define USER_LED_EVENT_POWER_OFF_PENDING 12
#define USER_LED_EVENT_PREPARE_POWER_OFF 13
#define USER_LED_EVENT_RESTART_PENDING 14
// Cancel a prepared power-off lock when the final guard detects new work.
#define USER_LED_EVENT_CANCEL_POWER_OFF 15
// Recalibrate CH583 LED output after the first DEVICE_INFO sync without gating communication.
#define USER_LED_EVENT_REAPPLY_CURRENT 16

// Persistent fault bits evaluated by the LED task in priority order.
#define USER_LED_FAULT_WIFI_NO_CONFIG (1UL << 0)
#define USER_LED_FAULT_WIFI_AUTH (1UL << 1)
#define USER_LED_FAULT_HTTP_SERVICE (1UL << 2)
#define USER_LED_FAULT_STORAGE (1UL << 3)
#define USER_LED_FAULT_FATAL (1UL << 4)
// Task stack size for USER LED STATUS TASK STACK SIZE; tune with runtime stack high-water data.
#define USER_LED_STATUS_TASK_STACK_SIZE (4 * 1024)
// FreeRTOS task priority for USER LED STATUS TASK PRIORITY; keep scheduler side effects in mind when changing it.
#define USER_LED_STATUS_TASK_PRIORITY 3

/* -------------------------------------------------------------------------- */
/* 20. Zlib File Compression Self-Test                                         */
/* -------------------------------------------------------------------------- */

// Use the fastest zlib compression level to limit startup test time.
#define USER_ZLIB_COMPRESSION_LEVEL 1
// Bound each streaming file read and write without loading the whole BIN file.
#define USER_ZLIB_STREAM_BUFFER_SIZE (8U * 1024U)
// Bound absolute paths constructed from the mounted base path and test paths.
#define USER_ZLIB_TEST_PATH_BUFFER_SIZE 160U
// Source BIN file used by the SD-card compression test.
#define USER_ZLIB_TEST_SOURCE_RELATIVE_PATH "/bin_img/2486aad8763e9822.bin"
// Keep the zlib stream beside its source so it can be inspected after the test.
#define USER_ZLIB_TEST_COMPRESSED_RELATIVE_PATH "/bin_img/2486aad8763e9822.bin.zlib"
// Use a separate temporary output so the source BIN file is never overwritten.
#define USER_ZLIB_TEST_DECOMPRESSED_RELATIVE_PATH "/bin_img/2486aad8763e9822.bin.unzlib.tmp"

/* -------------------------------------------------------------------------- */
/* 21. Global Runtime Variables / Shared APIs                                  */
/* -------------------------------------------------------------------------- */

extern uint16_t sleep_time;
extern uint32_t working_time;
extern uint32_t server_required_continue_work_time;
extern uint32_t wifi_standby_time_s;
extern int g_app_reset_reason;
void print_base_info(void);

esp_err_t app_nvs_read_u8(const char *key, uint8_t *value, uint8_t default_value);
esp_err_t app_nvs_write_u8(const char *key, uint8_t value);
esp_err_t app_nvs_read_str(const char *key, char *value, size_t value_size, const char *default_value);
esp_err_t app_nvs_write_str(const char *key, const char *value);
esp_err_t app_nvs_read_blob(const char *key, void *value, size_t value_size);
esp_err_t app_nvs_write_blob(const char *key, const void *value, size_t value_size);
esp_err_t app_nvs_erase_key(const char *key);

#ifdef __cplusplus
}
#endif
