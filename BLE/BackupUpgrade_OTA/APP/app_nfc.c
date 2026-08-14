#include "app_cfg.h"
#include "CONFIG.h"
#include "app_nfc.h"
#include "uart1_wifi_passthrough.h"
#include "commoninfo.h"
#include "OTA.h"

#ifdef ENABLE_APP_NFC

#include "ISO14443-3A.h"
#include "aes.h"
#include "wch_nfca_picc_bsp.h"
#include <stdio.h>
#include <string.h>

#define NFC_APP_PAGES_NUM             135U
#define NFC_APP_NDEF_FIRST_PAGE       4U
#define NFC_APP_NDEF_LAST_PAGE        95U
#define NFC_APP_CMD_FIRST_PAGE        112U
#define NFC_APP_CMD_LAST_PAGE         119U
#define NFC_APP_STORED_PAYLOAD_LEN    224U
#define NFC_APP_CMD_BYTES             ((NFC_APP_CMD_LAST_PAGE - NFC_APP_CMD_FIRST_PAGE + 1U) * 4U)
#define NFC_APP_NDEF_BYTES            ((NFC_APP_NDEF_LAST_PAGE - NFC_APP_NDEF_FIRST_PAGE + 1U) * 4U)
#define NFC_APP_CMD_PREFIX            "NWK1,"
#define NFC_APP_JSON_CMD_PREFIX       "\"cmd\":\"NWK1\""
#define NFC_APP_JSON_TOKEN_KEY        "\"token\":\""
#define NFC_APP_TOKEN_HEX_LEN         16U
#define NFC_APP_CMD_MIN_LEN           (5U + NFC_APP_TOKEN_HEX_LEN)
#define NFC_APP_TYPE2_SAK             0x00
#define NFC_APP_ACK_VALUE             0x0A
#define NFC_APP_NAK_INVALID_ARG       0x00
#define NFC_APP_WAKE_ACTIVE_LEVEL     1U
#define NFC_APP_PRESENT_HOLD_TICKS    ((3000UL * 8UL) / 5UL)
#define NFC_APP_PICC_WINDOW_TICKS     ((5000UL * 8UL) / 5UL)
#define NFC_APP_PICC_HARD_MAX_TICKS   ((8000UL * 8UL) / 5UL)
#define NFC_APP_DEFER_LOG_TICKS       ((3000UL * 8UL) / 5UL)
#define NFC_APP_BLE_QUIET_TICKS       ((100UL * 8UL) / 5UL)
#define NFC_APP_BLE_BOOT_COOLDOWN_TICKS ((1000UL * 8UL) / 5UL)
#define NFC_APP_SESSION_TIMEOUT_MS    8000U
#define NFC_APP_READ_DONE_IDLE_MS     500U
#define NFC_APP_NDEF_WRITE_IDLE_MS    120U
#define NFC_APP_SESSION_REQ_MAGIC     0x4EU
#define NFC_APP_SESSION_DONE_MAGIC    0x4FU
#define NFC_APP_SESSION_AUTH_MAGIC    0x50U
#define NFC_APP_PAYLOAD_MAGIC         0x4EU
#define NFC_APP_PAYLOAD_VERSION       0x01U
#define NFC_APP_PAYLOAD_HEADER_LEN    4U

#ifndef ENABLE_APP_NFC_AUTO_PICC_WINDOW
#define ENABLE_APP_NFC_AUTO_PICC_WINDOW 0
#endif

#define CMD_READ                      0x30
#define CMD_WRITE                     0xA2

typedef union
{
    uint32_t data32;
    uint16_t data16[2];
    uint8_t data8[4];
} nfc_app_page_t;

typedef enum
{
    NFC_APP_STATE_IDLE = 0,
    NFC_APP_STATE_READY1,
    NFC_APP_STATE_READY2,
    NFC_APP_STATE_ACTIVE,
} nfc_app_state_t;

typedef enum
{
    NFC_APP_STOP_TIMEOUT = 0,
    NFC_APP_STOP_MANUAL,
    NFC_APP_STOP_AUTH,
    NFC_APP_STOP_START_FAIL,
    NFC_APP_STOP_READ_DONE,
    NFC_APP_STOP_FIELD_LOST,
} nfc_app_stop_reason_t;

static nfc_app_page_t s_nfc_pages[NFC_APP_PAGES_NUM] __attribute__((aligned(4)));
static nfc_app_page_t s_nfc_ndef_backup[NFC_APP_NDEF_LAST_PAGE - NFC_APP_NDEF_FIRST_PAGE + 1U] __attribute__((aligned(4)));
static uint8_t s_uid[7];
static uint8_t s_uid_cl1[4];
static uint8_t s_uid_cl2[4];
static uint16_t s_payload_len = 0;
static volatile uint8_t s_nfc_state = NFC_APP_STATE_IDLE;
static volatile uint8_t s_field_online = Is_No;
static volatile uint8_t s_cmd_dirty = Is_No;
static volatile uint8_t s_ndef_dirty = Is_No;
static uint8_t s_initialized = Is_No;
static uint8_t s_picc_running = Is_No;
static uint8_t s_last_auth_result = 0;
static volatile uint8_t s_nfc_present_flag = Is_No;
static volatile uint32_t s_nfc_wake_irq_count = 0;
static uint32_t s_nfc_present_until_tick = 0;
static uint8_t s_present_consumed = Is_No;
static uint8_t s_window_pending_start = Is_No;
static uint8_t s_window_active = Is_No;
static uint8_t s_window_manual = Is_No;
static uint8_t s_window_ble_paused = Is_No;
static uint8_t s_window_stop_requested = Is_No;
static nfc_app_stop_reason_t s_window_stop_reason = NFC_APP_STOP_TIMEOUT;
static uint32_t s_window_start_tick = 0;
static uint32_t s_window_stop_tick = 0;
static uint32_t s_window_hard_stop_tick = 0;
static uint32_t s_last_defer_log_tick = 0;
static uint32_t s_session_cooldown_until_tick = 0;
static uint8_t s_nfc_only_session_running = Is_No;
static uint8_t s_nfc_only_auth_ok = Is_No;
static volatile uint8_t s_ndef_read_seen = Is_No;
static volatile uint8_t s_ndef_write_seen = Is_No;
static volatile uint8_t s_hlta_seen = Is_No;
static volatile uint8_t s_select_seen = Is_No;
static volatile uint32_t s_nfc_rx_seq = 0;
static volatile uint32_t s_nfc_activity_seq = 0;

static const uint8_t s_nfc_master_secret[16] =
{
    0x4E, 0x46, 0x43, 0x2D, 0x54, 0x44, 0x58, 0x31,
    0x2D, 0x43, 0x48, 0x35, 0x38, 0x35, 0x21, 0x01
};

static void NfcApp_PiccOnline(void);
static uint16_t NfcApp_PiccDataHandler(uint16_t bits_num);
static void NfcApp_PiccOffline(void);
static void NfcApp_ConfigWakePin(void);
static void NfcApp_PollWakeFlag(void);
static void NfcApp_PollPiccWindow(void);
static uint8_t NfcApp_OpenPiccWindow(uint8_t manual);
static void NfcApp_ClosePiccWindow(nfc_app_stop_reason_t reason);
static void NfcApp_LoadMacFromRom(void);
static void NfcApp_BackupNdefPages(void);
static void NfcApp_RestoreNdefPages(void);

static nfca_picc_cb_t s_nfc_cb =
{
    .online = NfcApp_PiccOnline,
    .data_handler = NfcApp_PiccDataHandler,
    .offline = NfcApp_PiccOffline,
};

static uint8_t NfcApp_IsHex(uint8_t value)
{
    return ((value >= '0' && value <= '9') ||
            (value >= 'A' && value <= 'F') ||
            (value >= 'a' && value <= 'f')) ? Is_Yes : Is_No;
}

static uint8_t NfcApp_ToUpperHex(uint8_t value)
{
    value &= 0x0F;
    return (uint8_t)((value < 10U) ? ('0' + value) : ('A' + value - 10U));
}

static void NfcApp_GetMac(uint8_t *mac)
{
#if(defined(BLE_MAC)) && (BLE_MAC == TRUE)
    memcpy(mac, MacAddr, 6);
#else
    memcpy(mac, Mac, 6);
#endif
}

static uint8_t NfcApp_IsValidMac(uint8_t *mac)
{
    uint8_t i;
    uint8_t all_00 = Is_Yes;
    uint8_t all_ff = Is_Yes;

    for(i = 0; i < 6; i++)
    {
        if(mac[i] != 0x00U)
        {
            all_00 = Is_No;
        }
        if(mac[i] != 0xFFU)
        {
            all_ff = Is_No;
        }
    }
    return (all_00 == Is_No && all_ff == Is_No) ? Is_Yes : Is_No;
}

static void NfcApp_LoadMacFromRom(void)
{
#if !((defined(BLE_MAC)) && (BLE_MAC == TRUE))
    uint8_t rom_mac[6];
    uint8_t flash_mac[6];

    memset(rom_mac, 0, sizeof(rom_mac));
    GetMACAddress(rom_mac);
    if(NfcApp_IsValidMac(rom_mac) == Is_Yes)
    {
        memcpy(Mac, rom_mac, sizeof(rom_mac));
        return;
    }

    Get_EEPROM_Flag(flash_mac, MAC_Position, MAC_Len);
    if(NfcApp_IsValidMac(flash_mac) == Is_Yes)
    {
        memcpy(Mac, flash_mac, sizeof(flash_mac));
    }
#endif
}

static void NfcApp_MakeUid(void)
{
    uint8_t mac[6];

    NfcApp_GetMac(mac);
    s_uid[0] = 0x04;
    memcpy(&s_uid[1], mac, 6);

    s_uid_cl1[0] = ISO14443A_UID0_CT;
    s_uid_cl1[1] = s_uid[0];
    s_uid_cl1[2] = s_uid[1];
    s_uid_cl1[3] = s_uid[2];
    memcpy(s_uid_cl2, &s_uid[3], sizeof(s_uid_cl2));
}

static void NfcApp_ClearPageRange(uint8_t first_page, uint8_t last_page)
{
    uint8_t page;

    for(page = first_page; page <= last_page; page++)
    {
        s_nfc_pages[page].data32 = 0;
    }
}

static void NfcApp_WriteByteToPageArea(uint8_t first_page, uint16_t offset, uint8_t value)
{
    uint8_t page = (uint8_t)(first_page + (offset / 4U));
    uint8_t byte = (uint8_t)(offset & 0x03U);

    if(page < NFC_APP_PAGES_NUM)
    {
        s_nfc_pages[page].data8[byte] = value;
    }
}

static void NfcApp_WriteNdefJson(uint8_t *json, uint16_t json_len)
{
    uint16_t offset = 0;
    uint8_t ndef_len = (uint8_t)(json_len + 7U);
    uint8_t text_payload_len = (uint8_t)(json_len + 3U);
    uint16_t i;

    NfcApp_ClearPageRange(NFC_APP_NDEF_FIRST_PAGE, NFC_APP_NDEF_LAST_PAGE);
    NfcApp_WriteByteToPageArea(NFC_APP_NDEF_FIRST_PAGE, offset++, 0x03);
    NfcApp_WriteByteToPageArea(NFC_APP_NDEF_FIRST_PAGE, offset++, ndef_len);
    NfcApp_WriteByteToPageArea(NFC_APP_NDEF_FIRST_PAGE, offset++, 0xD1);
    NfcApp_WriteByteToPageArea(NFC_APP_NDEF_FIRST_PAGE, offset++, 0x01);
    NfcApp_WriteByteToPageArea(NFC_APP_NDEF_FIRST_PAGE, offset++, text_payload_len);
    NfcApp_WriteByteToPageArea(NFC_APP_NDEF_FIRST_PAGE, offset++, 'T');
    NfcApp_WriteByteToPageArea(NFC_APP_NDEF_FIRST_PAGE, offset++, 0x02);
    NfcApp_WriteByteToPageArea(NFC_APP_NDEF_FIRST_PAGE, offset++, 'e');
    NfcApp_WriteByteToPageArea(NFC_APP_NDEF_FIRST_PAGE, offset++, 'n');

    for(i = 0; i < json_len; i++)
    {
        NfcApp_WriteByteToPageArea(NFC_APP_NDEF_FIRST_PAGE, offset++, json[i]);
    }
    NfcApp_WriteByteToPageArea(NFC_APP_NDEF_FIRST_PAGE, offset++, 0xFE);
    s_payload_len = json_len;
}

static void NfcApp_LoadDefaultJson(void)
{
    uint8_t mac[6];
    char json[96];
    int len;

    NfcApp_GetMac(mac);
    len = sprintf(json,
                  "{\"mac\":\"%02X%02X%02X%02X%02X%02X\",\"wifi\":\"sleep\",\"nfc\":\"ready\"}",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if(len < 0)
    {
        len = 0;
        json[0] = '\0';
    }
    NfcApp_WriteNdefJson((uint8_t *)json, (uint16_t)len);
}

static uint8_t NfcApp_PayloadChecksum(uint8_t *data, uint16_t len)
{
    uint16_t i;
    uint8_t checksum = 0;

    for(i = 0; i < len; i++)
    {
        checksum = (uint8_t)(checksum + data[i]);
    }
    return checksum;
}

static uint8_t NfcApp_LoadStoredJsonPayload(void)
{
    uint8_t store[EEPROM_PAGE_SIZE];
    uint16_t len;

    EEPROM_READ(NFC_DATAFLASH_ADD, (uint32_t *)store, EEPROM_PAGE_SIZE);
    len = store[2];

    if(store[0] != NFC_APP_PAYLOAD_MAGIC ||
       store[1] != NFC_APP_PAYLOAD_VERSION ||
       len == 0U ||
       len > NFC_APP_JSON_MAX_LEN ||
       len > (NFC_APP_STORED_PAYLOAD_LEN - NFC_APP_PAYLOAD_HEADER_LEN))
    {
        return Is_No;
    }

    if(store[3] != NfcApp_PayloadChecksum(&store[NFC_APP_PAYLOAD_HEADER_LEN], len))
    {
        return Is_No;
    }

    NfcApp_WriteNdefJson(&store[NFC_APP_PAYLOAD_HEADER_LEN], len);
    Print_I3("[NFC] stored payload loaded len=%u", len);
    return Is_Yes;
}

static uint8_t NfcApp_CheckStoredJsonPayload(uint8_t *data, uint16_t len)
{
    uint8_t store[EEPROM_PAGE_SIZE];

    EEPROM_READ(NFC_DATAFLASH_ADD, (uint32_t *)store, EEPROM_PAGE_SIZE);

    if(data == NULL || len == 0U)
    {
        return (store[0] == 0xFFU || store[0] == 0x00U) ? Is_Yes : Is_No;
    }

    if(store[0] != NFC_APP_PAYLOAD_MAGIC ||
       store[1] != NFC_APP_PAYLOAD_VERSION ||
       store[2] != (uint8_t)len ||
       store[3] != NfcApp_PayloadChecksum(data, len))
    {
        return Is_No;
    }

    return (memcmp(&store[NFC_APP_PAYLOAD_HEADER_LEN], data, len) == 0) ? Is_Yes : Is_No;
}

static uint8_t NfcApp_SaveStoredJsonPayload(uint8_t *data, uint16_t len)
{
    uint8_t store[EEPROM_PAGE_SIZE];
    uint32_t irq_status;

    memset(store, 0xFF, sizeof(store));
    if(data != NULL && len > 0U && len <= NFC_APP_JSON_MAX_LEN)
    {
        store[0] = NFC_APP_PAYLOAD_MAGIC;
        store[1] = NFC_APP_PAYLOAD_VERSION;
        store[2] = (uint8_t)len;
        store[3] = NfcApp_PayloadChecksum(data, len);
        memcpy(&store[NFC_APP_PAYLOAD_HEADER_LEN], data, len);
    }

    SYS_DisableAllIrq(&irq_status);
    EEPROM_ERASE(NFC_DATAFLASH_ADD, EEPROM_PAGE_SIZE);
    EEPROM_WRITE(NFC_DATAFLASH_ADD, (uint32_t *)store, EEPROM_PAGE_SIZE);
    SYS_RecoverIrq(irq_status);

    return NfcApp_CheckStoredJsonPayload(data, len);
}

static void NfcApp_InitType2Pages(void)
{
    memset(s_nfc_pages, 0, sizeof(s_nfc_pages));

    s_nfc_pages[0].data8[0] = s_uid[0];
    s_nfc_pages[0].data8[1] = s_uid[1];
    s_nfc_pages[0].data8[2] = s_uid[2];
    s_nfc_pages[0].data8[3] = ISO14443A_UID0_CT ^ s_uid[0] ^ s_uid[1] ^ s_uid[2];
    s_nfc_pages[1].data8[0] = s_uid[3];
    s_nfc_pages[1].data8[1] = s_uid[4];
    s_nfc_pages[1].data8[2] = s_uid[5];
    s_nfc_pages[1].data8[3] = s_uid[6];
    s_nfc_pages[2].data8[0] = s_uid[3] ^ s_uid[4] ^ s_uid[5] ^ s_uid[6];
    s_nfc_pages[2].data8[1] = 0x48;
    s_nfc_pages[2].data8[2] = 0x00;
    s_nfc_pages[2].data8[3] = 0x00;
    s_nfc_pages[3].data8[0] = 0xE1;
    s_nfc_pages[3].data8[1] = 0x10;
    s_nfc_pages[3].data8[2] = 0x3E;
    s_nfc_pages[3].data8[3] = 0x00;

    NfcApp_LoadDefaultJson();
    (void)NfcApp_LoadStoredJsonPayload();
    NfcApp_ClearPageRange(NFC_APP_CMD_FIRST_PAGE, NFC_APP_CMD_LAST_PAGE);
    NfcApp_BackupNdefPages();
}

static void NfcApp_BuildExpectedToken(char *token)
{
    uint8_t mac[6];
    uint8_t plain[16];
    uint8_t cipher[16];
    WORD key_schedule[AES_BLOCK_SIZE * 4];
    uint8_t i;

    memset(plain, 0, sizeof(plain));
    NfcApp_GetMac(mac);
    memcpy(plain, mac, 6);
    memcpy(&plain[6], "NFCWAKE", 7);
    plain[13] = 0x01;

    aes_key_setup(s_nfc_master_secret, key_schedule, 128);
    aes_encrypt(plain, cipher, key_schedule, 128);

    for(i = 0; i < 8; i++)
    {
        token[i * 2U] = (char)NfcApp_ToUpperHex(cipher[i] >> 4);
        token[i * 2U + 1U] = (char)NfcApp_ToUpperHex(cipher[i]);
    }
    token[NFC_APP_TOKEN_HEX_LEN] = '\0';
}

static uint8_t NfcApp_CopyCommandText(char *out, uint8_t out_size)
{
    uint8_t i;
    uint8_t value;

    if(out_size == 0)
    {
        return 0;
    }

    for(i = 0; i < NFC_APP_CMD_BYTES && i < (uint8_t)(out_size - 1U); i++)
    {
        value = s_nfc_pages[NFC_APP_CMD_FIRST_PAGE + (i / 4U)].data8[i & 0x03U];
        if(value == 0x00 || value == 0xFF)
        {
            break;
        }
        out[i] = (char)value;
    }
    out[i] = '\0';
    return i;
}

static void NfcApp_ClearCommandPages(void)
{
    NfcApp_ClearPageRange(NFC_APP_CMD_FIRST_PAGE, NFC_APP_CMD_LAST_PAGE);
}

static void NfcApp_BackupNdefPages(void)
{
    memcpy(s_nfc_ndef_backup,
           &s_nfc_pages[NFC_APP_NDEF_FIRST_PAGE],
           sizeof(s_nfc_ndef_backup));
}

static void NfcApp_RestoreNdefPages(void)
{
    memcpy(&s_nfc_pages[NFC_APP_NDEF_FIRST_PAGE],
           s_nfc_ndef_backup,
           sizeof(s_nfc_ndef_backup));
}

static uint8_t NfcApp_ReadNdefAreaByte(uint16_t offset)
{
    uint8_t page = (uint8_t)(NFC_APP_NDEF_FIRST_PAGE + (offset / 4U));
    uint8_t byte = (uint8_t)(offset & 0x03U);

    if(offset >= NFC_APP_NDEF_BYTES || page > NFC_APP_NDEF_LAST_PAGE)
    {
        return 0;
    }
    return s_nfc_pages[page].data8[byte];
}

static uint16_t NfcApp_CopyNdefText(char *out, uint16_t out_size)
{
    uint16_t offset = 0;
    uint16_t ndef_len;
    uint16_t payload_len;
    uint16_t text_offset;
    uint8_t status;
    uint8_t lang_len;
    uint16_t copy_len;
    uint16_t i;

    if(out == NULL || out_size == 0)
    {
        return 0;
    }
    out[0] = '\0';

    while(offset < NFC_APP_NDEF_BYTES && NfcApp_ReadNdefAreaByte(offset) == 0x00)
    {
        offset++;
    }

    if(offset >= NFC_APP_NDEF_BYTES || NfcApp_ReadNdefAreaByte(offset++) != 0x03)
    {
        return 0;
    }

    ndef_len = NfcApp_ReadNdefAreaByte(offset++);
    if(ndef_len == 0xFFU)
    {
        ndef_len = ((uint16_t)NfcApp_ReadNdefAreaByte(offset) << 8) | NfcApp_ReadNdefAreaByte(offset + 1U);
        offset += 2U;
    }

    if(ndef_len < 7U || (offset + ndef_len) > NFC_APP_NDEF_BYTES)
    {
        return 0;
    }

    if((NfcApp_ReadNdefAreaByte(offset) & 0xD7U) != 0xD1U ||
       NfcApp_ReadNdefAreaByte(offset + 1U) != 0x01U ||
       NfcApp_ReadNdefAreaByte(offset + 3U) != 'T')
    {
        return 0;
    }

    payload_len = NfcApp_ReadNdefAreaByte(offset + 2U);
    if(payload_len < 1U || payload_len > (ndef_len - 4U))
    {
        return 0;
    }

    status = NfcApp_ReadNdefAreaByte(offset + 4U);
    lang_len = status & 0x3FU;
    if(payload_len <= (uint16_t)(1U + lang_len))
    {
        return 0;
    }

    text_offset = offset + 5U + lang_len;
    copy_len = (uint16_t)(payload_len - 1U - lang_len);
    if(copy_len >= out_size)
    {
        copy_len = (uint16_t)(out_size - 1U);
    }

    for(i = 0; i < copy_len; i++)
    {
        out[i] = (char)NfcApp_ReadNdefAreaByte(text_offset + i);
    }
    out[copy_len] = '\0';
    return copy_len;
}

static uint8_t NfcApp_FindJsonToken(char *cmd, uint8_t len, char **token)
{
    char *p;

    if(cmd == NULL || token == NULL || len < (sizeof(NFC_APP_JSON_CMD_PREFIX) - 1U))
    {
        return Is_No;
    }

    if(strstr(cmd, NFC_APP_JSON_CMD_PREFIX) == NULL)
    {
        return Is_No;
    }

    p = strstr(cmd, NFC_APP_JSON_TOKEN_KEY);
    if(p == NULL)
    {
        return Is_No;
    }
    p += sizeof(NFC_APP_JSON_TOKEN_KEY) - 1U;
    if((uint16_t)(p - cmd) + NFC_APP_TOKEN_HEX_LEN > len)
    {
        return Is_No;
    }
    *token = p;
    return Is_Yes;
}

static uint8_t NfcApp_HandleAuthText(char *cmd, uint8_t len, uint8_t restore_ndef)
{
    char expected[NFC_APP_TOKEN_HEX_LEN + 1U];
    char *token = NULL;
    uint8_t i;

    if(len == 0)
    {
        return Is_No;
    }

    if(len < NFC_APP_CMD_MIN_LEN && strncmp(cmd, NFC_APP_CMD_PREFIX, len < 5U ? len : 5U) == 0)
    {
        return Is_No;
    }

    if(len >= NFC_APP_CMD_MIN_LEN && strncmp(cmd, NFC_APP_CMD_PREFIX, 5) == 0)
    {
        token = &cmd[5];
    }
    else if(NfcApp_FindJsonToken(cmd, len, &token) != Is_Yes)
    {
        s_last_auth_result = 2;
        Print_I3("[NFC] auth fail BAD_FORMAT len=%d", len);
        if(restore_ndef == Is_Yes)
        {
            NfcApp_RestoreNdefPages();
        }
        else
        {
            NfcApp_ClearCommandPages();
        }
        return Is_No;
    }

    if(token == NULL)
    {
        s_last_auth_result = 2;
        return Is_No;
    }

    for(i = 0; i < NFC_APP_TOKEN_HEX_LEN; i++)
    {
        if(NfcApp_IsHex((uint8_t)token[i]) != Is_Yes)
        {
            s_last_auth_result = 2;
            Print_I3("[NFC] auth fail BAD_FORMAT token");
            if(restore_ndef == Is_Yes)
            {
                NfcApp_RestoreNdefPages();
            }
            else
            {
                NfcApp_ClearCommandPages();
            }
            return Is_No;
        }
        if(token[i] >= 'a' && token[i] <= 'f')
        {
            token[i] = (char)(token[i] - 'a' + 'A');
        }
    }

    NfcApp_BuildExpectedToken(expected);
    if(memcmp(token, expected, NFC_APP_TOKEN_HEX_LEN) == 0)
    {
        s_last_auth_result = 1;
        Print_I3("[NFC] auth OK");
        if(restore_ndef == Is_Yes)
        {
            NfcApp_RestoreNdefPages();
        }
        else
        {
            NfcApp_ClearCommandPages();
        }
        if(s_nfc_only_session_running == Is_Yes)
        {
            s_nfc_only_auth_ok = Is_Yes;
        }
        else
        {
            WifiPassthrough_OnNfcAuthorizedWake();
        }
        s_window_stop_requested = Is_Yes;
        s_window_stop_reason = NFC_APP_STOP_AUTH;
        return Is_Yes;
    }

    s_last_auth_result = 3;
    Print_I3("[NFC] auth fail BAD_TOKEN");
    if(restore_ndef == Is_Yes)
    {
        NfcApp_RestoreNdefPages();
    }
    else
    {
        NfcApp_ClearCommandPages();
    }
    return Is_No;
}

static void NfcApp_ProcessCommand(void)
{
    char cmd[NFC_APP_CMD_BYTES + 1U];
    uint8_t len;

    len = NfcApp_CopyCommandText(cmd, sizeof(cmd));
    s_cmd_dirty = Is_No;
    (void)NfcApp_HandleAuthText(cmd, len, Is_No);
}

static void NfcApp_ProcessNdefWrite(void)
{
    char text[NFC_APP_JSON_MAX_LEN + 1U];
    uint16_t len;

    len = NfcApp_CopyNdefText(text, sizeof(text));
    s_ndef_dirty = Is_No;
    Print_I3("[NFC] NDEF write text len=%u", len);
    (void)NfcApp_HandleAuthText(text, (uint8_t)len, Is_Yes);
}

static uint8_t NfcApp_ReadPb15(void)
{
    return (GPIOB_ReadPortPin(GPIO_Pin_15) != 0U) ? 1U : 0U;
}

static void NfcApp_ConfigWakePin(void)
{
    GPIOB_ModeCfg(GPIO_Pin_15, GPIO_ModeIN_PD);
    GPIOB_ClearITFlagBit(GPIO_Pin_15);
    GPIOB_ITModeCfg(GPIO_Pin_15, GPIO_ITMode_RiseEdge);
    PFIC_EnableIRQ(GPIO_B_IRQn);
}

static void NfcApp_PollWakeFlag(void)
{
    static uint32_t s_last_seen_irq_count = 0;
    uint8_t pb15;
    uint8_t detected;
    uint8_t next_present;
    uint32_t now;
    uint32_t irq_count;

    pb15 = NfcApp_ReadPb15();
    now = TMOS_GetSystemClock();
    irq_count = s_nfc_wake_irq_count;
    detected = ((pb15 == NFC_APP_WAKE_ACTIVE_LEVEL) || (irq_count != s_last_seen_irq_count)) ? Is_Yes : Is_No;

    if(irq_count != s_last_seen_irq_count)
    {
        s_last_seen_irq_count = irq_count;
    }

    if(detected == Is_Yes)
    {
        s_nfc_present_until_tick = now + NFC_APP_PRESENT_HOLD_TICKS;
        next_present = Is_Yes;
    }
    else if(s_nfc_present_flag == Is_Yes && (int32_t)(now - s_nfc_present_until_tick) < 0)
    {
        next_present = Is_Yes;
    }
    else
    {
        next_present = Is_No;
    }

    if(next_present != s_nfc_present_flag)
    {
        s_nfc_present_flag = next_present;
        if(s_nfc_present_flag != Is_Yes)
        {
            s_present_consumed = Is_No;
        }
        Print_I3("[NFC] present_flag=%d pb15=%d irq=%lu port=%08lX mode=PD rise_irq active=HIGH picc=%d",
                 s_nfc_present_flag,
                 pb15,
                 s_nfc_wake_irq_count,
                 GPIOB_ReadPort(),
                 s_picc_running);
    }
}

static uint8_t NfcApp_TickExpired(uint32_t now, uint32_t target)
{
    return ((int32_t)(now - target) >= 0) ? Is_Yes : Is_No;
}

static char *NfcApp_StopReasonText(nfc_app_stop_reason_t reason)
{
    switch(reason)
    {
        case NFC_APP_STOP_MANUAL:
            return "manual";
        case NFC_APP_STOP_AUTH:
            return "auth";
        case NFC_APP_STOP_START_FAIL:
            return "start_fail";
        case NFC_APP_STOP_READ_DONE:
            return "read_done";
        case NFC_APP_STOP_FIELD_LOST:
            return "field_lost";
        case NFC_APP_STOP_TIMEOUT:
        default:
            return "timeout";
    }
}

static uint8_t NfcApp_CanAutoOpenWindow(char **reason)
{
    uint32_t now = TMOS_GetSystemClock();

    if(s_session_cooldown_until_tick != 0U &&
       NfcApp_TickExpired(now, s_session_cooldown_until_tick) != Is_Yes)
    {
        *reason = "NFC cooldown";
        return Is_No;
    }
    s_session_cooldown_until_tick = 0;

    if(Peripheral_IsBleConnected() == Is_Yes)
    {
        *reason = "BLE connected";
        return Is_No;
    }

#ifdef ENABLE_WIFI_UART1_PASSTHROUGH
    if(WifiPassthrough_IsWifiAwake() == Is_Yes || WifiPassthrough_IsWakePending() == Is_Yes)
    {
        *reason = "WiFi busy";
        return Is_No;
    }
#endif

    if(global_DEVICE_STATUS.fWorked == Is_Yes)
    {
        *reason = "device busy";
        return Is_No;
    }

    *reason = "OK";
    return Is_Yes;
}

static void NfcApp_ExtendPiccWindow(uint32_t now)
{
    uint32_t next_stop = now + NFC_APP_PICC_WINDOW_TICKS;

    if(NfcApp_TickExpired(next_stop, s_window_hard_stop_tick) == Is_Yes)
    {
        next_stop = s_window_hard_stop_tick;
    }
    s_window_stop_tick = next_stop;
}

static uint8_t NfcApp_OpenPiccWindow(uint8_t manual)
{
    uint8_t ret;
    uint32_t now;
    char *reason;

    now = TMOS_GetSystemClock();
    if(s_window_active == Is_Yes || s_window_pending_start == Is_Yes)
    {
        NfcApp_ExtendPiccWindow(now);
        return NFC_APP_STATUS_OK;
    }

    if(manual != Is_Yes && NfcApp_CanAutoOpenWindow(&reason) != Is_Yes)
    {
        if(s_last_defer_log_tick == 0 ||
           NfcApp_TickExpired(now, s_last_defer_log_tick + NFC_APP_DEFER_LOG_TICKS) == Is_Yes)
        {
            s_last_defer_log_tick = now;
            Print_I3("[NFC] window deferred: %s", reason);
        }
        return NFC_APP_STATUS_BUSY;
    }

    s_window_manual = (manual == Is_Yes) ? Is_Yes : Is_No;
    s_window_ble_paused = Is_No;
    s_window_stop_requested = Is_No;
    s_window_stop_reason = NFC_APP_STOP_TIMEOUT;
    s_window_start_tick = now;
    s_window_stop_tick = now + NFC_APP_PICC_WINDOW_TICKS;
    s_window_hard_stop_tick = now + NFC_APP_PICC_HARD_MAX_TICKS;

    if(manual != Is_Yes)
    {
        Peripheral_NfcPauseAdvertising();
        s_window_ble_paused = Is_Yes;
        s_window_start_tick = now + NFC_APP_BLE_QUIET_TICKS;
        s_window_pending_start = Is_Yes;
        Print_I3("[NFC] picc window pending, BLE quiet=%lu", NFC_APP_BLE_QUIET_TICKS);
        return NFC_APP_STATUS_OK;
    }

    ret = NfcApp_StartPicc();
    if(ret != NFC_APP_STATUS_OK)
    {
        NfcApp_ClosePiccWindow(NFC_APP_STOP_START_FAIL);
        return ret;
    }

    s_window_active = Is_Yes;
    Print_I3("[NFC] picc window start manual=%d timeout=%lu hard=%lu",
             s_window_manual,
             NFC_APP_PICC_WINDOW_TICKS,
             NFC_APP_PICC_HARD_MAX_TICKS);
    return NFC_APP_STATUS_OK;
}

static void NfcApp_ClosePiccWindow(nfc_app_stop_reason_t reason)
{
    if(s_picc_running == Is_Yes)
    {
        NfcApp_StopPicc();
    }

    if(s_window_pending_start == Is_Yes)
    {
        s_window_pending_start = Is_No;
    }

    if(s_window_ble_paused == Is_Yes)
    {
        Peripheral_NfcResumeAdvertising();
    }

    if(s_window_active == Is_Yes || reason == NFC_APP_STOP_START_FAIL)
    {
        Print_I3("[NFC] picc window stop reason=%s", NfcApp_StopReasonText(reason));
    }

    s_window_active = Is_No;
    s_window_pending_start = Is_No;
    s_window_manual = Is_No;
    s_window_ble_paused = Is_No;
    s_window_stop_requested = Is_No;
}

static void NfcApp_PollPiccWindow(void)
{
    uint32_t now;

    now = TMOS_GetSystemClock();

    if(s_window_pending_start == Is_Yes)
    {
        uint8_t ret;

        if(s_window_stop_requested == Is_Yes)
        {
            NfcApp_ClosePiccWindow(s_window_stop_reason);
            return;
        }

        if(NfcApp_TickExpired(now, s_window_start_tick) != Is_Yes)
        {
            return;
        }

        s_window_pending_start = Is_No;
        ret = NfcApp_StartPicc();
        if(ret != NFC_APP_STATUS_OK)
        {
            NfcApp_ClosePiccWindow(NFC_APP_STOP_START_FAIL);
            return;
        }

        s_window_active = Is_Yes;
        Print_I3("[NFC] picc window start manual=%d timeout=%lu hard=%lu",
                 s_window_manual,
                 NFC_APP_PICC_WINDOW_TICKS,
                 NFC_APP_PICC_HARD_MAX_TICKS);
        return;
    }

    if(s_window_active == Is_Yes)
    {
        if(s_field_online == Is_Yes)
        {
            NfcApp_ExtendPiccWindow(now);
        }

        if(s_window_stop_requested == Is_Yes)
        {
            NfcApp_ClosePiccWindow(s_window_stop_reason);
            return;
        }

        if(NfcApp_TickExpired(now, s_window_stop_tick) == Is_Yes ||
           NfcApp_TickExpired(now, s_window_hard_stop_tick) == Is_Yes)
        {
            NfcApp_ClosePiccWindow(NFC_APP_STOP_TIMEOUT);
        }
        return;
    }

    if(s_nfc_present_flag == Is_Yes && s_present_consumed != Is_Yes)
    {
        s_present_consumed = Is_Yes;
        NfcApp_RequestSessionBoot();
    }
}

static void NfcApp_PiccOnline(void)
{
    s_field_online = Is_Yes;
    s_hlta_seen = Is_No;
    s_nfc_state = NFC_APP_STATE_IDLE;
}

static void NfcApp_PiccOffline(void)
{
    s_field_online = Is_No;
    s_hlta_seen = Is_Yes;
    s_nfc_state = NFC_APP_STATE_IDLE;
}

__attribute__((section(".highcode")))
static uint16_t NfcApp_PiccDataHandler(uint16_t bits_num)
{
    uint16_t send_bits = 0;

    s_nfc_rx_seq++;

    if((bits_num == 7) &&
       (g_picc_data_buf[0] == ISO14443A_CMD_REQA || g_picc_data_buf[0] == ISO14443A_CMD_WUPA))
    {
        s_nfc_state = NFC_APP_STATE_READY1;
        g_picc_data_buf[0] = 0x44;
        g_picc_data_buf[1] = 0x00;
        ISO14443ACalOddParityBit(g_picc_data_buf, g_picc_parity_buf, 2);
        return 16;
    }

    switch(s_nfc_state)
    {
        case NFC_APP_STATE_READY1:
            if(g_picc_data_buf[0] == ISO14443A_CMD_SELECT_CL1)
            {
                if(ISO14443ASelect(g_picc_data_buf, &send_bits, s_uid_cl1, ISO14443A_SAK_INCOMPLETE) == 0)
                {
                    s_nfc_state = NFC_APP_STATE_READY2;
                }
                if(send_bits != 0)
                {
                    ISO14443ACalOddParityBit(g_picc_data_buf, g_picc_parity_buf, send_bits / 8U);
                }
            }
            else if(g_picc_data_buf[0] == ISO14443A_CMD_HLTA && g_picc_data_buf[1] == 0x00)
            {
                s_nfc_state = NFC_APP_STATE_IDLE;
            }
            else
            {
                s_nfc_state = NFC_APP_STATE_IDLE;
            }
            break;

        case NFC_APP_STATE_READY2:
            if(g_picc_data_buf[0] == ISO14443A_CMD_SELECT_CL2)
            {
                if(ISO14443ASelect(g_picc_data_buf, &send_bits, s_uid_cl2, NFC_APP_TYPE2_SAK) == 0)
                {
                    s_nfc_state = NFC_APP_STATE_ACTIVE;
                    s_select_seen = Is_Yes;
                }
                if(send_bits != 0)
                {
                    ISO14443ACalOddParityBit(g_picc_data_buf, g_picc_parity_buf, send_bits / 8U);
                }
            }
            else if(g_picc_data_buf[0] == ISO14443A_CMD_HLTA && g_picc_data_buf[1] == 0x00)
            {
                s_nfc_state = NFC_APP_STATE_IDLE;
            }
            else
            {
                s_nfc_state = NFC_APP_STATE_IDLE;
            }
            break;

        case NFC_APP_STATE_ACTIVE:
            if(g_picc_data_buf[0] == ISO14443A_CMD_HLTA && g_picc_data_buf[1] == 0x00)
            {
                s_hlta_seen = Is_Yes;
                s_nfc_activity_seq++;
                s_nfc_state = NFC_APP_STATE_IDLE;
            }
            else if(g_picc_data_buf[0] == CMD_READ)
            {
                uint8_t page_address = g_picc_data_buf[1];
                uint8_t first_page = page_address;
                uint8_t i;

                if(page_address < NFC_APP_PAGES_NUM)
                {
                    if(first_page >= NFC_APP_NDEF_FIRST_PAGE && first_page <= NFC_APP_NDEF_LAST_PAGE)
                    {
                        s_ndef_read_seen = Is_Yes;
                    }
                    s_nfc_activity_seq++;
                    for(i = 0; i < 4; i++)
                    {
                        g_picc_data_buf[i * 4U] = s_nfc_pages[page_address].data8[0];
                        g_picc_data_buf[i * 4U + 1U] = s_nfc_pages[page_address].data8[1];
                        g_picc_data_buf[i * 4U + 2U] = s_nfc_pages[page_address].data8[2];
                        g_picc_data_buf[i * 4U + 3U] = s_nfc_pages[page_address].data8[3];
                        page_address = (uint8_t)((page_address + 1U) % NFC_APP_PAGES_NUM);
                    }
                    ISO14443AAppendCRCA(g_picc_data_buf, 16);
                    ISO14443ACalOddParityBit(g_picc_data_buf, g_picc_parity_buf, 18);
                    send_bits = 18U * 8U;
                }
                else
                {
                    g_picc_data_buf[0] = NFC_APP_NAK_INVALID_ARG;
                    send_bits = 4;
                }
            }
            else if(g_picc_data_buf[0] == CMD_WRITE)
            {
                uint8_t page_address = g_picc_data_buf[1];

                if((page_address >= NFC_APP_NDEF_FIRST_PAGE && page_address <= NFC_APP_NDEF_LAST_PAGE) ||
                   (page_address >= NFC_APP_CMD_FIRST_PAGE && page_address <= NFC_APP_CMD_LAST_PAGE))
                {
                    s_nfc_pages[page_address].data8[0] = g_picc_data_buf[2];
                    s_nfc_pages[page_address].data8[1] = g_picc_data_buf[3];
                    s_nfc_pages[page_address].data8[2] = g_picc_data_buf[4];
                    s_nfc_pages[page_address].data8[3] = g_picc_data_buf[5];
                    if(page_address >= NFC_APP_CMD_FIRST_PAGE && page_address <= NFC_APP_CMD_LAST_PAGE)
                    {
                        s_cmd_dirty = Is_Yes;
                    }
                    else
                    {
                        s_ndef_dirty = Is_Yes;
                        s_ndef_write_seen = Is_Yes;
                    }
                    s_nfc_activity_seq++;
                    g_picc_data_buf[0] = NFC_APP_ACK_VALUE;
                }
                else
                {
                    g_picc_data_buf[0] = NFC_APP_NAK_INVALID_ARG;
                }
                send_bits = 4;
            }
            else
            {
                s_nfc_state = NFC_APP_STATE_IDLE;
            }
            break;

        default:
            s_nfc_state = NFC_APP_STATE_IDLE;
            break;
    }

    return send_bits;
}

void NfcApp_Init(void)
{
    if(s_initialized == Is_Yes)
    {
        return;
    }

    NfcApp_LoadMacFromRom();
    NfcApp_MakeUid();
    NfcApp_InitType2Pages();
    NfcApp_ConfigWakePin();
    nfca_picc_init();
    nfca_picc_register_callback(&s_nfc_cb);
    s_initialized = Is_Yes;
    Print_I3("[NFC] PICC T2T prepared payload=%d", s_payload_len);

#ifdef ENABLE_APP_NFC_AUTO_START
    NfcApp_StartPicc();
#else
    Print_I3("[NFC] PICC not auto-started, TIMER0/TIMER3 kept free");
#endif
}

uint8_t NfcApp_StartPicc(void)
{
    if(s_initialized != Is_Yes)
    {
        NfcApp_Init();
    }

    if(s_picc_running == Is_Yes)
    {
        return NFC_APP_STATUS_OK;
    }

    s_field_online = Is_No;
    s_nfc_state = NFC_APP_STATE_IDLE;
    s_cmd_dirty = Is_No;
    s_ndef_dirty = Is_No;
    s_ndef_read_seen = Is_No;
    s_ndef_write_seen = Is_No;
    s_hlta_seen = Is_No;
    s_select_seen = Is_No;
    s_nfc_rx_seq = 0;
    s_nfc_activity_seq = 0;
    PFIC_DisableIRQ(GPIO_B_IRQn);
    nfca_picc_start();
    s_picc_running = Is_Yes;
    Print_I3("[NFC] PICC started, TIMER0/TIMER3 owned by NFC");
    return NFC_APP_STATUS_OK;
}

void NfcApp_StopPicc(void)
{
    if(s_picc_running != Is_Yes)
    {
        return;
    }

    nfca_picc_stop();
    s_picc_running = Is_No;
    s_field_online = Is_No;
    s_nfc_state = NFC_APP_STATE_IDLE;
    NfcApp_ConfigWakePin();
    Print_I3("[NFC] PICC stopped");
}

uint8_t NfcApp_StartManualWindow(void)
{
    if(Peripheral_IsBleConnected() == Is_Yes)
    {
        Print_I3("[NFC] manual PICC denied: BLE connected");
        return NFC_APP_STATUS_BUSY;
    }

    return NfcApp_OpenPiccWindow(Is_Yes);
}

void NfcApp_StopManualWindow(void)
{
    NfcApp_ClosePiccWindow(NFC_APP_STOP_MANUAL);
}

void NfcApp_FastPoll(void)
{
    NfcApp_PollWakeFlag();

    if(s_cmd_dirty == Is_Yes)
    {
        NfcApp_ProcessCommand();
    }

    NfcApp_PollPiccWindow();
}

void NfcApp_EnterLowPowerIo(void)
{
    GPIOB_ModeCfg(GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_16 | GPIO_Pin_17, GPIO_ModeIN_Floating);
    R32_PIN_IN_DIS |= ((GPIO_Pin_8 | GPIO_Pin_9) << 16);
    R16_PIN_CONFIG |= ((GPIO_Pin_16 | GPIO_Pin_17) >> 8);

    GPIOB_ModeCfg(GPIO_Pin_14, GPIO_ModeIN_Floating);
    NfcApp_ConfigWakePin();
    PWR_PeriphWakeUpCfg(ENABLE, RB_SLP_GPIO_WAKE, Long_Delay);
}

uint8_t NfcApp_SetJsonPayload(uint8_t *data, uint16_t len)
{
    uint32_t irq_status;

    if(data == NULL && len != 0U)
    {
        return NFC_APP_STATUS_BAD_ARG;
    }

    if(len > NFC_APP_JSON_MAX_LEN)
    {
        return NFC_APP_STATUS_BAD_LEN;
    }

    if(s_picc_running == Is_Yes &&
       (s_field_online == Is_Yes || s_nfc_state == NFC_APP_STATE_ACTIVE))
    {
        return NFC_APP_STATUS_BUSY;
    }

    SYS_DisableAllIrq(&irq_status);
    if(len == 0U)
    {
        NfcApp_LoadDefaultJson();
    }
    else
    {
        NfcApp_WriteNdefJson(data, len);
    }
    SYS_RecoverIrq(irq_status);
    NfcApp_BackupNdefPages();

    if(len == 0U)
    {
        if(NfcApp_SaveStoredJsonPayload(NULL, 0) != Is_Yes)
        {
            Print_I3("[NFC] payload clear save verify failed");
            return NFC_APP_STATUS_BAD_ARG;
        }
        Print_I3("[NFC] payload cleared, default restored len=%u", s_payload_len);
    }
    else
    {
        if(NfcApp_SaveStoredJsonPayload(data, len) != Is_Yes)
        {
            Print_I3("[NFC] payload save verify failed len=%u", len);
            return NFC_APP_STATUS_BAD_ARG;
        }
        Print_I3("[NFC] payload set and saved len=%u", len);
    }
    return NFC_APP_STATUS_OK;
}

void NfcApp_ClearJsonPayload(void)
{
    NfcApp_SetJsonPayload(NULL, 0);
}

uint8_t NfcApp_IsSessionBootRequest(void)
{
    return (R8_GLOB_RESET_KEEP == NFC_APP_SESSION_REQ_MAGIC) ? Is_Yes : Is_No;
}

void NfcApp_RequestSessionBoot(void)
{
    char *reason;

    if(NfcApp_CanAutoOpenWindow(&reason) != Is_Yes)
    {
        Print_I3("[NFC] session deferred: %s", reason);
        return;
    }

    Print_I3("[NFC] session request, reboot to NFC-only");
    SYS_ResetKeepBuf(NFC_APP_SESSION_REQ_MAGIC);
    mDelaymS(10);
    SYS_ResetExecute();
    while(1);
}

void NfcApp_RunOnlySession(void)
{
    uint16_t elapsed_ms = 0;
    uint16_t last_activity_ms = 0;
    uint32_t last_activity_seq = 0;
    nfc_app_stop_reason_t reason = NFC_APP_STOP_TIMEOUT;
    uint8_t mac[6];
    uint8_t ret;

    SYS_ResetKeepBuf(0);
    WWDG_ResetCfg(DISABLE);
    NfcApp_LoadMacFromRom();
    NfcApp_GetMac(mac);
    Print_I3("[NFC] NFC-only boot mac=%02X %02X %02X %02X %02X %02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    NfcApp_Init();
    s_nfc_only_session_running = Is_Yes;
    s_nfc_only_auth_ok = Is_No;
    s_window_stop_requested = Is_No;
    s_window_stop_reason = NFC_APP_STOP_TIMEOUT;
    s_ndef_read_seen = Is_No;
    s_ndef_write_seen = Is_No;
    s_ndef_dirty = Is_No;
    s_hlta_seen = Is_No;
    s_select_seen = Is_No;
    s_nfc_rx_seq = 0;
    s_nfc_activity_seq = 0;

    ret = NfcApp_StartPicc();
    if(ret != NFC_APP_STATUS_OK)
    {
        reason = NFC_APP_STOP_START_FAIL;
        goto session_out;
    }

    Print_I3("[NFC] NFC-only PICC started timeout=%ums read_idle=%ums",
             NFC_APP_SESSION_TIMEOUT_MS,
             NFC_APP_READ_DONE_IDLE_MS);

    last_activity_seq = s_nfc_activity_seq;
    while(elapsed_ms < NFC_APP_SESSION_TIMEOUT_MS)
    {
        WWDG_SetCounter(0);

        if((elapsed_ms != 0U) && ((elapsed_ms % 500U) == 0U))
        {
            Print_I3("[NFC] session poll %ums rx=%lu act=%lu online=%d sel=%d read=%d state=%d",
                     elapsed_ms,
                     s_nfc_rx_seq,
                     s_nfc_activity_seq,
                     s_field_online,
                     s_select_seen,
                     s_ndef_read_seen,
                     s_nfc_state);
        }

        if(s_cmd_dirty == Is_Yes)
        {
            NfcApp_ProcessCommand();
        }

        if(s_nfc_activity_seq != last_activity_seq)
        {
            last_activity_seq = s_nfc_activity_seq;
            last_activity_ms = elapsed_ms;
        }

        if(s_ndef_dirty == Is_Yes &&
           (uint16_t)(elapsed_ms - last_activity_ms) >= NFC_APP_NDEF_WRITE_IDLE_MS)
        {
            NfcApp_ProcessNdefWrite();
        }

        if(s_window_stop_requested == Is_Yes)
        {
            reason = s_window_stop_reason;
            break;
        }

        if(s_ndef_dirty != Is_Yes &&
           (s_ndef_read_seen == Is_Yes || s_ndef_write_seen == Is_Yes) &&
           (uint16_t)(elapsed_ms - last_activity_ms) >= NFC_APP_READ_DONE_IDLE_MS)
        {
            reason = NFC_APP_STOP_READ_DONE;
            break;
        }

        mDelaymS(1);
        elapsed_ms++;
    }

session_out:
    if(s_picc_running == Is_Yes)
    {
        NfcApp_StopPicc();
    }
    s_nfc_only_session_running = Is_No;
    Print_I3("[NFC] NFC-only stop reason=%s elapsed=%ums read=%d act=%lu",
             NfcApp_StopReasonText(reason),
             elapsed_ms,
             s_ndef_read_seen,
             s_nfc_activity_seq);
    SYS_ResetKeepBuf(s_nfc_only_auth_ok == Is_Yes ? NFC_APP_SESSION_AUTH_MAGIC : NFC_APP_SESSION_DONE_MAGIC);
    mDelaymS(10);
    SYS_ResetExecute();
    while(1);
}

void NfcApp_HandleBleBootPendingActions(void)
{
    uint8_t auth_wake = Is_No;

    if(R8_GLOB_RESET_KEEP == NFC_APP_SESSION_AUTH_MAGIC)
    {
        auth_wake = Is_Yes;
    }

    if(R8_GLOB_RESET_KEEP == NFC_APP_SESSION_DONE_MAGIC ||
       R8_GLOB_RESET_KEEP == NFC_APP_SESSION_AUTH_MAGIC)
    {
        SYS_ResetKeepBuf(0);
        s_session_cooldown_until_tick = TMOS_GetSystemClock() + NFC_APP_BLE_BOOT_COOLDOWN_TICKS;
        Print_I3("[NFC] BLE mode restored, cooldown=%lu", NFC_APP_BLE_BOOT_COOLDOWN_TICKS);
    }

    if(auth_wake == Is_Yes)
    {
        Print_I3("[NFC] authorized wake pending, wake WiFi");
        WifiPassthrough_OnNfcAuthorizedWake();
    }
}

void NfcApp_GetStatus(char *out, uint16_t out_size)
{
    if(out == NULL || out_size == 0)
    {
        return;
    }

    sprintf(out, "%s,%u,%s", s_picc_running == Is_Yes ? "READY" : "IDLE", s_payload_len, NfcApp_GetLastAuthText());
}

uint16_t NfcApp_GetPayloadLen(void)
{
    return s_payload_len;
}

char *NfcApp_GetLastAuthText(void)
{
    switch(s_last_auth_result)
    {
        case 1:
            return "OK";
        case 2:
            return "BAD_FORMAT";
        case 3:
            return "BAD_TOKEN";
        default:
            return "NONE";
    }
}

uint8_t NfcApp_IsPiccRunning(void)
{
    return s_picc_running;
}

uint8_t NfcApp_IsWindowBusy(void)
{
    return (s_window_pending_start == Is_Yes || s_window_active == Is_Yes || s_picc_running == Is_Yes) ? Is_Yes : Is_No;
}

uint8_t NfcApp_IsPresent(void)
{
    return s_nfc_present_flag;
}

__INTERRUPT
__HIGH_CODE
void GPIOB_IRQHandler(void)
{
    if(GPIOB_ReadITFlagBit(GPIO_Pin_15))
    {
        GPIOB_ClearITFlagBit(GPIO_Pin_15);
        s_nfc_wake_irq_count++;
    }
    else
    {
        GPIOB_ClearITFlagBit(GPIOB_ReadITFlagPort());
    }
}

#else

void NfcApp_Init(void) {}
uint8_t NfcApp_StartPicc(void)
{
    return NFC_APP_STATUS_BAD_ARG;
}
void NfcApp_StopPicc(void) {}
uint8_t NfcApp_StartManualWindow(void)
{
    return NFC_APP_STATUS_BAD_ARG;
}
void NfcApp_StopManualWindow(void) {}
void NfcApp_FastPoll(void) {}
void NfcApp_EnterLowPowerIo(void) {}
uint8_t NfcApp_SetJsonPayload(uint8_t *data, uint16_t len)
{
    (void)data;
    (void)len;
    return NFC_APP_STATUS_BAD_ARG;
}
void NfcApp_ClearJsonPayload(void) {}
uint8_t NfcApp_IsSessionBootRequest(void)
{
    return Is_No;
}
void NfcApp_RequestSessionBoot(void) {}
void NfcApp_RunOnlySession(void) {}
void NfcApp_HandleBleBootPendingActions(void) {}
void NfcApp_GetStatus(char *out, uint16_t out_size)
{
    if(out != NULL && out_size > 0)
    {
        out[0] = '\0';
    }
}
uint16_t NfcApp_GetPayloadLen(void)
{
    return 0;
}
char *NfcApp_GetLastAuthText(void)
{
    return "DISABLED";
}
uint8_t NfcApp_IsPiccRunning(void)
{
    return Is_No;
}
uint8_t NfcApp_IsWindowBusy(void)
{
    return Is_No;
}
uint8_t NfcApp_IsPresent(void)
{
    return Is_No;
}

#endif
