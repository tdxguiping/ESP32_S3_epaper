#include "server_network_sta_delete.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "esp_log.h"
#include "file_serving_example_common.h"
#include "led_status.h"
#include "tdx_cfg.h"
#include "tdx_shared_spi.h"

static const char *TAG = "server_sta_delete";

typedef struct {
    char file_names[TDX_DELETE_MAX_FILES][TDX_SLIDESHOW_FILE_NAME_MAX_LEN];
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
    size_t len = strlen(file_name);
    if (len > TDX_IMAGE_BASE_NAME_MAX_BYTES) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)file_name[i];
        if (c < 0x20U || c > 0x7EU) {
            return false;
        }
    }
    return true;
}

typedef enum {
    DELETE_PARSE_OK = 0,
    DELETE_PARSE_MISSING,
    DELETE_PARSE_INVALID_NAME,
    DELETE_PARSE_TOO_MANY_FILES,
    DELETE_PARSE_INVALID_JSON,
} delete_parse_result_t;

static delete_parse_result_t parse_file_names(const char *body, delete_request_t *request)
{
    if (request != NULL) {
        memset(request, 0, sizeof(*request));
    }

    const char *pos = find_json_key(body, "fileNames");
    if (pos == NULL || request == NULL) {
        return DELETE_PARSE_MISSING;
    }
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
        bool name_too_long = false;
        while (*pos != '\0' && *pos != '"') {
            if (len < TDX_IMAGE_BASE_NAME_MAX_BYTES) {
                file_name[len] = *pos;
            } else {
                name_too_long = true;
            }
            len++;
            pos++;
        }
        if (*pos != '"') {
            return DELETE_PARSE_INVALID_JSON;
        }
        pos++;
        if (name_too_long) {
            ESP_LOGE(TAG,
                     "delete rejected: fileName too long index=%u len=%u max=%u",
                     (unsigned int)request->file_count,
                     (unsigned int)len,
                     (unsigned int)TDX_IMAGE_BASE_NAME_MAX_BYTES);
            return DELETE_PARSE_INVALID_NAME;
        }
        file_name[len] = '\0';

        if (!file_name_is_safe(file_name)) {
            return DELETE_PARSE_INVALID_NAME;
        }
        if (request->file_count >= TDX_DELETE_MAX_FILES) {
            return DELETE_PARSE_TOO_MANY_FILES;
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
    } else if (result == TDX_JSON_RESULT_FILE_NAMES_TOO_MANY) {
        snprintf(json, sizeof(json),
                 "{\"func\":\"delete_result\",\"result\":%d,\"message\":\"too many fileNames\",\"maxFiles\":%d}",
                 result,
                 TDX_DELETE_MAX_FILES);
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

typedef enum {
    DELETE_PATH_DELETED = 0,
    DELETE_PATH_NOT_FOUND,
    DELETE_PATH_FAILED,
} delete_path_result_t;

static delete_path_result_t delete_one_path(const char *path)
{
    if (unlink(path) == 0) {
        return DELETE_PATH_DELETED;
    }
    if (errno == ENOENT) {
        return DELETE_PATH_NOT_FOUND;
    }
    ESP_LOGE(TAG, "delete failed path=%s errno=%d", path, errno);
    return DELETE_PATH_FAILED;
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
                     parse_result == DELETE_PARSE_TOO_MANY_FILES ? TDX_JSON_RESULT_FILE_NAMES_TOO_MANY :
                     parse_result == DELETE_PARSE_INVALID_JSON ? TDX_JSON_RESULT_JSON_INVALID :
                     TDX_JSON_RESULT_FILE_NAMES_MISSING;
        const char *message = parse_result == DELETE_PARSE_INVALID_NAME ? "invalid fileName" :
                              parse_result == DELETE_PARSE_TOO_MANY_FILES ? "too many fileNames" :
                              parse_result == DELETE_PARSE_INVALID_JSON ? "invalid JSON" :
                              "fileNames missing";
        ESP_LOGW(TAG, "delete rejected result=%d parse=%d accepted_count=%u max=%d",
                 result, (int)parse_result, (unsigned int)request.file_count, TDX_DELETE_MAX_FILES);
        return send_delete_result(req, result, message);
    }

    char bin_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    char jpg_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    struct stat st = {0};
    snprintf(bin_dir, sizeof(bin_dir), "%s/bin_img", base_path);
    snprintf(jpg_dir, sizeof(jpg_dir), "%s/jpg_img", base_path);

    esp_err_t lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        UserLedStatus_ShowOperationFail();
        return send_delete_result(req, TDX_JSON_RESULT_TIMEOUT, "delete failed");
    }

    if (example_storage_supports_directories() &&
        (stat(bin_dir, &st) != 0 || !S_ISDIR(st.st_mode)) &&
        (stat(jpg_dir, &st) != 0 || !S_ISDIR(st.st_mode))) {
        TdxSharedSpi_Unlock();
        ESP_LOGE(TAG, "delete image dirs missing bin=%s jpg=%s", bin_dir, jpg_dir);
        UserLedStatus_ShowOperationFail();
        return send_delete_result(req, TDX_JSON_RESULT_DELETE_FAILED, "delete failed");
    }

    int deleted_path_count = 0;
    int missing_path_count = 0;
    int failed_path_count = 0;
    for (size_t i = 0; i < request.file_count; i++) {
        char bin_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + TDX_SLIDESHOW_FILE_NAME_MAX_LEN + 24];
        char jpg_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + TDX_SLIDESHOW_FILE_NAME_MAX_LEN + 24];
        snprintf(bin_path, sizeof(bin_path), "%s/%s.bin", bin_dir, request.file_names[i]);
        snprintf(jpg_path, sizeof(jpg_path), "%s/%s.jpg", jpg_dir, request.file_names[i]);

        delete_path_result_t path_results[] = {
            delete_one_path(bin_path),
            delete_one_path(jpg_path),
        };
        for (size_t result_index = 0;
             result_index < sizeof(path_results) / sizeof(path_results[0]);
             result_index++) {
            if (path_results[result_index] == DELETE_PATH_DELETED) {
                deleted_path_count++;
            } else if (path_results[result_index] == DELETE_PATH_NOT_FOUND) {
                missing_path_count++;
            } else {
                failed_path_count++;
            }
        }
    }
    TdxSharedSpi_Unlock();

    if (failed_path_count > 0 || deleted_path_count <= 0) {
        ESP_LOGW(TAG, "delete failed request_count=%u deleted=%d missing=%d failed=%d",
                 (unsigned int)request.file_count,
                 deleted_path_count,
                 missing_path_count,
                 failed_path_count);
        UserLedStatus_ShowOperationFail();
        return send_delete_result(req, TDX_JSON_RESULT_DELETE_FAILED, "delete failed");
    }

    ESP_LOGI(TAG, "delete success request_count=%u deleted=%d missing=%d failed=%d",
             (unsigned int)request.file_count,
             deleted_path_count,
             missing_path_count,
             failed_path_count);
    UserLedStatus_ShowSuccess();
    return send_delete_result(req, TDX_JSON_RESULT_OK, NULL);
}
