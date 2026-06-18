#include "server_network_sta_cast2pic.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "cast_core.h"
#include "esp_log.h"
#include "tdx_cfg.h"

static const char *TAG = "server_sta_cast2pic";

#define CAST2PIC_MAX_IMAGES 1

typedef struct {
    bool present;
    const char *data;
    size_t len;
    char filename[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX];
} multipart_part_t;

typedef struct {
    char file_name[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX];
    size_t bin_size;
    size_t image_size;
    multipart_part_t bin_part;
    multipart_part_t image_part;
} cast2pic_image_t;

typedef struct {
    char func[20];
    char screen[4];
    bool save;
    bool show;
    cast2pic_image_t images[CAST2PIC_MAX_IMAGES];
    size_t image_count;
} cast2pic_meta_t;

static const char *find_bytes(const char *haystack, size_t haystack_len, const char *needle, size_t needle_len)
{
    if (haystack == NULL || needle == NULL || needle_len == 0 || haystack_len < needle_len) {
        return NULL;
    }

    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return haystack + i;
        }
    }
    return NULL;
}

static bool extract_boundary(const char *content_type, char *boundary, size_t boundary_size)
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

static bool get_disposition_value(const char *headers, size_t headers_len,
                                  const char *key, char *out, size_t out_size)
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

    size_t copy_len = end - start;
    if (copy_len >= out_size) {
        copy_len = out_size - 1;
    }
    memcpy(out, start, copy_len);
    out[copy_len] = '\0';
    return true;
}

static void copy_part_text(const multipart_part_t *part, char *out, size_t out_size)
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

static bool parse_size_text(const multipart_part_t *part, size_t *out)
{
    char text[24];
    char *end_ptr = NULL;
    unsigned long value = 0;

    copy_part_text(part, text, sizeof(text));
    if (text[0] == '\0') {
        return false;
    }

    errno = 0;
    value = strtoul(text, &end_ptr, 10);
    if (errno != 0 || end_ptr == text || *end_ptr != '\0' || value == 0) {
        return false;
    }

    *out = (size_t)value;
    return true;
}

static bool parse_bool_text(const multipart_part_t *part, bool default_value)
{
    char text[12];
    copy_part_text(part, text, sizeof(text));
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

static bool file_name_is_safe(const char *file_name)
{
    if (file_name == NULL || file_name[0] == '\0') {
        return false;
    }
    if (strstr(file_name, "..") != NULL || strchr(file_name, '/') != NULL || strchr(file_name, '\\') != NULL) {
        return false;
    }
    return strlen(file_name) + 4 < SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX;
}

static esp_err_t send_cast2pic_result(httpd_req_t *req, const char *result)
{
    char json[160];
    int result_code = TDX_JSON_RESULT_OK;
    if (result == NULL || strcmp(result, "ok") == 0) {
        snprintf(json, sizeof(json),
                 "{\"func\":\"cast2pic_result\",\"result\":%d}",
                 TDX_JSON_RESULT_OK);
    } else {
        if (strcmp(result, "missing_func") == 0) {
            result_code = TDX_JSON_RESULT_UPLOAD_FUNC_MISSING;
        } else if (strcmp(result, "invalid_screen") == 0) {
            result_code = TDX_JSON_RESULT_CAST2PIC_SCREEN_INVALID;
        } else if (strcmp(result, "display_request_failed") == 0 || strcmp(result, "display_queue_failed") == 0) {
            result_code = TDX_JSON_RESULT_DISPLAY_QUEUE_FAILED;
        } else if (strcmp(result, "storage_not_ready") == 0) {
            result_code = TDX_JSON_RESULT_STORAGE_NOT_READY;
        } else if (strcmp(result, "storage_not_enough") == 0) {
            result_code = TDX_JSON_RESULT_STORAGE_NO_SPACE;
        } else if (strcmp(result, "missing_bin_file") == 0) {
            result_code = TDX_JSON_RESULT_UPLOAD_BIN_MISSING;
        } else if (strcmp(result, "missing_image_file") == 0) {
            result_code = TDX_JSON_RESULT_UPLOAD_IMAGE_MISSING;
        } else if (strstr(result, "size") != NULL) {
            result_code = TDX_JSON_RESULT_UPLOAD_SIZE_MISMATCH;
        } else if (strstr(result, "write_bin") != NULL || strcmp(result, "save_bin_failed") == 0) {
            result_code = TDX_JSON_RESULT_SAVE_BIN_FAILED;
        } else if (strstr(result, "write_image") != NULL || strcmp(result, "save_image_failed") == 0) {
            result_code = TDX_JSON_RESULT_SAVE_IMAGE_FAILED;
        } else if (strstr(result, "file") != NULL) {
            result_code = TDX_JSON_RESULT_UPLOAD_FILE_NAME_INVALID;
        } else {
            result_code = TDX_JSON_RESULT_UPLOAD_INVALID;
        }
        snprintf(json, sizeof(json),
                 "{\"func\":\"cast2pic_result\",\"result\":%d,\"message\":\"%s\",\"error\":\"%s\"}",
                 result_code,
                 result,
                 result);
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t send_cast2pic_core_result(httpd_req_t *req, const tdx_cast_core_result_t *result)
{
    if (result == NULL || result->result == TDX_JSON_RESULT_OK) {
        return send_cast2pic_result(req, "ok");
    }
    return send_cast2pic_result(req, result->error[0] ? result->error : "invalid_upload");
}

static bool screen_is_valid(const char *screen)
{
    return strcmp(screen, "a") == 0 || strcmp(screen, "b") == 0;
}

static size_t screen_required_images(const char *screen)
{
    (void)screen;
    return 1;
}

static uint8_t screen_to_epd_number(const char *screen)
{
    // Keep the hardware mapping: request screen A drives EPD2, request screen B drives EPD1.
    // 保留硬件映射：请求 screen A 驱动 EPD2，请求 screen B 驱动 EPD1。
    if (strcmp(screen, "a") == 0) {
        return 2;
    }
    if (strcmp(screen, "b") == 0) {
        return 1;
    }
    return 1;
}

static const char *image_to_save_name(uint8_t screen_number)
{
    // Keep the save-name mapping reversed to match the existing hardware screen files.
    // 保留反向保存名映射，用于匹配现有硬件屏幕文件。
    return screen_number == 2 ? "screen_b" : "screen_a";
}

static void copy_cast2pic_part(const multipart_part_t *src, usb_console_multipart_part_t *dst)
{
    if (src == NULL || dst == NULL) {
        return;
    }
    memset(dst, 0, sizeof(*dst));
    dst->present = src->present;
    dst->data = src->data;
    dst->len = src->len;
    snprintf(dst->filename, sizeof(dst->filename), "%s", src->filename);
}

static esp_err_t process_cast2pic_items(const char *base_path, const cast2pic_meta_t *meta, tdx_cast_core_result_t *result)
{
    tdx_image_transfer_item_t items[CAST2PIC_MAX_IMAGES] = {0};
    size_t count = screen_required_images(meta->screen);
    uint8_t screen_number = screen_to_epd_number(meta->screen);

    for (size_t i = 0; i < count; i++) {
        const char *save_name = image_to_save_name(screen_number);
        snprintf(items[i].save_name, sizeof(items[i].save_name), "%s", save_name);
        items[i].save = meta->save;
        items[i].show = meta->show;
        items[i].record_last_cast = false;
        items[i].epd_target = screen_number;
        copy_cast2pic_part(&meta->images[i].bin_part, &items[i].bin_part);
        copy_cast2pic_part(&meta->images[i].image_part, &items[i].image_part);
    }
    return TdxImageTransfer_ProcessItems(items, count, base_path, "network cast2pic", result);
}

static void assign_text_part(cast2pic_meta_t *meta, const char *name, const multipart_part_t *part)
{
    if (strcmp(name, "func") == 0) {
        copy_part_text(part, meta->func, sizeof(meta->func));
    } else if (strcmp(name, "screen") == 0) {
        copy_part_text(part, meta->screen, sizeof(meta->screen));
    } else if (strcmp(name, "save") == 0) {
        meta->save = parse_bool_text(part, true);
    } else if (strcmp(name, "show") == 0) {
        meta->show = parse_bool_text(part, true);
    }
}

static bool image_field_matches(const char *name, const char *base_name)
{
    size_t base_len = strlen(base_name);

    if (strcmp(name, base_name) == 0) {
        return true;
    }
    return strncmp(name, base_name, base_len) == 0 &&
           (name[base_len] == 'A' || name[base_len] == 'B') &&
           name[base_len + 1] == '\0';
}

static bool assign_image_part(cast2pic_meta_t *meta, const char *name, const multipart_part_t *part)
{
    if (image_field_matches(name, "fileName")) {
        if (meta->image_count >= CAST2PIC_MAX_IMAGES) {
            return true;
        }
        copy_part_text(part, meta->images[meta->image_count].file_name,
                       sizeof(meta->images[meta->image_count].file_name));
        return true;
    }

    if (meta->image_count >= CAST2PIC_MAX_IMAGES) {
        return true;
    }
    cast2pic_image_t *image = &meta->images[meta->image_count];
    if (image_field_matches(name, "bin_size")) {
        parse_size_text(part, &image->bin_size);
    } else if (image_field_matches(name, "image_size")) {
        parse_size_text(part, &image->image_size);
    } else if (image_field_matches(name, "bin")) {
        image->bin_part = *part;
    } else if (image_field_matches(name, "image")) {
        image->image_part = *part;
        meta->image_count++;
    }
    return true;
}

static esp_err_t parse_cast2pic_multipart(const char *body,
                                          size_t body_len,
                                          const char *boundary,
                                          cast2pic_meta_t *meta)
{
    char marker[SERVER_NETWORK_STA_OTA_BOUNDARY_MAX + 4];
    int marker_len = snprintf(marker, sizeof(marker), "--%s", boundary);
    const char *cursor = body;
    const char *end = body + body_len;

    if (marker_len <= 0 || marker_len >= (int)sizeof(marker)) {
        return ESP_ERR_INVALID_ARG;
    }

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

        char name[SERVER_NETWORK_STA_DATAUP_FIELD_NAME_MAX] = {0};
        size_t headers_len = headers_end - part_start;
        if (!get_disposition_value(part_start, headers_len, "name", name, sizeof(name))) {
            cursor = headers_end + 4;
            continue;
        }

        const char *data_start = headers_end + 4;
        const char *next_boundary = find_bytes(data_start, end - data_start, marker, (size_t)marker_len);
        if (next_boundary == NULL) {
            break;
        }

        const char *data_end = next_boundary;
        if (data_end >= data_start + 2 && data_end[-2] == '\r' && data_end[-1] == '\n') {
            data_end -= 2;
        }

        multipart_part_t part = {
            .present = true,
            .data = data_start,
            .len = (size_t)(data_end - data_start),
            .filename = {0},
        };
        (void)get_disposition_value(part_start, headers_len, "filename", part.filename, sizeof(part.filename));

        assign_text_part(meta, name, &part);
        assign_image_part(meta, name, &part);

        cursor = next_boundary;
    }

    return ESP_OK;
}

static const char *validate_cast2pic_meta(const cast2pic_meta_t *meta)
{
    if (meta->func[0] == '\0') {
        return "missing_func";
    }
    if (strcmp(meta->func, "cast2pic") != 0) {
        return "invalid_func";
    }
    if (!screen_is_valid(meta->screen)) {
        return "invalid_screen";
    }
    if (meta->image_count < screen_required_images(meta->screen)) {
        return "missing_image_file";
    }

    for (size_t i = 0; i < screen_required_images(meta->screen); i++) {
        const cast2pic_image_t *image = &meta->images[i];
        if (image->file_name[0] == '\0') {
            return "missing_fileName";
        }
        if (!file_name_is_safe(image->file_name)) {
            return "invalid_fileName";
        }
        if (image->bin_size == 0) {
            return "missing_bin_size";
        }
        if (image->image_size == 0) {
            return "missing_image_size";
        }
        if (!image->bin_part.present || image->bin_part.data == NULL) {
            return "missing_bin_file";
        }
        if (!image->image_part.present || image->image_part.data == NULL) {
            return "missing_image_file";
        }
        if (image->bin_part.len != image->bin_size) {
            return "bin_size_mismatch";
        }
        if (image->image_part.len != image->image_size) {
            return "image_size_mismatch";
        }
    }
    return NULL;
}

esp_err_t ServerNetworkStaCast2Pic_Process(httpd_req_t *req,
                                           const char *body,
                                           size_t body_len,
                                           const char *content_type,
                                           const char *base_path)
{
    char boundary[SERVER_NETWORK_STA_OTA_BOUNDARY_MAX] = {0};
    cast2pic_meta_t meta = {
        .save = true,
        .show = true,
    };

    if (!extract_boundary(content_type, boundary, sizeof(boundary))) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t parse_ret = parse_cast2pic_multipart(body, body_len, boundary, &meta);
    if (parse_ret != ESP_OK) {
        return send_cast2pic_result(req, "missing_func");
    }
    if (strcmp(meta.func, "cast2pic") != 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    const char *validate_error = validate_cast2pic_meta(&meta);
    if (validate_error != NULL) {
        return send_cast2pic_result(req, validate_error);
    }

    tdx_cast_core_result_t result = {0};
    (void)process_cast2pic_items(base_path, &meta, &result);
    return send_cast2pic_core_result(req, &result);
}
