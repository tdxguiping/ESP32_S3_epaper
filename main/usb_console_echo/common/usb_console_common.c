#include "usb_console_common.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "epd_display_app.h"
#include "file_serving_example_common.h"
#include "server_network_sta_slideshow.h"
#include "tdx_cfg.h"

static const char *TAG = "usb_console_common";

static uint32_t elapsed_ms_since(int64_t start_us)
{
    return (uint32_t)((esp_timer_get_time() - start_us) / 1000);
}

static const char *find_bytes(const char *haystack, size_t haystack_len, const char *needle, size_t needle_len)
{
    const char *cursor;
    const char *end;

    if (haystack == NULL || needle == NULL || needle_len == 0 || haystack_len < needle_len) {
        return NULL;
    }
    cursor = haystack;
    end = haystack + haystack_len - needle_len + 1;

    while (cursor < end) {
        cursor = (const char *)memchr(cursor, needle[0], (size_t)(end - cursor));
        if (cursor == NULL) {
            return NULL;
        }
        if (memcmp(cursor, needle, needle_len) == 0) {
            return cursor;
        }
        cursor++;
    }
    return NULL;
}

static const char *find_json_key(const char *body, const char *key)
{
    char pattern[64];
    const char *pos = body;

    if (body == NULL || key == NULL) {
        return NULL;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    while ((pos = strstr(pos, pattern)) != NULL) {
        const char *after = pos + strlen(pattern);
        while (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n') {
            after++;
        }
        if (*after == ':') {
            return pos;
        }
        pos += strlen(pattern);
    }
    return NULL;
}

static esp_err_t append_text(char *json, size_t json_size, size_t *used, const char *text)
{
    size_t len = strlen(text);
    if (json == NULL || used == NULL || *used + len + 1 >= json_size) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(json + *used, text, len);
    *used += len;
    json[*used] = '\0';
    return ESP_OK;
}

static esp_err_t append_format(char *json, size_t json_size, size_t *used, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int written = vsnprintf(json + *used, json_size - *used, fmt, ap);
    va_end(ap);
    if (written < 0 || *used + (size_t)written >= json_size) {
        return ESP_ERR_NO_MEM;
    }
    *used += (size_t)written;
    return ESP_OK;
}

bool UsbConsoleCommon_JsonFuncEquals(const char *body, const char *func)
{
    char value[48];
    return UsbConsoleCommon_JsonString(body, "func", value, sizeof(value)) && strcmp(value, func) == 0;
}

bool UsbConsoleCommon_JsonString(const char *body, const char *key, char *out, size_t out_size)
{
    const char *pos = find_json_key(body, key);
    if (pos == NULL || out == NULL || out_size == 0) {
        return false;
    }

    pos += strlen(key) + 2;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    if (*pos != ':') {
        return false;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    if (*pos != '"') {
        return false;
    }
    pos++;

    size_t len = 0;
    while (*pos != '\0' && *pos != '"' && len + 1 < out_size) {
        out[len++] = *pos++;
    }
    if (*pos != '"') {
        return false;
    }
    out[len] = '\0';
    return true;
}

bool UsbConsoleCommon_JsonU32(const char *body, const char *key, uint32_t *out)
{
    const char *pos = find_json_key(body, key);
    char *end_ptr = NULL;
    unsigned long value = 0;
    if (pos == NULL || out == NULL) {
        return false;
    }

    pos += strlen(key) + 2;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == ':') {
        pos++;
    }
    if (*pos == '-') {
        return false;
    }
    errno = 0;
    value = strtoul(pos, &end_ptr, 10);
    if (errno != 0 || end_ptr == pos || value > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

bool UsbConsoleCommon_JsonInt(const char *body, const char *key, int *out)
{
    uint32_t value = 0;
    if (!UsbConsoleCommon_JsonU32(body, key, &value)) {
        return false;
    }
    *out = (int)value;
    return true;
}

bool UsbConsoleCommon_JsonBool(const char *body, const char *key, bool *out)
{
    const char *pos = find_json_key(body, key);
    if (pos == NULL || out == NULL) {
        return false;
    }
    pos += strlen(key) + 2;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == ':') {
        pos++;
    }
    if (strncmp(pos, "true", 4) == 0 || *pos == '1') {
        *out = true;
        return true;
    }
    if (strncmp(pos, "false", 5) == 0 || *pos == '0') {
        *out = false;
        return true;
    }
    return false;
}

bool UsbConsoleCommon_FileNameIsSafe(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return false;
    }
    if (strstr(name, "..") != NULL || strchr(name, '/') != NULL || strchr(name, '\\') != NULL || strchr(name, '"') != NULL) {
        return false;
    }
    return strlen(name) < TDX_SLIDESHOW_FILE_NAME_MAX_LEN;
}

esp_err_t UsbConsoleCommon_SetJsonf(usb_console_http_response_t *response,
                                    int status,
                                    const char *reason,
                                    const char *fmt,
                                    ...)
{
    va_list ap;
    if (response == NULL || fmt == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    response->status = status;
    response->reason = reason != NULL ? reason : "OK";
    response->content_type = "application/json";
    va_start(ap, fmt);
    int written = vsnprintf(response->body, sizeof(response->body), fmt, ap);
    va_end(ap);
    return (written >= 0 && written < (int)sizeof(response->body)) ? ESP_OK : ESP_ERR_NO_MEM;
}

static bool has_jpg_extension(const char *name)
{
    size_t len = name != NULL ? strlen(name) : 0;
    return len > 4 && (strcmp(name + len - 4, ".jpg") == 0 || strcmp(name + len - 4, ".JPG") == 0);
}

static const char *saved_image_entry_name(const char *entry_name)
{
    const char *prefix = "jpg_img/";
    if (entry_name == NULL) {
        return NULL;
    }
    if (example_storage_supports_directories()) {
        return entry_name;
    }
    return strncmp(entry_name, prefix, strlen(prefix)) == 0 ? entry_name + strlen(prefix) : NULL;
}

esp_err_t UsbConsoleCommon_ListSavedImages(char *json, size_t json_size, size_t *used)
{
    char jpg_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    snprintf(jpg_dir, sizeof(jpg_dir), "%s/jpg_img", USB_CONSOLE_BASE_PATH);
    const char *scan_dir = example_storage_supports_directories() ? jpg_dir : USB_CONSOLE_BASE_PATH;
    DIR *dir = opendir(scan_dir);
    if (dir == NULL) {
        return append_text(json, json_size, used, "\"images\":[]");
    }

    ESP_RETURN_ON_ERROR(append_text(json, json_size, used, "\"images\":["), TAG, "append image list failed");
    int count = 0;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = saved_image_entry_name(entry->d_name);
        if (name == NULL || !has_jpg_extension(name) || !UsbConsoleCommon_FileNameIsSafe(name)) {
            continue;
        }

        char file_name[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX] = {0};
        size_t stem_len = strlen(name) - 4;
        if (stem_len == 0 || stem_len >= sizeof(file_name)) {
            continue;
        }
        memcpy(file_name, name, stem_len);
        file_name[stem_len] = '\0';
        ESP_RETURN_ON_ERROR(append_format(json,
                                          json_size,
                                          used,
                                          "%s{\"fileName\":\"%s\",\"thumbnailUrl\":\"%s%s\"}",
                                          count > 0 ? "," : "",
                                          file_name,
                                          SERVER_NETWORK_STA_THUMB_URI_PREFIX,
                                          name),
                            TAG, "append image item failed");
        count++;
    }
    closedir(dir);
    return append_text(json, json_size, used, "]");
}

esp_err_t UsbConsoleCommon_AppendSnapshot(char *json, size_t json_size, size_t *used)
{
    char control_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    char config_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    char control[256] = {0};
    char config[SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX] = {0};
    FILE *fp = NULL;
    int sw = 0;
    uint32_t interval = 0;
    bool random = false;

    snprintf(control_path, sizeof(control_path), "%s/bin_img/%s", USB_CONSOLE_BASE_PATH, TDX_SLIDESHOW_CONTROL_FILE);
    snprintf(config_path, sizeof(config_path), "%s/bin_img/%s", USB_CONSOLE_BASE_PATH, TDX_SLIDESHOW_CONFIG_FILE);

    fp = fopen(control_path, "rb");
    if (fp != NULL) {
        size_t len = fread(control, 1, sizeof(control) - 1, fp);
        fclose(fp);
        control[len] = '\0';
        (void)UsbConsoleCommon_JsonInt(control, "sw", &sw);
        (void)UsbConsoleCommon_JsonU32(control, "interval", &interval);
        (void)UsbConsoleCommon_JsonBool(control, "random", &random);
    }

    ESP_RETURN_ON_ERROR(append_format(json,
                                      json_size,
                                      used,
                                      ",\"slideshow\":{\"sw\":%d,\"fileNames\":[",
                                      sw),
                        TAG, "append snapshot slideshow failed");
    fp = fopen(config_path, "rb");
    if (fp != NULL) {
        size_t len = fread(config, 1, sizeof(config) - 1, fp);
        fclose(fp);
        config[len] = '\0';
        const char *array = find_json_key(config, "fileNames");
        if (array != NULL && (array = strchr(array, '[')) != NULL) {
            array++;
            int count = 0;
            while (*array != '\0' && *array != ']') {
                while (*array == ' ' || *array == '\t' || *array == '\r' || *array == '\n' || *array == ',') {
                    array++;
                }
                if (*array != '"') {
                    break;
                }
                array++;
                char file_name[TDX_SLIDESHOW_FILE_NAME_MAX_LEN] = {0};
                size_t name_len = 0;
                while (*array != '\0' && *array != '"' && name_len + 1 < sizeof(file_name)) {
                    file_name[name_len++] = *array++;
                }
                if (*array != '"') {
                    break;
                }
                array++;
                if (UsbConsoleCommon_FileNameIsSafe(file_name)) {
                    ESP_RETURN_ON_ERROR(append_format(json,
                                                      json_size,
                                                      used,
                                                      "%s\"%s\"",
                                                      count > 0 ? "," : "",
                                                      file_name),
                                        TAG, "append snapshot file failed");
                    count++;
                }
            }
        }
        if (interval == 0) {
            (void)UsbConsoleCommon_JsonU32(config, "interval", &interval);
        }
        (void)UsbConsoleCommon_JsonBool(config, "random", &random);
    }
    return append_format(json,
                         json_size,
                         used,
                         "],\"interval\":%lu,\"random\":%s}}",
                         (unsigned long)interval,
                         random ? "true" : "false");
}

bool UsbConsoleCommon_ExtractBoundary(const char *content_type, char *boundary, size_t boundary_size)
{
    const char *key = "boundary=";
    const char *pos = content_type != NULL ? strstr(content_type, key) : NULL;
    if (pos == NULL || boundary == NULL || boundary_size == 0) {
        return false;
    }
    pos += strlen(key);
    if (*pos == '"') {
        pos++;
    }
    size_t len = 0;
    while (pos[len] != '\0' && pos[len] != '"' && pos[len] != ';' && len + 1 < boundary_size) {
        boundary[len] = pos[len];
        len++;
    }
    boundary[len] = '\0';
    return len > 0;
}

static bool get_disposition_value(const char *headers, size_t headers_len, const char *key, char *out, size_t out_size)
{
    char pattern[SERVER_NETWORK_STA_DATAUP_FIELD_NAME_MAX];
    int pattern_len = snprintf(pattern, sizeof(pattern), "%s=\"", key);
    const char *start = find_bytes(headers, headers_len, pattern, (size_t)pattern_len);
    if (start == NULL || out == NULL || out_size == 0) {
        return false;
    }
    start += pattern_len;
    const char *end = memchr(start, '"', (headers + headers_len) - start);
    if (end == NULL) {
        return false;
    }
    size_t copy_len = (size_t)(end - start);
    if (copy_len >= out_size) {
        copy_len = out_size - 1;
    }
    memcpy(out, start, copy_len);
    out[copy_len] = '\0';
    return true;
}

bool UsbConsoleCommon_MultipartPart(const char *body,
                                    size_t body_len,
                                    const char *boundary,
                                    const char *name,
                                    usb_console_multipart_part_t *part)
{
    char marker[SERVER_NETWORK_STA_OTA_BOUNDARY_MAX + 4];
    int marker_len = snprintf(marker, sizeof(marker), "--%s", boundary);
    const char *cursor = body;
    const char *end = body + body_len;

    if (part == NULL || marker_len <= 0 || marker_len >= (int)sizeof(marker)) {
        return false;
    }
    memset(part, 0, sizeof(*part));

    while (cursor < end) {
        const char *boundary_pos = find_bytes(cursor, end - cursor, marker, (size_t)marker_len);
        if (boundary_pos == NULL) {
            break;
        }
        const char *part_start = boundary_pos + marker_len;
        if (part_start + 2 <= end && part_start[0] == '-' && part_start[1] == '-') {
            break;
        }
        if (part_start + 2 <= end && part_start[0] == '\r' && part_start[1] == '\n') {
            part_start += 2;
        }
        const char *headers_end = find_bytes(part_start, end - part_start, "\r\n\r\n", 4);
        if (headers_end == NULL) {
            break;
        }

        char part_name[SERVER_NETWORK_STA_DATAUP_FIELD_NAME_MAX] = {0};
        size_t headers_len = (size_t)(headers_end - part_start);
        if (get_disposition_value(part_start, headers_len, "name", part_name, sizeof(part_name)) &&
            strcmp(part_name, name) == 0) {
            const char *data_start = headers_end + 4;
            const char *next_boundary = find_bytes(data_start, end - data_start, marker, (size_t)marker_len);
            if (next_boundary == NULL) {
                break;
            }
            const char *data_end = next_boundary;
            if (data_end >= data_start + 2 && data_end[-2] == '\r' && data_end[-1] == '\n') {
                data_end -= 2;
            }
            part->present = true;
            part->data = data_start;
            part->len = (size_t)(data_end - data_start);
            (void)get_disposition_value(part_start, headers_len, "filename", part->filename, sizeof(part->filename));
            return true;
        }
        cursor = headers_end + 4;
    }
    return false;
}

bool UsbConsoleCommon_MultipartParts(const char *body,
                                     size_t body_len,
                                     const char *boundary,
                                     const char *const *names,
                                     usb_console_multipart_part_t *parts,
                                     size_t part_count)
{
    char marker[SERVER_NETWORK_STA_OTA_BOUNDARY_MAX + 4];
    int marker_len;
    const char *cursor;
    const char *end;
    size_t found = 0;

    if (body == NULL || boundary == NULL || names == NULL || parts == NULL || part_count == 0) {
        return false;
    }
    marker_len = snprintf(marker, sizeof(marker), "--%s", boundary);
    if (marker_len <= 0 || marker_len >= (int)sizeof(marker)) {
        return false;
    }
    cursor = body;
    end = body + body_len;
    for (size_t i = 0; i < part_count; i++) {
        memset(&parts[i], 0, sizeof(parts[i]));
    }

    while (cursor < end && found < part_count) {
        const char *boundary_pos = find_bytes(cursor, end - cursor, marker, (size_t)marker_len);
        if (boundary_pos == NULL) {
            break;
        }
        const char *part_start = boundary_pos + marker_len;
        if (part_start + 2 <= end && part_start[0] == '-' && part_start[1] == '-') {
            break;
        }
        if (part_start + 2 <= end && part_start[0] == '\r' && part_start[1] == '\n') {
            part_start += 2;
        }
        const char *headers_end = find_bytes(part_start, end - part_start, "\r\n\r\n", 4);
        if (headers_end == NULL) {
            break;
        }

        char part_name[SERVER_NETWORK_STA_DATAUP_FIELD_NAME_MAX] = {0};
        size_t headers_len = (size_t)(headers_end - part_start);
        const char *resume = headers_end + 4;
        if (get_disposition_value(part_start, headers_len, "name", part_name, sizeof(part_name))) {
            for (size_t i = 0; i < part_count; i++) {
                if (!parts[i].present && names[i] != NULL && strcmp(part_name, names[i]) == 0) {
                    const char *data_start = headers_end + 4;
                    const char *next_boundary = find_bytes(data_start, end - data_start, marker, (size_t)marker_len);
                    if (next_boundary == NULL) {
                        return found > 0;
                    }
                    const char *data_end = next_boundary;
                    if (data_end >= data_start + 2 && data_end[-2] == '\r' && data_end[-1] == '\n') {
                        data_end -= 2;
                    }
                    parts[i].present = true;
                    parts[i].data = data_start;
                    parts[i].len = (size_t)(data_end - data_start);
                    (void)get_disposition_value(part_start, headers_len, "filename", parts[i].filename, sizeof(parts[i].filename));
                    found++;
                    resume = next_boundary;
                    break;
                }
            }
        }
        cursor = resume;
    }
    return found > 0;
}

void UsbConsoleCommon_CopyPartText(const usb_console_multipart_part_t *part, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (part == NULL || !part->present || part->data == NULL) {
        return;
    }
    size_t copy_len = part->len;
    if (copy_len >= out_size) {
        copy_len = out_size - 1;
    }
    memcpy(out, part->data, copy_len);
    out[copy_len] = '\0';
}

bool UsbConsoleCommon_ParsePartSize(const usb_console_multipart_part_t *part, size_t *out)
{
    char text[24];
    char *end_ptr = NULL;
    UsbConsoleCommon_CopyPartText(part, text, sizeof(text));
    if (text[0] == '\0') {
        return false;
    }
    unsigned long value = strtoul(text, &end_ptr, 10);
    if (end_ptr == text || *end_ptr != '\0' || value == 0) {
        return false;
    }
    *out = (size_t)value;
    return true;
}

bool UsbConsoleCommon_ParsePartBool(const usb_console_multipart_part_t *part, bool default_value)
{
    char text[12];
    UsbConsoleCommon_CopyPartText(part, text, sizeof(text));
    if (text[0] == '\0') {
        return default_value;
    }
    if (strcmp(text, "true") == 0 || strcmp(text, "1") == 0 || strcmp(text, "yes") == 0) {
        return true;
    }
    if (strcmp(text, "false") == 0 || strcmp(text, "0") == 0 || strcmp(text, "no") == 0) {
        return false;
    }
    return default_value;
}

esp_err_t UsbConsoleCommon_SavePartFile(const char *dir,
                                        const char *file_name,
                                        const char *ext,
                                        const usb_console_multipart_part_t *part)
{
    int64_t start_us = esp_timer_get_time();
    char path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX + 24];
    char tmp_path[sizeof(path) + 4];
    if (dir == NULL || !UsbConsoleCommon_FileNameIsSafe(file_name) || ext == NULL || part == NULL || !part->present) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)mkdir(dir, 0775);
    snprintf(path, sizeof(path), "%s/%s%s", dir, file_name, ext);
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    FILE *fp = fopen(tmp_path, "wb");
    if (fp == NULL) {
        return ESP_FAIL;
    }
    void *io_buf = NULL;
#if USB_CONSOLE_FILE_SAVE_STREAM_BUF_SIZE > 0
    // Use an explicit stdio buffer so SD/FATFS writes can batch internal file operations.
    // 使用显式 stdio 缓冲，让 SD/FATFS 写入尽量合并内部文件操作。
    io_buf = malloc(USB_CONSOLE_FILE_SAVE_STREAM_BUF_SIZE);
    if (io_buf != NULL) {
        (void)setvbuf(fp, io_buf, _IOFBF, USB_CONSOLE_FILE_SAVE_STREAM_BUF_SIZE);
    }
#endif
    size_t written = fwrite(part->data, 1, part->len, fp);
    fclose(fp);
    free(io_buf);
    if (written != part->len) {
        unlink(tmp_path);
        return ESP_FAIL;
    }
    unlink(path);
    if (rename(tmp_path, path) != 0) {
        unlink(tmp_path);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "save file path=%s size=%u elapsed_ms=%lu",
             path,
             (unsigned int)part->len,
             (unsigned long)elapsed_ms_since(start_us));
    return ESP_OK;
}

esp_err_t UsbConsoleCommon_RecordLastCast(const char *file_name)
{
    int64_t start_us = esp_timer_get_time();
    char record_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 64];
    char json[256];
    if (!UsbConsoleCommon_FileNameIsSafe(file_name)) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(record_path, sizeof(record_path), "%s/bin_img/%s", USB_CONSOLE_BASE_PATH, SERVER_NETWORK_STA_LAST_CAST_FILE);
    int len = snprintf(json, sizeof(json),
                       "{\"fileName\":\"%s\",\"bin\":\"%s/bin_img/%s.bin\",\"image\":\"%s/jpg_img/%s.jpg\"}",
                       file_name, USB_CONSOLE_BASE_PATH, file_name, USB_CONSOLE_BASE_PATH, file_name);
    FILE *fp = fopen(record_path, "wb");
    if (fp == NULL) {
        return ESP_FAIL;
    }
    size_t written = fwrite(json, 1, (size_t)len, fp);
    fclose(fp);
    ESP_LOGI(TAG, "record last cast file=%s elapsed_ms=%lu",
             file_name,
             (unsigned long)elapsed_ms_since(start_us));
    return written == (size_t)len ? ESP_OK : ESP_FAIL;
}

esp_err_t UsbConsoleCommon_HandleImageTransfer(const usb_console_http_request_t *request,
                                               usb_console_http_response_t *response,
                                               const char *expected_func,
                                               const char *result_func)
{
    char boundary[SERVER_NETWORK_STA_OTA_BOUNDARY_MAX] = {0};
    char func[16] = {0};
    char screen[8] = {0};
    char file_name[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX] = {0};
    size_t bin_size = 0;
    size_t image_size = 0;
    bool save = true;
    bool show = false;
    char bin_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    char jpg_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    const char *part_names[] = {
        "func",
        "fileName",
        "screen",
        "bin_size",
        "image_size",
        "save",
        "show",
        "bin",
        "image",
    };
    usb_console_multipart_part_t parts[sizeof(part_names) / sizeof(part_names[0])] = {0};
    usb_console_multipart_part_t *func_part = &parts[0];
    usb_console_multipart_part_t *file_name_part = &parts[1];
    usb_console_multipart_part_t *screen_part = &parts[2];
    usb_console_multipart_part_t *bin_size_part = &parts[3];
    usb_console_multipart_part_t *image_size_part = &parts[4];
    usb_console_multipart_part_t *save_part = &parts[5];
    usb_console_multipart_part_t *show_part = &parts[6];
    usb_console_multipart_part_t *bin_part = &parts[7];
    usb_console_multipart_part_t *image_part = &parts[8];
    int64_t total_start_us = esp_timer_get_time();
    int64_t stage_start_us = total_start_us;

    if (request == NULL || response == NULL || expected_func == NULL || result_func == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!UsbConsoleCommon_ExtractBoundary(request->content_type, boundary, sizeof(boundary))) {
        return UsbConsoleCommon_SetJsonf(response, 200, "OK",
                                         "{\"func\":\"%s\",\"result\":1,\"message\":\"missing boundary\"}",
                                         result_func);
    }
    if (!UsbConsoleCommon_MultipartParts(request->body,
                                         request->body_len,
                                         boundary,
                                         part_names,
                                         parts,
                                         sizeof(parts) / sizeof(parts[0])) ||
        !func_part->present) {
        return UsbConsoleCommon_SetJsonf(response, 200, "OK",
                                         "{\"func\":\"%s\",\"result\":1,\"message\":\"missing func\"}",
                                         result_func);
    }
    UsbConsoleCommon_CopyPartText(func_part, func, sizeof(func));
    if (strcmp(func, expected_func) != 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    ESP_LOGI(TAG, "%s parse parts elapsed_ms=%lu",
             expected_func,
             (unsigned long)elapsed_ms_since(stage_start_us));

    stage_start_us = esp_timer_get_time();
    UsbConsoleCommon_CopyPartText(file_name_part, file_name, sizeof(file_name));
    UsbConsoleCommon_CopyPartText(screen_part, screen, sizeof(screen));
    if (strcmp(expected_func, "cast2pic") == 0) {
        snprintf(file_name, sizeof(file_name), "%s", strcmp(screen, "b") == 0 ? "screen_b" : "screen_a");
    }
    save = UsbConsoleCommon_ParsePartBool(save_part, true);
    show = UsbConsoleCommon_ParsePartBool(show_part, strcmp(expected_func, "upload") == 0 ? false : true);

    if (!UsbConsoleCommon_FileNameIsSafe(file_name) ||
        !UsbConsoleCommon_ParsePartSize(bin_size_part, &bin_size) ||
        !UsbConsoleCommon_ParsePartSize(image_size_part, &image_size) ||
        !bin_part->present || !image_part->present ||
        bin_part->len != bin_size || image_part->len != image_size) {
        return UsbConsoleCommon_SetJsonf(response, 200, "OK",
                                         "{\"func\":\"%s\",\"result\":1,\"message\":\"invalid upload\"}",
                                         result_func);
    }
    ESP_LOGI(TAG, "%s meta file=%s save=%d show=%d body=%u bin=%u/%u image=%u/%u",
             expected_func,
             file_name,
             save ? 1 : 0,
             show ? 1 : 0,
             (unsigned int)request->body_len,
             (unsigned int)bin_part->len,
             (unsigned int)bin_size,
             (unsigned int)image_part->len,
             (unsigned int)image_size);
    if (strcmp(expected_func, "cast") == 0 && !save) {
        return UsbConsoleCommon_SetJsonf(response, 200, "OK",
                                         "{\"func\":\"%s\",\"result\":1,\"message\":\"cast failed\",\"error\":\"save_required_for_last_cast\"}",
                                         result_func);
    }
    ESP_LOGI(TAG, "%s validate elapsed_ms=%lu total_ms=%lu",
             expected_func,
             (unsigned long)elapsed_ms_since(stage_start_us),
             (unsigned long)elapsed_ms_since(total_start_us));

    if (save) {
        snprintf(bin_dir, sizeof(bin_dir), "%s/bin_img", USB_CONSOLE_BASE_PATH);
        snprintf(jpg_dir, sizeof(jpg_dir), "%s/jpg_img", USB_CONSOLE_BASE_PATH);
        stage_start_us = esp_timer_get_time();
        esp_err_t save_bin_ret = UsbConsoleCommon_SavePartFile(bin_dir, file_name, ".bin", bin_part);
        ESP_LOGI(TAG, "%s save bin ret=%s elapsed_ms=%lu total_ms=%lu",
                 expected_func,
                 esp_err_to_name(save_bin_ret),
                 (unsigned long)elapsed_ms_since(stage_start_us),
                 (unsigned long)elapsed_ms_since(total_start_us));
        if (save_bin_ret != ESP_OK) {
            return UsbConsoleCommon_SetJsonf(response, 200, "OK",
                                             "{\"func\":\"%s\",\"result\":1,\"message\":\"%s failed\",\"error\":\"save_bin_failed\"}",
                                             result_func,
                                             expected_func);
        }
        stage_start_us = esp_timer_get_time();
        esp_err_t save_image_ret = UsbConsoleCommon_SavePartFile(jpg_dir, file_name, ".jpg", image_part);
        ESP_LOGI(TAG, "%s save image ret=%s elapsed_ms=%lu total_ms=%lu",
                 expected_func,
                 esp_err_to_name(save_image_ret),
                 (unsigned long)elapsed_ms_since(stage_start_us),
                 (unsigned long)elapsed_ms_since(total_start_us));
        if (save_image_ret != ESP_OK) {
            return UsbConsoleCommon_SetJsonf(response, 200, "OK",
                                             "{\"func\":\"%s\",\"result\":1,\"message\":\"%s failed\",\"error\":\"save_image_failed\"}",
                                             result_func,
                                             expected_func);
        }
        if (strcmp(expected_func, "cast") == 0) {
            stage_start_us = esp_timer_get_time();
            esp_err_t record_ret = UsbConsoleCommon_RecordLastCast(file_name);
            ESP_LOGI(TAG, "%s record last ret=%s elapsed_ms=%lu total_ms=%lu",
                     expected_func,
                     esp_err_to_name(record_ret),
                     (unsigned long)elapsed_ms_since(stage_start_us),
                     (unsigned long)elapsed_ms_since(total_start_us));
            if (record_ret != ESP_OK) {
                return UsbConsoleCommon_SetJsonf(response, 200, "OK",
                                                 "{\"func\":\"%s\",\"result\":1,\"message\":\"%s failed\",\"error\":\"last_cast_failed\"}",
                                                 result_func,
                                                 expected_func);
            }
        }
    }

    if (show) {
        stage_start_us = esp_timer_get_time();
        esp_err_t display_ret = ESP_OK;
        if (strcmp(expected_func, "cast2pic") == 0) {
            uint8_t screen_number = (strcmp(screen, "b") == 0) ? 2 : 1;
            display_ret = ServerNetworkStaEpdDisplay_QueueToScreen((const uint8_t *)bin_part->data, bin_part->len, screen_number);
        } else {
            display_ret = ServerNetworkStaEpdDisplay_Queue((const uint8_t *)bin_part->data, bin_part->len);
        }
        ESP_LOGI(TAG, "%s display queue ret=%s elapsed_ms=%lu total_ms=%lu",
                 expected_func,
                 esp_err_to_name(display_ret),
                 (unsigned long)elapsed_ms_since(stage_start_us),
                 (unsigned long)elapsed_ms_since(total_start_us));
        if (display_ret != ESP_OK) {
            return UsbConsoleCommon_SetJsonf(response, 200, "OK",
                                             "{\"func\":\"%s\",\"result\":1,\"message\":\"%s failed\",\"error\":\"display_queue_failed\"}",
                                             result_func,
                                             expected_func);
        }
        if (strcmp(expected_func, "cast") == 0) {
            ServerNetworkStaSlideshow_Stop();
        }
    }

    ESP_LOGI(TAG, "%s result ready total_ms=%lu",
             expected_func,
             (unsigned long)elapsed_ms_since(total_start_us));
    return UsbConsoleCommon_SetJsonf(response,
                                     200,
                                     "OK",
                                     "{\"func\":\"%s\",\"result\":0,\"message\":\"ok\",\"fileName\":\"%s\"}",
                                     result_func,
                                     file_name);
}
