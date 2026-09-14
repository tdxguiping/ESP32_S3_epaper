#include "factory_selftest.h"

#if APP_FACTORY_UART0_ENABLE

#include <stdlib.h>
#include <string.h>

#include "aes_util.h"
#include "ch583_secure.h"
#include "commoninfo.h"
#include "adc_api.h"
#include "release_trace.h"
#include "release_uart0.h"

#define FACTORY_LINE_MAX            80U
#define FACTORY_AES_HEX_LENGTH       32U
#define FACTORY_AES_LENGTH           16U
#define FACTORY_EVENT_REBOOT         0x0001U
#define FACTORY_REBOOT_DELAY_TICKS   160U

static char s_factory_line[FACTORY_LINE_MAX];
static UINT8 s_factory_line_len;
static tmosTaskID s_factory_task_id = 0xffU;

static tmosEvents FactorySelftest_ProcessEvent(tmosTaskID task_id, tmosEvents events);

void __attribute__((weak)) FactorySelftest_OnAppLine(const char *line)
{
    (void)line;
}

static void Factory_WriteText(const char *text)
{
    if(text != NULL)
    {
        release_uart0_write((const UINT8 *)text, (UINT16)strlen(text));
    }
}

static void Factory_WriteHex(UINT8 value)
{
    static const char digits[] = "0123456789ABCDEF";
    UINT8 out[2];
    out[0] = (UINT8)digits[value >> 4];
    out[1] = (UINT8)digits[value & 0x0fU];
    release_uart0_write(out, 2U);
}

static void Factory_WriteDec(UINT32 value)
{
    UINT8 digits[10];
    UINT8 count = 0;
    do
    {
        digits[count++] = (UINT8)('0' + (value % 10U));
        value /= 10U;
    } while(value != 0U);
    while(count != 0U)
    {
        UINT8 c = digits[--count];
        release_uart0_write(&c, 1U);
    }
}

static UINT16 Factory_Crc16Update(UINT16 crc, UINT8 value)
{
    UINT8 bit;
    crc ^= (UINT16)value << 8;
    for(bit = 0; bit < 8U; bit++)
    {
        crc = (crc & 0x8000U) ? (UINT16)((crc << 1) ^ 0x1021U) : (UINT16)(crc << 1);
    }
    return crc;
}

static UINT16 Factory_CrcText(UINT16 crc, const char *text)
{
    while(*text != '\0')
    {
        crc = Factory_Crc16Update(crc, (UINT8)*text++);
    }
    return crc;
}

static UINT8 Factory_HexNibble(char value)
{
    if(value >= '0' && value <= '9') return (UINT8)(value - '0');
    if(value >= 'A' && value <= 'F') return (UINT8)(value - 'A' + 10);
    if(value >= 'a' && value <= 'f') return (UINT8)(value - 'a' + 10);
    return 0xffU;
}

static UINT8 Factory_ParseKey(const char *text, UINT8 out[FACTORY_AES_LENGTH])
{
    UINT8 i;
    if(text == NULL || strlen(text) != FACTORY_AES_HEX_LENGTH) return Is_No;
    for(i = 0; i < FACTORY_AES_LENGTH; i++)
    {
        UINT8 hi = Factory_HexNibble(text[i * 2U]);
        UINT8 lo = Factory_HexNibble(text[i * 2U + 1U]);
        if(hi == 0xffU || lo == 0xffU) return Is_No;
        out[i] = (UINT8)((hi << 4) | lo);
    }
    return Is_Yes;
}

static UINT8 Factory_CheckKey(UINT8 cipher[FACTORY_AES_LENGTH])
{
#ifdef ENABLE_BOARD_ENCRYPT
    char aes_key[KEY_Len];
    UINT8 expected_mac[MAC_Len];
    UINT8 i;
    unsigned int plain_len = 0;
    unsigned char *plain;

    get_aes_key(aes_key);
    plain = decrypt_ecb(cipher, FACTORY_AES_LENGTH, &plain_len, (unsigned char *)aes_key);
    if(plain == NULL) return Is_No;
    for(i = 0; i < MAC_Len; i++) expected_mac[i] = Mac[MAC_Len - 1U - i];
    i = (plain_len == MAC_Len && memcmp(plain, expected_mac, MAC_Len) == 0) ? Is_Yes : Is_No;
    free(plain);
    return i;
#else
    (void)cipher;
    return Is_No;
#endif
}

static UINT8 Factory_Register(const char *key_text)
{
    UINT8 cipher[FACTORY_AES_LENGTH];
    if(Factory_ParseKey(key_text, cipher) != Is_Yes || Factory_CheckKey(cipher) != Is_Yes) return Is_No;
    if(write_key_to_epprom(cipher, Is_Yes) != Is_Yes) return Is_No;
    return is_illegal_device(Mac, MAC_Len) == Is_Yes ? Is_Yes : Is_No;
}

static UINT8 Factory_CleanRegister(const char *key_text)
{
    UINT8 cipher[FACTORY_AES_LENGTH];
    UINT8 clear_key[KEY_Len];
    UINT8 clear_burn[KEY_BURN_Len];
    if(Factory_ParseKey(key_text, cipher) != Is_Yes || Factory_CheckKey(cipher) != Is_Yes) return Is_No;
    memset(clear_key, 0xff, sizeof(clear_key));
    memset(clear_burn, 0xff, sizeof(clear_burn));
    Save_Key_EEPROM_Flag(clear_key, KEY_Position, KEY_Len);
    Save_Key_EEPROM_Flag(clear_burn, KEY_BURN_Position, KEY_BURN_Len);
    global_DEVICE_STATUS.fisVaildDevice = Is_No;
    global_DEVICE_STATUS.fisBurnId = Is_No;
    return Is_Yes;
}

static char *Factory_Trim(char *text)
{
    char *end;
    while(*text == ' ' || *text == '\t') text++;
    end = text + strlen(text);
    while(end > text && (end[-1] == ' ' || end[-1] == '\t')) end--;
    *end = '\0';
    return text;
}

static void Factory_SendError(const char *reason)
{
    Factory_WriteText("facBleErr ");
    Factory_WriteText(reason);
    Factory_WriteText("\n");
}

static void Factory_HandleBle(char *payload)
{
    const char *key;
    if(strcmp(payload, "facReboot") == 0)
    {
        if(tmos_start_task(s_factory_task_id, FACTORY_EVENT_REBOOT, FACTORY_REBOOT_DELAY_TICKS) == 0)
            Factory_WriteText("facReboot failed\n");
        else
            Factory_WriteText("facReboot success\n");
        return;
    }
    if(strncmp(payload, "facCleanRegist", 15U) == 0 && (payload[15] == ' ' || payload[15] == '\t'))
    {
        key = Factory_Trim(payload + 15U);
        Factory_WriteText(Factory_CleanRegister(key) ? "facCleanRegist success\n" : "facCleanRegist failed\n");
        return;
    }
    if(strncmp(payload, "facRegist", 9U) == 0 && (payload[9] == ' ' || payload[9] == '\t'))
    {
        key = Factory_Trim(payload + 9U);
        Factory_WriteText(Factory_Register(key) ? "facRegist success\n" : "facRegist failed\n");
        return;
    }
    Factory_SendError("BAD_CMD");
}

static void Factory_ProcessLine(char *line)
{
    line = Factory_Trim(line);
    if(*line == '\0') return;
    if(strncmp(line, "ble ", 4U) == 0)
    {
        Factory_HandleBle(Factory_Trim(line + 4U));
    }
    else if(strncmp(line, "wifi", 4U) == 0 && (line[4] == '\0' || line[4] == ' ' || line[4] == '\t'))
    {
        Factory_SendError("BAD_CMD");
    }
    else
    {
        FactorySelftest_OnAppLine(line);
    }
}

static tmosEvents FactorySelftest_ProcessEvent(tmosTaskID task_id, tmosEvents events)
{
    (void)task_id;
    if(events & FACTORY_EVENT_REBOOT)
    {
        while((UART0_GetLinSTA() & STA_TXALL_EMP) == 0) { }
        SYS_ResetExecute();
        return events ^ FACTORY_EVENT_REBOOT;
    }
    return 0;
}

void FactorySelftest_Init(void)
{
    if(s_factory_task_id == 0xffU)
        s_factory_task_id = TMOS_ProcessEventRegister(FactorySelftest_ProcessEvent);
    s_factory_line_len = 0;
}

UINT8 FactorySelftest_ShouldBlockDeepSleep(void)
{
#if APP_FACTORY_POWER_HOLD_ENABLE
    /* Read PB13 directly: global_DEVICE_STATUS.fIsCharg is periodically updated. */
    return (GetCurrentChargeStatus() == Is_Yes) ? Is_Yes : Is_No;
#else
    return Is_No;
#endif
}

void FactorySelftest_FastPoll(void)
{
    UINT8 byte;
    if(release_uart0_rx_overflow_count() != 0U)
    {
        release_uart0_clear_rx();
        s_factory_line_len = 0;
        Factory_SendError("LINE_TOO_LONG");
        return;
    }
    while(release_uart0_read(&byte, 1U) == 1U)
    {
        if(byte == '\r') continue;
        if(byte == '\n')
        {
            s_factory_line[s_factory_line_len] = '\0';
            Factory_ProcessLine(s_factory_line);
            s_factory_line_len = 0;
        }
        else if(byte >= 0x20U && byte <= 0x7eU && s_factory_line_len < (FACTORY_LINE_MAX - 1U))
        {
            s_factory_line[s_factory_line_len++] = (char)byte;
        }
        else if(s_factory_line_len >= (FACTORY_LINE_MAX - 1U))
        {
            s_factory_line_len = 0;
            Factory_SendError("LINE_TOO_LONG");
        }
    }
}

void FactorySelftest_SendBootReport(void)
{
    const char *resolution;
    const char *color;
    const char *maker;
    char mac_text[13];
    UINT8 i;
    UINT16 crc = 0xffffU;

#if defined(ENABLE_INK_SCREEN_JD79665_800X480_COLOR_4)
    resolution = "800x480"; color = "7.5C4"; maker = "XT";
#elif defined(ENABLE_INK_SCREEN_JD79665CA_800X480_COLOR_4)
    resolution = "800x480"; color = "7.5C4"; maker = "DKE";
#elif defined(ENABLE_INK_SCREEN_SPD1657_800X480_COLOR_6)
    resolution = "800x480"; color = "7.5C6"; maker = "YT";
#else
    resolution = "UNKNOWN"; color = "UNKNOWN"; maker = "UNKNOWN";
#endif

    for(i = 0; i < 6U; i++)
    {
        UINT8 value = Mac[5U - i];
        static const char digits[] = "0123456789ABCDEF";
        mac_text[i * 2U] = digits[value >> 4];
        mac_text[i * 2U + 1U] = digits[value & 0x0fU];
    }
    mac_text[12] = '\0';
    crc = Factory_CrcText(crc, mac_text);
    {
        UINT32 version = VER;
        char version_text[11];
        UINT8 count = 0;
        do { version_text[count++] = (char)('0' + version % 10U); version /= 10U; } while(version != 0U);
        while(count != 0U) crc = Factory_Crc16Update(crc, (UINT8)version_text[--count]);
    }
    crc = Factory_CrcText(crc, resolution);
    crc = Factory_CrcText(crc, color);
    crc = Factory_CrcText(crc, maker);

    Factory_WriteText("facBleMac ");
    Factory_WriteText(mac_text); Factory_WriteText(",");
    Factory_WriteDec(VER); Factory_WriteText(",");
    Factory_WriteText(resolution); Factory_WriteText(",");
    Factory_WriteText(color); Factory_WriteText(",");
    Factory_WriteText(maker); Factory_WriteText(",");
    Factory_WriteHex((UINT8)(crc >> 8)); Factory_WriteHex((UINT8)crc);
    Factory_WriteText("\n");
    BOOT_LOG_TEXT("BOOT panel-ready\r\n");
}

#endif
