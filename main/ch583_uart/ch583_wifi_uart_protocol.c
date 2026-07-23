// EPD_Vcc:PA.6  // if 1 then open ,if 0 then 0
// VBAT_Vcc:PB.3 // if 1 then open ,if 0 then 0


#include "ch583_wifi_uart_protocol.h"
#include "debug_output.h"
#include "epd_display_mode.h"
#include "epd_sd_power_test.h"
#include "epd_type.h"
#include "led_status.h"
#include "server_network_sta_time.h"
#include "server_network_sta_wifi_work_time.h"
#include "tdx_cfg.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <esp_app_desc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <driver/uart.h>
#include <nvs.h>

#define CH583_WIFI_MAX_ARG_LEN 300
#define CH583_WIFI_MAX_WIFI_DATA_LEN 256
#define CH583_WIFI_MAX_FRAME_BODY_LEN 448
#define CH583_WIFI_MAX_BLE_MESSAGE_LEN 2048

static const char *TAG = "ch583_proto";

#ifndef CH583_WIFI_UART_PORT
// Use the board-level CH583 UART port so protocol TX follows the C5 UART1 wiring.
// 使用板级 CH583 串口号，保证协议发送跟随 C5 的 UART1 接线。
#define CH583_WIFI_UART_PORT USER_CH583_UART_PORT
#endif

#ifndef CH583_WIFI_UART_DEBUG_PRINT_ENABLE
#define CH583_WIFI_UART_DEBUG_PRINT_ENABLE 0
#endif

#ifndef CH583_WIFI_UART_DIRECTION_PRINT_ENABLE
#define CH583_WIFI_UART_DIRECTION_PRINT_ENABLE 0
#endif

#ifndef CH583_WIFI_UART_TX_SILENCE_MS
#define CH583_WIFI_UART_TX_SILENCE_MS 100
#endif

#ifndef CH583_WIFI_UART_BAD_CRC_RETRY_MAX
#define CH583_WIFI_UART_BAD_CRC_RETRY_MAX 10
#endif

#define CH583_WIFI_TX_PENDING_MAX 8

#if CH583_WIFI_UART_DEBUG_PRINT_ENABLE
#define CH583_WIFI_DEBUG_PRINTF(...) UserDebugOutput_Printf(__VA_ARGS__)
#else
#define CH583_WIFI_DEBUG_PRINTF(...) do { } while (0)
#endif

#if CH583_WIFI_UART_DIRECTION_PRINT_ENABLE
#define CH583_WIFI_DIRECTION_PRINTF(...) UserDebugOutput_Printf(__VA_ARGS__)
#else
#define CH583_WIFI_DIRECTION_PRINTF(...) do { } while (0)
#endif

typedef struct {
    uint16_t seq;
    char cmd[16];
    size_t arg_len;
    uint16_t part;
    uint16_t total;
    const char *arg;
} ch583_wifi_frame_t;

typedef struct {
    char mac[CH583_DEVICE_INFO_MAC_HEX_LEN + 1];
    uint8_t ble_ver;
    char screen_type;
    uint8_t board_info;
    uint8_t epd_type;
} ch583_wifi_device_info_t;

typedef enum {
    CH583_DEVICE_INFO_PARSE_OK = 0,
    CH583_DEVICE_INFO_PARSE_BAD_ARG,
    CH583_DEVICE_INFO_PARSE_UNSUPPORTED_EPD,
} ch583_wifi_device_info_parse_result_t;

// Keep the TX sequence independent because each sender owns its own SEQ counter.
static uint16_t s_tx_seq;
static SemaphoreHandle_t s_tx_mutex;

typedef struct {
    bool valid;
    uint16_t seq;
    uint8_t bad_crc_retry_count;
    uint32_t order;
    size_t frame_len;
    char frame[CH583_WIFI_MAX_FRAME_BODY_LEN + 24];
} ch583_wifi_pending_tx_t;

static ch583_wifi_pending_tx_t s_pending_tx[CH583_WIFI_TX_PENDING_MAX];
static uint32_t s_pending_tx_order;

// Cache partial UART bytes so split frames can still be parsed.
static bool s_in_frame;
static bool s_wait_frame_start_hash;
static char s_frame_body[CH583_WIFI_MAX_FRAME_BODY_LEN + 1];
static size_t s_frame_body_len;
static bool s_rx_seq_seen;
static uint16_t s_last_rx_seq;

// Keep only one BLE_DATA split message because V1 does not allow interleaved transfers.
static bool s_ble_join_active;
static uint16_t s_ble_expected_part;
static uint16_t s_ble_total;
static size_t s_ble_len;
static char s_ble_buf[CH583_WIFI_MAX_BLE_MESSAGE_LEN + 1];
static bool s_ble_activity_active;
static char s_ble_mac[CH583_DEVICE_INFO_MAC_HEX_LEN + 1];
static uint8_t s_ble_ver;
static bool s_device_info_loaded;
static bool s_ble_ver_nvs_valid;
// DEVICE_INFO is optional for transport readiness. This flag only records whether
// this ESP32 boot has successfully received and acknowledged current device data.
static bool s_device_info_received;
#if CH583_WIFI_VER_COMPAT_ON_PING_ENABLE
// Send the ESP32 version once when CH583 continues an old session after an ESP32-only restart.
static bool s_compat_wifi_ver_sent;
#endif
static uint8_t s_wifi_provision_status;
static bool s_wifi_provision_status_valid;

static bool ch583_wifi_is_upper_hex_string(const char *text, size_t len);

static int ch583_wifi_base64url_encode(const uint8_t *in, size_t in_len, char *out, size_t out_size)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t out_len = 0;

    if (in == NULL || out == NULL || out_size == 0) {
        return -1;
    }

    for (size_t i = 0; i < in_len; i += 3) {
        uint32_t value = ((uint32_t)in[i]) << 16;
        size_t remain = in_len - i;
        if (remain > 1) {
            value |= ((uint32_t)in[i + 1]) << 8;
        }
        if (remain > 2) {
            value |= in[i + 2];
        }

        size_t emit = remain >= 3 ? 4 : remain + 1;
        for (size_t j = 0; j < emit; j++) {
            if (out_len + 1 >= out_size) {
                return -1;
            }
            out[out_len++] = table[(value >> (18 - (6 * j))) & 0x3FU];
        }
    }

    out[out_len] = '\0';
    return (int)out_len;
}

static uint16_t ch583_wifi_crc16_ccitt_false(const char *data, size_t len)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint8_t)data[i] << 8);
        for (int bit = 0; bit < 8; bit++) {
            if ((crc & 0x8000) != 0) {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

static void ch583_wifi_load_device_info_from_nvs(void)
{
    char saved_mac[CH583_DEVICE_INFO_MAC_HEX_LEN + 1] = {0};
    esp_err_t mac_ret;
    esp_err_t ver_ret;

    if (s_device_info_loaded) {
        return;
    }
    s_device_info_loaded = true;

    mac_ret = app_nvs_read_str(CH583_DEVICE_INFO_MAC_NVS_KEY,
                               saved_mac,
                               sizeof(saved_mac),
                               "");
    if (mac_ret != ESP_OK && mac_ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "DEVICE_INFO MAC load failed ret=%s", esp_err_to_name(mac_ret));
    } else if (strlen(saved_mac) == CH583_DEVICE_INFO_MAC_HEX_LEN &&
               ch583_wifi_is_upper_hex_string(saved_mac, CH583_DEVICE_INFO_MAC_HEX_LEN)) {
        memcpy(s_ble_mac, saved_mac, CH583_DEVICE_INFO_MAC_HEX_LEN + 1);
    }

    ver_ret = app_nvs_read_u8(CH583_DEVICE_INFO_BLE_VER_NVS_KEY,
                              &s_ble_ver,
                              CH583_DEVICE_INFO_BLE_VER_DEFAULT);
    s_ble_ver_nvs_valid = ver_ret == ESP_OK;
    if (ver_ret != ESP_OK) {
        ESP_LOGE(TAG, "DEVICE_INFO BLE version load failed ret=%s",
                 esp_err_to_name(ver_ret));
    }

    ESP_LOGI(TAG, "DEVICE_INFO data loaded MAC=%s BLE version=%u",
             s_ble_mac[0] != '\0' ? s_ble_mac : "(empty)",
             (unsigned int)s_ble_ver);
}

int ch583_wifi_uart_protocol_init(void)
{
    if (s_tx_mutex == NULL) {
        s_tx_mutex = xSemaphoreCreateMutex();
    }
    ch583_wifi_load_device_info_from_nvs();
    return s_tx_mutex != NULL ? 0 : -1;
}

static int ch583_wifi_write_frame_text(const char *frame_text, size_t frame_len)
{
    int ret = -1;

    if (frame_text == NULL || frame_len == 0) {
        return -1;
    }

    // vTaskDelay(pdMS_TO_TICKS(CH583_WIFI_UART_TX_SILENCE_MS));

    // Write one complete protocol frame through the UART driver so console logs are less likely to split it.
    ret = uart_write_bytes(CH583_WIFI_UART_PORT, frame_text, frame_len);

    // Wait until the frame leaves the UART TX FIFO before allowing later debug text to print.
    uart_wait_tx_done(CH583_WIFI_UART_PORT, pdMS_TO_TICKS(100));
    // vTaskDelay(pdMS_TO_TICKS(CH583_WIFI_UART_TX_SILENCE_MS));

    return ret == (int)frame_len ? 0 : -1;
}

static bool ch583_wifi_cmd_expects_reply(const char *cmd)
{
    return cmd != NULL &&
           strcmp(cmd, "ACK") != 0 &&
           strcmp(cmd, "ERR") != 0 &&
           strcmp(cmd, "PONG") != 0 &&
           strcmp(cmd, "POWER_OFF") != 0 &&
           strcmp(cmd, "TIME_GET") != 0 &&
           strcmp(cmd, "TIME_STATUS") != 0 &&
           strcmp(cmd, "NFC_STATUS") != 0;
}

static ch583_wifi_pending_tx_t *ch583_wifi_find_pending_tx_locked(uint16_t seq)
{
    for (size_t i = 0; i < CH583_WIFI_TX_PENDING_MAX; i++) {
        if (s_pending_tx[i].valid && s_pending_tx[i].seq == seq) {
            return &s_pending_tx[i];
        }
    }
    return NULL;
}

static ch583_wifi_pending_tx_t *ch583_wifi_alloc_pending_tx_locked(void)
{
    ch583_wifi_pending_tx_t *oldest = &s_pending_tx[0];
    for (size_t i = 0; i < CH583_WIFI_TX_PENDING_MAX; i++) {
        if (!s_pending_tx[i].valid) {
            return &s_pending_tx[i];
        }
        if (s_pending_tx[i].order < oldest->order) {
            oldest = &s_pending_tx[i];
        }
    }
    UserDebugOutput_Printf("CH583_PROTO pending evict seq=%u\r\n", (unsigned int)oldest->seq);
    return oldest;
}

static int ch583_wifi_retry_pending_frame_locked(ch583_wifi_pending_tx_t *pending)
{
    if (pending == NULL || !pending->valid || pending->frame_len == 0) {
        return -1;
    }

    if (pending->bad_crc_retry_count >= CH583_WIFI_UART_BAD_CRC_RETRY_MAX) {
        UserDebugOutput_Printf("CH583_PROTO retry stop seq=%u count=%u\r\n",
               (unsigned int)pending->seq,
               (unsigned int)pending->bad_crc_retry_count);
        pending->valid = false;
        return -1;
    }

    pending->bad_crc_retry_count++;
    UserDebugOutput_Printf("CH583_PROTO retry BAD_CRC seq=%u count=%u\r\n",
           (unsigned int)pending->seq,
           (unsigned int)pending->bad_crc_retry_count);
    return ch583_wifi_write_frame_text(pending->frame, pending->frame_len);
}

static int ch583_wifi_send_frame(const char *cmd, const char *arg, uint8_t need_printf)
{
    char body[CH583_WIFI_MAX_FRAME_BODY_LEN + 1];
    char frame_text[CH583_WIFI_MAX_FRAME_BODY_LEN + 24];
    size_t arg_len = (arg != NULL) ? strlen(arg) : 0;
    int ret = -1;

    if (cmd == NULL || arg_len > CH583_WIFI_MAX_ARG_LEN) {
        UserDebugOutput_Printf("CH583_PROTO tx reject cmd=%s arg_len=%u\r\n",
               cmd ? cmd : "NULL", (unsigned int)arg_len);
        return -1;
    }
    if (s_tx_mutex == NULL || xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        UserDebugOutput_Printf("CH583_PROTO tx mutex unavailable/timeout\r\n");
        return -1;
    }

    uint16_t current_seq = s_tx_seq;
    int body_len = snprintf(body,
                            sizeof(body),
                            "V1|SEQ=%u|CMD=%s|LEN=%u|PART=1|TOTAL=1|ARG=%s",
                            (unsigned int)current_seq,
                            cmd,
                            (unsigned int)arg_len,
                            arg ? arg : "");
    if (body_len <= 0 || body_len >= (int)sizeof(body)) {
        UserDebugOutput_Printf("CH583_PROTO tx body overflow cmd=%s\r\n", cmd);
        goto done;
    }

    uint16_t crc = ch583_wifi_crc16_ccitt_false(body, (size_t)body_len);
    int frame_len = snprintf(frame_text, sizeof(frame_text), "@#%s|CRC=%04X^&\n\r", body, crc);
    if (frame_len <= 0 || frame_len >= (int)sizeof(frame_text)) {
        UserDebugOutput_Printf("CH583_PROTO tx frame overflow cmd=%s\r\n", cmd);
        goto done;
    }

    ch583_wifi_pending_tx_t *pending = NULL;
    if (ch583_wifi_cmd_expects_reply(cmd)) {
        pending = ch583_wifi_alloc_pending_tx_locked();
        *pending = (ch583_wifi_pending_tx_t){
            .valid = true,
            .seq = current_seq,
            .order = ++s_pending_tx_order,
            .frame_len = (size_t)frame_len,
        };
        memcpy(pending->frame, frame_text, (size_t)frame_len + 1);
    }

    if (need_printf == 1) {
        CH583_WIFI_DIRECTION_PRINTF("WiFi -> CH583: seq=%u cmd=%s arg=%s\r\n",
                                    (unsigned int)current_seq, cmd, arg ? arg : "");
    }

    ret = ch583_wifi_write_frame_text(frame_text, (size_t)frame_len);
    if (ret != 0 && pending != NULL) {
        pending->valid = false;
    }
    s_tx_seq++;

done:
    xSemaphoreGive(s_tx_mutex);
    return ret;
}

static int __attribute__((unused)) ch583_wifi_send_wifi_provision_frame(uint8_t combined_status)
{
    char body[CH583_WIFI_MAX_FRAME_BODY_LEN + 1];
    char frame_text[CH583_WIFI_MAX_FRAME_BODY_LEN + 24];
    const char cmd[] = "WIFI_PROVISION";
    int ret = -1;

    // Reserved for a possible future binary-ARG WIFI_PROVISION protocol.
    // The active protocol uses a two-character hex text ARG and ch583_wifi_send_frame().
    if (s_tx_mutex == NULL || xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        UserDebugOutput_Printf("CH583_PROTO WIFI_PROVISION tx mutex unavailable/timeout\r\n");
        return -1;
    }

    uint16_t current_seq = s_tx_seq;
    int prefix_len = snprintf(body,
                              sizeof(body),
                              "V1|SEQ=%u|CMD=%s|LEN=1|PART=1|TOTAL=1|ARG=",
                              (unsigned int)current_seq,
                              cmd);
    if (prefix_len <= 0 || prefix_len + 1 >= (int)sizeof(body)) {
        UserDebugOutput_Printf("CH583_PROTO WIFI_PROVISION tx body overflow\r\n");
        goto done;
    }
    body[prefix_len] = (char)combined_status;
    size_t body_len = (size_t)prefix_len + 1U;

    uint16_t crc = ch583_wifi_crc16_ccitt_false(body, body_len);
    memcpy(frame_text, "@#", 2);
    memcpy(frame_text + 2, body, body_len);
    int suffix_len = snprintf(frame_text + 2 + body_len,
                              sizeof(frame_text) - 2 - body_len,
                              "|CRC=%04X^&\n\r",
                              crc);
    if (suffix_len <= 0 || 2U + body_len + (size_t)suffix_len >= sizeof(frame_text)) {
        UserDebugOutput_Printf("CH583_PROTO WIFI_PROVISION tx frame overflow\r\n");
        goto done;
    }
    size_t frame_len = 2U + body_len + (size_t)suffix_len;

    ch583_wifi_pending_tx_t *pending = ch583_wifi_alloc_pending_tx_locked();
    *pending = (ch583_wifi_pending_tx_t){
        .valid = true,
        .seq = current_seq,
        .order = ++s_pending_tx_order,
        .frame_len = frame_len,
    };
    memcpy(pending->frame, frame_text, frame_len);
    pending->frame[frame_len] = '\0';

    ret = ch583_wifi_write_frame_text(frame_text, frame_len);
    if (ret != 0) {
        pending->valid = false;
    }
    s_tx_seq++;

done:
    xSemaphoreGive(s_tx_mutex);
    return ret;
}

static int ch583_wifi_send_ack(uint16_t received_seq)
{
    char arg[8];

    snprintf(arg, sizeof(arg), "%u", (unsigned int)received_seq);
    return ch583_wifi_send_frame("ACK", arg,1);
}

static int ch583_wifi_send_err(uint16_t received_seq, const char *reason)
{
    char arg[32];

    snprintf(arg, sizeof(arg), "%u,%s", (unsigned int)received_seq, reason ? reason : "BAD_FORMAT");
    return ch583_wifi_send_frame("ERR", arg,1);
}

static uint16_t ch583_wifi_find_seq_for_error(const char *body)
{
    const char *seq = body != NULL ? strstr(body, "SEQ=") : NULL;
    char *end = NULL;
    unsigned long value = 0;

    if (seq == NULL) {
        return 0;
    }

    value = strtoul(seq + 4, &end, 10);
    if (end == seq + 4 || value > 65535UL) {
        return 0;
    }

    return (uint16_t)value;
}

static bool ch583_wifi_parse_u16_field(const char *text, const char *prefix, uint16_t *out)
{
    char *end = NULL;
    unsigned long value = 0;
    size_t prefix_len = strlen(prefix);

    if (text == NULL || out == NULL || strncmp(text, prefix, prefix_len) != 0) {
        return false;
    }

    value = strtoul(text + prefix_len, &end, 10);
    if (end == text + prefix_len || *end != '\0' || value > 65535UL) {
        return false;
    }

    *out = (uint16_t)value;
    return true;
}

static bool ch583_wifi_parse_size_field(const char *text, const char *prefix, size_t *out)
{
    char *end = NULL;
    unsigned long value = 0;
    size_t prefix_len = strlen(prefix);

    if (text == NULL || out == NULL || strncmp(text, prefix, prefix_len) != 0) {
        return false;
    }

    value = strtoul(text + prefix_len, &end, 10);
    if (end == text + prefix_len || *end != '\0') {
        return false;
    }

    *out = (size_t)value;
    return true;
}

static bool ch583_wifi_parse_u8_dec_arg(const char *arg, uint8_t *out)
{
    char *end = NULL;
    unsigned long value = 0;

    if (arg == NULL || arg[0] == '\0' || out == NULL) {
        return false;
    }

    for (const char *p = arg; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }

    value = strtoul(arg, &end, 10);
    if (end == arg || *end != '\0' || value > 255UL) {
        return false;
    }

    *out = (uint8_t)value;
    return true;
}

static bool ch583_wifi_parse_app_version_byte_pair(const char *version, uint8_t *high, uint8_t *low)
{
    char *end = NULL;
    unsigned long high_value = 0;
    unsigned long low_value = 0;

    if (version == NULL || high == NULL || low == NULL) {
        return false;
    }

    high_value = strtoul(version, &end, 10);
    if (end == version || *end != '.' || high_value > 255UL) {
        return false;
    }

    const char *low_text = end + 1;
    low_value = strtoul(low_text, &end, 10);
    if (end == low_text || *end != '\0' || low_value > 255UL) {
        return false;
    }

    *high = (uint8_t)high_value;
    *low = (uint8_t)low_value;
    return true;
}

static uint16_t ch583_wifi_get_current_wifi_ver(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    uint8_t high = 0;
    uint8_t low = 0;

    if (app == NULL || !ch583_wifi_parse_app_version_byte_pair(app->version, &high, &low)) {
        ESP_LOGE(TAG, "WIFI_VER app version parse failed version=%s",
                 app != NULL ? app->version : "(null)");
        return 0;
    }

    return (uint16_t)((((uint16_t)high) << 8) | low);
}

static bool ch583_wifi_is_upper_hex_string(const char *text, size_t len)
{
    if (text == NULL) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        char ch = text[i];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F'))) {
            return false;
        }
    }

    return true;
}

static bool ch583_wifi_parse_hex_byte(const char *text, uint8_t *value)
{
    char *end = NULL;
    unsigned long parsed = 0;

    if (text == NULL || value == NULL || strlen(text) != 2 ||
        !ch583_wifi_is_upper_hex_string(text, 2)) {
        return false;
    }

    parsed = strtoul(text, &end, 16);
    if (end == text || *end != '\0' || parsed > 0xFFUL) {
        return false;
    }
    *value = (uint8_t)parsed;
    return true;
}

static ch583_wifi_device_info_parse_result_t ch583_wifi_parse_device_info_arg(
    const ch583_wifi_frame_t *frame,
    ch583_wifi_device_info_t *device_info)
{
    char arg_copy[CH583_DEVICE_INFO_ARG_MAX_LEN + 1];
    char *fields[4] = {0};
    char *separator = NULL;

    if (frame == NULL || device_info == NULL || frame->arg == NULL ||
        frame->arg_len == 0 || frame->arg_len > CH583_DEVICE_INFO_ARG_MAX_LEN) {
        return CH583_DEVICE_INFO_PARSE_BAD_ARG;
    }

    memcpy(arg_copy, frame->arg, frame->arg_len + 1);
    fields[0] = arg_copy;
    for (size_t i = 1; i < 4; ++i) {
        separator = strchr(fields[i - 1], ',');
        if (separator == NULL) {
            return CH583_DEVICE_INFO_PARSE_BAD_ARG;
        }
        *separator = '\0';
        fields[i] = separator + 1;
    }

    if (strchr(fields[3], ',') != NULL ||
        strlen(fields[0]) != CH583_DEVICE_INFO_MAC_HEX_LEN ||
        !ch583_wifi_is_upper_hex_string(fields[0], CH583_DEVICE_INFO_MAC_HEX_LEN) ||
        strlen(fields[1]) == 0 ||
        strlen(fields[1]) > CH583_DEVICE_INFO_BLE_VER_TEXT_MAX_LEN ||
        !ch583_wifi_parse_u8_dec_arg(fields[1], &device_info->ble_ver) ||
        strlen(fields[2]) != 1 ||
        (fields[2][0] != CH583_DEVICE_INFO_SCREEN_TYPE_133 &&
         fields[2][0] != CH583_DEVICE_INFO_SCREEN_TYPE_709) ||
        !ch583_wifi_parse_hex_byte(fields[3], &device_info->board_info)) {
        return CH583_DEVICE_INFO_PARSE_BAD_ARG;
    }

    memcpy(device_info->mac, fields[0], CH583_DEVICE_INFO_MAC_HEX_LEN + 1);
    device_info->screen_type = fields[2][0];

    if (device_info->screen_type == CH583_DEVICE_INFO_SCREEN_TYPE_133 &&
        device_info->board_info == CH583_DEVICE_INFO_BOARD_XINGTAI) {
        device_info->epd_type = EPD_TYPE_1600_1200_133;
    } else if (device_info->screen_type == CH583_DEVICE_INFO_SCREEN_TYPE_133 &&
               device_info->board_info == CH583_DEVICE_INFO_BOARD_DKE) {
        device_info->epd_type = EPD_TYPE_1600_1200_133_DKE;
    } else if (device_info->screen_type == CH583_DEVICE_INFO_SCREEN_TYPE_709 &&
               device_info->board_info == CH583_DEVICE_INFO_BOARD_XINGTAI) {
        device_info->epd_type = EPD_TYPE_1600_1200_79;
    } else {
        return CH583_DEVICE_INFO_PARSE_UNSUPPORTED_EPD;
    }

    return CH583_DEVICE_INFO_PARSE_OK;
}

static bool ch583_wifi_parse_reply_seq_arg(const char *arg, uint16_t *seq_out)
{
    char *end = NULL;
    unsigned long value = 0;

    if (arg == NULL || seq_out == NULL) {
        return false;
    }

    value = strtoul(arg, &end, 10);
    if (end == arg || value > 65535UL) {
        return false;
    }

    if (*end != '\0' && *end != ',') {
        return false;
    }

    *seq_out = (uint16_t)value;
    return true;
}

static void ch583_wifi_handle_reply_status(const ch583_wifi_frame_t *frame)
{
    uint16_t reply_seq = 0;

    if (frame == NULL || frame->arg == NULL) {
        return;
    }

    if (!ch583_wifi_parse_reply_seq_arg(frame->arg, &reply_seq)) {
        return;
    }

    if (s_tx_mutex == NULL || xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return;
    }

    ch583_wifi_pending_tx_t *pending = ch583_wifi_find_pending_tx_locked(reply_seq);
    if (pending == NULL) {
        xSemaphoreGive(s_tx_mutex);
        return;
    }

    if (strcmp(frame->cmd, "ERR") == 0 && strstr(frame->arg, "BAD_CRC") != NULL) {
        (void)ch583_wifi_retry_pending_frame_locked(pending);
        xSemaphoreGive(s_tx_mutex);
        return;
    }

    // Any non-BAD_CRC reply completes the matching pending frame only.
    pending->valid = false;
    xSemaphoreGive(s_tx_mutex);
}

static void ch583_wifi_check_rx_seq_gap(const ch583_wifi_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }

    if (s_rx_seq_seen) {
        uint16_t expected_seq = (uint16_t)(s_last_rx_seq + 1U);
        if (frame->seq != expected_seq) {
            uint16_t lost_count = (uint16_t)(frame->seq - expected_seq);
            UserDebugOutput_Printf("E ch583_proto: CH583 RX seq gap last=%u expected=%u now=%u lost=%u cmd=%s\r\n",
                                   (unsigned int)s_last_rx_seq,
                                   (unsigned int)expected_seq,
                                   (unsigned int)frame->seq,
                                   (unsigned int)lost_count,
                                   frame->cmd);
        }
    }

    s_last_rx_seq = frame->seq;
    s_rx_seq_seen = true;
}

static bool ch583_wifi_parse_frame(char *body, ch583_wifi_frame_t *frame, uint16_t *crc_received, const char **error_reason)
{
    char *crc_pos = NULL;
    char *save = NULL;
    char *field = NULL;
    char *fields[8] = {0};
    int field_count = 0;
    char *end = NULL;
    unsigned long crc_value = 0;
    uint16_t crc_calc = 0;
    size_t crc_input_len = 0;

    if (body == NULL || frame == NULL || crc_received == NULL) {
        if (error_reason != NULL) {
            *error_reason = "BAD_FORMAT";
        }
        return false;
    }

    memset(frame, 0, sizeof(*frame));
    crc_pos = strstr(body, "|CRC=");
    if (crc_pos == NULL) {
        CH583_WIFI_DEBUG_PRINTF("CH583_PROTO bad format no CRC body=%s\r\n", body);
        if (error_reason != NULL) {
            *error_reason = "BAD_FORMAT";
        }
        return false;
    }

    crc_input_len = (size_t)(crc_pos - body);
    crc_value = strtoul(crc_pos + 5, &end, 16);
    if (end == crc_pos + 5 || *end != '\0' || crc_value > 0xFFFFUL) {
        CH583_WIFI_DEBUG_PRINTF("CH583_PROTO bad format crc text=%s\r\n", crc_pos + 5);
        if (error_reason != NULL) {
            *error_reason = "BAD_FORMAT";
        }
        return false;
    }

    crc_calc = ch583_wifi_crc16_ccitt_false(body, crc_input_len);
    *crc_received = (uint16_t)crc_value;
    if (crc_calc != *crc_received) {
        CH583_WIFI_DEBUG_PRINTF("CH583_PROTO bad crc calc=%04X recv=%04X body=%.*s\r\n",
               crc_calc,
               *crc_received,
               (int)crc_input_len,
               body);
        if (error_reason != NULL) {
            *error_reason = "BAD_CRC";
        }
        return false;
    }

    *crc_pos = '\0';
    field = strtok_r(body, "|", &save);
    while (field != NULL && field_count < (int)(sizeof(fields) / sizeof(fields[0]))) {
        fields[field_count++] = field;
        field = strtok_r(NULL, "|", &save);
    }

    if (field_count != 7 || strcmp(fields[0], "V1") != 0 ||
        !ch583_wifi_parse_u16_field(fields[1], "SEQ=", &frame->seq) ||
        strncmp(fields[2], "CMD=", 4) != 0 ||
        !ch583_wifi_parse_size_field(fields[3], "LEN=", &frame->arg_len) ||
        !ch583_wifi_parse_u16_field(fields[4], "PART=", &frame->part) ||
        !ch583_wifi_parse_u16_field(fields[5], "TOTAL=", &frame->total) ||
        strncmp(fields[6], "ARG=", 4) != 0) {
        CH583_WIFI_DEBUG_PRINTF("CH583_PROTO bad format fields=%d\r\n", field_count);
        if (error_reason != NULL) {
            *error_reason = "BAD_FORMAT";
        }
        return false;
    }

    snprintf(frame->cmd, sizeof(frame->cmd), "%s", fields[2] + 4);
    frame->arg = fields[6] + 4;
    if (error_reason != NULL) {
        *error_reason = NULL;
    }
    return true;
}

static bool ch583_wifi_validate_len_and_part(const ch583_wifi_frame_t *frame)
{
    size_t real_arg_len = 0;

    if (frame == NULL || frame->arg == NULL) {
        return false;
    }

    real_arg_len = strlen(frame->arg);
    if (frame->arg_len != real_arg_len || frame->arg_len > CH583_WIFI_MAX_ARG_LEN) {
        CH583_WIFI_DEBUG_PRINTF("CH583_PROTO bad len seq=%u cmd=%s len=%u real=%u\r\n",
               (unsigned int)frame->seq,
               frame->cmd,
               (unsigned int)frame->arg_len,
               (unsigned int)real_arg_len);
        ch583_wifi_send_err(frame->seq, "BAD_LEN");
        return false;
    }

    if (frame->part == 0 || frame->total == 0 || frame->part > frame->total ||
        (frame->total == 1 && frame->part != 1)) {
        CH583_WIFI_DEBUG_PRINTF("CH583_PROTO bad part seq=%u cmd=%s part=%u total=%u\r\n",
               (unsigned int)frame->seq,
               frame->cmd,
               (unsigned int)frame->part,
               (unsigned int)frame->total);
        ch583_wifi_send_err(frame->seq, "BAD_PART");
        return false;
    }

    if (frame->total > 1 && strcmp(frame->cmd, "BLE_DATA") != 0) {
        CH583_WIFI_DEBUG_PRINTF("CH583_PROTO bad part seq=%u cmd=%s total=%u non-ble split\r\n",
               (unsigned int)frame->seq,
               frame->cmd,
               (unsigned int)frame->total);
        ch583_wifi_send_err(frame->seq, "BAD_PART");
        return false;
    }

    return true;
}

static void ch583_wifi_reset_ble_join(void)
{
    if (s_ble_activity_active) {
        UserLedStatus_ActivityEnd(USER_LED_ACTIVITY_UART_RX);
        s_ble_activity_active = false;
    }
    s_ble_join_active = false;
    s_ble_expected_part = 0;
    s_ble_total = 0;
    s_ble_len = 0;
}

static bool ch583_wifi_handle_ble_data(const ch583_wifi_frame_t *frame,
                                       ch583_wifi_ble_data_callback_t ble_data_callback)
{
    if (frame == NULL) {
        return false;
    }

    if (frame->total == 1) {
        bool activity_active = frame->arg_len >= USER_LED_UART_LARGE_DATA_THRESHOLD;
        CH583_WIFI_DEBUG_PRINTF("CH583_PROTO ble single seq=%u len=%u data=%s\r\n",
               (unsigned int)frame->seq,
               (unsigned int)frame->arg_len,
               frame->arg);
        ServerNetworkStaWifiWorkTime_OnCh583Activity();
        ch583_wifi_send_ack(frame->seq);
        if (activity_active) {
            UserLedStatus_ActivityBegin(USER_LED_ACTIVITY_UART_RX);
        }
        if (ble_data_callback != NULL) {
            // Pass only ARG Data to the WiFi JSON handler, not the whole UART protocol frame.
            ble_data_callback(frame->arg);
        }
        if (activity_active) {
            UserLedStatus_ActivityEnd(USER_LED_ACTIVITY_UART_RX);
        }
        return true;
    }

    if (frame->part == 1) {
        ch583_wifi_reset_ble_join();
        s_ble_join_active = true;
        s_ble_activity_active = true;
        UserLedStatus_ActivityBegin(USER_LED_ACTIVITY_UART_RX);
        s_ble_expected_part = 1;
        s_ble_total = frame->total;
        CH583_WIFI_DEBUG_PRINTF("CH583_PROTO ble join start seq=%u total=%u\r\n",
               (unsigned int)frame->seq,
               (unsigned int)frame->total);
    }

    if (!s_ble_join_active || frame->total != s_ble_total || frame->part != s_ble_expected_part) {
        CH583_WIFI_DEBUG_PRINTF("CH583_PROTO ble join bad part seq=%u part=%u expected=%u total=%u cached_total=%u\r\n",
               (unsigned int)frame->seq,
               (unsigned int)frame->part,
               (unsigned int)s_ble_expected_part,
               (unsigned int)frame->total,
               (unsigned int)s_ble_total);
        ch583_wifi_reset_ble_join();
        ch583_wifi_send_err(frame->seq, "BAD_PART");
        return false;
    }

    if (s_ble_len + frame->arg_len > CH583_WIFI_MAX_BLE_MESSAGE_LEN) {
        CH583_WIFI_DEBUG_PRINTF("CH583_PROTO ble join overflow seq=%u cached=%u add=%u max=%u\r\n",
               (unsigned int)frame->seq,
               (unsigned int)s_ble_len,
               (unsigned int)frame->arg_len,
               (unsigned int)sizeof(s_ble_buf));
        ch583_wifi_reset_ble_join();
        ch583_wifi_send_err(frame->seq, "BAD_LEN");
        return false;
    }

    ServerNetworkStaWifiWorkTime_OnCh583Activity();
    memcpy(&s_ble_buf[s_ble_len], frame->arg, frame->arg_len);
    s_ble_len += frame->arg_len;
    s_ble_buf[s_ble_len] = '\0';
    s_ble_expected_part++;
    ch583_wifi_send_ack(frame->seq);

    if (frame->part == frame->total) {
        CH583_WIFI_DEBUG_PRINTF("CH583_PROTO ble join done seq=%u total_len=%u\r\n",
               (unsigned int)frame->seq,
               (unsigned int)s_ble_len);
        if (ble_data_callback != NULL) {
            // Pass the reassembled ARG Data as one string so upper JSON logic sees the original BLE write.
            ble_data_callback(s_ble_buf);
        }
        ch583_wifi_reset_ble_join();
    }
    return true;
}

static void ch583_wifi_handle_device_info(const ch583_wifi_frame_t *frame)
{
    ch583_wifi_device_info_t device_info = {0};
    ch583_wifi_device_info_parse_result_t parse_result;
    uint8_t saved_epd_type = USER_EPD_TYPE_DEFAULT;
    bool epd_saved_changed = false;
    bool was_received = s_device_info_received;
    esp_err_t ret;

    if (frame == NULL) {
        return;
    }

    if (frame->part != 1 || frame->total != 1) {
        ESP_LOGE(TAG, "DEVICE_INFO bad part seq=%u part=%u total=%u",
                 (unsigned int)frame->seq,
                 (unsigned int)frame->part,
                 (unsigned int)frame->total);
        ch583_wifi_send_err(frame->seq, "BAD_PART");
        return;
    }

    parse_result = ch583_wifi_parse_device_info_arg(frame, &device_info);
    if (parse_result == CH583_DEVICE_INFO_PARSE_UNSUPPORTED_EPD) {
        ESP_LOGE(TAG,
                 "DEVICE_INFO unsupported EPD mapping seq=%u screen_type=%c board_info_hex=%02X",
                 (unsigned int)frame->seq,
                 device_info.screen_type,
                 (unsigned int)device_info.board_info);
        ch583_wifi_send_err(frame->seq, "BAD_ARG");
        return;
    }
    if (parse_result != CH583_DEVICE_INFO_PARSE_OK) {
        ESP_LOGE(TAG, "DEVICE_INFO bad arg seq=%u len=%u arg=%s",
                 (unsigned int)frame->seq,
                 (unsigned int)frame->arg_len,
                 frame->arg);
        ch583_wifi_send_err(frame->seq, "BAD_ARG");
        return;
    }

    ret = EpdType_LoadSavedOrDefault();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DEVICE_INFO load EPD type failed seq=%u ret=%s",
                 (unsigned int)frame->seq, esp_err_to_name(ret));
        ch583_wifi_send_err(frame->seq, CH583_DEVICE_INFO_ERR_SAVE_FAILED);
        return;
    }

    // Keep the active driver selected from startup NVS (or its validated default).
    // DEVICE_INFO may arrive during a display, so only persist it for the next boot.
    ret = EpdType_SaveForNextBoot(device_info.epd_type, &epd_saved_changed);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DEVICE_INFO save EPD type=%u failed seq=%u ret=%s",
                 (unsigned int)device_info.epd_type,
                 (unsigned int)frame->seq,
                 esp_err_to_name(ret));
        ch583_wifi_send_err(frame->seq, CH583_DEVICE_INFO_ERR_SAVE_FAILED);
        return;
    }

    // Verify NVS after an unchanged RAM value so a previous failed write cannot be acknowledged.
    ret = app_nvs_read_u8(USER_EPD_TYPE_NVS_KEY, &saved_epd_type, USER_EPD_TYPE_DEFAULT);
    if (ret != ESP_OK || saved_epd_type != device_info.epd_type) {
        ret = app_nvs_write_u8(USER_EPD_TYPE_NVS_KEY, device_info.epd_type);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "DEVICE_INFO verify EPD type=%u failed seq=%u ret=%s",
                     (unsigned int)device_info.epd_type,
                     (unsigned int)frame->seq,
                     esp_err_to_name(ret));
            ch583_wifi_send_err(frame->seq, CH583_DEVICE_INFO_ERR_SAVE_FAILED);
            return;
        }
    }

    if (strcmp(s_ble_mac, device_info.mac) != 0) {
        ret = app_nvs_write_str(CH583_DEVICE_INFO_MAC_NVS_KEY, device_info.mac);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "DEVICE_INFO save MAC failed seq=%u ret=%s",
                     (unsigned int)frame->seq, esp_err_to_name(ret));
            ch583_wifi_send_err(frame->seq, CH583_DEVICE_INFO_ERR_SAVE_FAILED);
            return;
        }
    }
    if (!s_ble_ver_nvs_valid || s_ble_ver != device_info.ble_ver) {
        ret = app_nvs_write_u8(CH583_DEVICE_INFO_BLE_VER_NVS_KEY, device_info.ble_ver);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "DEVICE_INFO save BLE version=%u failed seq=%u ret=%s",
                     (unsigned int)device_info.ble_ver,
                     (unsigned int)frame->seq,
                     esp_err_to_name(ret));
            ch583_wifi_send_err(frame->seq, CH583_DEVICE_INFO_ERR_SAVE_FAILED);
            return;
        }
        s_ble_ver_nvs_valid = true;
    }

    memcpy(s_ble_mac, device_info.mac, CH583_DEVICE_INFO_MAC_HEX_LEN + 1);
    s_ble_ver = device_info.ble_ver;
    ServerNetworkStaWifiWorkTime_OnCh583Activity();

    if (ch583_wifi_send_ack(frame->seq) != 0) {
        ESP_LOGE(TAG, "DEVICE_INFO ACK send failed seq=%u", (unsigned int)frame->seq);
        return;
    }

    s_device_info_received = true;
    if (was_received) {
        ESP_LOGW(TAG, "DEVICE_INFO repeated seq=%u, ACK sent again", (unsigned int)frame->seq);
    } else {
        ESP_LOGI(TAG,
                 "DEVICE_INFO synced seq=%u MAC=%s BLE version=%u screen=%c board=%02X reported_epd=%u active_epd=%u saved_changed=%d",
                 (unsigned int)frame->seq,
                 s_ble_mac,
                 (unsigned int)s_ble_ver,
                 device_info.screen_type,
                 (unsigned int)device_info.board_info,
                 (unsigned int)device_info.epd_type,
                 (unsigned int)EPD_type,
                 epd_saved_changed ? 1 : 0);
    }

    if (ch583_wifi_uart_send_wifi_ver(ch583_wifi_get_current_wifi_ver()) != 0) {
        ESP_LOGE(TAG, "DEVICE_INFO WIFI_VER send failed seq=%u", (unsigned int)frame->seq);
    }
    if (ch583_wifi_uart_send_current_wifi_provision_status() != 0) {
        ESP_LOGE(TAG, "DEVICE_INFO WIFI_PROVISION replay failed seq=%u",
                 (unsigned int)frame->seq);
    }
    if (!was_received) {
        esp_err_t led_ret = UserLedStatus_ReapplyCurrent();
        if (led_ret != ESP_OK) {
            ESP_LOGE(TAG, "DEVICE_INFO LED reapply request failed seq=%u ret=%s",
                     (unsigned int)frame->seq, esp_err_to_name(led_ret));
        }
        if (ServerNetworkStaTime_IsReliableForRtcRestore()) {
            esp_err_t time_ret =
                ServerNetworkStaTime_BackupCurrentToCh583("device_info_synced");
            if (time_ret != ESP_OK) {
                ESP_LOGE(TAG, "DEVICE_INFO TIME_SET replay failed seq=%u ret=%s",
                         (unsigned int)frame->seq, esp_err_to_name(time_ret));
            }
        }
    }
}

static void ch583_wifi_handle_frame_body(const char *body, ch583_wifi_ble_data_callback_t ble_data_callback)
{
    char parse_buf[CH583_WIFI_MAX_FRAME_BODY_LEN + 1];
    ch583_wifi_frame_t frame;
    uint16_t crc_received = 0;
    uint16_t error_seq = ch583_wifi_find_seq_for_error(body);
    const char *error_reason = NULL;

    snprintf(parse_buf, sizeof(parse_buf), "%s", body);
    if (!ch583_wifi_parse_frame(parse_buf, &frame, &crc_received, &error_reason)) {
        ch583_wifi_send_err(error_seq, error_reason);
        return;
    }

    CH583_WIFI_DEBUG_PRINTF("CH583_PROTO rx seq=%u cmd=%s len=%u part=%u total=%u crc=%04X arg=%s\r\n",
           (unsigned int)frame.seq,
           frame.cmd,
           (unsigned int)frame.arg_len,
           (unsigned int)frame.part,
           (unsigned int)frame.total,
           crc_received,
           frame.arg);

    // WiFi -> CH583: seq=9 cmd=PING arg=24  // 除了心跳，其他都打印
    if (!ch583_wifi_validate_len_and_part(&frame)) {
        return;
    }
    ch583_wifi_check_rx_seq_gap(&frame);
    if (strcmp(frame.cmd, CH583_DEVICE_INFO_CMD) == 0) {
        ch583_wifi_handle_device_info(&frame);
    } else if (strcmp(frame.cmd, "PING") == 0) {
        char arg[8];
        snprintf(arg, sizeof(arg), "%u", (unsigned int)frame.seq);
        if (ch583_wifi_send_frame("PONG", arg, 0) != 0) {
            ESP_LOGE(TAG, "PONG send failed seq=%u", (unsigned int)frame.seq);
        }
#if CH583_WIFI_VER_COMPAT_ON_PING_ENABLE
        if (!s_device_info_received && !s_compat_wifi_ver_sent) {
            if (ch583_wifi_uart_send_wifi_ver(ch583_wifi_get_current_wifi_ver()) == 0) {
                s_compat_wifi_ver_sent = true;
                ESP_LOGI(TAG, "WIFI_VER sent for continued CH583 session before DEVICE_INFO");
            } else {
                ESP_LOGE(TAG, "WIFI_VER compatibility send failed before DEVICE_INFO");
            }
        }
#endif

       // CH583_WIFI_DIRECTION_PRINTF("CH583 -> WiFi: seq=%u cmd=%s arg=%s\r\n",
       //      (unsigned int)frame.seq,frame.cmd,frame.arg);
    } else if (strcmp(frame.cmd, "BLE_DATA") == 0) {
        // Only a validated BLE_DATA frame received from CH583 may interrupt the rail-off test.
        EpdSdPowerTest_OnCh583BleDataReceived();
        CH583_WIFI_DIRECTION_PRINTF("CH583 -> WiFi: seq=%u cmd=%s arg=%s\r\n",
           (unsigned int)frame.seq,frame.cmd,frame.arg);
        if (ch583_wifi_handle_ble_data(&frame, ble_data_callback)) {
            // Preserve the original full work timer reset only for accepted BLE data.
            ServerNetworkStaWifiWorkTime_OnNetworkData();
        }
    } else if (strcmp(frame.cmd, "ACK") == 0 ||
               strcmp(frame.cmd, "ERR") == 0 ||
               strcmp(frame.cmd, "PONG") == 0 ||
               strcmp(frame.cmd, "GPIO_VALUE") == 0 ||
               strcmp(frame.cmd, "TIME_STATUS") == 0 ||
               strcmp(frame.cmd, "NFC_STATUS") == 0) {

      CH583_WIFI_DIRECTION_PRINTF("CH583 -> WiFi: seq=%u cmd=%s arg=%s\r\n",
           (unsigned int)frame.seq,frame.cmd,frame.arg);

        if (strcmp(frame.cmd, "TIME_STATUS") == 0) {
            ServerNetworkStaTime_OnCh583TimeStatus(frame.arg);
        }
        ch583_wifi_handle_reply_status(&frame);
        CH583_WIFI_DEBUG_PRINTF("CH583_PROTO status cmd=%s arg=%s\r\n", frame.cmd, frame.arg);
    } else {
        ESP_LOGE(TAG, "unsupported cmd=%s seq=%u", frame.cmd, (unsigned int)frame.seq);
        ch583_wifi_send_err(frame.seq, "BAD_CMD");
    }
}

void ch583_wifi_uart_process_bytes(const uint8_t *data, size_t len, ch583_wifi_ble_data_callback_t ble_data_callback)
{
    if (data == NULL || len == 0) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];

        if (!s_in_frame) {
            if (s_wait_frame_start_hash) {
                if (byte == '#') {
                    s_in_frame = true;
                    s_frame_body_len = 0;
                    CH583_WIFI_DEBUG_PRINTF("CH583_PROTO frame start\r\n");
                }
                s_wait_frame_start_hash = false;
            } else if (byte == '@') {
                s_wait_frame_start_hash = true;
            }
            continue;
        }

        if (byte == '&' && s_frame_body_len > 0 && s_frame_body[s_frame_body_len - 1] == '^') {
            s_frame_body_len--;
            s_frame_body[s_frame_body_len] = '\0';
            CH583_WIFI_DEBUG_PRINTF("CH583_PROTO frame end body_len=%u body=%s\r\n", (unsigned int)s_frame_body_len, s_frame_body);
            ch583_wifi_handle_frame_body(s_frame_body, ble_data_callback);
            s_in_frame = false;
            s_frame_body_len = 0;
            continue;
        }

        if (s_frame_body_len >= CH583_WIFI_MAX_FRAME_BODY_LEN) {
            CH583_WIFI_DEBUG_PRINTF("CH583_PROTO frame overflow max=%u\r\n", (unsigned int)CH583_WIFI_MAX_FRAME_BODY_LEN);
            s_in_frame = false;
            s_frame_body_len = 0;
            ch583_wifi_send_err(0, "BAD_FORMAT");
            continue;
        }

        s_frame_body[s_frame_body_len++] = (char)byte;
        s_frame_body[s_frame_body_len] = '\0';
    }
}

int ch583_wifi_uart_send_wifi_data(const char *message)
{
    size_t len = 0;

    if (message == NULL) {
        return -1;
    }

    len = strlen(message);
    if (len > CH583_WIFI_MAX_WIFI_DATA_LEN) {
        UserDebugOutput_Printf("CH583_PROTO WIFI_DATA too long len=%u max=%u\r\n",
               (unsigned int)len,
               (unsigned int)CH583_WIFI_MAX_WIFI_DATA_LEN);
        return -1;
    }
    bool activity_active = len >= USER_LED_UART_LARGE_DATA_THRESHOLD;
    if (activity_active) {
        UserLedStatus_ActivityBegin(USER_LED_ACTIVITY_UART_TX);
    }
    // Send WiFi-to-frontend data as one WIFI_DATA frame.
    int ret = ch583_wifi_send_frame("WIFI_DATA", message,1);
    if (activity_active) {
        UserLedStatus_ActivityEnd(USER_LED_ACTIVITY_UART_TX);
    }
    return ret;
}

int ch583_wifi_uart_send_wifi_provision_status(uint8_t status)
{
    if (status != 0U && status != 1U) {
        UserDebugOutput_Printf("CH583_PROTO WIFI_PROVISION reject bad status=%u\r\n",
                               (unsigned int)status);
        return -1;
    }

    s_wifi_provision_status = status;
    s_wifi_provision_status_valid = true;

    uint8_t mode = EpdDisplayMode_Get();
    if (mode > USER_EPD_DISPLAY_MODE_DAILY) {
        mode = USER_EPD_DISPLAY_MODE_NORMAL;
    }
    uint8_t provision_nibble = (status == 1U) ? 0x5U : 0x4U;
    uint8_t combined_status = (uint8_t)((provision_nibble << 4) | (mode & 0x0FU));
    char status_hex[3];
    snprintf(status_hex, sizeof(status_hex), "%02X", (unsigned int)combined_status);

    int ret = ch583_wifi_send_frame("WIFI_PROVISION", status_hex, 1);
    UserDebugOutput_Printf("CH583_PROTO WIFI_PROVISION provision=%u mode=%u combined=0x%02X arg=%s send_ret=%d\r\n",
                           (unsigned int)status,
                           (unsigned int)mode,
                           (unsigned int)combined_status,
                           status_hex,
                           ret);
    return ret;
}

int ch583_wifi_uart_send_current_wifi_provision_status(void)
{
    if (!s_wifi_provision_status_valid) {
        s_wifi_provision_status = 0U;
        s_wifi_provision_status_valid = true;
    }
    return ch583_wifi_uart_send_wifi_provision_status(s_wifi_provision_status);
}

int ch583_wifi_uart_send_wifi_provision_before_power_off(bool slideshow_enabled)
{
    // This is a one-shot CH583 notification. Do not update the cached provision
    // status or persistent EPD mode; 0xF only means that this shutdown enters standby.
    uint8_t provision_status = s_wifi_provision_status_valid ? s_wifi_provision_status : 0U;
    uint8_t provision_nibble = (provision_status == 1U) ? 0x5U : 0x4U;
    uint8_t mode = slideshow_enabled ? USER_EPD_DISPLAY_MODE_SLIDESHOW :
                                       CH583_WIFI_PROVISION_MODE_STANDBY;
    uint8_t combined_status = (uint8_t)((provision_nibble << 4) | (mode & 0x0FU));
    char status_hex[3];

    snprintf(status_hex, sizeof(status_hex), "%02X", (unsigned int)combined_status);
    return ch583_wifi_send_frame("WIFI_PROVISION", status_hex, 1);
}

const char *ch583_wifi_uart_get_ble_mac(void)
{
    ch583_wifi_load_device_info_from_nvs();
    return s_ble_mac[0] != '\0' ? s_ble_mac : NULL;
}

uint8_t ch583_wifi_uart_get_ble_ver(void)
{
    ch583_wifi_load_device_info_from_nvs();
    return s_ble_ver;
}

int ch583_wifi_uart_send_wifi_ver(uint16_t wifi_ver)
{
    char arg[8];

    snprintf(arg, sizeof(arg), "%u", (unsigned int)wifi_ver);
    return ch583_wifi_send_frame("WIFI_VER", arg, 1);
}

int ch583_wifi_uart_send_wake_timer_on(uint32_t seconds)
{
    char arg[24];

    if (seconds < CH583_WAKE_TIMER_MIN_SECONDS ||
        seconds > CH583_WAKE_TIMER_MAX_SECONDS) {
        return -1;
    }

    snprintf(arg, sizeof(arg), "ON,%lu", (unsigned long)seconds);
    return ch583_wifi_send_frame("WAKE_TIMER", arg, 1);
}

int ch583_wifi_uart_send_wake_timer_off(void)
{
    return ch583_wifi_send_frame("WAKE_TIMER", "OFF,0", 1);
}

int ch583_wifi_uart_send_nfc_set_json(const char *json)
{
    char encoded[CH583_WIFI_NFC_BASE64URL_MAX_LEN + 1];
    size_t json_len;
    int encoded_len;

    if (json == NULL) {
        return -1;
    }

    json_len = strlen(json);
    if (json_len == 0 || json_len > CH583_WIFI_NFC_JSON_MAX_LEN) {
        UserDebugOutput_Printf("CH583_PROTO NFC_SET reject json_len=%u max=%u\r\n",
                               (unsigned int)json_len,
                               (unsigned int)CH583_WIFI_NFC_JSON_MAX_LEN);
        return -1;
    }

    encoded_len = ch583_wifi_base64url_encode((const uint8_t *)json,
                                              json_len,
                                              encoded,
                                              sizeof(encoded));
    if (encoded_len <= 0 || encoded_len > CH583_WIFI_MAX_ARG_LEN) {
        UserDebugOutput_Printf("CH583_PROTO NFC_SET encode failed json_len=%u encoded_len=%d max_arg=%u\r\n",
                               (unsigned int)json_len,
                               encoded_len,
                               (unsigned int)CH583_WIFI_MAX_ARG_LEN);
        return -1;
    }

    return ch583_wifi_send_frame("NFC_SET", encoded, 1);
}

int ch583_wifi_uart_send_nfc_clear(void)
{
    return ch583_wifi_send_frame("NFC_CLEAR", "", 1);
}

int ch583_wifi_uart_send_nfc_status(void)
{
    return ch583_wifi_send_frame("NFC_STATUS", "", 1);
}

int ch583_wifi_uart_send_time_get(void)
{
    return ch583_wifi_send_frame("TIME_GET", "", 1);
}

int ch583_wifi_uart_send_time_set(const char *beijing_time)
{
    if (beijing_time == NULL || strlen(beijing_time) != 19U) {
        UserDebugOutput_Printf("CH583_PROTO TIME_SET reject arg=%s\r\n",
                               beijing_time != NULL ? beijing_time : "(null)");
        return -1;
    }
    return ch583_wifi_send_frame("TIME_SET", beijing_time, 1);
}

bool ch583_wifi_uart_test_nfc_step(void)
{
#if CH583_WIFI_NFC_TEST_ENABLE
    static uint8_t step = 0;
    static const char test_json[] =
        "{\"daa\":\"asdasefasdfe\",\"ddfe\":\"elkdj\",\"nfc\":\"lkkdy\",\"iuy\":\"kdkdd\"}";

    switch (step) {
    case 0:
        step++;
        UserDebugOutput_Printf("CH583_PROTO NFC test step=1 NFC_SET\r\n");
        (void)ch583_wifi_uart_send_nfc_set_json(test_json);
        return false;
    case 1:
        step++;
        UserDebugOutput_Printf("CH583_PROTO NFC test step=2 NFC_STATUS\r\n");
        (void)ch583_wifi_uart_send_nfc_status();
        return false;
    case 2:
        step++;
        //UserDebugOutput_Printf("CH583_PROTO NFC test step=3 NFC_CLEAR\r\n");
        //(void)ch583_wifi_uart_send_nfc_clear();
        return false;
    case 3:
        step++;
        UserDebugOutput_Printf("CH583_PROTO NFC test step=4 NFC_STATUS\r\n");
        (void)ch583_wifi_uart_send_nfc_status();
        return true;
    default:
        return true;
    }
#else
    return true;
#endif
}

int ch583_wifi_uart_send_power_off(void)
{    
    return ch583_wifi_send_frame("POWER_OFF", "",1);
}

int ch583_wifi_uart_send_gpio(const char *port, int pin, const char *mode, const char *level)
{
    char arg[32];

    if (port == NULL || mode == NULL || level == NULL) {
        return -1;
    }

    snprintf(arg, sizeof(arg), "%s,%d,%s,%s", port, pin, mode, level);
    return ch583_wifi_send_frame("GPIO", arg,1);
}

int ch583_wifi_uart_send_gpio_read(const char *port, int pin)
{
    char arg[16];

    if (port == NULL) {
        return -1;
    }

    snprintf(arg, sizeof(arg), "%s,%d", port, pin);
    return ch583_wifi_send_frame("GPIO_READ", arg,1);
}

static bool ch583_wifi_led_name_is_valid(const char *led)
{
    return led != NULL &&
           (strcmp(led, "RED") == 0 || strcmp(led, "GREEN") == 0);
}

int ch583_wifi_uart_send_led_blink(const char *led, uint32_t interval_ms)
{
    char arg[32];

    if (!ch583_wifi_led_name_is_valid(led) || interval_ms < 1U || interval_ms > 10000U) {
        return -1;
    }

    snprintf(arg, sizeof(arg), "%s,%lu", led, (unsigned long)interval_ms);
    return ch583_wifi_send_frame("LED_BLINK", arg, 1);
}

int ch583_wifi_uart_send_led_blink_stop(const char *led)
{
    if (!ch583_wifi_led_name_is_valid(led)) {
        return -1;
    }

    return ch583_wifi_send_frame("LED_BLINK_STOP", led, 1);
}

int ch583_wifi_uart_test_gpio_pa1_high(void)
{    // Send the fixed GPIO test command through the same V1 frame builder used by real protocol replies.
    // 通过 V1 帧构造器发送固定 GPIO 测试命令，避免测试口绕过 CH583 协议输出。
    static uint8_t u8dat=0;

    u8dat++;
    if(u8dat ==1)        return ch583_wifi_uart_send_gpio("PB", 5, "OUT", "HIGH");
    else if(u8dat ==2)            return ch583_wifi_uart_send_gpio("PB", 5, "OUT", "LOW");
    else if(u8dat ==3)            return ch583_wifi_uart_send_gpio("PB", 6, "OUT", "HIGH");
    else if(u8dat ==4)            return ch583_wifi_uart_send_gpio("PB", 6, "OUT", "LOW");
    else  u8dat=0;

    return ch583_wifi_uart_send_gpio("PB", 5, "OUT", "HIGH");
}
