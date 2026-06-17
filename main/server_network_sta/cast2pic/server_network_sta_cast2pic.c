#include "server_network_sta_cast2pic.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "esp_spiffs.h"
#include "file_serving_example_common.h"
#include "epd_display_app.h"
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
        } else if (strcmp(result, "display_request_failed") == 0) {
            result_code = TDX_JSON_RESULT_DISPLAY_QUEUE_FAILED;
        } else if (strstr(result, "write_bin") != NULL || strstr(result, "bin") != NULL) {
            result_code = TDX_JSON_RESULT_SAVE_BIN_FAILED;
        } else if (strstr(result, "image") != NULL) {
            result_code = TDX_JSON_RESULT_SAVE_IMAGE_FAILED;
        } else if (strstr(result, "size") != NULL) {
            result_code = TDX_JSON_RESULT_UPLOAD_SIZE_MISMATCH;
        } else if (strstr(result, "file") != NULL) {
            result_code = TDX_JSON_RESULT_UPLOAD_FILE_NAME_INVALID;
        } else {
            result_code = TDX_JSON_RESULT_UPLOAD_INVALID;
        }
        snprintf(json, sizeof(json),
                 "{\"func\":\"cast2pic_result\",\"result\":%d,\"message\":\"%s\"}",
                 result_code,
                 result);
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
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
    if (strcmp(screen, "a") == 0) {
        return 2; // for hardware compatibility, screen A is EPD2, screen B is EPD1, 
    }
    if (strcmp(screen, "b") == 0) {
        return 1;// for hardware compatibility, screen A is EPD2, screen B is EPD1, 
    }
    return 1;// for hardware compatibility, screen A is EPD2, screen B is EPD1, 
}

static const char *image_to_save_name(uint8_t screen_number)
{
    return screen_number == 2 ? "screen_b" : "screen_a";
}

static esp_err_t ensure_dir(const char *path)
{
    struct stat st = {0};

    if (path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!example_storage_supports_directories()) {
        return ESP_OK;
    }

    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? ESP_OK : ESP_FAIL;
    }

    errno = 0;
    if (mkdir(path, 0775) == 0 || errno == EEXIST) {
        return ESP_OK;
    }

    int err = errno;
    if (err == ENOTSUP || err == EOPNOTSUPP) {
        return ESP_OK;
    }

    ESP_LOGE(TAG, "cast2pic mkdir failed path=%s errno=%d", path, err);
    return ESP_FAIL;
}

static esp_err_t write_file_exact(const char *path, const char *data, size_t len)
{
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "cast2pic open failed path=%s errno=%d", path, errno);
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, len, fp);
    fclose(fp);
    return written == len ? ESP_OK : ESP_FAIL;
}

static esp_err_t save_one_cast2pic_file(const char *dir_path, const char *file_name,
                                        const char *ext, const char *data, size_t len)
{
    char tmp_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX + 32];
    char final_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX + 32];
    struct stat st = {0};

    snprintf(tmp_path, sizeof(tmp_path), "%s/%s.tmp", dir_path, file_name);
    snprintf(final_path, sizeof(final_path), "%s/%s%s", dir_path, file_name, ext);

    unlink(tmp_path);
    esp_err_t ret = write_file_exact(tmp_path, data, len);
    if (ret != ESP_OK) {
        unlink(tmp_path);
        return ret;
    }

    int stat_ret = stat(tmp_path, &st);
    if (stat_ret != 0 || (size_t)st.st_size != len) {
        unlink(tmp_path);
        return ESP_FAIL;
    }

    unlink(final_path);
    if (rename(tmp_path, final_path) != 0) {
        ESP_LOGE(TAG, "cast2pic rename failed tmp=%s final=%s errno=%d", tmp_path, final_path, errno);
        unlink(tmp_path);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t check_save_space(const char *base_path, size_t required_len)
{
    uint64_t free_bytes = 0;
    if (base_path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t info_ret = example_storage_get_free_bytes(base_path, &free_bytes);
    if (info_ret != ESP_OK) {
        ESP_LOGW(TAG, "cast2pic storage info failed base=%s ret=%s, continue without space check",
                 base_path, esp_err_to_name(info_ret));
        return ESP_OK;
    }
    return free_bytes >= (required_len + SERVER_NETWORK_STA_CAST_SAVE_RESERVE_BYTES) ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t save_cast2pic_files(const char *base_path,
                                     const cast2pic_meta_t *meta,
                                     const char **error_out)
{
    char bin_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    char jpg_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    size_t required_len = 0;

    snprintf(bin_dir, sizeof(bin_dir), "%s/bin_img", base_path);
    snprintf(jpg_dir, sizeof(jpg_dir), "%s/jpg_img", base_path);

    if (ensure_dir(bin_dir) != ESP_OK || ensure_dir(jpg_dir) != ESP_OK) {
        *error_out = "storage_not_ready";
        return ESP_ERR_NOT_FOUND;
    }

    for (size_t i = 0; i < meta->image_count; i++) {
        required_len += meta->images[i].bin_part.len + meta->images[i].image_part.len;
    }
    if (check_save_space(base_path, required_len) != ESP_OK) {
        *error_out = "storage_not_enough";
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < meta->image_count; i++) {
        uint8_t screen_number = screen_to_epd_number(meta->screen);
        const char *save_name = image_to_save_name(screen_number);

        if (save_one_cast2pic_file(bin_dir, save_name, ".bin",
                                   meta->images[i].bin_part.data,
                                   meta->images[i].bin_part.len) != ESP_OK) {
            *error_out = "write_bin_failed";
            return ESP_FAIL;
        }
        if (save_one_cast2pic_file(jpg_dir, save_name, ".jpg",
                                   meta->images[i].image_part.data,
                                   meta->images[i].image_part.len) != ESP_OK) {
            *error_out = "write_image_failed";
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

static esp_err_t queue_cast2pic_display(const cast2pic_meta_t *meta)
{
    uint8_t screen_number = screen_to_epd_number(meta->screen);

    for (size_t i = 0; i < screen_required_images(meta->screen); i++) {
        ESP_LOGI(TAG, "cast2pic display screen=%s epd=%u len=%u",
                 meta->screen,
                 (unsigned int)screen_number,
                 (unsigned int)meta->images[i].bin_part.len);
        esp_err_t display_ret = ServerNetworkStaEpdDisplay_QueueToScreen(
            (const uint8_t *)meta->images[i].bin_part.data,
            meta->images[i].bin_part.len,
            screen_number);
        if (display_ret != ESP_OK) {
            ESP_LOGE(TAG, "cast2pic display failed screen=%s ret=%s",
                     meta->screen, esp_err_to_name(display_ret));
            return display_ret;
        }
    }

    return ESP_OK;
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

static bool assign_image_part(cast2pic_meta_t *meta, const char *name, const multipart_part_t *part)
{
    if (strcmp(name, "fileName") == 0) {
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
    if (strcmp(name, "bin_size") == 0) {
        parse_size_text(part, &image->bin_size);
    } else if (strcmp(name, "image_size") == 0) {
        parse_size_text(part, &image->image_size);
    } else if (strcmp(name, "bin") == 0) {
        image->bin_part = *part;
    } else if (strcmp(name, "image") == 0) {
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

    if (meta.show) {
        esp_err_t display_ret = queue_cast2pic_display(&meta);
        if (display_ret != ESP_OK) {
            return send_cast2pic_result(req, "display_request_failed");
        }
    }

    if (meta.save) {
        const char *save_error = NULL;
        if (save_cast2pic_files(base_path, &meta, &save_error) != ESP_OK) {
            ESP_LOGW(TAG, "cast2pic save failed after display error=%s",
                     save_error != NULL ? save_error : "write_bin_failed");
            return send_cast2pic_result(req, save_error != NULL ? save_error : "write_bin_failed");
        }
    }

    return send_cast2pic_result(req, "ok");
}
