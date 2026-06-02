// EPD_Vcc:PA.6  // if 1 then open ,if 0 then 0
// VBAT_Vcc:PB.3 // if 1 then open ,if 0 then 0


#include "ch583_wifi_uart_protocol.h"
#include "tdx_cfg.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <driver/uart.h>

#define CH583_WIFI_MAX_ARG_LEN 300
#define CH583_WIFI_MAX_WIFI_DATA_LEN 256
#define CH583_WIFI_BLE_MAC_LEN 12
#define CH583_WIFI_MAX_FRAME_BODY_LEN 448
#define CH583_WIFI_MAX_BLE_MESSAGE_LEN 2048

#ifndef CH583_WIFI_UART_PORT
#define CH583_WIFI_UART_PORT UART_NUM_0
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

#if CH583_WIFI_UART_DEBUG_PRINT_ENABLE
#define CH583_WIFI_DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
#define CH583_WIFI_DEBUG_PRINTF(...) do { } while (0)
#endif

#if CH583_WIFI_UART_DIRECTION_PRINT_ENABLE
#define CH583_WIFI_DIRECTION_PRINTF(...) printf(__VA_ARGS__)
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

// Keep the TX sequence independent because each sender owns its own SEQ counter.
static uint16_t s_tx_seq;
static SemaphoreHandle_t s_tx_mutex;

static char s_last_tx_frame[CH583_WIFI_MAX_FRAME_BODY_LEN + 24];
static size_t s_last_tx_frame_len;
static uint16_t s_last_tx_seq;
static uint8_t s_last_tx_bad_crc_retry_count;
static bool s_last_tx_retry_valid;

// Cache partial UART bytes so split frames can still be parsed.
static bool s_in_frame;
static bool s_wait_frame_start_hash;
static char s_frame_body[CH583_WIFI_MAX_FRAME_BODY_LEN + 1];
static size_t s_frame_body_len;

// Keep only one BLE_DATA split message because V1 does not allow interleaved transfers.
static bool s_ble_join_active;
static uint16_t s_ble_expected_part;
static uint16_t s_ble_total;
static size_t s_ble_len;
static char s_ble_buf[CH583_WIFI_MAX_BLE_MESSAGE_LEN + 1];
static char s_ble_mac[CH583_WIFI_BLE_MAC_LEN + 1];
static bool s_ble_mac_loaded;

static bool ch583_wifi_is_upper_hex_string(const char *text, size_t len);

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

static void ch583_wifi_load_ble_mac_from_nvs(void)
{
    char saved_mac[CH583_WIFI_BLE_MAC_LEN + 1] = {0};

    if (s_ble_mac_loaded) {
        return;
    }
    s_ble_mac_loaded = true;

    (void)app_nvs_read_str(CH583_BLE_MAC_NVS_KEY, saved_mac, sizeof(saved_mac), "");
    if (strlen(saved_mac) == CH583_WIFI_BLE_MAC_LEN &&
        ch583_wifi_is_upper_hex_string(saved_mac, CH583_WIFI_BLE_MAC_LEN)) {
        memcpy(s_ble_mac, saved_mac, CH583_WIFI_BLE_MAC_LEN);
        s_ble_mac[CH583_WIFI_BLE_MAC_LEN] = '\0';
        printf("CH583_PROTO BLE_MAC load nvs=%s\r\n", s_ble_mac);
    }
}

static void ch583_wifi_tx_mutex_init(void)
{
    if (s_tx_mutex == NULL) {
        // Create the TX mutex lazily so every WiFi-to-CH583 protocol frame uses the same serialized path.
        s_tx_mutex = xSemaphoreCreateMutex();
    }
}

static int ch583_wifi_write_frame_text(const char *frame_text, size_t frame_len)
{
    int ret = -1;

    if (frame_text == NULL || frame_len == 0) {
        return -1;
    }

    ch583_wifi_tx_mutex_init();
    if (s_tx_mutex != NULL && xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        printf("CH583_PROTO tx mutex timeout\r\n");
        return -1;
    }

    vTaskDelay(pdMS_TO_TICKS(CH583_WIFI_UART_TX_SILENCE_MS));

    // Write one complete protocol frame through the UART driver so console logs are less likely to split it.
    ret = uart_write_bytes(CH583_WIFI_UART_PORT, frame_text, frame_len);

    // Wait until the frame leaves the UART TX FIFO before allowing later debug text to print.
    uart_wait_tx_done(CH583_WIFI_UART_PORT, pdMS_TO_TICKS(100));
    // vTaskDelay(pdMS_TO_TICKS(CH583_WIFI_UART_TX_SILENCE_MS));

    if (s_tx_mutex != NULL) {
        xSemaphoreGive(s_tx_mutex);
    }

    return ret;
}

static int ch583_wifi_retry_last_frame(void)
{
    if (!s_last_tx_retry_valid || s_last_tx_frame_len == 0) {
        return -1;
    }

    if (s_last_tx_bad_crc_retry_count >= CH583_WIFI_UART_BAD_CRC_RETRY_MAX) {
        printf("CH583_PROTO retry stop seq=%u count=%u\r\n",
               (unsigned int)s_last_tx_seq,
               (unsigned int)s_last_tx_bad_crc_retry_count);
        s_last_tx_retry_valid = false;
        return -1;
    }

    s_last_tx_bad_crc_retry_count++;
    printf("CH583_PROTO retry BAD_CRC seq=%u count=%u\r\n",
           (unsigned int)s_last_tx_seq,
           (unsigned int)s_last_tx_bad_crc_retry_count);
    return ch583_wifi_write_frame_text(s_last_tx_frame, s_last_tx_frame_len);
}

static int ch583_wifi_send_frame(const char *cmd, const char *arg)
{
    char body[CH583_WIFI_MAX_FRAME_BODY_LEN + 1];
    char frame_text[CH583_WIFI_MAX_FRAME_BODY_LEN + 24];
    uint16_t crc = 0;
    size_t arg_len = (arg != NULL) ? strlen(arg) : 0;
    int body_len = 0;
    int frame_len = 0;
    int ret = -1;
    uint16_t current_seq = s_tx_seq;

    if (cmd == NULL || arg_len > CH583_WIFI_MAX_ARG_LEN) {
        printf("CH583_PROTO tx reject cmd=%s arg_len=%u\r\n", cmd ? cmd : "NULL", (unsigned int)arg_len);
        return -1;
    }

    body_len = snprintf(body,
                        sizeof(body),
                        "V1|SEQ=%u|CMD=%s|LEN=%u|PART=1|TOTAL=1|ARG=%s",
                        (unsigned int)current_seq,
                        cmd,
                        (unsigned int)arg_len,
                        arg ? arg : "");
    if (body_len <= 0 || body_len >= (int)sizeof(body)) {
        printf("CH583_PROTO tx body overflow cmd=%s\r\n", cmd);
        return -1;
    }

    crc = ch583_wifi_crc16_ccitt_false(body, (size_t)body_len);
    //printf("CH583_PROTO tx seq=%u cmd=%s arg=%s crc=%04X\r\n", (unsigned int)s_tx_seq, cmd, arg ? arg : "", crc);

    frame_len = snprintf(frame_text, sizeof(frame_text), "@#%s|CRC=%04X^&\n\r", body, crc);
    if (frame_len <= 0 || frame_len >= (int)sizeof(frame_text)) {
        printf("CH583_PROTO tx frame overflow cmd=%s\r\n", cmd);
        return -1;
    }

    memcpy(s_last_tx_frame, frame_text, (size_t)frame_len + 1);
    s_last_tx_frame_len = (size_t)frame_len;
    s_last_tx_seq = current_seq;
    s_last_tx_bad_crc_retry_count = 0;
    s_last_tx_retry_valid = true;

    CH583_WIFI_DIRECTION_PRINTF("WiFi -> CH583: seq=%u cmd=%s arg=%s\r\n", (unsigned int)current_seq, cmd, arg ? arg : "");
    ret = ch583_wifi_write_frame_text(frame_text, (size_t)frame_len);
    s_tx_seq++;
    return ret;
}

static int ch583_wifi_send_ack(uint16_t received_seq)
{
    char arg[8];

    snprintf(arg, sizeof(arg), "%u", (unsigned int)received_seq);
    return ch583_wifi_send_frame("ACK", arg);
}

static int ch583_wifi_send_err(uint16_t received_seq, const char *reason)
{
    char arg[32];

    snprintf(arg, sizeof(arg), "%u,%s", (unsigned int)received_seq, reason ? reason : "BAD_FORMAT");
    return ch583_wifi_send_frame("ERR", arg);
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

    if (!s_last_tx_retry_valid || reply_seq != s_last_tx_seq) {
        return;
    }

    if (strcmp(frame->cmd, "ERR") == 0 && strstr(frame->arg, "BAD_CRC") != NULL) {
        ch583_wifi_retry_last_frame();
        return;
    }

    // Any non-BAD_CRC reply for the cached frame means this send attempt is finished.
    s_last_tx_retry_valid = false;
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
    s_ble_join_active = false;
    s_ble_expected_part = 0;
    s_ble_total = 0;
    s_ble_len = 0;
}

static void ch583_wifi_handle_ble_data(const ch583_wifi_frame_t *frame, ch583_wifi_ble_data_callback_t ble_data_callback)
{
    if (frame->total == 1) {
        CH583_WIFI_DEBUG_PRINTF("CH583_PROTO ble single seq=%u len=%u data=%s\r\n",
               (unsigned int)frame->seq,
               (unsigned int)frame->arg_len,
               frame->arg);
        ch583_wifi_send_ack(frame->seq);
        if (ble_data_callback != NULL) {
            // Pass only ARG Data to the WiFi JSON handler, not the whole UART protocol frame.
            ble_data_callback(frame->arg);
        }
        return;
    }

    if (frame->part == 1) {
        ch583_wifi_reset_ble_join();
        s_ble_join_active = true;
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
        return;
    }

    if (s_ble_len + frame->arg_len > CH583_WIFI_MAX_BLE_MESSAGE_LEN) {
        CH583_WIFI_DEBUG_PRINTF("CH583_PROTO ble join overflow seq=%u cached=%u add=%u max=%u\r\n",
               (unsigned int)frame->seq,
               (unsigned int)s_ble_len,
               (unsigned int)frame->arg_len,
               (unsigned int)sizeof(s_ble_buf));
        ch583_wifi_reset_ble_join();
        ch583_wifi_send_err(frame->seq, "BAD_LEN");
        return;
    }

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
}

static void ch583_wifi_handle_ble_mac(const ch583_wifi_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }

    if (frame->part != 1 || frame->total != 1) {
        ch583_wifi_send_err(frame->seq, "BAD_PART");
        return;
    }

    if (frame->arg_len != CH583_WIFI_BLE_MAC_LEN) {
        ch583_wifi_send_err(frame->seq, "BAD_LEN");
        return;
    }

    if (!ch583_wifi_is_upper_hex_string(frame->arg, CH583_WIFI_BLE_MAC_LEN)) {
        ch583_wifi_send_err(frame->seq, "BAD_ARG");
        return;
    }

    // Save the last CH583 BLE MAC so later WiFi logic can identify the frontend path.
    memcpy(s_ble_mac, frame->arg, CH583_WIFI_BLE_MAC_LEN);
    s_ble_mac[CH583_WIFI_BLE_MAC_LEN] = '\0';
    s_ble_mac_loaded = true;
    (void)app_nvs_write_str(CH583_BLE_MAC_NVS_KEY, s_ble_mac);
    printf("CH583_PROTO BLE_MAC saved=%s\r\n", s_ble_mac);
    ch583_wifi_send_ack(frame->seq);
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
    CH583_WIFI_DIRECTION_PRINTF("CH583 -> WiFi: seq=%u cmd=%s arg=%s\r\n",
           (unsigned int)frame.seq,
           frame.cmd,
           frame.arg);

    if (!ch583_wifi_validate_len_and_part(&frame)) {
        return;
    }

    if (strcmp(frame.cmd, "PING") == 0) {
        char arg[8];
        snprintf(arg, sizeof(arg), "%u", (unsigned int)frame.seq);
        ch583_wifi_send_frame("PONG", arg);
    } else if (strcmp(frame.cmd, "BLE_MAC") == 0) {
        ch583_wifi_handle_ble_mac(&frame);
    } else if (strcmp(frame.cmd, "BLE_DATA") == 0) {
        ch583_wifi_handle_ble_data(&frame, ble_data_callback);
    } else if (strcmp(frame.cmd, "ACK") == 0 ||
               strcmp(frame.cmd, "ERR") == 0 ||
               strcmp(frame.cmd, "PONG") == 0 ||
               strcmp(frame.cmd, "GPIO_VALUE") == 0) {
        ch583_wifi_handle_reply_status(&frame);
        CH583_WIFI_DEBUG_PRINTF("CH583_PROTO status cmd=%s arg=%s\r\n", frame.cmd, frame.arg);
    } else {
        CH583_WIFI_DEBUG_PRINTF("CH583_PROTO unsupported cmd=%s seq=%u\r\n", frame.cmd, (unsigned int)frame.seq);
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
        printf("CH583_PROTO WIFI_DATA too long len=%u max=%u\r\n",
               (unsigned int)len,
               (unsigned int)CH583_WIFI_MAX_WIFI_DATA_LEN);
        return -1;
    }
    // Send WiFi-to-frontend data as one WIFI_DATA frame.
    return ch583_wifi_send_frame("WIFI_DATA", message);
}

const char *ch583_wifi_uart_get_ble_mac(void)
{
    ch583_wifi_load_ble_mac_from_nvs();
    return s_ble_mac[0] != '\0' ? s_ble_mac : NULL;
}

int ch583_wifi_uart_send_power_off(void)
{    
    return ch583_wifi_send_frame("POWER_OFF", "");
}

int ch583_wifi_uart_send_gpio(const char *port, int pin, const char *mode, const char *level)
{
    char arg[32];

    if (port == NULL || mode == NULL || level == NULL) {
        return -1;
    }

    snprintf(arg, sizeof(arg), "%s,%d,%s,%s", port, pin, mode, level);
    return ch583_wifi_send_frame("GPIO", arg);
}

int ch583_wifi_uart_send_gpio_read(const char *port, int pin)
{
    char arg[16];

    if (port == NULL) {
        return -1;
    }

    snprintf(arg, sizeof(arg), "%s,%d", port, pin);
    return ch583_wifi_send_frame("GPIO_READ", arg);
}

int ch583_wifi_uart_test_gpio_pa1_high(void)
{    // Send the fixed GPIO test command through the same V1 frame builder used by real protocol replies.
    // 闁俺绻冮惇鐔风杽閸楀繗顔呴崶鐐差槻閸忚京鏁ら惃?V1 缂佸嫬鎶氶崙鑺ユ殶閸欐垿鈧礁娴愮€?GPIO 濞村鐦崨鎴掓姢閿涘本鏌熸笟璺ㄢ€樼拋?CH583 閺勵垰鎯侀幍褑顢戦妴?
    static uint8_t u8dat=0;

    u8dat++;
    if(u8dat ==1)        return ch583_wifi_uart_send_gpio("PB", 5, "OUT", "HIGH");
    else if(u8dat ==2)            return ch583_wifi_uart_send_gpio("PB", 5, "OUT", "LOW");
    else if(u8dat ==3)            return ch583_wifi_uart_send_gpio("PB", 6, "OUT", "HIGH");
    else if(u8dat ==4)            return ch583_wifi_uart_send_gpio("PB", 6, "OUT", "LOW");
    else  u8dat=0;

    return ch583_wifi_uart_send_gpio("PB", 5, "OUT", "HIGH");
}
