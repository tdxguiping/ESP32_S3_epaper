#include "server_network_sta_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "tdx_cfg.h"
#include "server_network_sta_wifi_work_time.h"

static const char *TAG = "server_sta_time";

#define TDX_SNTP_SERVER_MAIN "ntp.aliyun.com"
#define TDX_SNTP_SERVER_1    "ntp1.aliyun.com"
#define TDX_SNTP_SERVER_2    "ntp2.aliyun.com"
#define TDX_SNTP_TIMEZONE    "CST-8"

#define TDX_DEFAULT_YEAR   2026
#define TDX_DEFAULT_MONTH  1
#define TDX_DEFAULT_DAY    1
#define TDX_DEFAULT_HOUR   0
#define TDX_DEFAULT_MIN    0
#define TDX_DEFAULT_SEC    0

static bool s_time_module_inited;
static bool s_sntp_started;
static bool s_sntp_synced;
static server_network_sta_time_source_t s_time_source = SERVER_NETWORK_STA_TIME_SOURCE_NONE;
static int64_t s_last_sync_epoch;

static bool time_uri_matches(const char *uri)
{
    const size_t time_len = strlen(SERVER_NETWORK_STA_TIME_URI);

    if (uri == NULL || strncmp(uri, SERVER_NETWORK_STA_TIME_URI, time_len) != 0) {
        return false;
    }
    return uri[time_len] == '\0' || uri[time_len] == '?' || uri[time_len] == '#';
}

static const char *time_source_to_str(server_network_sta_time_source_t source)
{
    switch (source) {
    case SERVER_NETWORK_STA_TIME_SOURCE_DEFAULT:
        return "default";
    case SERVER_NETWORK_STA_TIME_SOURCE_APP:
        return "timestamp";
    case SERVER_NETWORK_STA_TIME_SOURCE_SNTP:
        return "sntp";
    case SERVER_NETWORK_STA_TIME_SOURCE_NONE:
    default:
        return "none";
    }
}

static bool is_time_reasonable(time_t now)
{
    struct tm timeinfo = {0};

    if (now <= 0) {
        return false;
    }
    localtime_r(&now, &timeinfo);
    return timeinfo.tm_year >= (TDX_DEFAULT_YEAR - 1900);
}

static time_t make_default_epoch_local(void)
{
    struct tm tm_default = {0};

    tm_default.tm_year = TDX_DEFAULT_YEAR - 1900;
    tm_default.tm_mon = TDX_DEFAULT_MONTH - 1;
    tm_default.tm_mday = TDX_DEFAULT_DAY;
    tm_default.tm_hour = TDX_DEFAULT_HOUR;
    tm_default.tm_min = TDX_DEFAULT_MIN;
    tm_default.tm_sec = TDX_DEFAULT_SEC;
    tm_default.tm_isdst = 0;

    return mktime(&tm_default);
}

static void format_time_strings(time_t now,
                                char *local_buf,
                                size_t local_size,
                                char *utc_buf,
                                size_t utc_size)
{
    struct tm tm_local = {0};
    struct tm tm_utc = {0};

    localtime_r(&now, &tm_local);
    gmtime_r(&now, &tm_utc);
    strftime(local_buf, local_size, "%Y-%m-%d %H:%M:%S", &tm_local);
    strftime(utc_buf, utc_size, "%Y-%m-%d %H:%M:%S", &tm_utc);
}

esp_err_t ServerNetworkStaTime_SetDefaultIfInvalid(void)
{
    time_t now = 0;
    time(&now);

    if (is_time_reasonable(now)) {
        char local_buf[32] = {0};
        char utc_buf[32] = {0};

        format_time_strings(now, local_buf, sizeof(local_buf), utc_buf, sizeof(utc_buf));
        ESP_LOGI(TAG,
                 "RTC/system time already valid, keep it epoch=%lld local=%s utc=%s",
                 (long long)now,
                 local_buf,
                 utc_buf);
        if (s_time_source == SERVER_NETWORK_STA_TIME_SOURCE_NONE) {
            s_time_source = SERVER_NETWORK_STA_TIME_SOURCE_DEFAULT;
        }
        return ESP_OK;
    }

    time_t default_epoch = make_default_epoch_local();
    if (default_epoch <= 0) {
        ESP_LOGE(TAG, "make default epoch failed");
        return ESP_FAIL;
    }

    struct timeval tv = {
        .tv_sec = default_epoch,
        .tv_usec = 0,
    };

    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGE(TAG, "set default RTC/system time failed");
        return ESP_FAIL;
    }

    s_time_source = SERVER_NETWORK_STA_TIME_SOURCE_DEFAULT;

    char local_buf[32] = {0};
    char utc_buf[32] = {0};
    format_time_strings(default_epoch, local_buf, sizeof(local_buf), utc_buf, sizeof(utc_buf));
    ESP_LOGW(TAG,
             "RTC/system default time set epoch=%lld local=%s utc=%s",
             (long long)default_epoch,
             local_buf,
             utc_buf);
    return ESP_OK;
}

static void sntp_sync_cb(struct timeval *tv)
{
    if (tv == NULL) {
        return;
    }

    s_sntp_synced = true;
    s_time_source = SERVER_NETWORK_STA_TIME_SOURCE_SNTP;
    s_last_sync_epoch = (int64_t)tv->tv_sec;

    char local_buf[32] = {0};
    char utc_buf[32] = {0};
    format_time_strings((time_t)tv->tv_sec, local_buf, sizeof(local_buf), utc_buf, sizeof(utc_buf));
    ESP_LOGI(TAG,
             "SNTP synced, RTC/system time updated server=%s epoch=%lld local=%s utc=%s",
             TDX_SNTP_SERVER_MAIN,
             (long long)s_last_sync_epoch,
             local_buf,
             utc_buf);
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;

    if (base != IP_EVENT || id != IP_EVENT_STA_GOT_IP) {
        return;
    }
    if (!s_time_module_inited) {
        ESP_LOGW(TAG, "IP got, but time module not inited");
        return;
    }
    if (s_sntp_started) {
        return;
    }

    esp_err_t ret = esp_netif_sntp_start();
    if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
        s_sntp_started = true;
        ESP_LOGI(TAG, "SNTP start after WiFi got IP ret=%s server=%s", esp_err_to_name(ret), TDX_SNTP_SERVER_MAIN);
    } else {
        ESP_LOGW(TAG, "SNTP start failed ret=%s", esp_err_to_name(ret));
    }
}

esp_err_t ServerNetworkStaTime_Init(void)
{
    if (s_time_module_inited) {
        return ESP_OK;
    }

    setenv("TZ", TDX_SNTP_TIMEZONE, 1);
    tzset();

    esp_err_t ret = ServerNetworkStaTime_SetDefaultIfInvalid();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register IP event failed ret=%s", esp_err_to_name(ret));
        return ret;
    }

    esp_sntp_config_t config =
        ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(3,
                                               ESP_SNTP_SERVER_LIST(TDX_SNTP_SERVER_MAIN,
                                                                    TDX_SNTP_SERVER_1,
                                                                    TDX_SNTP_SERVER_2));
    config.start = false;
    config.smooth_sync = false;
    config.sync_cb = sntp_sync_cb;

    ret = esp_netif_sntp_init(&config);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_sntp_init failed ret=%s", esp_err_to_name(ret));
        return ret;
    }

    s_time_module_inited = true;
    ESP_LOGI(TAG,
             "time init ok start=false server0=%s server1=%s server2=%s timezone=%s",
             TDX_SNTP_SERVER_MAIN,
             TDX_SNTP_SERVER_1,
             TDX_SNTP_SERVER_2,
             TDX_SNTP_TIMEZONE);
    return ESP_OK;
}

bool ServerNetworkStaTime_IsSntpSynced(void)
{
    return s_sntp_synced;
}

esp_err_t ServerNetworkStaTime_SetTimestamp(int64_t timestamp)
{
    char local_buf[32] = {0};
    char utc_buf[32] = {0};
    format_time_strings((time_t)timestamp, local_buf, sizeof(local_buf), utc_buf, sizeof(utc_buf));

    if (timestamp <= 0 || !is_time_reasonable((time_t)timestamp)) {
        ESP_LOGW(TAG,
                 "timestamp rejected epoch=%lld local=%s utc=%s",
                 (long long)timestamp,
                 local_buf,
                 utc_buf);
        return ESP_ERR_INVALID_ARG;
    }

    struct timeval tv = {
        .tv_sec = (time_t)timestamp,
        .tv_usec = 0,
    };
    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGE(TAG,
                 "set timestamp RTC/system time failed epoch=%lld local=%s utc=%s",
                 (long long)timestamp,
                 local_buf,
                 utc_buf);
        return ESP_FAIL;
    }

    s_time_source = SERVER_NETWORK_STA_TIME_SOURCE_APP;
    s_last_sync_epoch = timestamp;

    ESP_LOGI(TAG,
             "timestamp time set, RTC/system time updated epoch=%lld local=%s utc=%s",
             (long long)timestamp,
             local_buf,
             utc_buf);
    return ESP_OK;
}

esp_err_t ServerNetworkStaTime_SetAppTime(int64_t epoch)
{
    return ServerNetworkStaTime_SetTimestamp(epoch);
}

esp_err_t ServerNetworkStaTime_GetInfo(server_network_sta_time_info_t *info)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(*info));
    strlcpy(info->server, TDX_SNTP_SERVER_MAIN, sizeof(info->server));
    strlcpy(info->timezone, TDX_SNTP_TIMEZONE, sizeof(info->timezone));

    time_t now = 0;
    time(&now);

    info->valid = is_time_reasonable(now);
    info->sntp_synced = s_sntp_synced;
    info->source = s_time_source;
    info->epoch = info->valid ? (int64_t)now : 0;

    if (info->valid) {
        format_time_strings(now,
                            info->local_time,
                            sizeof(info->local_time),
                            info->utc_time,
                            sizeof(info->utc_time));
    }

    return ESP_OK;
}

esp_err_t ServerNetworkStaTime_ProcessGet(httpd_req_t *req)
{
    if (req == NULL || !time_uri_matches(req->uri)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    ServerNetworkStaWifiWorkTime_OnNetworkData();

    server_network_sta_time_info_t info = {0};
    esp_err_t ret = ServerNetworkStaTime_GetInfo(&info);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "time info failed");
        return ESP_FAIL;
    }

    char json[384] = {0};
    if (info.valid && info.sntp_synced) {
        snprintf(json,
                 sizeof(json),
                 "{\"func\":\"time_result\",\"result\":%d,"
                 "\"message\":\"ok\",\"valid\":true,\"synced\":true,"
                 "\"source\":\"%s\",\"server\":\"%s\",\"timezone\":\"%s\","
                 "\"epoch\":%lld,\"local\":\"%s\",\"utc\":\"%s\"}",
                 TDX_JSON_RESULT_OK,
                 time_source_to_str(info.source),
                 info.server,
                 info.timezone,
                 (long long)info.epoch,
                 info.local_time,
                 info.utc_time);
    } else if (info.valid) {
        snprintf(json,
                 sizeof(json),
                 "{\"func\":\"time_result\",\"result\":1,"
                 "\"message\":\"using default time\",\"valid\":true,\"synced\":false,"
                 "\"source\":\"%s\",\"server\":\"%s\",\"timezone\":\"%s\","
                 "\"epoch\":%lld,\"local\":\"%s\",\"utc\":\"%s\"}",
                 time_source_to_str(info.source),
                 info.server,
                 info.timezone,
                 (long long)info.epoch,
                 info.local_time,
                 info.utc_time);
    } else {
        snprintf(json,
                 sizeof(json),
                 "{\"func\":\"time_result\",\"result\":2,"
                 "\"message\":\"time invalid\",\"valid\":false,\"synced\":false,"
                 "\"source\":\"none\",\"server\":\"%s\",\"timezone\":\"%s\","
                 "\"epoch\":0,\"local\":\"\",\"utc\":\"\"}",
                 info.server,
                 info.timezone);
    }

    ESP_LOGI(TAG,
             "time request uri=%s valid=%d synced=%d source=%s epoch=%lld",
             req->uri,
             info.valid ? 1 : 0,
             info.sntp_synced ? 1 : 0,
             time_source_to_str(info.source),
             (long long)info.epoch);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    return httpd_resp_sendstr(req, json);
}
