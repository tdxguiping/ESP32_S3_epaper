#include "server_network_sta_saved_images.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "file_serving_example_common.h"
#include "tdx_cfg.h"

static const char *TAG = "server_sta_saved";

static bool json_func_equals(const char *body, const char *func)
{
    char pattern[96] = {0};
    snprintf(pattern, sizeof(pattern), "\"func\":\"%s\"", func);
    if (strstr(body, pattern) != NULL) {
        return true;
    }

    snprintf(pattern, sizeof(pattern), "\"func\" : \"%s\"", func);
    return strstr(body, pattern) != NULL;
}

static bool has_jpg_extension(const char *name)
{
    size_t len = name != NULL ? strlen(name) : 0;
    if (len <= 4) {
        return false;
    }
    return strcmp(name + len - 4, ".jpg") == 0 || strcmp(name + len - 4, ".JPG") == 0;
}

static bool saved_image_name_is_safe(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return false;
    }
    if (strstr(name, "..") != NULL || strchr(name, '/') != NULL || strchr(name, '\\') != NULL || strchr(name, '"') != NULL) {
        return false;
    }
    return true;
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

static esp_err_t send_saved_images_error(httpd_req_t *req)
{
    char json[128];
    ESP_LOGW(TAG, "get_saved_images failed");
    snprintf(json, sizeof(json),
             "{\"func\":\"get_saved_images_result\",\"result\":%d,\"message\":\"read saved images failed\"}",
             TDX_JSON_RESULT_IMAGES_READ_FAILED);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t send_saved_images_empty(httpd_req_t *req)
{
    ESP_LOGI(TAG, "get_saved_images empty result");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req,
                              "{\"func\":\"get_saved_images_result\",\"result\":0,\"images\":[]}");
}

esp_err_t ServerNetworkStaSavedImages_ProcessJson(httpd_req_t *req,
                                                 const char *body,
                                                 size_t body_len,
                                                 const char *base_path)
{
    (void)body_len;
    if (body == NULL || !json_func_equals(body, "get_saved_images")) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    char jpg_dir[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 16];
    snprintf(jpg_dir, sizeof(jpg_dir), "%s/jpg_img", base_path);
    const char *scan_dir = example_storage_supports_directories() ? jpg_dir : base_path;

    struct stat st = {0};
    if (example_storage_supports_directories() && (stat(base_path, &st) != 0 || !S_ISDIR(st.st_mode))) {
        ESP_LOGE(TAG, "get_saved_images base path missing: %s", base_path);
        return send_saved_images_error(req);
    }
    if (example_storage_supports_directories() && (stat(jpg_dir, &st) != 0 || !S_ISDIR(st.st_mode))) {
        ESP_LOGI(TAG, "get_saved_images jpg dir missing, return empty: %s", jpg_dir);
        return send_saved_images_empty(req);
    }

    DIR *dir = opendir(scan_dir);
    if (dir == NULL) {
        ESP_LOGE(TAG, "get_saved_images opendir failed: %s", scan_dir);
        return send_saved_images_error(req);
    }

    char *json = (char *)malloc(SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX);
    if (json == NULL) {
        closedir(dir);
        ESP_LOGE(TAG, "get_saved_images json alloc failed");
        return send_saved_images_error(req);
    }

    size_t used = 0;
    int count = 0;
    int written = snprintf(json, SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX,
                           "{\"func\":\"get_saved_images_result\",\"result\":0,\"images\":[");
    if (written < 0 || (size_t)written >= SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX) {
        free(json);
        closedir(dir);
        return send_saved_images_error(req);
    }
    used = (size_t)written;

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = saved_image_entry_name(entry->d_name);
        if (name == NULL || !has_jpg_extension(name) || !saved_image_name_is_safe(name)) {
            continue;
        }

        char file_name[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX] = {0};
        size_t name_len = strlen(name);
        size_t stem_len = name_len - 4;
        if (stem_len == 0 || stem_len >= sizeof(file_name)) {
            ESP_LOGW(TAG, "get_saved_images skip long file: %s", name);
            continue;
        }
        memcpy(file_name, name, stem_len);
        file_name[stem_len] = '\0';

        char item[SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX * 2 + 80];
        written = snprintf(item, sizeof(item),
                           "%s{\"fileName\":\"%s\",\"thumbnailUrl\":\"%s%s\"}",
                           count > 0 ? "," : "",
                           file_name,
                           SERVER_NETWORK_STA_THUMB_URI_PREFIX,
                           name);
        if (written < 0 || used + (size_t)written + 3 >= SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX) {
            ESP_LOGW(TAG, "get_saved_images json full at count=%d", count);
            break;
        }

        memcpy(json + used, item, (size_t)written);
        used += (size_t)written;
        count++;
        ESP_LOGI(TAG, "get_saved_images add fileName=%s thumbnail=%s%s",
                 file_name, SERVER_NETWORK_STA_THUMB_URI_PREFIX, name);
    }

    closedir(dir);
    snprintf(json + used, SERVER_NETWORK_STA_SAVED_IMAGES_JSON_MAX - used, "]}");

    ESP_LOGI(TAG, "get_saved_images result count=%d", count);
    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_sendstr(req, json);
    free(json);
    return ret;
}

esp_err_t ServerNetworkStaSavedImages_SendThumbnail(httpd_req_t *req,
                                                    const char *base_path,
                                                    const char *uri,
                                                    char *scratch,
                                                    size_t scratch_size)
{
    if (uri == NULL || strncmp(uri, SERVER_NETWORK_STA_THUMB_URI_PREFIX,
                              strlen(SERVER_NETWORK_STA_THUMB_URI_PREFIX)) != 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    const char *name = uri + strlen(SERVER_NETWORK_STA_THUMB_URI_PREFIX);
    if (!saved_image_name_is_safe(name) || !has_jpg_extension(name)) {
        ESP_LOGW(TAG, "thumb invalid name uri=%s", uri);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid thumbnail name");
        return ESP_FAIL;
    }

    char path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX + 24];
    snprintf(path, sizeof(path), "%s/jpg_img/%s", base_path, name);

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "thumb open failed path=%s", path);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Thumbnail does not exist");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "thumb send path=%s", path);
    httpd_resp_set_type(req, "image/jpeg");

    size_t read_len = 0;
    while ((read_len = fread(scratch, 1, scratch_size, fp)) > 0) {
        if (httpd_resp_send_chunk(req, scratch, read_len) != ESP_OK) {
            fclose(fp);
            ESP_LOGE(TAG, "thumb send chunk failed path=%s", path);
            httpd_resp_sendstr_chunk(req, NULL);
            return ESP_FAIL;
        }
    }

    fclose(fp);
    return httpd_resp_send_chunk(req, NULL, 0);
}
