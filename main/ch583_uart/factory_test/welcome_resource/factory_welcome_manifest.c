#include "factory_welcome_manifest.h"

#include <limits.h>
#include <string.h>

#include "cJSON.h"

static bool json_u32(const cJSON *item, uint32_t *value)
{
    if (!cJSON_IsNumber(item) || value == NULL || item->valuedouble < 0 ||
        item->valuedouble > UINT32_MAX) {
        return false;
    }
    uint32_t converted = (uint32_t)item->valuedouble;
    if ((double)converted != item->valuedouble) {
        return false;
    }
    *value = converted;
    return true;
}

static bool json_compressed_size(const cJSON *item, size_t *value)
{
    uint32_t converted = 0;
    if (!json_u32(item, &converted) || converted == 0U ||
        converted > FACTORY_WELCOME_MAX_COMPRESSED_SIZE) {
        return false;
    }
    *value = (size_t)converted;
    return true;
}

static bool parse_url(const char *url, char *file_name, size_t file_name_size)
{
    if (url == NULL || file_name == NULL || file_name_size == 0U) {
        return false;
    }

    const char *name = NULL;
    const size_t local_prefix_size =
        sizeof(FACTORY_WELCOME_LOCAL_FILE_URL_PREFIX) - 1U;
    if (strncmp(url,
                FACTORY_WELCOME_LOCAL_FILE_URL_PREFIX,
                local_prefix_size) == 0) {
        name = url + local_prefix_size;
    } else {
        return false;
    }

    size_t name_size = strlen(name);
    if (name_size < 5U || name_size >= file_name_size ||
        strstr(name, "..") != NULL || strchr(name, '/') != NULL ||
        strchr(name, '\\') != NULL || strchr(name, '?') != NULL ||
        strchr(name, '#') != NULL ||
        strcmp(name + name_size - 4U, ".bin") != 0) {
        return false;
    }
    for (size_t i = 0; i < name_size; ++i) {
        unsigned char ch = (unsigned char)name[i];
        bool allowed = (ch >= 'a' && ch <= 'z') ||
                       (ch >= 'A' && ch <= 'Z') ||
                       (ch >= '0' && ch <= '9') ||
                       ch == '_' || ch == '-' || ch == '.';
        if (!allowed) {
            return false;
        }
    }
    strlcpy(file_name, name, file_name_size);
    return true;
}

static bool file_name_is_duplicate(const factory_welcome_manifest_t *manifest,
                                   const char *file_name)
{
    for (size_t i = 0; i < manifest->file_count; ++i) {
        if (strcmp(manifest->files[i].file_name, file_name) == 0) {
            return true;
        }
    }
    return false;
}

esp_err_t FactoryWelcomeManifest_ParseForResolution(
    const char *json,
    size_t json_size,
    const char *resolution,
    factory_welcome_manifest_t *manifest)
{
    if (json == NULL || json_size == 0U || resolution == NULL ||
        resolution[0] == '\0' || manifest == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(manifest, 0, sizeof(*manifest));

    cJSON *root = cJSON_ParseWithLength(json, json_size);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *resources = cJSON_GetObjectItemCaseSensitive(root, "resources");
    if (!json_u32(version, &manifest->version) || !cJSON_IsArray(resources)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    bool matched = false;
    const cJSON *resource = NULL;
    cJSON_ArrayForEach(resource, resources) {
        const cJSON *resolution_item = cJSON_IsObject(resource) ?
            cJSON_GetObjectItemCaseSensitive(resource, "resolution") : NULL;
        if (!cJSON_IsString(resolution_item) ||
            resolution_item->valuestring == NULL ||
            strcmp(resolution_item->valuestring, resolution) != 0) {
            continue;
        }
        if (matched) {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_RESPONSE;
        }
        matched = true;

        const cJSON *images = cJSON_GetObjectItemCaseSensitive(resource, "images");
        const cJSON *welcome = cJSON_IsObject(images) ?
            cJSON_GetObjectItemCaseSensitive(images, "welcome") : NULL;
        int file_count = cJSON_IsArray(welcome) ? cJSON_GetArraySize(welcome) : 0;
        if (file_count <= 0 || file_count > (int)FACTORY_WELCOME_MAX_FILES) {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_RESPONSE;
        }

        for (int i = 0; i < file_count; ++i) {
            const cJSON *file = cJSON_GetArrayItem(welcome, i);
            const cJSON *url = cJSON_IsObject(file) ?
                cJSON_GetObjectItemCaseSensitive(file, "url") : NULL;
            const cJSON *compressed_size = cJSON_IsObject(file) ?
                cJSON_GetObjectItemCaseSensitive(file, "compressed_size") : NULL;
            factory_welcome_file_t *output = &manifest->files[manifest->file_count];
            if (!cJSON_IsString(url) || url->valuestring == NULL ||
                strlen(url->valuestring) >= sizeof(output->url) ||
                !parse_url(url->valuestring,
                           output->file_name,
                           sizeof(output->file_name)) ||
                file_name_is_duplicate(manifest, output->file_name) ||
                !json_compressed_size(compressed_size,
                                      &output->compressed_size)) {
                cJSON_Delete(root);
                memset(manifest, 0, sizeof(*manifest));
                return ESP_ERR_INVALID_RESPONSE;
            }
            strlcpy(output->url, url->valuestring, sizeof(output->url));
            ++manifest->file_count;
        }
    }

    cJSON_Delete(root);
    if (!matched) {
        memset(manifest, 0, sizeof(*manifest));
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}
