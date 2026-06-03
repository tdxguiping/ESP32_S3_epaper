#include "server_network_sta_upload.h"

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
#include "epd_display_app.h"
#include "tdx_cfg.h"

static const char *TAG = "server_sta_upload";
#define SERVER_NETWORK_STA_UPLOAD_WRITE_CHUNK 4096

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
} upload_meta_t;

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
    ESP_LOGI(TAG, "upload boundary parsed len=%u value=%s", (unsigned int)len, boundary);
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
            ESP_LOGI(TAG, "upload part found name=%s filename=%s len=%u",
                     wanted_name, part->filename[0] ? part->filename : "<none>", (unsigned int)part->len);
            return true;
        }

        cursor = headers_end + 4;
    }

    ESP_LOGW(TAG, "upload part missing name=%s", wanted_name);
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

static esp_err_t send_upload_result(httpd_req_t *req, bool ok, const char *message,
                                    const char *error, const upload_meta_t *meta)
{
    char json[SERVER_NETWORK_STA_UPLOAD_RESULT_JSON_MAX];
    char bin_file[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX + 4] = {0};
    char image_file[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX + 4] = {0};
    char message_text[128] = {0};
    char error_text[128] = {0};
    const char *file_name = (meta != NULL && meta->file_name[0]) ? meta->file_name : "";

    strlcpy(message_text, message != NULL ? message : "", sizeof(message_text));
    if (ok && (error == NULL || error[0] == '\0')) {
        strlcpy(error_text, "no error", sizeof(error_text));
    } else {
        strlcpy(error_text, error != NULL ? error : "", sizeof(error_text));
    }
    if (file_name[0] != '\0') {
        snprintf(bin_file, sizeof(bin_file), "%s.bin", file_name);
        snprintf(image_file, sizeof(image_file), "%s.jpg", file_name);
    }

    int json_len = snprintf(json, sizeof(json),
                            "{\"func\":\"upload_result\",\"result\":%d,\"message\":\"%s\",\"fileName\":\"%s\","
                            "\"bin_file\":\"%s\",\"image_file\":\"%s\",\"save\":%s,\"show\":%s,\"error\":\"%s\"}",
                            ok ? 1 : 0,
                            message_text,
                            file_name,
                            bin_file,
                            image_file,
                            (meta != NULL && meta->save) ? "true" : "false",
                            (meta != NULL && meta->show) ? "true" : "false",
                            error_text);
    if (json_len < 0 || (size_t)json_len >= sizeof(json)) {
        ESP_LOGE(TAG, "upload response json too long file=%s", file_name);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req,
                                  "{\"func\":\"upload_result\",\"result\":0,\"message\":\"upload response too long\",\"error\":\"response_too_long\"}");
    }
    ESP_LOGI(TAG, "upload response: %s", json);
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
        ESP_LOGW(TAG, "upload mkdir not supported, skip path=%s errno=%d", path, err);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "upload mkdir failed path=%s errno=%d", path, err);
    return ESP_FAIL;
}

static esp_err_t write_file_exact(const char *path, const char *data, size_t len)
{
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "upload open failed path=%s errno=%d", path, errno);
        return ESP_FAIL;
    }

    size_t written_total = 0;
    while (written_total < len) {
        size_t remain = len - written_total;
        size_t chunk = remain > SERVER_NETWORK_STA_UPLOAD_WRITE_CHUNK ?
                       SERVER_NETWORK_STA_UPLOAD_WRITE_CHUNK : remain;
        errno = 0;
        size_t written = fwrite(data + written_total, 1, chunk, fp);
        if (written != chunk) {
            int err = errno;
            int file_error = ferror(fp);
            fclose(fp);
            ESP_LOGE(TAG, "upload write failed path=%s offset=%u chunk=%u written=%u errno=%d ferror=%d",
                     path,
                     (unsigned int)written_total,
                     (unsigned int)chunk,
                     (unsigned int)written,
                     err,
                     file_error);
            return ESP_FAIL;
        }
        written_total += written;
    }

    if (fflush(fp) != 0) {
        int err = errno;
        fclose(fp);
        ESP_LOGE(TAG, "upload flush failed path=%s errno=%d", path, err);
        return ESP_FAIL;
    }

    fclose(fp);
    ESP_LOGI(TAG, "upload write path=%s len=%u written=%u",
             path, (unsigned int)len, (unsigned int)written_total);
    return ESP_OK;
}

static esp_err_t check_upload_save_space(const char *base_path, size_t bin_len, size_t image_len)
{
    if (base_path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#ifdef CONFIG_EXAMPLE_MOUNT_SD_CARD
    uint64_t total_bytes = 0;
    uint64_t free_bytes64 = 0;
    esp_err_t info_ret = esp_vfs_fat_info(base_path, &total_bytes, &free_bytes64);
    if (info_ret != ESP_OK) {
        ESP_LOGW(TAG, "upload fatfs info failed base=%s ret=%s, continue without space check",
                 base_path, esp_err_to_name(info_ret));
        return ESP_OK;
    }
    size_t free_bytes = (size_t)free_bytes64;
#else
    size_t total = 0;
    size_t used = 0;
    esp_err_t info_ret = esp_spiffs_info(NULL, &total, &used);
    if (info_ret != ESP_OK || total < used) {
        ESP_LOGW(TAG, "upload spiffs info failed base=%s ret=%s total=%u used=%u, continue without space check",
                 base_path, esp_err_to_name(info_ret), (unsigned int)total, (unsigned int)used);
        return ESP_OK;
    }

    size_t free_bytes = total - used;
#endif
    size_t required_bytes = bin_len + image_len + SERVER_NETWORK_STA_CAST_SAVE_RESERVE_BYTES;

    if (free_bytes < required_bytes) {
        ESP_LOGE(TAG, "upload not enough storage free=%u required=%u",
                 (unsigned int)free_bytes, (unsigned int)required_bytes);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t save_one_upload_file(const char *dir_path, const char *file_name,
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
        ESP_LOGE(TAG, "upload temp size mismatch path=%s stat=%d actual=%u expected=%u",
                 tmp_path, stat_ret, stat_ret == 0 ? (unsigned int)st.st_size : 0, (unsigned int)len);
        unlink(tmp_path);
        return ESP_FAIL;
    }

    unlink(final_path);
    if (rename(tmp_path, final_path) != 0) {
        ESP_LOGE(TAG, "upload rename failed tmp=%s final=%s errno=%d", tmp_path, final_path, errno);
        unlink(tmp_path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "upload saved final=%s size=%u", final_path, (unsigned int)len);
    return ESP_OK;
}

static esp_err_t save_upload_files(const char *base_path, const upload_meta_t *meta,
                                   const multipart_part_t *bin_part,
                                   const multipart_part_t *image_part)
{
    char bin_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    char jpg_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];

    snprintf(bin_dir, sizeof(bin_dir), "%s/bin_img", base_path);
    snprintf(jpg_dir, sizeof(jpg_dir), "%s/jpg_img", base_path);

    ESP_LOGI(TAG, "upload ensure dirs bin_dir=%s jpg_dir=%s", bin_dir, jpg_dir);
    if (ensure_dir(bin_dir) != ESP_OK || ensure_dir(jpg_dir) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t space_ret = check_upload_save_space(base_path, bin_part->len, image_part->len);
    if (space_ret != ESP_OK) {
        return space_ret;
    }
    if (save_one_upload_file(bin_dir, meta->file_name, ".bin", bin_part->data, bin_part->len) != ESP_OK) {
        return ESP_FAIL;
    }
    if (save_one_upload_file(jpg_dir, meta->file_name, ".jpg", image_part->data, image_part->len) != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t ServerNetworkStaUpload_Process(httpd_req_t *req,
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
    upload_meta_t meta = {0};

    if (!extract_boundary(content_type, boundary, sizeof(boundary))) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!extract_multipart_part(body, body_len, boundary, "func", &func_part)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    copy_part_text(&func_part, meta.func, sizeof(meta.func));
    if (strcmp(meta.func, "upload") != 0) {
        ESP_LOGW(TAG, "upload unsupported func=%s", meta.func);
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
    meta.show = parse_bool_field(&show_part, false);

    ESP_LOGI(TAG, "upload meta func=%s fileName=%s save=%d show=%d",
             meta.func, meta.file_name, meta.save ? 1 : 0, meta.show ? 1 : 0);

    if (!file_name_is_safe(meta.file_name)) {
        return send_upload_result(req, false, "invalid fileName", "invalid_fileName", &meta);
    }
    if (!parse_size_field(&bin_size_part, &meta.bin_size)) {
        return send_upload_result(req, false, "invalid bin size", "invalid_bin_size", &meta);
    }
    if (!parse_size_field(&image_size_part, &meta.image_size)) {
        return send_upload_result(req, false, "invalid image size", "invalid_image_size", &meta);
    }
    if (!bin_part.present || bin_part.data == NULL) {
        return send_upload_result(req, false, "missing bin", "missing_bin", &meta);
    }
    if (!image_part.present || image_part.data == NULL) {
        return send_upload_result(req, false, "missing image", "missing_image", &meta);
    }
    if (bin_part.len != meta.bin_size) {
        ESP_LOGE(TAG, "upload bin size mismatch expect=%u actual=%u",
                 (unsigned int)meta.bin_size, (unsigned int)bin_part.len);
        return send_upload_result(req, false, "bin size mismatch", "bin_size_mismatch", &meta);
    }
    if (image_part.len != meta.image_size) {
        ESP_LOGE(TAG, "upload image size mismatch expect=%u actual=%u",
                 (unsigned int)meta.image_size, (unsigned int)image_part.len);
        return send_upload_result(req, false, "image size mismatch", "image_size_mismatch", &meta);
    }

    if (meta.show) {
        ESP_LOGI(TAG, "upload show requested fileName=%s bin_len=%u queue display",
                 meta.file_name, (unsigned int)bin_part.len);
        esp_err_t display_ret = ServerNetworkStaEpdDisplay_Queue((const uint8_t *)bin_part.data, bin_part.len);
        if (display_ret != ESP_OK) {
            ESP_LOGE(TAG, "upload display queue failed fileName=%s ret=%s",
                     meta.file_name, esp_err_to_name(display_ret));
            return send_upload_result(req, false, "show failed", "show_failed", &meta);
        }
    }

    if (meta.save) {
        esp_err_t save_ret = save_upload_files(base_path, &meta, &bin_part, &image_part);
        if (save_ret == ESP_ERR_NOT_FOUND) {
            return send_upload_result(req, false, "sd not ready", "sd_not_ready", &meta);
        }
        if (save_ret == ESP_ERR_NO_MEM) {
            return send_upload_result(req, false, "storage not enough", "storage_not_enough", &meta);
        }
        if (save_ret != ESP_OK) {
            return send_upload_result(req, false, "save failed", "save_failed", &meta);
        }
    }

    return send_upload_result(req, true, "upload success", "", &meta);
}
