#include "server_network_sta_cast.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "esp_log.h"
#include "epd_display_app.h"
#include "tdx_cfg.h"

static const char *TAG = "server_sta_cast";

typedef struct {
    bool present;
    const char *data;
    size_t len;
    char filename[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX];
} multipart_part_t;

typedef struct {
    char func[16];
    char file_name[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX];
    size_t bin_size;
    size_t image_size;
    bool save;
    bool show;
} cast_meta_t;

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
    ESP_LOGI(TAG, "cast boundary parsed len=%u value=%s", (unsigned int)len, boundary);
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

static bool extract_multipart_part(const char *body, size_t body_len,
                                   const char *boundary, const char *wanted_name,
                                   multipart_part_t *part)
{
    char marker[SERVER_NETWORK_STA_OTA_BOUNDARY_MAX + 4];
    int marker_len = snprintf(marker, sizeof(marker), "--%s", boundary);
    const char *cursor = body;
    const char *end = body + body_len;

    if (part == NULL) {
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

        char name[SERVER_NETWORK_STA_DATAUP_FIELD_NAME_MAX] = {0};
        size_t headers_len = headers_end - part_start;
        if (get_disposition_value(part_start, headers_len, "name", name, sizeof(name)) &&
            strcmp(name, wanted_name) == 0) {
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
            part->len = data_end - data_start;
            (void)get_disposition_value(part_start, headers_len, "filename", part->filename, sizeof(part->filename));
            ESP_LOGI(TAG, "cast part found name=%s filename=%s len=%u",
                     wanted_name, part->filename[0] ? part->filename : "<none>", (unsigned int)part->len);
            return true;
        }

        cursor = headers_end + 4;
    }

    ESP_LOGW(TAG, "cast part missing name=%s", wanted_name);
    return false;
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

static bool parse_size_field(const multipart_part_t *part, size_t *out)
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

static bool parse_bool_field(const multipart_part_t *part, bool default_value)
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

static esp_err_t send_cast_result(httpd_req_t *req, bool ok, const char *message, const char *error)
{
    char json[160];
    if (ok) {
        snprintf(json, sizeof(json), "{\"func\":\"cast_result\",\"result\":0}");
    } else {
        snprintf(json, sizeof(json),
                 "{\"func\":\"cast_result\",\"result\":1,\"message\":\"%s\",\"error\":\"%s\"}",
                 message != NULL ? message : "cast failed",
                 error != NULL ? error : "");
    }

    ESP_LOGI(TAG, "cast response: %s", json);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t ensure_dir(const char *path)
{
    struct stat st = {0};

    if (path == NULL) {
        return ESP_ERR_INVALID_ARG;
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
        /* SPIFFS does not support real directories, skip mkdir and keep flat paths. */
        /* SPIFFS 不支持真实目录，跳过 mkdir，后续继续按扁平路径写入。 */
        ESP_LOGW(TAG, "cast mkdir not supported, skip path=%s errno=%d", path, err);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "cast mkdir failed path=%s errno=%d", path, err);

    return ESP_FAIL;
}

static esp_err_t write_file_exact(const char *path, const char *data, size_t len)
{
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "cast open failed path=%s errno=%d", path, errno);
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, len, fp);
    fclose(fp);
    ESP_LOGI(TAG, "cast write path=%s len=%u written=%u",
             path, (unsigned int)len, (unsigned int)written);
    return written == len ? ESP_OK : ESP_FAIL;
}

static esp_err_t save_one_cast_file(const char *dir_path, const char *file_name,
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
        ESP_LOGE(TAG, "cast temp size mismatch path=%s stat=%d actual=%u expected=%u",
                 tmp_path, stat_ret, stat_ret == 0 ? (unsigned int)st.st_size : 0, (unsigned int)len);
        unlink(tmp_path);
        return ESP_FAIL;
    }

    unlink(final_path);
    if (rename(tmp_path, final_path) != 0) {
        ESP_LOGE(TAG, "cast rename failed tmp=%s final=%s errno=%d", tmp_path, final_path, errno);
        unlink(tmp_path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "cast saved final=%s size=%u", final_path, (unsigned int)len);
    return ESP_OK;
}

static esp_err_t save_cast_files(const char *base_path, const cast_meta_t *meta,
                                 const multipart_part_t *bin_part,
                                 const multipart_part_t *image_part)
{
    char bin_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    char jpg_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];

    snprintf(bin_dir, sizeof(bin_dir), "%s/bin_img", base_path);
    snprintf(jpg_dir, sizeof(jpg_dir), "%s/jpg_img", base_path);

    ESP_LOGI(TAG, "cast ensure dirs bin_dir=%s jpg_dir=%s", bin_dir, jpg_dir);
    if (ensure_dir(bin_dir) != ESP_OK || ensure_dir(jpg_dir) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    if (save_one_cast_file(bin_dir, meta->file_name, ".bin", bin_part->data, bin_part->len) != ESP_OK) {
        return ESP_FAIL;
    }
    if (save_one_cast_file(jpg_dir, meta->file_name, ".jpg", image_part->data, image_part->len) != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t record_last_success_cast(const char *base_path, const cast_meta_t *meta)
{
    char bin_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX + 24];
    char image_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX + 24];
    char record_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 32];
    char record_json[512];

    snprintf(bin_path, sizeof(bin_path), "%s/bin_img/%s.bin", base_path, meta->file_name);
    snprintf(image_path, sizeof(image_path), "%s/jpg_img/%s.jpg", base_path, meta->file_name);
    snprintf(record_path, sizeof(record_path), "%s/bin_img/%s", base_path, SERVER_NETWORK_STA_LAST_CAST_FILE);
    int record_len = snprintf(record_json, sizeof(record_json),
                              "{\"fileName\":\"%s\",\"bin\":\"%s\",\"image\":\"%s\"}",
                              meta->file_name, bin_path, image_path);
    if (record_len < 0 || (size_t)record_len >= sizeof(record_json)) {
        ESP_LOGE(TAG, "cast record json too long file=%s", meta->file_name);
        return ESP_ERR_INVALID_SIZE;
    }

    // Write last_cast only after save/show work succeeds so reboot recovery never points to a broken cast.
    // 只在保存和显示流程成功后写入 last_cast，避免重启恢复时指向损坏的投图文件。
    ESP_LOGI(TAG, "cast record last file=%s data=%s", record_path, record_json);
    return write_file_exact(record_path, record_json, strlen(record_json));
}

esp_err_t ServerNetworkStaCast_Process(httpd_req_t *req,
                                       const char *body,
                                       size_t body_len,
                                       const char *content_type,
                                       const char *base_path)
{
    char boundary[SERVER_NETWORK_STA_OTA_BOUNDARY_MAX] = {0};
    multipart_part_t func_part = {0};
    multipart_part_t file_name_part = {0};
    multipart_part_t bin_size_part = {0};
    multipart_part_t image_size_part = {0};
    multipart_part_t save_part = {0};
    multipart_part_t show_part = {0};
    multipart_part_t bin_part = {0};
    multipart_part_t image_part = {0};
    cast_meta_t meta = {0};

    if (!extract_boundary(content_type, boundary, sizeof(boundary))) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!extract_multipart_part(body, body_len, boundary, "func", &func_part)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    copy_part_text(&func_part, meta.func, sizeof(meta.func));
    if (strcmp(meta.func, "cast") != 0) {
        ESP_LOGW(TAG, "cast unsupported func=%s", meta.func);
        return ESP_ERR_NOT_SUPPORTED;
    }

    extract_multipart_part(body, body_len, boundary, "fileName", &file_name_part);
    extract_multipart_part(body, body_len, boundary, "bin_size", &bin_size_part);
    extract_multipart_part(body, body_len, boundary, "image_size", &image_size_part);
    extract_multipart_part(body, body_len, boundary, "save", &save_part);
    extract_multipart_part(body, body_len, boundary, "show", &show_part);
    extract_multipart_part(body, body_len, boundary, "bin", &bin_part);
    extract_multipart_part(body, body_len, boundary, "image", &image_part);

    copy_part_text(&file_name_part, meta.file_name, sizeof(meta.file_name));
    meta.save = parse_bool_field(&save_part, true);
    meta.show = parse_bool_field(&show_part, true);

    ESP_LOGI(TAG, "cast meta func=%s fileName=%s save=%d show=%d",
             meta.func, meta.file_name, meta.save ? 1 : 0, meta.show ? 1 : 0);

    if (!file_name_is_safe(meta.file_name)) {
        return send_cast_result(req, false, "cast failed", "invalid_fileName");
    }
    if (!parse_size_field(&bin_size_part, &meta.bin_size)) {
        return send_cast_result(req, false, "cast failed", "invalid_bin_size");
    }
    if (!parse_size_field(&image_size_part, &meta.image_size)) {
        return send_cast_result(req, false, "cast failed", "invalid_image_size");
    }
    if (!bin_part.present || bin_part.data == NULL) {
        return send_cast_result(req, false, "cast failed", "missing_bin");
    }
    if (!image_part.present || image_part.data == NULL) {
        return send_cast_result(req, false, "cast failed", "missing_image");
    }
    if (bin_part.len != meta.bin_size) {
        ESP_LOGE(TAG, "cast bin size mismatch expect=%u actual=%u",
                 (unsigned int)meta.bin_size, (unsigned int)bin_part.len);
        return send_cast_result(req, false, "cast failed", "bin_size_mismatch");
    }
    if (image_part.len != meta.image_size) {
        ESP_LOGE(TAG, "cast image size mismatch expect=%u actual=%u",
                 (unsigned int)meta.image_size, (unsigned int)image_part.len);
        return send_cast_result(req, false, "cast failed", "image_size_mismatch");
    }
    if (!meta.save) {
        ESP_LOGW(TAG, "cast save=false rejected because last_cast must point to persistent files");
        return send_cast_result(req, false, "cast failed", "save_required_for_last_cast");
    }

    esp_err_t save_ret = save_cast_files(base_path, &meta, &bin_part, &image_part);
    if (save_ret == ESP_ERR_NOT_FOUND) {
        return send_cast_result(req, false, "cast failed", "sd_not_ready");
    }
    if (save_ret != ESP_OK) {
        return send_cast_result(req, false, "cast failed", "save_failed");
    }

    if (meta.show) {
        ESP_LOGI(TAG, "cast show requested fileName=%s bin_len=%u queue display",
                 meta.file_name, (unsigned int)bin_part.len);
        esp_err_t display_ret = ServerNetworkStaEpdDisplay_Queue((const uint8_t *)bin_part.data, bin_part.len);
        if (display_ret != ESP_OK) {
            ESP_LOGE(TAG, "cast display queue failed fileName=%s ret=%s",
                     meta.file_name, esp_err_to_name(display_ret));
            return send_cast_result(req, false, "cast failed", "show_failed");
        }
    }

    if (record_last_success_cast(base_path, &meta) != ESP_OK) {
        return send_cast_result(req, false, "cast failed", "record_last_cast_failed");
    }

    return send_cast_result(req, true, NULL, NULL);
}
