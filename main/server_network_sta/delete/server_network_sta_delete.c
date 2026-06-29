#include "server_network_sta_delete.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "esp_log.h"
#include "file_serving_example_common.h"
#include "tdx_cfg.h"

static const char *TAG = "server_sta_delete";

typedef struct {
    char file_names[SERVER_NETWORK_STA_DELETE_MAX_FILES][TDX_SLIDESHOW_FILE_NAME_MAX_LEN];
    size_t file_count;
} delete_request_t;

static bool json_func_equals(const char *body, const char *func)
{
    const char *pos = strstr(body, "\"func\"");
    if (pos == NULL || func == NULL) {
        return false;
    }
    pos += strlen("\"func\"");
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

    size_t func_len = strlen(func);
    return strncmp(pos, func, func_len) == 0 && pos[func_len] == '"';
}

static const char *find_json_key(const char *body, const char *key)
{
    char pattern[64];
    const char *pos = body;
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

static bool file_name_is_safe(const char *file_name)
{
    if (file_name == NULL || file_name[0] == '\0') {
        return false;
    }
    if (strstr(file_name, "..") != NULL || strchr(file_name, '/') != NULL ||
        strchr(file_name, '\\') != NULL || strchr(file_name, '"') != NULL) {
        return false;
    }
    return strlen(file_name) < TDX_SLIDESHOW_FILE_NAME_MAX_LEN;
}

typedef enum {
    DELETE_PARSE_OK = 0,
    DELETE_PARSE_MISSING,
    DELETE_PARSE_INVALID_NAME,
    DELETE_PARSE_INVALID_JSON,
} delete_parse_result_t;

static delete_parse_result_t parse_file_names(const char *body, delete_request_t *request)
{
    const char *pos = find_json_key(body, "fileNames");
    if (pos == NULL || request == NULL) {
        return DELETE_PARSE_MISSING;
    }

    memset(request, 0, sizeof(*request));
    pos += strlen("fileNames") + 2;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    if (*pos != ':') {
        return DELETE_PARSE_INVALID_JSON;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    if (*pos != '[') {
        return DELETE_PARSE_INVALID_JSON;
    }
    pos++;

    bool closed = false;
    while (*pos != '\0') {
        while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == ',') {
            pos++;
        }
        if (*pos == ']') {
            closed = true;
            break;
        }
        if (*pos != '"') {
            return DELETE_PARSE_INVALID_JSON;
        }
        pos++;

        char file_name[TDX_SLIDESHOW_FILE_NAME_MAX_LEN] = {0};
        size_t len = 0;
        while (*pos != '\0' && *pos != '"' && len + 1 < sizeof(file_name)) {
            file_name[len++] = *pos++;
        }
        if (*pos != '"') {
            return DELETE_PARSE_INVALID_JSON;
        }
        pos++;
        file_name[len] = '\0';

        if (!file_name_is_safe(file_name) || request->file_count >= SERVER_NETWORK_STA_DELETE_MAX_FILES) {
            return DELETE_PARSE_INVALID_NAME;
        }
        strlcpy(request->file_names[request->file_count], file_name,
                sizeof(request->file_names[request->file_count]));
        request->file_count++;
    }

    if (!closed) {
        return DELETE_PARSE_INVALID_JSON;
    }
    return request->file_count > 0 ? DELETE_PARSE_OK : DELETE_PARSE_MISSING;
}

static esp_err_t send_delete_result(httpd_req_t *req, int result, const char *message)
{
    char json[160];
    if (result == TDX_JSON_RESULT_OK) {
        snprintf(json, sizeof(json),
                 "{\"func\":\"delete_result\",\"result\":%d}",
                 TDX_JSON_RESULT_OK);
    } else {
        snprintf(json, sizeof(json),
                 "{\"func\":\"delete_result\",\"result\":%d,\"message\":\"%s\"}",
                 result,
                 message != NULL ? message : "delete failed");
    }

    ESP_LOGI(TAG, "delete response: %s", json);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static bool delete_one_path(const char *path)
{
    if (unlink(path) == 0) {
        ESP_LOGI(TAG, "delete removed path=%s", path);
        return true;
    }
    if (errno == ENOENT) {
        ESP_LOGI(TAG, "delete path already missing=%s", path);
        return false;
    }
    ESP_LOGE(TAG, "delete failed path=%s errno=%d", path, errno);
    return false;
}

esp_err_t ServerNetworkStaDelete_ProcessJson(httpd_req_t *req,
                                             const char *body,
                                             size_t body_len,
                                             const char *base_path)
{
    (void)body_len;
    if (!json_func_equals(body, "delete")) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    delete_request_t request;
    delete_parse_result_t parse_result = parse_file_names(body, &request);
    if (parse_result != DELETE_PARSE_OK) {
        int result = parse_result == DELETE_PARSE_INVALID_NAME ? TDX_JSON_RESULT_FILE_NAME_INVALID :
                     parse_result == DELETE_PARSE_INVALID_JSON ? TDX_JSON_RESULT_JSON_INVALID :
                     TDX_JSON_RESULT_FILE_NAMES_MISSING;
        return send_delete_result(req, result, "delete failed");
    }

    char bin_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    char jpg_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    struct stat st = {0};
    snprintf(bin_dir, sizeof(bin_dir), "%s/bin_img", base_path);
    snprintf(jpg_dir, sizeof(jpg_dir), "%s/jpg_img", base_path);

    if (example_storage_supports_directories() &&
        (stat(bin_dir, &st) != 0 || !S_ISDIR(st.st_mode)) &&
        (stat(jpg_dir, &st) != 0 || !S_ISDIR(st.st_mode))) {
        ESP_LOGE(TAG, "delete image dirs missing bin=%s jpg=%s", bin_dir, jpg_dir);
        return send_delete_result(req, TDX_JSON_RESULT_DELETE_FAILED, "delete failed");
    }

    int removed_count = 0;
    for (size_t i = 0; i < request.file_count; i++) {
        char bin_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + TDX_SLIDESHOW_FILE_NAME_MAX_LEN + 24];
        char jpg_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + TDX_SLIDESHOW_FILE_NAME_MAX_LEN + 24];
        snprintf(bin_path, sizeof(bin_path), "%s/%s.bin", bin_dir, request.file_names[i]);
        snprintf(jpg_path, sizeof(jpg_path), "%s/%s.jpg", jpg_dir, request.file_names[i]);

        if (delete_one_path(bin_path)) {
            removed_count++;
        }
        if (delete_one_path(jpg_path)) {
            removed_count++;
        }
    }

    if (removed_count <= 0) {
        return send_delete_result(req, TDX_JSON_RESULT_DELETE_FAILED, "delete failed");
    }

    ESP_LOGI(TAG, "delete success removed_count=%d request_count=%u",
             removed_count, (unsigned int)request.file_count);
    return send_delete_result(req, TDX_JSON_RESULT_OK, NULL);
}
