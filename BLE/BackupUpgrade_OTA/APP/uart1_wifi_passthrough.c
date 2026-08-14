#include "app_cfg.h"
#include "CONFIG.h"
#include "uart1_wifi_passthrough.h"
#include "wifi_time_wake.h"
#include "app_nfc.h"
#include "tdxinfoservice.h"
#include "commoninfo.h"
#include "adc_api.h"
#include "epd_driver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ENABLE_WIFI_UART1_PASSTHROUGH

#if VER > 255
#error "BLE version VER must be 0..255 for WiFi composite version"
#endif

#define WIFI_POWER_PIN                 GPIO_Pin_6
#define WIFI_UART1_BAUDRATE            115200UL
#define WIFI_BOOT_WAKE_DELAY_MS        500U
#define WIFI_BOOT_WAKE_DELAY_TICKS     ((WIFI_BOOT_WAKE_DELAY_MS * 8U) / 5U)
#define WIFI_WAKE_DELAY_MS             500U
#define WIFI_WAKE_DELAY_TICKS          ((WIFI_WAKE_DELAY_MS * 8U) / 5U)
#define WIFI_RX_BUFFER_SIZE            1024U
#define WIFI_RX_BUFFER_MASK            (WIFI_RX_BUFFER_SIZE - 1U)
#define WIFI_FRAME_PAYLOAD_SIZE        512U
#define WIFI_TX_BUFFER_SIZE            4096U
#define WIFI_TX_BUFFER_MASK            (WIFI_TX_BUFFER_SIZE - 1U)
#define WIFI_EVT_READY                 0x0001
#define WIFI_EVT_RX_POLL               0x0002
#define WIFI_EVT_SEND_PENDING_BLE      0x0004
#define WIFI_EVT_HEARTBEAT             0x0008
#define WIFI_EVT_LED_RED_BLINK         0x0010
#define WIFI_EVT_LED_GREEN_BLINK       0x0020
#define WIFI_EVT_BOOT_WAKE             0x0040
#define WIFI_EVT_TIMED_WAKE            0x0080
#define WIFI_RX_POLL_TICKS             1U
#define WIFI_DEVICE_INFO_FIRST_DELAY_MS 1000U
#define WIFI_DEVICE_INFO_FIRST_DELAY_TICKS ((WIFI_DEVICE_INFO_FIRST_DELAY_MS * 8U) / 5U)
#define WIFI_DEVICE_INFO_RETRY_MS      1000U
#define WIFI_DEVICE_INFO_RETRY_TICKS   ((WIFI_DEVICE_INFO_RETRY_MS * 8U) / 5U)
#define WIFI_AFTER_DEVICE_INFO_ACK_MS  200U
#define WIFI_AFTER_DEVICE_INFO_ACK_TICKS ((WIFI_AFTER_DEVICE_INFO_ACK_MS * 8U) / 5U)
#define WIFI_HEARTBEAT_MS              10000U
#define WIFI_HEARTBEAT_TICKS           ((WIFI_HEARTBEAT_MS * 8U) / 5U)
#define WIFI_DEVICE_INFO_ACK_MAX       10U
#define WIFI_MISSED_PONG_MAX           6U
#define WIFI_PROTOCOL_BODY_SIZE        512U
#define WIFI_PROTOCOL_FRAME_SIZE       576U
#define WIFI_PROTOCOL_ARG_MAX          300U
#define WIFI_FRONTEND_DATA_MAX         256U
#define WIFI_PENDING_BLE_DATA_SIZE     1024U
#define WIFI_LED_RED_PIN               GPIO_Pin_5
#define WIFI_LED_GREEN_PIN             GPIO_Pin_6
#define WIFI_LED_INTERVAL_MIN_MS       1U
#define WIFI_LED_INTERVAL_MAX_MS       10000U

#define WIFI_ERR_BAD_CRC               "BAD_CRC"
#define WIFI_ERR_BAD_LEN               "BAD_LEN"
#define WIFI_ERR_BAD_PART              "BAD_PART"
#define WIFI_ERR_BAD_FORMAT            "BAD_FORMAT"
#define WIFI_ERR_BAD_CMD               "BAD_CMD"
#define WIFI_ERR_BAD_ARG               "BAD_ARG"
#define WIFI_ERR_BAD_PORT              "BAD_PORT"
#define WIFI_ERR_BAD_PIN               "BAD_PIN"
#define WIFI_ERR_BAD_MODE              "BAD_MODE"
#define WIFI_ERR_BAD_LEVEL             "BAD_LEVEL"
#define WIFI_ERR_BAD_TIME              "BAD_TIME"
#define WIFI_ERR_DENY_GPIO             "DENY_GPIO"
#define WIFI_ERR_BLE_NOT_CONNECTED     "BLE_NOT_CONNECTED"
#define WIFI_ERR_BLE_NOTIFY_DISABLED   "BLE_NOTIFY_DISABLED"
#define WIFI_ERR_BLE_NOTIFY_FAIL       "BLE_NOTIFY_FAIL"
#define WIFI_ERR_NFC_BUSY              "NFC_BUSY"
#define WIFI_ERR_DEVICE_INFO_REQUIRED  "DEVICE_INFO_REQUIRED"

static uint8_t s_wifi_awake = Is_No;
static uint8_t s_wifi_wake_delay_pending = Is_No;
static uint16_t s_wifi_tx_seq = 0;
static uint8_t s_waiting_pong = Is_No;
static uint8_t s_missed_pong_count = 0;
static uint16_t s_last_ping_seq = 0;
static uint8_t s_ble_connected = Is_No;
static uint8_t s_device_info_acked = Is_No;
static uint8_t s_waiting_device_info_ack = Is_No;
static uint16_t s_ble_conn_handle = INVALID_CONNHANDLE;
static uint16_t s_last_device_info_seq = 0;
static tmosTaskID s_wifi_task_id = 0xFF;
static uint8_t s_rx_buffer[WIFI_RX_BUFFER_SIZE];
static volatile uint16_t s_rx_head = 0;
static volatile uint16_t s_rx_tail = 0;
static volatile uint8_t s_rx_overflow = Is_No;
static volatile uint32_t s_rx_irq_count = 0;
static volatile uint32_t s_rx_irq_bytes = 0;
static volatile uint32_t s_rx_ready_count = 0;
static volatile uint32_t s_rx_tout_count = 0;
static volatile uint32_t s_rx_line_count = 0;
static uint8_t s_tx_buffer[WIFI_TX_BUFFER_SIZE];
static uint16_t s_tx_head = 0;
static uint16_t s_tx_tail = 0;
static uint8_t s_frame_state = 0;
static uint8_t s_frame_payload[WIFI_FRAME_PAYLOAD_SIZE];
static uint16_t s_frame_payload_len = 0;
static uint8_t s_pending_ble_data[WIFI_PENDING_BLE_DATA_SIZE];
static uint16_t s_pending_ble_len = 0;
static uint8_t s_usb_power_present = Is_No;
static uint8_t s_wifi_adc_refresh_pending = Is_No;
static uint8_t s_runtime_wifi_provision = WIFI_UNPROVISIONED;
static uint8_t s_runtime_frame_mode = FRAME_WORK_MODE_NORMAL;
static uint8_t s_runtime_status_inited = Is_No;
static uint8_t s_runtime_status_dirty = Is_No;
static uint16_t s_runtime_wifi_version = 0;
static uint8_t s_runtime_wifi_version_valid = Is_No;
static uint8_t s_runtime_wifi_version_dirty = Is_No;
volatile uint8_t g_wifi_passthrough_disable_hal_sleep = WIFI_FORCE_ALWAYS_ON;

typedef enum
{
    WIFI_WAKE_REASON_UNKNOWN = 0,
    WIFI_WAKE_REASON_BOOT,
    WIFI_WAKE_REASON_USB,
    WIFI_WAKE_REASON_KEY_PB1,
    WIFI_WAKE_REASON_KEY_PB2,
    WIFI_WAKE_REASON_BLE_CONNECT,
    WIFI_WAKE_REASON_BLE_WRITE,
    WIFI_WAKE_REASON_NFC,
    WIFI_WAKE_REASON_TIMER
} wifi_wake_reason_t;

static wifi_wake_reason_t s_wifi_wake_reason = WIFI_WAKE_REASON_UNKNOWN;
static uint8_t s_pending_pb1_key_event = Is_No;
static uint8_t s_pending_pb2_key_event = Is_No;

typedef struct
{
    uint32_t pin_mask;
    uint16_t event;
    uint16_t interval_ticks;
    uint8_t enabled;
    uint8_t high;
} wifi_led_blink_t;

static wifi_led_blink_t s_red_led = {WIFI_LED_RED_PIN, WIFI_EVT_LED_RED_BLINK, 0, Is_No, Is_Yes};
static wifi_led_blink_t s_green_led = {WIFI_LED_GREEN_PIN, WIFI_EVT_LED_GREEN_BLINK, 0, Is_No, Is_Yes};

static tmosEvents WifiPassthrough_ProcessEvent(tmosTaskID task_id, tmosEvents events);
static void WifiPassthrough_ConfigUartOffIo(void);
static void WifiPassthrough_ResetRx(void);
static void WifiPassthrough_ResetTx(void);
static void WifiPassthrough_ResetSessionCounters(void);
static void WifiPassthrough_StopAllLeds(void);
static void WifiPassthrough_RefreshAdcOnWifiOff(void);
static void WifiPassthrough_InitRuntimeStatus(void);
static void WifiPassthrough_EnsureRuntimeStatus(void);
static void WifiPassthrough_SaveRuntimeStatusIfDirty(void);

static char *WifiPassthrough_WakeReasonText(wifi_wake_reason_t reason)
{
    switch(reason)
    {
        case WIFI_WAKE_REASON_BOOT:
            return "BOOT";

        case WIFI_WAKE_REASON_USB:
            return "USB";

        case WIFI_WAKE_REASON_KEY_PB1:
            return "KEY_PB1";

        case WIFI_WAKE_REASON_KEY_PB2:
            return "KEY_PB2";

        case WIFI_WAKE_REASON_BLE_CONNECT:
            return "BLE_CONNECT";

        case WIFI_WAKE_REASON_BLE_WRITE:
            return "BLE_WRITE";

        case WIFI_WAKE_REASON_NFC:
            return "NFC";

        case WIFI_WAKE_REASON_TIMER:
            return "TIMER";

        case WIFI_WAKE_REASON_UNKNOWN:
        default:
            return "UNKNOWN";
    }
}

static void WifiPassthrough_ResetWakeReason(void)
{
    s_wifi_wake_reason = WIFI_WAKE_REASON_UNKNOWN;
}

static void WifiPassthrough_ResetPendingKeyEvent(void)
{
    s_pending_pb1_key_event = Is_No;
    s_pending_pb2_key_event = Is_No;
}

static void WifiPassthrough_StopScheduledEvents(void)
{
    if(s_wifi_task_id == 0xFF)
    {
        return;
    }

    tmos_stop_task(s_wifi_task_id, WIFI_EVT_READY);
    tmos_stop_task(s_wifi_task_id, WIFI_EVT_RX_POLL);
    tmos_stop_task(s_wifi_task_id, WIFI_EVT_SEND_PENDING_BLE);
    tmos_stop_task(s_wifi_task_id, WIFI_EVT_HEARTBEAT);
    tmos_stop_task(s_wifi_task_id, WIFI_EVT_LED_RED_BLINK);
    tmos_stop_task(s_wifi_task_id, WIFI_EVT_LED_GREEN_BLINK);
    tmos_stop_task(s_wifi_task_id, WIFI_EVT_BOOT_WAKE);
}

static void WifiPassthrough_ForceWifiOffAndRequestLowPower(char *reason)
{
    if(reason == NULL)
    {
        reason = "unknown";
    }

    Print_I3("[WIFI_PT] protocol low power, force WiFi off, reason=%s", reason);
    WifiTimeWake_SaveTimeIfDirtyOrValid();
    WifiTimeWake_FinalizeForWifiOff();
    WifiPassthrough_StopAllLeds();
    WifiPassthrough_StopScheduledEvents();
    WifiPassthrough_ConfigUartOffIo();
    R32_PA_CLR |= WIFI_POWER_PIN;
    g_wifi_passthrough_disable_hal_sleep = 0;
    s_wifi_awake = Is_No;
    s_wifi_wake_delay_pending = Is_No;
    s_waiting_pong = Is_No;
    s_waiting_device_info_ack = Is_No;
    s_device_info_acked = Is_No;
    s_missed_pong_count = 0;
    s_pending_ble_len = 0;
    WifiPassthrough_ResetWakeReason();
    WifiPassthrough_ResetPendingKeyEvent();
    WifiPassthrough_ResetRx();
    WifiPassthrough_ResetTx();
    WifiPassthrough_RefreshAdcOnWifiOff();
    tmos_start_task(main_task_ID, EVENT_Low_Power, 1);
}

static uint8_t WifiPassthrough_ReadUsbWakeLock(void)
{
    uint8_t lock = WIFI_USB_WAKE_UNLOCK_VALUE;

    Get_EEPROM_Flag(&lock, WIFI_USB_WAKE_LOCK_POSITION, WIFI_USB_WAKE_LOCK_LEN);
    return lock;
}

static void WifiPassthrough_WriteUsbWakeLock(uint8_t lock)
{
    Save_EEPROM_Flag(&lock, WIFI_USB_WAKE_LOCK_POSITION, WIFI_USB_WAKE_LOCK_LEN);
}

static void WifiPassthrough_BeginWakeSession(uint8_t from_timed_wake)
{
    WifiPassthrough_ResetSessionCounters();
    s_wifi_adc_refresh_pending = Is_Yes;
    WifiTimeWake_BeginWakeSession(from_timed_wake);
}

static void WifiPassthrough_RefreshAdcOnWifiOff(void)
{
    if(s_wifi_adc_refresh_pending != Is_Yes)
    {
        return;
    }

    s_wifi_adc_refresh_pending = Is_No;
    AdcRefreshBatteryForWifiDone();
}

static uint8_t WifiPassthrough_NormalizeFrameMode(uint8_t mode)
{
    if(mode > WIFI_COMPOSITE_WORK_MODE_MAX)
    {
        return FRAME_WORK_MODE_NORMAL;
    }

    return mode;
}

static void WifiPassthrough_InitRuntimeStatus(void)
{
    uint8_t workmode[WORKMODE_Len] = {0};
    uint8_t version[WIFI_COMPOSITE_VERSION_LEN] = {0};

    s_runtime_wifi_provision = getWifiProvisionStatus();
    Get_EEPROM_Flag(workmode, WORKMODE_Position, WORKMODE_Len);
    s_runtime_frame_mode = WifiPassthrough_NormalizeFrameMode(workmode[0]);
    getWifiCompositeVersion(version);
    s_runtime_wifi_version = (uint16_t)(((uint16_t)version[1] << 8) | version[2]);
    s_runtime_wifi_version_valid = Is_Yes;
    s_runtime_wifi_version_dirty = Is_No;
    s_runtime_status_dirty = Is_No;
    s_runtime_status_inited = Is_Yes;
}

static void WifiPassthrough_EnsureRuntimeStatus(void)
{
    if(s_runtime_status_inited != Is_Yes)
    {
        WifiPassthrough_InitRuntimeStatus();
    }
}

static void WifiPassthrough_SaveRuntimeStatusIfDirty(void)
{
    uint8_t workmode[WORKMODE_Len] = {0};

    WifiPassthrough_EnsureRuntimeStatus();

    if(s_runtime_status_dirty == Is_Yes)
    {
        workmode[0] = s_runtime_frame_mode;
        setWifiProvisionStatus(s_runtime_wifi_provision);
        Save_EEPROM_Flag(workmode, WORKMODE_Position, WORKMODE_Len);
        s_runtime_status_dirty = Is_No;
        Print_I3("[WIFI_PT] WIFI_PROVISION saved provision=%d mode=%d",
                 s_runtime_wifi_provision, s_runtime_frame_mode);
    }

    if(s_runtime_wifi_version_dirty == Is_Yes && s_runtime_wifi_version_valid == Is_Yes)
    {
        setWifiCompositeVersion((uint8_t)VER, s_runtime_wifi_version);
        s_runtime_wifi_version_dirty = Is_No;
        Print_I3("[WIFI_PT] WIFI_VER saved ble=%d wifi=%d", (uint8_t)VER, s_runtime_wifi_version);
    }
}


static void WifiPassthrough_ConfigUartActiveIo(void)
{
    GPIOPinRemap(DISABLE, RB_PIN_UART1);
    R32_PIN_IN_DIS &= ~(uint32_t)(GPIO_Pin_8 | GPIO_Pin_9);
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);
    UART1_INTCfg(ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
    PFIC_EnableIRQ(UART1_IRQn);
}

static void WifiPassthrough_ConfigUartOffIo(void)
{
    UART1_INTCfg(DISABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
    PFIC_DisableIRQ(UART1_IRQn);
    GPIOPinRemap(DISABLE, RB_PIN_UART1);
    GPIOA_ModeCfg(GPIO_Pin_8 | GPIO_Pin_9, GPIO_ModeIN_Floating);
    R32_PIN_IN_DIS |= (uint32_t)(GPIO_Pin_8 | GPIO_Pin_9);
}

static void WifiPassthrough_SetWake(uint8_t on)
{
    GPIOA_ModeCfg(WIFI_POWER_PIN, GPIO_ModeOut_PP_5mA);
#if WIFI_FORCE_ALWAYS_ON
    on = Is_On;
#endif
    if(on == Is_On)
    {
        R32_PA_OUT |= WIFI_POWER_PIN;
        WifiPassthrough_ConfigUartActiveIo();
        g_wifi_passthrough_disable_hal_sleep = 1;
        s_wifi_awake = Is_Yes;
        s_waiting_pong = Is_No;
        s_waiting_device_info_ack = Is_No;
        s_device_info_acked = Is_No;
        s_missed_pong_count = 0;
        Print_I3("[WIFI_PT] PA6 high, WiFi wake");
    }
    else
    {
        WifiPassthrough_StopScheduledEvents();
        WifiPassthrough_ConfigUartOffIo();
        R32_PA_CLR |= WIFI_POWER_PIN;
        g_wifi_passthrough_disable_hal_sleep = 0;
        s_wifi_awake = Is_No;
        s_wifi_wake_delay_pending = Is_No;
        s_waiting_pong = Is_No;
        s_waiting_device_info_ack = Is_No;
        s_device_info_acked = Is_No;
        s_missed_pong_count = 0;
        s_pending_ble_len = 0;
        WifiPassthrough_ResetWakeReason();
        WifiPassthrough_ResetPendingKeyEvent();
        WifiPassthrough_ResetRx();
        WifiPassthrough_ResetTx();
        Print_I3("[WIFI_PT] PA6 low, WiFi off");
    }
}

static void WifiPassthrough_RequestWakeEx(char *reason, wifi_wake_reason_t wake_reason, uint8_t from_timed_wake)
{
    if(reason == NULL)
    {
        reason = "unknown";
    }

    if(s_wifi_awake != Is_Yes)
    {
        s_wifi_wake_reason = wake_reason;
        Print_I3("[WIFI_PT] Request wake reason=%s proto=%s",
                 reason, WifiPassthrough_WakeReasonText(s_wifi_wake_reason));
        WifiPassthrough_BeginWakeSession(from_timed_wake);
        WifiPassthrough_SetWake(Is_On);
        s_wifi_wake_delay_pending = Is_Yes;
        if(s_wifi_task_id != 0xFF)
        {
            tmos_start_task(s_wifi_task_id, WIFI_EVT_READY, WIFI_WAKE_DELAY_TICKS);
        }
        return;
    }

    if(s_wifi_wake_delay_pending == Is_Yes)
    {
        Print_I3("[WIFI_PT] Request wake reason=%s, wait ready, session=%s",
                 reason, WifiPassthrough_WakeReasonText(s_wifi_wake_reason));
        return;
    }

    Print_I3("[WIFI_PT] Request wake reason=%s, WiFi already awake, session=%s",
             reason, WifiPassthrough_WakeReasonText(s_wifi_wake_reason));
    if(s_wifi_task_id != 0xFF)
    {
        tmos_start_task(s_wifi_task_id, WIFI_EVT_HEARTBEAT, WIFI_HEARTBEAT_TICKS);
    }
}

static void WifiPassthrough_RequestWake(char *reason, wifi_wake_reason_t wake_reason)
{
    WifiPassthrough_RequestWakeEx(reason, wake_reason, Is_No);
}

static void WifiPassthrough_RxPush(uint8_t value)
{
    uint16_t next = (uint16_t)((s_rx_head + 1U) & WIFI_RX_BUFFER_MASK);

    if(next == s_rx_tail)
    {
        s_rx_overflow = Is_Yes;
        return;
    }

    s_rx_buffer[s_rx_head] = value;
    s_rx_head = next;
}

static uint8_t WifiPassthrough_DrainUartRxFifo(void)
{
    uint8_t temp[UART_FIFO_SIZE];
    uint8_t count;
    uint8_t i;

    count = UART1_RecvString(temp);
    for(i = 0; i < count; i++)
    {
        WifiPassthrough_RxPush(temp[i]);
    }

    return count;
}

static uint8_t WifiPassthrough_RxPop(uint8_t *value)
{
    if(s_rx_tail == s_rx_head)
    {
        return Is_No;
    }

    *value = s_rx_buffer[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1U) & WIFI_RX_BUFFER_MASK);
    return Is_Yes;
}

static void WifiPassthrough_ResetRx(void)
{
    s_rx_head = 0;
    s_rx_tail = 0;
    s_rx_overflow = Is_No;
    s_frame_state = 0;
    s_frame_payload_len = 0;
}

static void WifiPassthrough_ResetTx(void)
{
    s_tx_head = 0;
    s_tx_tail = 0;
}

static void WifiPassthrough_ResetSessionCounters(void)
{
    s_wifi_tx_seq = 0;
    s_last_ping_seq = 0;
    s_last_device_info_seq = 0;
    s_rx_irq_count = 0;
    s_rx_irq_bytes = 0;
    s_rx_ready_count = 0;
    s_rx_tout_count = 0;
    s_rx_line_count = 0;
}

static void WifiPassthrough_ResetPendingBleData(void)
{
    s_pending_ble_len = 0;
}

static uint8_t WifiPassthrough_TxPush(uint8_t value)
{
    uint16_t next = (uint16_t)((s_tx_head + 1U) & WIFI_TX_BUFFER_MASK);

    if(next == s_tx_tail)
    {
        return Is_No;
    }

    s_tx_buffer[s_tx_head] = value;
    s_tx_head = next;
    return Is_Yes;
}

static uint16_t WifiPassthrough_TxFree(void)
{
    if(s_tx_head >= s_tx_tail)
    {
        return (uint16_t)(WIFI_TX_BUFFER_SIZE - (s_tx_head - s_tx_tail) - 1U);
    }

    return (uint16_t)(s_tx_tail - s_tx_head - 1U);
}

static uint8_t WifiPassthrough_TxPop(uint8_t *value)
{
    if(s_tx_tail == s_tx_head)
    {
        return Is_No;
    }

    *value = s_tx_buffer[s_tx_tail];
    s_tx_tail = (uint16_t)((s_tx_tail + 1U) & WIFI_TX_BUFFER_MASK);
    return Is_Yes;
}

static uint8_t WifiPassthrough_QueueTx(uint8_t *data, uint16_t len)
{
    uint16_t i;

    if(WifiPassthrough_TxFree() < len)
    {
        return Is_No;
    }

    for(i = 0; i < len; i++)
    {
        if(WifiPassthrough_TxPush(data[i]) != Is_Yes)
        {
            return Is_No;
        }
    }

    return Is_Yes;
}

static void WifiPassthrough_FlushTx(void)
{
    uint8_t value;

    if(s_wifi_awake != Is_Yes || s_wifi_wake_delay_pending == Is_Yes)
    {
        return;
    }

    while(WifiPassthrough_TxPop(&value) == Is_Yes)
    {
        UART1_SendByte(value);
    }
}

static uint16_t WifiPassthrough_Crc16Ccitt(uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    uint16_t i;
    uint8_t bit;

    for(i = 0; i < len; i++)
    {
        crc ^= ((uint16_t)data[i] << 8);
        for(bit = 0; bit < 8; bit++)
        {
            if(crc & 0x8000)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

static uint8_t WifiPassthrough_HexNibble(uint8_t value)
{
    if(value >= '0' && value <= '9')
    {
        return (uint8_t)(value - '0');
    }

    if(value >= 'A' && value <= 'F')
    {
        return (uint8_t)(value - 'A' + 10);
    }

    if(value >= 'a' && value <= 'f')
    {
        return (uint8_t)(value - 'a' + 10);
    }

    return 0xFF;
}

static uint8_t WifiPassthrough_ParseHex16(char *text, uint16_t *value)
{
    uint16_t i;
    uint16_t result = 0;
    uint8_t nibble;

    for(i = 0; i < 4; i++)
    {
        nibble = WifiPassthrough_HexNibble((uint8_t)text[i]);
        if(nibble == 0xFF)
        {
            return Is_No;
        }
        result = (uint16_t)((result << 4) | nibble);
    }

    if(text[4] != '\0')
    {
        return Is_No;
    }

    *value = result;
    return Is_Yes;
}

static void WifiPassthrough_SendBytes(uint8_t *data, uint16_t len)
{
    if(s_wifi_awake != Is_Yes)
    {
        return;
    }

    if(s_wifi_wake_delay_pending == Is_Yes)
    {
        if(WifiPassthrough_QueueTx(data, len) == Is_Yes)
        {
            return;
        }

        while(s_wifi_wake_delay_pending == Is_Yes)
        {
            TMOS_SystemProcess();
        }
    }

    UART1_SendString(data, len);
}

static void WifiPassthrough_SendProtocolFrameEx(char *cmd, uint8_t *arg, uint16_t arg_len, uint16_t part, uint16_t total)
{
    char body[WIFI_PROTOCOL_BODY_SIZE];
    char frame[WIFI_PROTOCOL_FRAME_SIZE];
    uint16_t seq = s_wifi_tx_seq++;
    uint16_t crc;
    int prefix_len;
    uint16_t body_len;
    int frame_len;

    if(arg_len > WIFI_PROTOCOL_ARG_MAX || (arg_len > 0U && arg == NULL) || part == 0U || total == 0U || part > total)
    {
        return;
    }

    prefix_len = sprintf(body, "V1|SEQ=%u|CMD=%s|LEN=%u|PART=%u|TOTAL=%u|ARG=", seq, cmd, arg_len, part, total);
    if(prefix_len <= 0 || (uint16_t)prefix_len + arg_len >= WIFI_PROTOCOL_BODY_SIZE)
    {
        return;
    }

    if(arg_len > 0U && arg != NULL)
    {
        memcpy(&body[prefix_len], arg, arg_len);
    }
    body_len = (uint16_t)prefix_len + arg_len;
    body[body_len] = '\0';

    crc = WifiPassthrough_Crc16Ccitt((uint8_t *)body, body_len);
    frame_len = sprintf(frame, "@#");
    memcpy(&frame[frame_len], body, body_len);
    frame_len += body_len;
    frame_len += sprintf(&frame[frame_len], "|CRC=%04X^&", crc);
    if(frame_len <= 0 || frame_len >= WIFI_PROTOCOL_FRAME_SIZE)
    {
        return;
    }

    WifiPassthrough_SendBytes((uint8_t *)frame, (uint16_t)frame_len);
    Print_I3("[WIFI_PT] TX CMD=%s SEQ=%d LEN=%d PART=%d/%d", cmd, seq, arg_len, part, total);
}

static void WifiPassthrough_SendProtocolFrame(char *cmd, char *arg)
{
    uint16_t arg_len = (arg == NULL) ? 0U : (uint16_t)strlen(arg);

    WifiPassthrough_SendProtocolFrameEx(cmd, (uint8_t *)arg, arg_len, 1U, 1U);
}

static void WifiPassthrough_SendBleDataChunks(uint8_t *data, uint16_t len)
{
    uint16_t offset = 0;
    uint16_t chunk;
    uint16_t part = 1;
    uint16_t total;

    if(data == NULL || len == 0)
    {
        return;
    }

    total = (uint16_t)((len + WIFI_PROTOCOL_ARG_MAX - 1U) / WIFI_PROTOCOL_ARG_MAX);
    while(offset < len)
    {
        chunk = (uint16_t)((len - offset) > WIFI_PROTOCOL_ARG_MAX ? WIFI_PROTOCOL_ARG_MAX : (len - offset));
        WifiPassthrough_SendProtocolFrameEx("BLE_DATA", &data[offset], chunk, part, total);
        offset = (uint16_t)(offset + chunk);
        part++;
    }
}

static uint8_t WifiPassthrough_StorePendingBleData(uint8_t *data, uint16_t len)
{
    if(data == NULL || len == 0)
    {
        return Is_No;
    }

    if((uint32_t)s_pending_ble_len + len > WIFI_PENDING_BLE_DATA_SIZE)
    {
        Print_I3("[WIFI_PT] pending BLE_DATA overflow old=%d add=%d", s_pending_ble_len, len);
        return Is_No;
    }

    memcpy(&s_pending_ble_data[s_pending_ble_len], data, len);
    s_pending_ble_len = (uint16_t)(s_pending_ble_len + len);
    Print_I3("[WIFI_PT] BLE_DATA queued len=%d total=%d, wait WiFi ready", len, s_pending_ble_len);
    return Is_Yes;
}

static void WifiPassthrough_SendPendingBleData(void)
{
    if(s_pending_ble_len == 0)
    {
        return;
    }

    Print_I3("[WIFI_PT] send pending BLE_DATA len=%d", s_pending_ble_len);
    WifiPassthrough_SendBleDataChunks(s_pending_ble_data, s_pending_ble_len);
    s_pending_ble_len = 0;
}

static void WifiPassthrough_SendAck(uint16_t ack_seq)
{
    char arg[16];

    sprintf(arg, "%u", ack_seq);
    WifiPassthrough_SendProtocolFrame("ACK", arg);
}

static void WifiPassthrough_SendErr(uint16_t err_seq, char *reason)
{
    char arg[48];

    sprintf(arg, "%u,%s", err_seq, reason);
    Print_I3("[WIFI_PT] ERR seq=%d reason=%s", err_seq, reason);
    WifiPassthrough_SendProtocolFrame("ERR", arg);
}

static uint8_t WifiPassthrough_IsPreDeviceInfoCmdAllowed(char *cmd)
{
    if(cmd == NULL)
    {
        return Is_No;
    }

    if(strcmp(cmd, "ACK") == 0 || strcmp(cmd, "ERR") == 0 ||
       strcmp(cmd, "TIME_GET") == 0 ||
       strcmp(cmd, "GPIO_READ") == 0 ||
       strcmp(cmd, "LED_BLINK") == 0 ||
       strcmp(cmd, "LED_BLINK_STOP") == 0)
    {
        return Is_Yes;
    }

    return Is_No;
}

static void WifiPassthrough_SendPing(void)
{
    s_last_ping_seq = s_wifi_tx_seq;
    WifiPassthrough_SendProtocolFrame("PING", "");
    s_waiting_pong = Is_Yes;
}

static char WifiPassthrough_ToHexChar(uint8_t value)
{
    return (value < 10U) ? (char)('0' + value) : (char)('A' + value - 10U);
}

static void WifiPassthrough_SendDeviceInfo(void)
{
    char mac_arg[13];
    char info_arg[48];
    uint8_t order[6] = {5, 4, 3, 2, 1, 0};
    uint8_t i;
    uint8_t value;
    uint8_t board_info;

    for(i = 0; i < 6U; i++)
    {
        value = Mac[order[i]];
        mac_arg[i * 2U] = WifiPassthrough_ToHexChar((uint8_t)(value >> 4));
        mac_arg[(i * 2U) + 1U] = WifiPassthrough_ToHexChar((uint8_t)(value & 0x0FU));
    }
    mac_arg[12] = '\0';
    board_info = EPD_GetBoardInfo();
    sprintf(info_arg, "%s,%u,%c,%c%c,%s",
            mac_arg,
            (unsigned int)((uint8_t)VER),
            EPD_GetScreenType(),
            WifiPassthrough_ToHexChar((uint8_t)(board_info >> 4)),
            WifiPassthrough_ToHexChar((uint8_t)(board_info & 0x0FU)),
            WifiPassthrough_WakeReasonText(s_wifi_wake_reason));

    s_last_device_info_seq = s_wifi_tx_seq;
    WifiPassthrough_SendProtocolFrame("DEVICE_INFO", info_arg);
    s_waiting_device_info_ack = Is_Yes;
}

static void WifiPassthrough_SendKeyEventPress(char *key)
{
    char arg[16];

    sprintf(arg, "%s,PRESS", key);
    WifiPassthrough_SendProtocolFrame("KEY_EVENT", arg);
}

static void WifiPassthrough_SendPendingKeyEvents(void)
{
    if(s_pending_pb1_key_event == Is_Yes)
    {
        s_pending_pb1_key_event = Is_No;
        WifiPassthrough_SendKeyEventPress("PB1");
    }

    if(s_pending_pb2_key_event == Is_Yes)
    {
        s_pending_pb2_key_event = Is_No;
        WifiPassthrough_SendKeyEventPress("PB2");
    }
}

static uint8_t WifiPassthrough_ParseDec16(char *text, uint16_t *value)
{
    uint32_t result = 0;

    if(text == NULL || *text == '\0')
    {
        return Is_No;
    }

    while(*text)
    {
        if(*text < '0' || *text > '9')
        {
            return Is_No;
        }

        result = (result * 10U) + (uint32_t)(*text - '0');
        if(result > 65535U)
        {
            return Is_No;
        }
        text++;
    }

    *value = (uint16_t)result;
    return Is_Yes;
}

static uint8_t WifiPassthrough_ParseDec32(char *text, uint32_t *value)
{
    uint32_t result = 0;

    if(text == NULL || *text == '\0')
    {
        return Is_No;
    }

    while(*text)
    {
        if(*text < '0' || *text > '9')
        {
            return Is_No;
        }

        if(result > ((0xFFFFFFFFUL - (uint32_t)(*text - '0')) / 10UL))
        {
            return Is_No;
        }

        result = (result * 10UL) + (uint32_t)(*text - '0');
        text++;
    }

    *value = result;
    return Is_Yes;
}


static uint8_t WifiPassthrough_ParsePortPin(char *arg, char **port, uint8_t *pin)
{
    char *comma;
    uint16_t pin_value;

    if(arg == NULL || strlen(arg) < 4U)
    {
        return Is_No;
    }

    comma = strchr(arg, ',');
    if(comma == NULL)
    {
        return Is_No;
    }

    *comma = '\0';
    *port = arg;

    if(WifiPassthrough_ParseDec16(comma + 1, &pin_value) != Is_Yes || pin_value > 31U)
    {
        return Is_No;
    }

    *pin = (uint8_t)pin_value;
    return Is_Yes;
}

static uint8_t WifiPassthrough_IsPortValid(char *port)
{
    return (strcmp(port, "PA") == 0 || strcmp(port, "PB") == 0) ? Is_Yes : Is_No;
}

static uint8_t WifiPassthrough_IsDeniedGpio(char *port, uint8_t pin)
{
    if(strcmp(port, "PA") == 0)
    {
        return (pin == 6U || pin == 8U || pin == 9U) ? Is_Yes : Is_No;
    }

    if(strcmp(port, "PB") == 0)
    {
        return (pin == 3U || pin == 4U || pin == 7U || pin == 13U) ? Is_Yes : Is_No;
    }

    return Is_Yes;
}

static void WifiPassthrough_ModeCfg(char *port, uint32_t pin_mask, uint8_t mode)
{
    if(strcmp(port, "PA") == 0)
    {
        GPIOA_ModeCfg(pin_mask, mode);
    }
    else
    {
        GPIOB_ModeCfg(pin_mask, mode);
    }
}

static void WifiPassthrough_SetGpioLevel(char *port, uint32_t pin_mask, char *level)
{
    if(strcmp(port, "PA") == 0)
    {
        if(strcmp(level, "HIGH") == 0)
        {
            GPIOA_SetBits(pin_mask);
        }
        else
        {
            GPIOA_ResetBits(pin_mask);
        }
    }
    else
    {
        if(strcmp(level, "HIGH") == 0)
        {
            GPIOB_SetBits(pin_mask);
        }
        else
        {
            GPIOB_ResetBits(pin_mask);
        }
    }
}

static uint8_t WifiPassthrough_ReadGpioLevel(char *port, uint32_t pin_mask)
{
    if(strcmp(port, "PA") == 0)
    {
        return (GPIOA_ReadPortPin(pin_mask) != 0U) ? Is_Yes : Is_No;
    }

    return (GPIOB_ReadPortPin(pin_mask) != 0U) ? Is_Yes : Is_No;
}

static void WifiPassthrough_SendGpioValue(uint16_t read_seq, char *port, uint8_t pin, uint8_t high)
{
    char arg[32];

    sprintf(arg, "%u,%s,%u,%s", read_seq, port, pin, high == Is_Yes ? "HIGH" : "LOW");
    WifiPassthrough_SendProtocolFrame("GPIO_VALUE", arg);
}

static uint16_t WifiPassthrough_LedMsToTicks(uint16_t interval_ms)
{
    uint32_t ticks = (((uint32_t)interval_ms * 8U) + 4U) / 5U;

    return (ticks == 0U) ? 1U : (uint16_t)ticks;
}

static uint8_t WifiPassthrough_GetLed(char *name, wifi_led_blink_t **led)
{
    if(strcmp(name, "RED") == 0)
    {
        *led = &s_red_led;
        return Is_Yes;
    }

    if(strcmp(name, "GREEN") == 0)
    {
        *led = &s_green_led;
        return Is_Yes;
    }

    return Is_No;
}

static void WifiPassthrough_SetLedHigh(wifi_led_blink_t *led, uint8_t high)
{
    GPIOB_ModeCfg(led->pin_mask, GPIO_ModeOut_PP_5mA);
    if(high == Is_Yes)
    {
        GPIOB_SetBits(led->pin_mask);
    }
    else
    {
        GPIOB_ResetBits(led->pin_mask);
    }
    led->high = high;
}

static void WifiPassthrough_SetLedOn(wifi_led_blink_t *led)
{
    WifiPassthrough_SetLedHigh(led, Is_No);
}

static void WifiPassthrough_SetLedOff(wifi_led_blink_t *led)
{
    WifiPassthrough_SetLedHigh(led, Is_Yes);
}

static void WifiPassthrough_StopLed(wifi_led_blink_t *led, uint8_t force_off)
{
    led->enabled = Is_No;
    if(s_wifi_task_id != 0xFF)
    {
        tmos_stop_task(s_wifi_task_id, led->event);
    }

    if(force_off == Is_Yes)
    {
        WifiPassthrough_SetLedOff(led);
    }
}

static void WifiPassthrough_StopAllLeds(void)
{
    WifiPassthrough_StopLed(&s_red_led, Is_Yes);
    WifiPassthrough_StopLed(&s_green_led, Is_Yes);
}

static void WifiPassthrough_StopLedForGpio(char *port, uint8_t pin)
{
    if(strcmp(port, "PB") != 0)
    {
        return;
    }

    if(pin == 5U)
    {
        WifiPassthrough_StopLed(&s_red_led, Is_No);
    }
    else if(pin == 6U)
    {
        WifiPassthrough_StopLed(&s_green_led, Is_No);
    }
}

static void WifiPassthrough_ToggleLed(wifi_led_blink_t *led)
{
    if(led->enabled != Is_Yes)
    {
        return;
    }

    WifiPassthrough_SetLedHigh(led, led->high == Is_Yes ? Is_No : Is_Yes);
    tmos_start_task(s_wifi_task_id, led->event, led->interval_ticks);
}

static void WifiPassthrough_HandleLedBlink(uint16_t seq, char *arg)
{
    char arg_copy[32];
    char *led_name;
    char *interval_text;
    uint16_t interval_ms;
    wifi_led_blink_t *led;

    if(arg == NULL || strlen(arg) >= sizeof(arg_copy))
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_ARG);
        return;
    }

    strcpy(arg_copy, arg);
    led_name = strtok(arg_copy, ",");
    interval_text = strtok(NULL, ",");
    if(led_name == NULL || interval_text == NULL || strtok(NULL, ",") != NULL)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_ARG);
        return;
    }

    if(WifiPassthrough_GetLed(led_name, &led) != Is_Yes ||
       WifiPassthrough_ParseDec16(interval_text, &interval_ms) != Is_Yes ||
       interval_ms < WIFI_LED_INTERVAL_MIN_MS ||
       interval_ms > WIFI_LED_INTERVAL_MAX_MS)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_ARG);
        return;
    }

    WifiPassthrough_StopLed(led, Is_No);
    led->interval_ticks = WifiPassthrough_LedMsToTicks(interval_ms);
    led->enabled = Is_Yes;
    WifiPassthrough_SetLedOn(led);
    if(s_wifi_task_id != 0xFF)
    {
        tmos_start_task(s_wifi_task_id, led->event, led->interval_ticks);
    }
    Print_I3("[WIFI_PT] LED_BLINK %s interval=%dms ticks=%d", led_name, interval_ms, led->interval_ticks);
    WifiPassthrough_SendAck(seq);
}

static void WifiPassthrough_HandleLedBlinkStop(uint16_t seq, char *arg)
{
    char arg_copy[16];
    wifi_led_blink_t *led;

    if(arg == NULL || strlen(arg) >= sizeof(arg_copy))
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_ARG);
        return;
    }

    strcpy(arg_copy, arg);
    if(WifiPassthrough_GetLed(arg_copy, &led) != Is_Yes)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_ARG);
        return;
    }

    WifiPassthrough_StopLed(led, Is_Yes);
    Print_I3("[WIFI_PT] LED_BLINK_STOP %s", arg_copy);
    WifiPassthrough_SendAck(seq);
}


static void WifiPassthrough_HandleGpio(uint16_t seq, char *arg)
{
    char arg_copy[64];
    char *port;
    char *pin_text;
    char *mode;
    char *level;
    uint16_t pin_value;
    uint8_t mode_cfg;
    uint32_t pin_mask;

    if(arg == NULL || strlen(arg) >= sizeof(arg_copy))
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_ARG);
        return;
    }

    strcpy(arg_copy, arg);
    port = strtok(arg_copy, ",");
    pin_text = strtok(NULL, ",");
    mode = strtok(NULL, ",");
    level = strtok(NULL, ",");
    if(port == NULL || pin_text == NULL || mode == NULL || level == NULL || strtok(NULL, ",") != NULL)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_ARG);
        return;
    }

    if(WifiPassthrough_IsPortValid(port) != Is_Yes)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_PORT);
        return;
    }

    if(WifiPassthrough_ParseDec16(pin_text, &pin_value) != Is_Yes || pin_value > 31U)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_PIN);
        return;
    }

    if(WifiPassthrough_IsDeniedGpio(port, (uint8_t)pin_value) == Is_Yes)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_DENY_GPIO);
        return;
    }

    if(strcmp(mode, "OUT") == 0)
    {
        if(strcmp(level, "HIGH") != 0 && strcmp(level, "LOW") != 0)
        {
            WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_LEVEL);
            return;
        }
        mode_cfg = GPIO_ModeOut_PP_5mA;
    }
    else
    {
        if(strcmp(level, "KEEP") != 0)
        {
            WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_LEVEL);
            return;
        }

        if(strcmp(mode, "IN_PU") == 0)
        {
            mode_cfg = GPIO_ModeIN_PU;
        }
        else if(strcmp(mode, "IN_PD") == 0)
        {
            mode_cfg = GPIO_ModeIN_PD;
        }
        else if(strcmp(mode, "IN_FLOAT") == 0)
        {
            mode_cfg = GPIO_ModeIN_Floating;
        }
        else
        {
            WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_MODE);
            return;
        }
    }

    pin_mask = (uint32_t)1U << pin_value;
    WifiPassthrough_StopLedForGpio(port, (uint8_t)pin_value);
    if(strcmp(mode, "OUT") == 0)
    {
        WifiPassthrough_SetGpioLevel(port, pin_mask, level);
    }
    WifiPassthrough_ModeCfg(port, pin_mask, mode_cfg);
    WifiPassthrough_SendAck(seq);
}

static void WifiPassthrough_HandleGpioRead(uint16_t seq, char *arg)
{
    char arg_copy[24];
    char *port;
    uint8_t pin;
    uint32_t pin_mask;

    if(arg == NULL || strlen(arg) >= sizeof(arg_copy))
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_ARG);
        return;
    }

    strcpy(arg_copy, arg);
    if(WifiPassthrough_ParsePortPin(arg_copy, &port, &pin) != Is_Yes)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_ARG);
        return;
    }

    if(WifiPassthrough_IsPortValid(port) != Is_Yes)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_PORT);
        return;
    }

    if(WifiPassthrough_IsDeniedGpio(port, pin) == Is_Yes)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_DENY_GPIO);
        return;
    }

    pin_mask = (uint32_t)1U << pin;
    WifiPassthrough_SendGpioValue(seq, port, pin, WifiPassthrough_ReadGpioLevel(port, pin_mask));
}

static void WifiPassthrough_HandleWifiData(uint16_t seq, uint16_t len, uint16_t part, uint16_t total, char *arg)
{
    bStatus_t ret;

    Print_I3("[WIFI_PT] WIFI_DATA seq=%d len=%d ble_connected=%d handle=%x", seq, len, s_ble_connected, s_ble_conn_handle);
    if(part != 1U || total != 1U)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_PART);
        return;
    }

    if(len > WIFI_FRONTEND_DATA_MAX)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_LEN);
        return;
    }

    if(s_ble_connected != Is_Yes || s_ble_conn_handle == INVALID_CONNHANDLE)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BLE_NOT_CONNECTED);
        return;
    }

    ret = TdxInfo_SendWifiDataToFrontend(s_ble_conn_handle, (uint8_t *)arg, len);
    Print_I3("[WIFI_PT] WIFI_DATA notify ret=%d", ret);
    if(ret == SUCCESS)
    {
        WifiPassthrough_SendAck(seq);
    }
    else if(ret == bleIncorrectMode)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BLE_NOTIFY_DISABLED);
    }
    else
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BLE_NOTIFY_FAIL);
    }
}

static uint8_t WifiPassthrough_ParseHexNibble(char value, uint8_t *out)
{
    if(value >= '0' && value <= '9')
    {
        *out = (uint8_t)(value - '0');
        return Is_Yes;
    }
    if(value >= 'A' && value <= 'F')
    {
        *out = (uint8_t)(value - 'A' + 10U);
        return Is_Yes;
    }
    if(value >= 'a' && value <= 'f')
    {
        *out = (uint8_t)(value - 'a' + 10U);
        return Is_Yes;
    }

    return Is_No;
}

static void WifiPassthrough_HandleWifiProvision(uint16_t seq, uint16_t len, uint16_t part, uint16_t total, char *arg)
{
    uint8_t provision_nibble;
    uint8_t provision;
    uint8_t mode;

    if(part != 1U || total != 1U)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_PART);
        return;
    }

    if(arg == NULL || len != 2U)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_LEN);
        return;
    }

    if(WifiPassthrough_ParseHexNibble(arg[0], &provision_nibble) != Is_Yes ||
       WifiPassthrough_ParseHexNibble(arg[1], &mode) != Is_Yes)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_ARG);
        return;
    }

    if(provision_nibble == WIFI_UNPROVISIONED_NIBBLE)
    {
        provision = WIFI_UNPROVISIONED;
    }
    else if(provision_nibble == WIFI_PROVISIONED_NIBBLE)
    {
        provision = WIFI_PROVISIONED;
    }
    else
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_ARG);
        return;
    }

    if(mode > WIFI_COMPOSITE_WORK_MODE_MAX)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_ARG);
        return;
    }

    WifiPassthrough_EnsureRuntimeStatus();
    s_runtime_wifi_provision = provision;
    s_runtime_frame_mode = mode;
    s_runtime_status_dirty = Is_Yes;

    Print_I3("[WIFI_PT] WIFI_PROVISION ram provision=%d mode=%d", provision, mode);
#ifdef ENABLE_SOFTWARE_TO_TDX
    P_scanWifiCompositeRspData(provision, mode);
#endif
    WifiPassthrough_SendAck(seq);
}

static void WifiPassthrough_HandleWifiVersion(uint16_t seq, uint16_t len, uint16_t part, uint16_t total, char *arg)
{
    uint16_t wifi_ver;
    uint8_t version[WIFI_COMPOSITE_VERSION_LEN];

    if(part != 1U || total != 1U)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_PART);
        return;
    }

    if(arg == NULL || len == 0U || WifiPassthrough_ParseDec16(arg, &wifi_ver) != Is_Yes)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_ARG);
        return;
    }

    WifiPassthrough_EnsureRuntimeStatus();
    s_runtime_wifi_version = wifi_ver;
    s_runtime_wifi_version_valid = Is_Yes;
    s_runtime_wifi_version_dirty = Is_Yes;

    version[0] = (uint8_t)VER;
    version[1] = (uint8_t)(wifi_ver >> 8);
    version[2] = (uint8_t)(wifi_ver & 0xFF);

    Print_I3("[WIFI_PT] WIFI_VER ram ble=%d wifi=%d", version[0], wifi_ver);
#ifdef ENABLE_SOFTWARE_TO_TDX
    P_scanWifiVersionRspData(version);
#endif
    WifiPassthrough_SendAck(seq);
}

static uint8_t WifiPassthrough_Base64UrlValue(uint8_t value, uint8_t *out)
{
    if(value >= 'A' && value <= 'Z')
    {
        *out = (uint8_t)(value - 'A');
        return Is_Yes;
    }
    if(value >= 'a' && value <= 'z')
    {
        *out = (uint8_t)(value - 'a' + 26U);
        return Is_Yes;
    }
    if(value >= '0' && value <= '9')
    {
        *out = (uint8_t)(value - '0' + 52U);
        return Is_Yes;
    }
    if(value == '+' || value == '-')
    {
        *out = 62;
        return Is_Yes;
    }
    if(value == '/' || value == '_')
    {
        *out = 63;
        return Is_Yes;
    }
    return Is_No;
}

static uint8_t WifiPassthrough_DecodeBase64Url(char *arg, uint16_t len, uint8_t *out, uint16_t out_size, uint16_t *out_len)
{
    uint16_t i;
    uint32_t acc = 0;
    uint8_t bits = 0;
    uint8_t value;
    uint16_t written = 0;
    uint8_t seen_pad = Is_No;

    if(arg == NULL || out == NULL || out_len == NULL)
    {
        return Is_No;
    }

    for(i = 0; i < len; i++)
    {
        if(arg[i] == '=')
        {
            seen_pad = Is_Yes;
            continue;
        }

        if(seen_pad == Is_Yes)
        {
            return Is_No;
        }

        if(WifiPassthrough_Base64UrlValue((uint8_t)arg[i], &value) != Is_Yes)
        {
            return Is_No;
        }

        acc = (acc << 6) | value;
        bits = (uint8_t)(bits + 6U);
        while(bits >= 8U)
        {
            bits = (uint8_t)(bits - 8U);
            if(written >= out_size)
            {
                return Is_No;
            }
            out[written++] = (uint8_t)((acc >> bits) & 0xFFU);
        }
    }

    *out_len = written;
    return Is_Yes;
}

static void WifiPassthrough_HandleNfcSet(uint16_t seq, uint16_t len, uint16_t part, uint16_t total, char *arg)
{
#ifdef ENABLE_APP_NFC
    uint8_t decoded[NFC_APP_JSON_MAX_LEN + 1U];
    uint16_t decoded_len = 0;
    uint8_t ret;

    if(part != 1U || total != 1U)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_PART);
        return;
    }

    if(len == 0U || len > WIFI_PROTOCOL_ARG_MAX)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_LEN);
        return;
    }

    if(WifiPassthrough_DecodeBase64Url(arg, len, decoded, NFC_APP_JSON_MAX_LEN, &decoded_len) != Is_Yes)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_ARG);
        return;
    }

    ret = NfcApp_SetJsonPayload(decoded, decoded_len);
    if(ret == NFC_APP_STATUS_OK)
    {
        WifiPassthrough_SendAck(seq);
    }
    else if(ret == NFC_APP_STATUS_BUSY)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_NFC_BUSY);
    }
    else if(ret == NFC_APP_STATUS_BAD_LEN)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_LEN);
    }
    else
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_ARG);
    }
#else
    (void)len;
    (void)part;
    (void)total;
    (void)arg;
    WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_CMD);
#endif
}

static void WifiPassthrough_HandleNfcClear(uint16_t seq, uint16_t len, uint16_t part, uint16_t total)
{
#ifdef ENABLE_APP_NFC
    if(part != 1U || total != 1U)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_PART);
        return;
    }
    if(len != 0U)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_LEN);
        return;
    }

    NfcApp_ClearJsonPayload();
    WifiPassthrough_SendAck(seq);
#else
    (void)len;
    (void)part;
    (void)total;
    WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_CMD);
#endif
}

static void WifiPassthrough_HandleNfcStatus(uint16_t seq, uint16_t len, uint16_t part, uint16_t total)
{
#ifdef ENABLE_APP_NFC
    char status[64];

    if(part != 1U || total != 1U)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_PART);
        return;
    }
    if(len != 0U)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_LEN);
        return;
    }

    NfcApp_GetStatus(status, sizeof(status));
    WifiPassthrough_SendProtocolFrame("NFC_STATUS", status);
#else
    (void)len;
    (void)part;
    (void)total;
    WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_CMD);
#endif
}

static void WifiPassthrough_HandleNfcOn(uint16_t seq, uint16_t len, uint16_t part, uint16_t total)
{
#ifdef ENABLE_APP_NFC
    uint8_t ret;

    if(part != 1U || total != 1U)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_PART);
        return;
    }
    if(len != 0U)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_LEN);
        return;
    }

    ret = NfcApp_StartManualWindow();
    if(ret == NFC_APP_STATUS_OK)
    {
        WifiPassthrough_SendAck(seq);
    }
    else
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_ARG);
    }
#else
    (void)len;
    (void)part;
    (void)total;
    WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_CMD);
#endif
}

static void WifiPassthrough_HandleNfcOff(uint16_t seq, uint16_t len, uint16_t part, uint16_t total)
{
#ifdef ENABLE_APP_NFC
    if(part != 1U || total != 1U)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_PART);
        return;
    }
    if(len != 0U)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_LEN);
        return;
    }

    NfcApp_StopManualWindow();
    WifiPassthrough_SendAck(seq);
#else
    (void)len;
    (void)part;
    (void)total;
    WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_CMD);
#endif
}

static void WifiPassthrough_HandleTimeSet(uint16_t seq, uint16_t len, uint16_t part, uint16_t total, char *arg)
{
    wifi_time_wake_status_t ret;

    if(part != 1U || total != 1U)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_PART);
        return;
    }

    ret = WifiTimeWake_TimeSet(arg, len);
    if(ret == WIFI_TIME_WAKE_OK)
    {
        WifiPassthrough_SendAck(seq);
    }
    else if(ret == WIFI_TIME_WAKE_BAD_LEN)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_LEN);
    }
    else
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_TIME);
    }
}

static void WifiPassthrough_HandleTimeGet(uint16_t seq, uint16_t len, uint16_t part, uint16_t total)
{
    char status[32];
    wifi_time_wake_status_t ret;

    if(part != 1U || total != 1U)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_PART);
        return;
    }
    if(len != 0U)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_LEN);
        return;
    }

    ret = WifiTimeWake_TimeGetStatus(status, sizeof(status));
    if(ret != WIFI_TIME_WAKE_OK)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_LEN);
        return;
    }

    WifiPassthrough_SendProtocolFrame("TIME_STATUS", status);
}

static void WifiPassthrough_HandleWakeTimer(uint16_t seq, uint16_t len, uint16_t part, uint16_t total, char *arg)
{
    char arg_copy[32];
    char *mode;
    char *seconds_text;
    uint32_t seconds;
    wifi_time_wake_status_t ret;

    if(part != 1U || total != 1U)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_PART);
        return;
    }

    if(arg == NULL || len == 0U || strlen(arg) >= sizeof(arg_copy))
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_ARG);
        return;
    }

    strcpy(arg_copy, arg);
    mode = strtok(arg_copy, ",");
    seconds_text = strtok(NULL, ",");
    if(mode == NULL || seconds_text == NULL || strtok(NULL, ",") != NULL)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_ARG);
        return;
    }

    if(WifiPassthrough_ParseDec32(seconds_text, &seconds) != Is_Yes)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_TIME);
        return;
    }

    if(strcmp(mode, "ON") == 0)
    {
        ret = WifiTimeWake_SetWakeTimerOn(seconds);
        if(ret != WIFI_TIME_WAKE_OK)
        {
            WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_TIME);
            return;
        }

        WifiPassthrough_SendAck(seq);
        return;
    }

    if(strcmp(mode, "OFF") == 0)
    {
        if(seconds != 0)
        {
            WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_TIME);
            return;
        }

        WifiTimeWake_SetWakeTimerOff();
        WifiPassthrough_SendAck(seq);
        return;
    }

    WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_ARG);
}

static uint8_t WifiPassthrough_ParseFrameFields(char *body, uint16_t *seq, char **cmd, uint16_t *len, uint16_t *part, uint16_t *total, char **arg)
{
    char *seq_field;
    char *cmd_field;
    char *len_field;
    char *part_field;
    char *total_field;
    char *arg_field;

    if(strncmp(body, "V1|", 3) != 0)
    {
        return Is_No;
    }

    seq_field = body + 3;
    cmd_field = strstr(seq_field, "|CMD=");
    if(strncmp(seq_field, "SEQ=", 4) != 0 || cmd_field == NULL)
    {
        return Is_No;
    }

    *cmd_field = '\0';
    if(WifiPassthrough_ParseDec16(seq_field + 4, seq) != Is_Yes)
    {
        *cmd_field = '|';
        return Is_No;
    }

    cmd_field += 5;
    len_field = strstr(cmd_field, "|LEN=");
    if(len_field == NULL)
    {
        return Is_No;
    }

    *len_field = '\0';
    *cmd = cmd_field;
    len_field += 5;
    part_field = strstr(len_field, "|PART=");
    if(part_field == NULL)
    {
        return Is_No;
    }

    *part_field = '\0';
    if(WifiPassthrough_ParseDec16(len_field, len) != Is_Yes)
    {
        return Is_No;
    }

    part_field += 6;
    total_field = strstr(part_field, "|TOTAL=");
    if(total_field == NULL)
    {
        return Is_No;
    }

    *total_field = '\0';
    if(WifiPassthrough_ParseDec16(part_field, part) != Is_Yes)
    {
        return Is_No;
    }

    total_field += 7;
    arg_field = strstr(total_field, "|ARG=");
    if(arg_field == NULL)
    {
        return Is_No;
    }

    *arg_field = '\0';
    if(WifiPassthrough_ParseDec16(total_field, total) != Is_Yes)
    {
        return Is_No;
    }

    *arg = arg_field + 5;
    return Is_Yes;
}

static uint8_t WifiPassthrough_TryParseSeq(char *body, uint16_t *seq)
{
    char *seq_start;
    char *seq_end;
    char saved;
    uint8_t ok;

    seq_start = strstr(body, "SEQ=");
    if(seq_start == NULL)
    {
        *seq = 0;
        return Is_No;
    }

    seq_start += 4;
    seq_end = strchr(seq_start, '|');
    if(seq_end == NULL)
    {
        *seq = 0;
        return Is_No;
    }

    saved = *seq_end;
    *seq_end = '\0';
    ok = WifiPassthrough_ParseDec16(seq_start, seq);
    *seq_end = saved;
    if(ok != Is_Yes)
    {
        *seq = 0;
    }
    return ok;
}

static void WifiPassthrough_HandleProtocolFrame(void)
{
    char frame[WIFI_FRAME_PAYLOAD_SIZE];
    char *crc_field;
    uint16_t rx_crc;
    uint16_t calc_crc;
    uint16_t seq = 0;
    uint16_t len;
    uint16_t part;
    uint16_t total;
    char *cmd;
    char *arg;

    if(s_frame_payload_len >= sizeof(frame))
    {
        return;
    }

    memcpy(frame, s_frame_payload, s_frame_payload_len);
    frame[s_frame_payload_len] = '\0';
    Print_I3("[WIFI_PT] RX frame data=%s", frame);

    crc_field = strstr(frame, "|CRC=");
    if(crc_field == NULL)
    {
        WifiPassthrough_SendErr(0, WIFI_ERR_BAD_FORMAT);
        return;
    }

    *crc_field = '\0';
    WifiPassthrough_TryParseSeq(frame, &seq);
    crc_field += 5;
    if(WifiPassthrough_ParseHex16(crc_field, &rx_crc) != Is_Yes)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_CRC);
        return;
    }

    calc_crc = WifiPassthrough_Crc16Ccitt((uint8_t *)frame, (uint16_t)strlen(frame));
    if(calc_crc != rx_crc)
    {
        Print_I3("[WIFI_PT] BAD_CRC seq=%d rx=%04X calc=%04X", seq, rx_crc, calc_crc);
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_CRC);
        return;
    }

    if(WifiPassthrough_ParseFrameFields(frame, &seq, &cmd, &len, &part, &total, &arg) != Is_Yes)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_FORMAT);
        return;
    }

    if(len > WIFI_PROTOCOL_ARG_MAX || strlen(arg) != len)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_LEN);
        return;
    }

    if(part == 0U || total == 0U || part > total)
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_PART);
        return;
    }

    if(strcmp(cmd, "BLE_DATA") != 0 && (part != 1U || total != 1U))
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_PART);
        return;
    }

    if(s_device_info_acked != Is_Yes && WifiPassthrough_IsPreDeviceInfoCmdAllowed(cmd) != Is_Yes)
    {
        Print_I3("[WIFI_PT] reject CMD=%s before DEVICE_INFO ACK", cmd);
        WifiPassthrough_SendErr(seq, WIFI_ERR_DEVICE_INFO_REQUIRED);
        return;
    }

    if(strcmp(cmd, "PONG") == 0)
    {
        uint16_t pong_seq;

        if(WifiPassthrough_ParseDec16(arg, &pong_seq) == Is_Yes && pong_seq == s_last_ping_seq)
        {
            s_waiting_pong = Is_No;
            s_missed_pong_count = 0;
            Print_I3("[WIFI_PT] PONG arg=%s", arg);
        }
        else
        {
            Print_I3("[WIFI_PT] PONG arg mismatch=%s", arg);
        }
    }
    else if(strcmp(cmd, "POWER_OFF") == 0 || strcmp(cmd, "LOWPOWER") == 0 || strcmp(cmd, "lowpower") == 0)
    {
        WifiPassthrough_SendAck(seq);
        while((UART1_GetLinSTA() & STA_TXALL_EMP) == 0)
        {
        }
        WifiPassthrough_SaveRuntimeStatusIfDirty();
        WifiPassthrough_ForceWifiOffAndRequestLowPower("wifi cmd");
    }
    else if(strcmp(cmd, "WAKE_TIMER") == 0)
    {
        WifiPassthrough_HandleWakeTimer(seq, len, part, total, arg);
    }
    else if(strcmp(cmd, "TIME_SET") == 0)
    {
        WifiPassthrough_HandleTimeSet(seq, len, part, total, arg);
    }
    else if(strcmp(cmd, "TIME_GET") == 0)
    {
        WifiPassthrough_HandleTimeGet(seq, len, part, total);
    }
    else if(strcmp(cmd, "GPIO") == 0)
    {
        WifiPassthrough_HandleGpio(seq, arg);
    }
    else if(strcmp(cmd, "GPIO_READ") == 0)
    {
        WifiPassthrough_HandleGpioRead(seq, arg);
    }
    else if(strcmp(cmd, "LED_BLINK") == 0)
    {
        WifiPassthrough_HandleLedBlink(seq, arg);
    }
    else if(strcmp(cmd, "LED_BLINK_STOP") == 0)
    {
        WifiPassthrough_HandleLedBlinkStop(seq, arg);
    }
    else if(strcmp(cmd, "WIFI_DATA") == 0)
    {
        WifiPassthrough_HandleWifiData(seq, len, part, total, arg);
    }
    else if(strcmp(cmd, "WIFI_PROVISION") == 0)
    {
        WifiPassthrough_HandleWifiProvision(seq, len, part, total, arg);
    }
    else if(strcmp(cmd, "WIFI_VER") == 0)
    {
        WifiPassthrough_HandleWifiVersion(seq, len, part, total, arg);
    }
    else if(strcmp(cmd, "NFC_SET") == 0)
    {
        WifiPassthrough_HandleNfcSet(seq, len, part, total, arg);
    }
    else if(strcmp(cmd, "NFC_CLEAR") == 0)
    {
        WifiPassthrough_HandleNfcClear(seq, len, part, total);
    }
    else if(strcmp(cmd, "NFC_STATUS") == 0)
    {
        WifiPassthrough_HandleNfcStatus(seq, len, part, total);
    }
    else if(strcmp(cmd, "NFC_ON") == 0)
    {
        WifiPassthrough_HandleNfcOn(seq, len, part, total);
    }
    else if(strcmp(cmd, "NFC_OFF") == 0)
    {
        WifiPassthrough_HandleNfcOff(seq, len, part, total);
    }
    else if(strcmp(cmd, "ACK") == 0)
    {
        uint16_t ack_seq;

        Print_I3("[WIFI_PT] ACK arg=%s", arg);
        if(s_device_info_acked != Is_Yes &&
           WifiPassthrough_ParseDec16(arg, &ack_seq) == Is_Yes &&
           ack_seq == s_last_device_info_seq)
        {
            s_device_info_acked = Is_Yes;
            s_waiting_device_info_ack = Is_No;
            s_missed_pong_count = 0;
            Print_I3("[WIFI_PT] DEVICE_INFO ACK matched seq=%d", ack_seq);
            WifiPassthrough_SendPendingKeyEvents();
            if(s_pending_ble_len > 0)
            {
                tmos_start_task(s_wifi_task_id, WIFI_EVT_SEND_PENDING_BLE, WIFI_AFTER_DEVICE_INFO_ACK_TICKS);
            }
            else if(s_wifi_task_id != 0xFF)
            {
                tmos_start_task(s_wifi_task_id, WIFI_EVT_HEARTBEAT, WIFI_HEARTBEAT_TICKS);
            }
        }
    }
    else if(strcmp(cmd, "ERR") == 0)
    {
        Print_I3("[WIFI_PT] ERR arg=%s", arg);
    }
    else
    {
        WifiPassthrough_SendErr(seq, WIFI_ERR_BAD_CMD);
    }
}

static void WifiPassthrough_ProcessFrameByte(uint8_t value)
{
    switch(s_frame_state)
    {
        case 0:
            s_frame_state = (value == '@') ? 1 : 0;
            break;

        case 1:
            if(value == '#')
            {
                s_frame_payload_len = 0;
                s_frame_state = 2;
            }
            else
            {
                s_frame_state = (value == '@') ? 1 : 0;
            }
            break;

        case 2:
            if(value == '^')
            {
                s_frame_state = 3;
            }
            else if(s_frame_payload_len < (WIFI_FRAME_PAYLOAD_SIZE - 1U))
            {
                s_frame_payload[s_frame_payload_len++] = (value >= 0x20U && value <= 0x7eU) ? value : '.';
            }
            else
            {
                Print_I3("[WIFI_PT] UART1 frame overflow, drop");
                s_frame_state = 0;
                s_frame_payload_len = 0;
            }
            break;

        case 3:
            if(value == '&')
            {
                WifiPassthrough_HandleProtocolFrame();
                s_frame_state = 0;
                s_frame_payload_len = 0;
            }
            else
            {
                if(s_frame_payload_len < (WIFI_FRAME_PAYLOAD_SIZE - 2U))
                {
                    s_frame_payload[s_frame_payload_len++] = '^';
                    s_frame_payload[s_frame_payload_len++] = (value >= 0x20U && value <= 0x7eU) ? value : '.';
                    s_frame_state = 2;
                }
                else
                {
                    Print_I3("[WIFI_PT] UART1 frame overflow, drop");
                    s_frame_state = 0;
                    s_frame_payload_len = 0;
                }
            }
            break;

        default:
            s_frame_state = 0;
            s_frame_payload_len = 0;
            break;
    }
}

void WifiPassthrough_Init(void)
{
    WifiPassthrough_SetWake(Is_Off);
    s_wifi_task_id = TMOS_ProcessEventRegister(WifiPassthrough_ProcessEvent);
    WifiPassthrough_InitRuntimeStatus();
    WifiTimeWake_Init(s_wifi_task_id, WIFI_EVT_TIMED_WAKE);
    WifiTimeWake_LoadTimeBackup();

    GPIOPinRemap(DISABLE, RB_PIN_UART1);
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();
    UART1_BaudRateCfg(WIFI_UART1_BAUDRATE);
    UART1_ByteTrigCfg(UART_7BYTE_TRIG);
    UART1_INTCfg(ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
    PFIC_SetPriority(UART1_IRQn, 0x00);
    PFIC_EnableIRQ(UART1_IRQn);
    PFIC_EnableAllIRQ();

    WifiPassthrough_ResetRx();
    WifiPassthrough_ResetTx();
    WifiPassthrough_ConfigUartOffIo();
}

void WifiPassthrough_RequestBootWake(void)
{
    if(s_wifi_task_id == 0xFF)
    {
        return;
    }

    Print_I3("[WIFI_PT] Boot wake scheduled delay=%dms", WIFI_BOOT_WAKE_DELAY_MS);
    tmos_start_task(s_wifi_task_id, WIFI_EVT_BOOT_WAKE, WIFI_BOOT_WAKE_DELAY_TICKS);
}

void WifiPassthrough_OnBleConnected(uint16_t conn_handle)
{
    s_ble_connected = Is_Yes;
    s_ble_conn_handle = conn_handle;
    WifiPassthrough_RequestWake("BLE connected", WIFI_WAKE_REASON_BLE_CONNECT);
}

void WifiPassthrough_OnBleDisconnected(void)
{
    s_ble_connected = Is_No;
    s_ble_conn_handle = INVALID_CONNHANDLE;
    Print_I3("[WIFI_PT] BLE disconnected, keep WiFi awake until WiFi power-off cmd");
}

void WifiPassthrough_BleWrite(uint8_t *data, uint16_t len)
{
    if(data == NULL || len == 0)
    {
        return;
    }

    WifiPassthrough_RequestWake("BLE write", WIFI_WAKE_REASON_BLE_WRITE);

    if(s_device_info_acked != Is_Yes)
    {
        WifiPassthrough_StorePendingBleData(data, len);
        return;
    }

    WifiPassthrough_SendBleDataChunks(data, len);
}

void WifiPassthrough_ProcessRx(void)
{
    uint8_t value;

    if(s_rx_head == s_rx_tail)
    {
        return;
    }

    if(s_rx_overflow == Is_Yes)
    {
        Print_I3("[WIFI_PT] RX overflow, reset buffer");
        WifiPassthrough_ResetRx();
    }

    while(WifiPassthrough_RxPop(&value) == Is_Yes)
    {
        WifiPassthrough_ProcessFrameByte(value);
    }
}

void WifiPassthrough_FastPoll(void)
{
    WifiPassthrough_ProcessRx();
}

void WifiPassthrough_OnWifiDone(void)
{
    WifiPassthrough_SaveRuntimeStatusIfDirty();
    WifiTimeWake_SaveTimeIfDirtyOrValid();
    WifiTimeWake_FinalizeForWifiOff();

#if WIFI_FORCE_ALWAYS_ON
    Print_I3("[WIFI_PT] WiFi done, keep PA6 high for debug");
    WifiPassthrough_SetWake(Is_On);
    WifiPassthrough_ResetWakeReason();
    WifiPassthrough_ResetPendingKeyEvent();
#else
    if(s_usb_power_present == Is_Yes)
    {
        WifiPassthrough_WriteUsbWakeLock(WIFI_USB_WAKE_LOCK_VALUE);
        Print_I3("[WIFI_PT] USB present, set wake lock before WiFi off");
    }
    Print_I3("[WIFI_PT] WiFi done, power off and schedule low power");
    WifiPassthrough_HandleReservedIo();
    WifiPassthrough_SetWake(Is_Off);
    WifiPassthrough_RefreshAdcOnWifiOff();
#endif
    WifiPassthrough_ResetTx();
    tmos_start_task(main_task_ID, EVENT_Low_Power, 500);
}

void WifiPassthrough_EnterOtaGuard(void)
{
    Print_I3("[WIFI_PT] OTA guard, force PA6 low");
    WifiPassthrough_StopAllLeds();
    WifiPassthrough_StopScheduledEvents();
    WifiPassthrough_ConfigUartOffIo();
    R32_PA_CLR |= WIFI_POWER_PIN;
    g_wifi_passthrough_disable_hal_sleep = 0;
    s_wifi_awake = Is_No;
    s_wifi_wake_delay_pending = Is_No;
    s_waiting_pong = Is_No;
    s_waiting_device_info_ack = Is_No;
    s_device_info_acked = Is_No;
    s_missed_pong_count = 0;
    s_pending_ble_len = 0;
    WifiPassthrough_ResetWakeReason();
    WifiPassthrough_ResetPendingKeyEvent();
    WifiPassthrough_ResetRx();
    WifiPassthrough_ResetTx();
    WifiPassthrough_RefreshAdcOnWifiOff();
}

void WifiPassthrough_HandleReservedIo(void)
{
    WifiPassthrough_StopAllLeds();
}

void WifiPassthrough_EnterLowPowerIo(void)
{
    WifiPassthrough_StopAllLeds();
    WifiPassthrough_SetWake(Is_Off);
    WifiPassthrough_ConfigUartOffIo();
#if WIFI_FORCE_ALWAYS_ON
    WifiPassthrough_SetWake(Is_On);
#endif
}



void WifiPassthrough_OnUsbPowerChanged(uint8_t present)
{
    uint8_t lock;

    if(present == Is_Yes)
    {
        if(s_usb_power_present == Is_Yes)
        {
            return;
        }

        s_usb_power_present = Is_Yes;
        lock = WifiPassthrough_ReadUsbWakeLock();
        if(lock == WIFI_USB_WAKE_LOCK_VALUE)
        {
            Print_I3("[WIFI_PT] USB power present, wake lock set, skip WiFi wake");
            return;
        }

        Print_I3("[WIFI_PT] USB power present, wake WiFi");
        WifiPassthrough_RequestWake("USB", WIFI_WAKE_REASON_USB);
        return;
    }

    if(s_usb_power_present != Is_No)
    {
        s_usb_power_present = Is_No;
        WifiPassthrough_WriteUsbWakeLock(WIFI_USB_WAKE_UNLOCK_VALUE);
        Print_I3("[WIFI_PT] USB power removed, clear wake lock");

        if(s_wifi_awake != Is_Yes &&
           s_wifi_wake_delay_pending != Is_Yes &&
           global_DEVICE_STATUS.fisBleConnect != Is_Yes &&
           global_DEVICE_STATUS.fWorked != Is_Yes &&
           global_DEVICE_STATUS.fisHaveData != Is_Yes)
        {
            Print_I3("[WIFI_PT] USB removed, schedule low power check");
            tmos_start_task(main_task_ID, EVENT_Low_Power, 500);
        }
    }
}

static void WifiPassthrough_HandleWakeKeyPressed(char *key, wifi_wake_reason_t wake_reason, uint8_t *pending_event)
{
    Print_I3("[WIFI_PT] %s wake key pressed", key);
    if(s_wifi_awake != Is_Yes)
    {
        WifiPassthrough_RequestWake(key, wake_reason);
        return;
    }

    if(s_wifi_wake_delay_pending == Is_Yes || s_device_info_acked != Is_Yes)
    {
        *pending_event = Is_Yes;
        Print_I3("[WIFI_PT] %s key event pending until DEVICE_INFO ACK", key);
        return;
    }

    WifiPassthrough_SendKeyEventPress(key);
}

void WifiPassthrough_OnWakeKeyPb1Pressed(void)
{
    WifiPassthrough_HandleWakeKeyPressed("PB1", WIFI_WAKE_REASON_KEY_PB1, &s_pending_pb1_key_event);
}

void WifiPassthrough_OnWakeKeyPb2Pressed(void)
{
    WifiPassthrough_HandleWakeKeyPressed("PB2", WIFI_WAKE_REASON_KEY_PB2, &s_pending_pb2_key_event);
}

void WifiPassthrough_OnWakeKeyPressed(void)
{
    WifiPassthrough_OnWakeKeyPb2Pressed();
}

void WifiPassthrough_OnNfcAuthorizedWake(void)
{
    Print_I3("[WIFI_PT] NFC authorized wake");
    WifiPassthrough_RequestWake("NFC", WIFI_WAKE_REASON_NFC);
}

uint8_t WifiPassthrough_IsWifiAwake(void)
{
    return s_wifi_awake;
}

uint8_t WifiPassthrough_IsUsbPowerPresent(void)
{
    return s_usb_power_present;
}

uint8_t WifiPassthrough_IsWakePending(void)
{
    return s_wifi_wake_delay_pending;
}

uint8_t WifiPassthrough_IsTimedWakeArmed(void)
{
    return WifiTimeWake_IsTimedWakeArmed();
}

static void WifiPassthrough_OnTimedWakeExpired(void)
{
    if(WifiTimeWake_OnTimedWakeEvent() == Is_Yes)
    {
        WifiPassthrough_RequestWakeEx("timed wake", WIFI_WAKE_REASON_TIMER, Is_Yes);
    }
}

__INTERRUPT
__HIGH_CODE
void UART1_IRQHandler(void)
{
    volatile uint8_t i;
    uint8_t temp[UART_FIFO_SIZE];
    uint16_t len;

    s_rx_irq_count++;
    switch(UART1_GetITFlag())
    {
        case UART_II_LINE_STAT:
            UART1_GetLinSTA();
            s_rx_line_count++;
            break;

        case UART_II_RECV_RDY:
            s_rx_ready_count++;
            for(i = 0; i != 7; i++)
            {
                WifiPassthrough_RxPush(UART1_RecvByte());
            }
            s_rx_irq_bytes += 7U;
            if(s_wifi_task_id != 0xFF)
            {
                tmos_set_event(s_wifi_task_id, WIFI_EVT_RX_POLL);
            }
            break;

        case UART_II_RECV_TOUT:
            s_rx_tout_count++;
            len = UART1_RecvString(temp);
            for(i = 0; i < len; i++)
            {
                WifiPassthrough_RxPush(temp[i]);
            }
            s_rx_irq_bytes += len;
            if(s_wifi_task_id != 0xFF)
            {
                tmos_set_event(s_wifi_task_id, WIFI_EVT_RX_POLL);
            }
            break;

        default:
            break;
    }
}

static tmosEvents WifiPassthrough_ProcessEvent(tmosTaskID task_id, tmosEvents events)
{
    if(events & WIFI_EVT_READY)
    {
        s_wifi_wake_delay_pending = Is_No;
        WifiPassthrough_FlushTx();
        s_device_info_acked = Is_No;
        s_waiting_device_info_ack = Is_No;
        s_waiting_pong = Is_No;
        s_missed_pong_count = 0;
        Print_I3("[WIFI_PT] WiFi ready, DEVICE_INFO delay=%dms", WIFI_DEVICE_INFO_FIRST_DELAY_MS);
        tmos_start_task(s_wifi_task_id, WIFI_EVT_HEARTBEAT, WIFI_DEVICE_INFO_FIRST_DELAY_TICKS);
        return events ^ WIFI_EVT_READY;
    }

    if(events & WIFI_EVT_RX_POLL)
    {
        WifiPassthrough_ProcessRx();
        return events ^ WIFI_EVT_RX_POLL;
    }

    if(events & WIFI_EVT_SEND_PENDING_BLE)
    {
        if(s_wifi_awake == Is_Yes && s_wifi_wake_delay_pending != Is_Yes && s_device_info_acked == Is_Yes)
        {
            WifiPassthrough_SendPendingBleData();
            s_waiting_pong = Is_No;
            tmos_start_task(s_wifi_task_id, WIFI_EVT_HEARTBEAT, WIFI_HEARTBEAT_TICKS);
        }
        return events ^ WIFI_EVT_SEND_PENDING_BLE;
    }

    if(events & WIFI_EVT_LED_RED_BLINK)
    {
        WifiPassthrough_ToggleLed(&s_red_led);
        return events ^ WIFI_EVT_LED_RED_BLINK;
    }

    if(events & WIFI_EVT_LED_GREEN_BLINK)
    {
        WifiPassthrough_ToggleLed(&s_green_led);
        return events ^ WIFI_EVT_LED_GREEN_BLINK;
    }

    if(events & WIFI_EVT_BOOT_WAKE)
    {
        WifiPassthrough_RequestWake("boot", WIFI_WAKE_REASON_BOOT);
        return events ^ WIFI_EVT_BOOT_WAKE;
    }

    if(events & WIFI_EVT_TIMED_WAKE)
    {
        WifiPassthrough_OnTimedWakeExpired();
        return events ^ WIFI_EVT_TIMED_WAKE;
    }

    if(events & WIFI_EVT_HEARTBEAT)
    {
        if(s_wifi_awake == Is_Yes && s_wifi_wake_delay_pending != Is_Yes)
        {
            if(s_device_info_acked != Is_Yes)
            {
                if(s_waiting_device_info_ack == Is_Yes)
                {
                    s_missed_pong_count++;
                    Print_I3("[WIFI_PT] DEVICE_INFO ACK timeout count=%d", s_missed_pong_count);
                    Print_I3("[WIFI_PT] RX stat irq=%lu bytes=%lu ready=%lu tout=%lu line=%lu rfc=%d lsr=%02X",
                             s_rx_irq_count, s_rx_irq_bytes, s_rx_ready_count, s_rx_tout_count,
                             s_rx_line_count, R8_UART1_RFC, UART1_GetLinSTA());
                    if(s_missed_pong_count >= WIFI_DEVICE_INFO_ACK_MAX)
                    {
                        if(s_usb_power_present == Is_Yes)
                        {
                            Print_I3("[WIFI_PT] USB present, ignore DEVICE_INFO ACK timeout");
                            s_missed_pong_count = 0;
                        }
                        else
                        {
                            Print_I3("[WIFI_PT] DEVICE_INFO ACK timeout, power off WiFi");
                            WifiPassthrough_OnWifiDone();
                            return events ^ WIFI_EVT_HEARTBEAT;
                        }
                    }
                }

                WifiPassthrough_SendDeviceInfo();
                tmos_start_task(s_wifi_task_id, WIFI_EVT_HEARTBEAT, WIFI_DEVICE_INFO_RETRY_TICKS);
                return events ^ WIFI_EVT_HEARTBEAT;
            }

            if(s_waiting_pong == Is_Yes)
            {
                s_missed_pong_count++;
                Print_I3("[WIFI_PT] PONG timeout count=%d", s_missed_pong_count);
                Print_I3("[WIFI_PT] RX stat irq=%lu bytes=%lu ready=%lu tout=%lu line=%lu rfc=%d lsr=%02X",
                         s_rx_irq_count, s_rx_irq_bytes, s_rx_ready_count, s_rx_tout_count,
                         s_rx_line_count, R8_UART1_RFC, UART1_GetLinSTA());
                if(s_missed_pong_count >= WIFI_MISSED_PONG_MAX)
                {
                    if(s_usb_power_present == Is_Yes)
                    {
                        Print_I3("[WIFI_PT] USB present, ignore PONG timeout");
                        s_missed_pong_count = 0;
                    }
                    else
                    {
                        Print_I3("[WIFI_PT] PONG timeout, power off WiFi");
                        WifiPassthrough_OnWifiDone();
                        return events ^ WIFI_EVT_HEARTBEAT;
                    }
                }
            }

            WifiPassthrough_SendPing();
            tmos_start_task(s_wifi_task_id, WIFI_EVT_HEARTBEAT, WIFI_HEARTBEAT_TICKS);
        }
        return events ^ WIFI_EVT_HEARTBEAT;
    }

    return 0;
}

#endif
