#include "app_cfg.h"
#include "CONFIG.h"
#include "wifi_time_wake.h"
#include "commoninfo.h"
#include <stdio.h>
#include <string.h>

#ifdef ENABLE_WIFI_UART1_PASSTHROUGH

#define WIFI_TIMED_WAKE_MAGIC          0xA6
#define WIFI_TIMED_WAKE_VERSION        0x01
#define WIFI_TIMED_WAKE_MIN_SECONDS    0UL
#define WIFI_TIMED_WAKE_MAX_SECONDS    604800UL
#define WIFI_TIMED_WAKE_TICKS_PER_SEC  1600UL
#define WIFI_TIMED_WAKE_CHUNK_SECONDS  14400UL
#define WIFI_TIME_MAGIC                0xA7
#define WIFI_TIME_VERSION              0x01
#define WIFI_TIME_STATE_SAVED          0x01
#define WIFI_TIME_EPOCH_YEAR           2020U
#define WIFI_TIME_MAX_YEAR             2064U
#define WIFI_TIME_SET_LEN              19U

typedef struct
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} wifi_time_calendar_t;

static tmosTaskID s_time_wake_task_id = 0xFF;
static uint16_t s_time_wake_event = 0;
static uint8_t s_wifi_wake_timer_received_this_session = Is_No;
static uint8_t s_wifi_timed_wake_enabled = Is_No;
static uint8_t s_wifi_timed_wake_armed = Is_No;
static uint32_t s_wifi_timed_wake_seconds = 0;
static uint32_t s_wifi_wake_due_seconds = 0;
static uint32_t s_wifi_relative_remaining_seconds = 0;
static uint32_t s_wifi_current_chunk_seconds = 0;
static uint8_t s_wifi_timed_wake_uses_wall_time = Is_No;
static uint8_t s_time_valid = Is_No;
static uint8_t s_time_stale = Is_No;
static uint8_t s_time_dirty = Is_No;
static uint8_t s_time_loaded = Is_No;
static uint32_t s_base_wall_seconds = 0;
static uint32_t s_base_rtc_seconds = 0;
static uint32_t s_last_saved_seconds = 0;

static void WifiTimeWake_PutLe32(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)(value & 0xFFU);
    buf[1] = (uint8_t)((value >> 8) & 0xFFU);
    buf[2] = (uint8_t)((value >> 16) & 0xFFU);
    buf[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static uint32_t WifiTimeWake_GetLe32(uint8_t *buf)
{
    return ((uint32_t)buf[0]) |
           (((uint32_t)buf[1]) << 8) |
           (((uint32_t)buf[2]) << 16) |
           (((uint32_t)buf[3]) << 24);
}

static uint8_t WifiTimeWake_IsLeapYear(uint16_t year)
{
    if((year % 400U) == 0U)
    {
        return Is_Yes;
    }
    if((year % 100U) == 0U)
    {
        return Is_No;
    }
    return ((year % 4U) == 0U) ? Is_Yes : Is_No;
}

static uint8_t WifiTimeWake_DaysInMonth(uint16_t year, uint8_t month)
{
    static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if(month < 1U || month > 12U)
    {
        return 0;
    }
    if(month == 2U && WifiTimeWake_IsLeapYear(year) == Is_Yes)
    {
        return 29;
    }
    return days[month - 1U];
}

static uint8_t WifiTimeWake_CalendarToSeconds(wifi_time_calendar_t *cal, uint32_t *seconds)
{
    uint16_t year;
    uint8_t month;
    uint32_t days = 0;

    if(cal == NULL || seconds == NULL)
    {
        return Is_No;
    }
    if(cal->year < WIFI_TIME_EPOCH_YEAR || cal->year > WIFI_TIME_MAX_YEAR ||
       cal->month < 1U || cal->month > 12U ||
       cal->day < 1U || cal->day > WifiTimeWake_DaysInMonth(cal->year, cal->month) ||
       cal->hour > 23U || cal->minute > 59U || cal->second > 59U)
    {
        return Is_No;
    }

    for(year = WIFI_TIME_EPOCH_YEAR; year < cal->year; year++)
    {
        days += (WifiTimeWake_IsLeapYear(year) == Is_Yes) ? 366UL : 365UL;
    }
    for(month = 1U; month < cal->month; month++)
    {
        days += WifiTimeWake_DaysInMonth(cal->year, month);
    }
    days += (uint32_t)(cal->day - 1U);

    *seconds = days * 86400UL +
               ((uint32_t)cal->hour) * 3600UL +
               ((uint32_t)cal->minute) * 60UL +
               cal->second;
    return Is_Yes;
}

static uint8_t WifiTimeWake_SecondsToCalendar(uint32_t seconds, wifi_time_calendar_t *cal)
{
    uint32_t days;
    uint32_t rem;
    uint16_t year;
    uint8_t month;
    uint16_t year_days;
    uint8_t month_days;

    if(cal == NULL)
    {
        return Is_No;
    }

    days = seconds / 86400UL;
    rem = seconds % 86400UL;
    year = WIFI_TIME_EPOCH_YEAR;
    while(year <= WIFI_TIME_MAX_YEAR)
    {
        year_days = (WifiTimeWake_IsLeapYear(year) == Is_Yes) ? 366U : 365U;
        if(days < year_days)
        {
            break;
        }
        days -= year_days;
        year++;
    }
    if(year > WIFI_TIME_MAX_YEAR)
    {
        return Is_No;
    }

    month = 1U;
    while(month <= 12U)
    {
        month_days = WifiTimeWake_DaysInMonth(year, month);
        if(days < month_days)
        {
            break;
        }
        days -= month_days;
        month++;
    }
    if(month > 12U)
    {
        return Is_No;
    }

    cal->year = year;
    cal->month = month;
    cal->day = (uint8_t)(days + 1U);
    cal->hour = (uint8_t)(rem / 3600UL);
    rem %= 3600UL;
    cal->minute = (uint8_t)(rem / 60UL);
    cal->second = (uint8_t)(rem % 60UL);
    return Is_Yes;
}

static uint8_t WifiTimeWake_ParseFixedDec(char *text, uint8_t len, uint16_t *out)
{
    uint8_t i;
    uint16_t value = 0;

    if(text == NULL || out == NULL || len == 0U)
    {
        return Is_No;
    }
    for(i = 0; i < len; i++)
    {
        if(text[i] < '0' || text[i] > '9')
        {
            return Is_No;
        }
        value = (uint16_t)(value * 10U + (uint16_t)(text[i] - '0'));
    }
    *out = value;
    return Is_Yes;
}

static uint8_t WifiTimeWake_ParseCalendarText(char *text, wifi_time_calendar_t *cal)
{
    uint16_t value;
    uint32_t seconds;

    if(text == NULL || cal == NULL)
    {
        return Is_No;
    }
    if(text[4] != '-' || text[7] != '-' || text[10] != ',' ||
       text[13] != ':' || text[16] != ':')
    {
        return Is_No;
    }
    if(WifiTimeWake_ParseFixedDec(&text[0], 4U, &value) != Is_Yes)
    {
        return Is_No;
    }
    cal->year = value;
    if(WifiTimeWake_ParseFixedDec(&text[5], 2U, &value) != Is_Yes)
    {
        return Is_No;
    }
    cal->month = (uint8_t)value;
    if(WifiTimeWake_ParseFixedDec(&text[8], 2U, &value) != Is_Yes)
    {
        return Is_No;
    }
    cal->day = (uint8_t)value;
    if(WifiTimeWake_ParseFixedDec(&text[11], 2U, &value) != Is_Yes)
    {
        return Is_No;
    }
    cal->hour = (uint8_t)value;
    if(WifiTimeWake_ParseFixedDec(&text[14], 2U, &value) != Is_Yes)
    {
        return Is_No;
    }
    cal->minute = (uint8_t)value;
    if(WifiTimeWake_ParseFixedDec(&text[17], 2U, &value) != Is_Yes)
    {
        return Is_No;
    }
    cal->second = (uint8_t)value;

    return WifiTimeWake_CalendarToSeconds(cal, &seconds);
}

static void WifiTimeWake_FormatCalendar(char *buf, wifi_time_calendar_t *cal)
{
    sprintf(buf, "%04u-%02u-%02u,%02u:%02u:%02u",
            cal->year,
            cal->month,
            cal->day,
            cal->hour,
            cal->minute,
            cal->second);
}

static uint8_t WifiTimeWake_GetRtcSeconds(uint32_t *seconds)
{
    wifi_time_calendar_t cal;
    uint16_t year;
    uint16_t month;
    uint16_t day;
    uint16_t hour;
    uint16_t minute;
    uint16_t second;

    if(seconds == NULL)
    {
        return Is_No;
    }

    RTC_GetTime(&year, &month, &day, &hour, &minute, &second);
    cal.year = year;
    cal.month = (uint8_t)month;
    cal.day = (uint8_t)day;
    cal.hour = (uint8_t)hour;
    cal.minute = (uint8_t)minute;
    cal.second = (uint8_t)second;
    return WifiTimeWake_CalendarToSeconds(&cal, seconds);
}

static uint8_t WifiTimeWake_GetBackupTime(uint32_t *seconds, uint8_t *stale)
{
    uint32_t rtc_now;

    if(seconds == NULL)
    {
        return Is_No;
    }
    if(s_time_valid == Is_Yes)
    {
        if(WifiTimeWake_GetRtcSeconds(&rtc_now) != Is_Yes || rtc_now < s_base_rtc_seconds)
        {
            return Is_No;
        }
        *seconds = s_base_wall_seconds + (rtc_now - s_base_rtc_seconds);
        if(stale != NULL)
        {
            *stale = Is_No;
        }
        return Is_Yes;
    }
    if(s_time_stale == Is_Yes)
    {
        *seconds = s_last_saved_seconds;
        if(stale != NULL)
        {
            *stale = Is_Yes;
        }
        return Is_Yes;
    }
    return Is_No;
}

void WifiTimeWake_Init(tmosTaskID task_id, uint16_t timed_wake_event)
{
    s_time_wake_task_id = task_id;
    s_time_wake_event = timed_wake_event;
}

void WifiTimeWake_LoadTimeBackup(void)
{
    uint8_t data[WIFI_TIME_LEN];
    wifi_time_calendar_t cal;

    if(s_time_loaded == Is_Yes)
    {
        return;
    }

    memset(data, 0, sizeof(data));
    Get_EEPROM_Flag(data, WIFI_TIME_POSITION, WIFI_TIME_LEN);
    s_time_valid = Is_No;
    s_time_stale = Is_No;
    s_time_dirty = Is_No;
    s_base_wall_seconds = 0;
    s_base_rtc_seconds = 0;
    s_last_saved_seconds = 0;
    if(data[0] == WIFI_TIME_MAGIC &&
       data[1] == WIFI_TIME_VERSION &&
       data[2] == WIFI_TIME_STATE_SAVED)
    {
        s_last_saved_seconds = WifiTimeWake_GetLe32(&data[12]);
        s_base_wall_seconds = WifiTimeWake_GetLe32(&data[4]);
        s_base_rtc_seconds = WifiTimeWake_GetLe32(&data[8]);
        if(WifiTimeWake_SecondsToCalendar(s_last_saved_seconds, &cal) == Is_Yes)
        {
            s_time_stale = Is_Yes;
        }
    }
    s_time_loaded = Is_Yes;
}

static void WifiTimeWake_WriteTimeBackup(uint32_t base_wall, uint32_t base_rtc, uint32_t last_saved)
{
    uint8_t data[WIFI_TIME_LEN];

    memset(data, 0, sizeof(data));
    data[0] = WIFI_TIME_MAGIC;
    data[1] = WIFI_TIME_VERSION;
    data[2] = WIFI_TIME_STATE_SAVED;
    WifiTimeWake_PutLe32(&data[4], base_wall);
    WifiTimeWake_PutLe32(&data[8], base_rtc);
    WifiTimeWake_PutLe32(&data[12], last_saved);
    Save_EEPROM_Flag(data, WIFI_TIME_POSITION, WIFI_TIME_LEN);
}

void WifiTimeWake_SaveTimeIfDirtyOrValid(void)
{
    uint32_t current;
    uint8_t stale;

    WifiTimeWake_LoadTimeBackup();
    if(s_time_dirty != Is_Yes && s_time_valid != Is_Yes)
    {
        return;
    }
    if(WifiTimeWake_GetBackupTime(&current, &stale) != Is_Yes || stale == Is_Yes)
    {
        return;
    }
    s_last_saved_seconds = current;
    WifiTimeWake_WriteTimeBackup(s_base_wall_seconds, s_base_rtc_seconds, s_last_saved_seconds);
    s_time_dirty = Is_No;
    s_time_stale = Is_No;
    Print_I3("[WIFI_PT] TIME saved seconds=%lu", s_last_saved_seconds);
}

static void WifiTimeWake_SaveCurrentTimeSnapshot(void)
{
    uint32_t current;
    uint32_t rtc_now;
    uint8_t stale;

    WifiTimeWake_LoadTimeBackup();
    if(WifiTimeWake_GetBackupTime(&current, &stale) != Is_Yes || stale == Is_Yes)
    {
        return;
    }
    if(WifiTimeWake_GetRtcSeconds(&rtc_now) != Is_Yes)
    {
        return;
    }
    s_base_wall_seconds = current;
    s_base_rtc_seconds = rtc_now;
    s_last_saved_seconds = current;
    s_time_valid = Is_Yes;
    s_time_stale = Is_No;
    s_time_dirty = Is_No;
    WifiTimeWake_WriteTimeBackup(s_base_wall_seconds, s_base_rtc_seconds, s_last_saved_seconds);
    Print_I3("[WIFI_PT] TIME snapshot seconds=%lu", s_last_saved_seconds);
}

static void WifiTimeWake_WriteTimedWakeConfig(uint8_t enabled, uint32_t seconds)
{
    uint8_t data[WIFI_TIMED_WAKE_LEN];

    memset(data, 0, sizeof(data));
    data[0] = WIFI_TIMED_WAKE_MAGIC;
    data[1] = WIFI_TIMED_WAKE_VERSION;
    data[2] = (enabled == Is_Yes) ? Is_Yes : Is_No;
    WifiTimeWake_PutLe32(&data[4], seconds);
    Save_EEPROM_Flag(data, WIFI_TIMED_WAKE_POSITION, WIFI_TIMED_WAKE_LEN);
}

static void WifiTimeWake_StopTimedWake(uint8_t write_flash)
{
    if(s_time_wake_task_id != 0xFF)
    {
        tmos_stop_task(s_time_wake_task_id, s_time_wake_event);
    }

    s_wifi_timed_wake_enabled = Is_No;
    s_wifi_timed_wake_armed = Is_No;
    s_wifi_timed_wake_seconds = 0;
    s_wifi_wake_due_seconds = 0;
    s_wifi_relative_remaining_seconds = 0;
    s_wifi_current_chunk_seconds = 0;
    s_wifi_timed_wake_uses_wall_time = Is_No;

    if(write_flash == Is_Yes)
    {
        WifiTimeWake_WriteTimedWakeConfig(Is_No, 0);
    }
}

static void WifiTimeWake_ScheduleTimedWakeChunk(void)
{
    uint32_t current;
    uint32_t remaining;
    uint8_t stale = Is_No;
    tmosTimer ticks;

    if(s_time_wake_task_id == 0xFF || s_wifi_timed_wake_armed != Is_Yes)
    {
        return;
    }

    if(s_wifi_relative_remaining_seconds > 0)
    {
        remaining = s_wifi_relative_remaining_seconds;
    }
    else if(s_wifi_timed_wake_uses_wall_time == Is_Yes)
    {
        if(WifiTimeWake_GetBackupTime(&current, &stale) != Is_Yes || stale == Is_Yes)
        {
            Print_I3("[WIFI_PT] Timed wake lost valid time, wake now");
            s_wifi_current_chunk_seconds = 0;
            tmos_stop_task(s_time_wake_task_id, s_time_wake_event);
            tmos_start_task(s_time_wake_task_id, s_time_wake_event, 0);
            return;
        }
        remaining = (s_wifi_wake_due_seconds > current) ? (s_wifi_wake_due_seconds - current) : 0;
    }
    else
    {
        remaining = s_wifi_relative_remaining_seconds;
    }

    s_wifi_current_chunk_seconds = (remaining > WIFI_TIMED_WAKE_CHUNK_SECONDS) ? WIFI_TIMED_WAKE_CHUNK_SECONDS : remaining;
    ticks = s_wifi_current_chunk_seconds * WIFI_TIMED_WAKE_TICKS_PER_SEC;

    tmos_stop_task(s_time_wake_task_id, s_time_wake_event);
    tmos_start_task(s_time_wake_task_id, s_time_wake_event, ticks);
    Print_I3("[WIFI_PT] Timed wake chunk seconds=%lu ticks=%lu", s_wifi_current_chunk_seconds, ticks);
}

static void WifiTimeWake_StartTimedWake(uint32_t seconds)
{
    uint32_t current;
    uint8_t stale = Is_No;

    s_wifi_timed_wake_enabled = Is_Yes;
    s_wifi_timed_wake_armed = Is_Yes;
    s_wifi_timed_wake_seconds = seconds;
    s_wifi_wake_due_seconds = 0;
    s_wifi_relative_remaining_seconds = seconds;
    s_wifi_current_chunk_seconds = 0;
    s_wifi_timed_wake_uses_wall_time = Is_No;

    WifiTimeWake_LoadTimeBackup();
    if(WifiTimeWake_GetBackupTime(&current, &stale) == Is_Yes && stale != Is_Yes)
    {
        s_wifi_timed_wake_uses_wall_time = Is_Yes;
        s_wifi_wake_due_seconds = current + seconds;
        Print_I3("[WIFI_PT] Timed wake armed seconds=%lu due=%lu", seconds, s_wifi_wake_due_seconds);
    }
    else
    {
        Print_I3("[WIFI_PT] Timed wake armed seconds=%lu relative", seconds);
    }

    WifiTimeWake_ScheduleTimedWakeChunk();
}

wifi_time_wake_status_t WifiTimeWake_TimeSet(char *arg, uint16_t len)
{
    wifi_time_calendar_t wall_time;
    uint32_t wall_seconds;
    uint32_t rtc_seconds;

    if(arg == NULL || len != WIFI_TIME_SET_LEN || strlen(arg) != WIFI_TIME_SET_LEN)
    {
        return WIFI_TIME_WAKE_BAD_LEN;
    }
    if(WifiTimeWake_ParseCalendarText(arg, &wall_time) != Is_Yes ||
       WifiTimeWake_CalendarToSeconds(&wall_time, &wall_seconds) != Is_Yes)
    {
        return WIFI_TIME_WAKE_BAD_TIME;
    }
    if(WifiTimeWake_GetRtcSeconds(&rtc_seconds) != Is_Yes)
    {
        return WIFI_TIME_WAKE_BAD_TIME;
    }

    WifiTimeWake_LoadTimeBackup();
    s_base_wall_seconds = wall_seconds;
    s_base_rtc_seconds = rtc_seconds;
    s_last_saved_seconds = wall_seconds;
    s_time_valid = Is_Yes;
    s_time_stale = Is_No;
    s_time_dirty = Is_Yes;
    Print_I3("[WIFI_PT] TIME_SET ram=%s", arg);
    return WIFI_TIME_WAKE_OK;
}

wifi_time_wake_status_t WifiTimeWake_TimeGetStatus(char *status_buf, uint16_t status_buf_size)
{
    uint32_t seconds;
    uint8_t stale = Is_No;
    wifi_time_calendar_t cal;
    char time_text[20];

    if(status_buf == NULL || status_buf_size < 32U)
    {
        return WIFI_TIME_WAKE_BAD_LEN;
    }

    WifiTimeWake_LoadTimeBackup();
    if(WifiTimeWake_GetBackupTime(&seconds, &stale) != Is_Yes ||
       WifiTimeWake_SecondsToCalendar(seconds, &cal) != Is_Yes)
    {
        strcpy(status_buf, "INVALID");
        return WIFI_TIME_WAKE_OK;
    }

    WifiTimeWake_FormatCalendar(time_text, &cal);
    sprintf(status_buf, "%s,%s", (stale == Is_Yes) ? "STALE" : "VALID", time_text);
    return WIFI_TIME_WAKE_OK;
}

wifi_time_wake_status_t WifiTimeWake_SetWakeTimerOn(uint32_t seconds)
{
    if(seconds < WIFI_TIMED_WAKE_MIN_SECONDS || seconds > WIFI_TIMED_WAKE_MAX_SECONDS)
    {
        return WIFI_TIME_WAKE_BAD_TIME;
    }

    s_wifi_wake_timer_received_this_session = Is_Yes;
    s_wifi_timed_wake_enabled = Is_Yes;
    s_wifi_timed_wake_armed = Is_No;
    s_wifi_timed_wake_seconds = seconds;
    Print_I3("[WIFI_PT] WAKE_TIMER ON seconds=%lu", seconds);
    return WIFI_TIME_WAKE_OK;
}

wifi_time_wake_status_t WifiTimeWake_SetWakeTimerOff(void)
{
    s_wifi_wake_timer_received_this_session = Is_Yes;
    WifiTimeWake_StopTimedWake(Is_No);
    Print_I3("[WIFI_PT] WAKE_TIMER OFF");
    return WIFI_TIME_WAKE_OK;
}

void WifiTimeWake_BeginWakeSession(uint8_t from_timed_wake)
{
    s_wifi_wake_timer_received_this_session = Is_No;

    if(s_wifi_timed_wake_armed == Is_Yes)
    {
        if(s_time_wake_task_id != 0xFF)
        {
            tmos_stop_task(s_time_wake_task_id, s_time_wake_event);
        }
        s_wifi_timed_wake_armed = Is_No;
        s_wifi_timed_wake_enabled = Is_No;
        s_wifi_timed_wake_seconds = 0;
        s_wifi_wake_due_seconds = 0;
        s_wifi_relative_remaining_seconds = 0;
        s_wifi_current_chunk_seconds = 0;
        s_wifi_timed_wake_uses_wall_time = Is_No;
        if(from_timed_wake != Is_Yes)
        {
            Print_I3("[WIFI_PT] Non-timer wake cancels pending timed wake");
        }
    }
}

void WifiTimeWake_FinalizeForWifiOff(void)
{
    if(s_wifi_wake_timer_received_this_session != Is_Yes)
    {
        Print_I3("[WIFI_PT] No valid WAKE_TIMER this session, disable timed wake");
        WifiTimeWake_StopTimedWake(Is_Yes);
        return;
    }

    if(s_wifi_timed_wake_enabled == Is_Yes)
    {
        WifiTimeWake_StartTimedWake(s_wifi_timed_wake_seconds);
        return;
    }

    WifiTimeWake_StopTimedWake(Is_Yes);
}

uint8_t WifiTimeWake_IsTimedWakeArmed(void)
{
    return s_wifi_timed_wake_armed;
}

uint8_t WifiTimeWake_OnTimedWakeEvent(void)
{
    uint32_t current;
    uint32_t remaining;
    uint8_t stale = Is_No;

    if(s_wifi_timed_wake_armed != Is_Yes)
    {
        Print_I3("[WIFI_PT] Timed wake event ignored, not armed");
        return Is_No;
    }

    if(s_wifi_relative_remaining_seconds > s_wifi_current_chunk_seconds)
    {
        s_wifi_relative_remaining_seconds -= s_wifi_current_chunk_seconds;
        Print_I3("[WIFI_PT] Timed wake chunk expired countdown remain=%lu", s_wifi_relative_remaining_seconds);
        WifiTimeWake_ScheduleTimedWakeChunk();
        return Is_No;
    }

    s_wifi_relative_remaining_seconds = 0;
    if(s_wifi_timed_wake_uses_wall_time == Is_Yes &&
       WifiTimeWake_GetBackupTime(&current, &stale) == Is_Yes &&
       stale != Is_Yes &&
       current < s_wifi_wake_due_seconds)
    {
        remaining = s_wifi_wake_due_seconds - current;
        Print_I3("[WIFI_PT] Timed wake rtc still remain=%lu, wake by countdown", remaining);
    }

    Print_I3("[WIFI_PT] Timed wake expired seconds=%lu", s_wifi_timed_wake_seconds);
    WifiTimeWake_SaveCurrentTimeSnapshot();
    s_wifi_timed_wake_armed = Is_No;
    s_wifi_timed_wake_enabled = Is_No;
    s_wifi_timed_wake_seconds = 0;
    s_wifi_wake_due_seconds = 0;
    s_wifi_relative_remaining_seconds = 0;
    s_wifi_current_chunk_seconds = 0;
    s_wifi_timed_wake_uses_wall_time = Is_No;
    return Is_Yes;
}

#endif
