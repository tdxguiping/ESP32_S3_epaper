#include "factory_reset_welcome.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "memory_policy.h"
#include "tdx_shared_spi.h"

static const char *TAG = "factory_reset_welcome";

#define FACTORY_RESET_WELCOME_DIRECTORY_NAME "welcome"
#define FACTORY_RESET_WELCOME_NAME_MARKER "_welcome_"
#define FACTORY_RESET_WELCOME_FILE_SUFFIX ".bin"
#define FACTORY_RESET_WELCOME_MAX_COMPRESSED_SIZE (2U * 1024U * 1024U)
#define FACTORY_RESET_WELCOME_PATH_MAX_SIZE 320U

static bool factory_reset_welcome_parse_sequence(const char *name,
                                                 unsigned long long *sequence)
{
    if (name == NULL || sequence == NULL) {
        return false;
    }

    const char *marker = strstr(name, FACTORY_RESET_WELCOME_NAME_MARKER);
    if (marker == NULL) {
        return false;
    }

    const char *digits = marker + strlen(FACTORY_RESET_WELCOME_NAME_MARKER);
    const char *suffix = strrchr(digits, '.');
    if (suffix == NULL || strcmp(suffix, FACTORY_RESET_WELCOME_FILE_SUFFIX) != 0 ||
        suffix == digits) {
        return false;
    }
    for (const char *cursor = digits; cursor < suffix; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
    }

    // Compare the numeric suffix as a number so 10 is newer than 9.
    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(digits, &end, 10);
    if (errno == ERANGE || end != suffix) {
        return false;
    }
    *sequence = parsed;
    return true;
}

static esp_err_t factory_reset_welcome_select_file(const char *directory_path,
                                                   char *selected_name,
                                                   size_t selected_name_size)
{
    DIR *directory = opendir(directory_path);
    if (directory == NULL) {
        ESP_LOGE(TAG, "welcome directory open failed path=%s errno=%d",
                 directory_path, errno);
        return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }

    bool found = false;
    unsigned long long selected_sequence = 0;
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    struct dirent *entry = NULL;
    while (true) {
        errno = 0;
        entry = readdir(directory);
        if (entry == NULL) {
            if (errno != 0) {
                ESP_LOGE(TAG, "welcome directory scan failed path=%s errno=%d",
                         directory_path, errno);
                ret = ESP_FAIL;
                goto cleanup;
            }
            break;
        }
        unsigned long long sequence = 0;
        if (!factory_reset_welcome_parse_sequence(entry->d_name, &sequence)) {
            continue;
        }
        if (strlen(entry->d_name) >= selected_name_size) {
            ESP_LOGE(TAG, "welcome file name too long");
            ret = ESP_ERR_INVALID_SIZE;
            goto cleanup;
        }

        char candidate_path[FACTORY_RESET_WELCOME_PATH_MAX_SIZE];
        int candidate_len = snprintf(candidate_path, sizeof(candidate_path),
                                     "%s/%s", directory_path, entry->d_name);
        if (candidate_len < 0 || (size_t)candidate_len >= sizeof(candidate_path)) {
            ESP_LOGE(TAG, "welcome file path too long");
            ret = ESP_ERR_INVALID_SIZE;
            goto cleanup;
        }
        struct stat candidate_stat;
        if (stat(candidate_path, &candidate_stat) != 0) {
            ESP_LOGE(TAG, "welcome candidate stat failed file=%s errno=%d",
                     entry->d_name, errno);
            ret = ESP_FAIL;
            goto cleanup;
        }
        if (!S_ISREG(candidate_stat.st_mode)) {
            continue;
        }

        // The full name provides a deterministic tie-break for equal numbers.
        if (!found || sequence > selected_sequence ||
            (sequence == selected_sequence && strcmp(entry->d_name, selected_name) > 0)) {
            strlcpy(selected_name, entry->d_name, selected_name_size);
            selected_sequence = sequence;
            found = true;
        }
    }
    if (!found) {
        ESP_LOGE(TAG, "welcome file not found path=%s", directory_path);
        ret = ESP_ERR_NOT_FOUND;
        goto cleanup;
    }
    ret = ESP_OK;

cleanup:
    if (closedir(directory) != 0 && ret == ESP_OK) {
        ESP_LOGE(TAG, "welcome directory close failed path=%s errno=%d",
                 directory_path, errno);
        ret = ESP_FAIL;
    }
    return ret;
}

esp_err_t FactoryResetWelcome_Load(const char *base_path,
                                   factory_reset_welcome_file_t *file)
{
    if (base_path == NULL || file == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(file, 0, sizeof(*file));

    char directory_path[FACTORY_RESET_WELCOME_PATH_MAX_SIZE];
    int path_len = snprintf(directory_path, sizeof(directory_path), "%s/%s",
                            base_path, FACTORY_RESET_WELCOME_DIRECTORY_NAME);
    if (path_len < 0 || (size_t)path_len >= sizeof(directory_path)) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "welcome SPI lock failed ret=%s", esp_err_to_name(ret));
        return ret;
    }

    FILE *input = NULL;
    char file_path[FACTORY_RESET_WELCOME_PATH_MAX_SIZE];
    struct stat file_stat;
    ret = factory_reset_welcome_select_file(directory_path,
                                            file->file_name,
                                            sizeof(file->file_name));
    if (ret != ESP_OK) {
        goto cleanup;
    }

    path_len = snprintf(file_path, sizeof(file_path), "%s/%s",
                        directory_path, file->file_name);
    if (path_len < 0 || (size_t)path_len >= sizeof(file_path)) {
        ret = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }
    if (stat(file_path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
        ESP_LOGE(TAG, "welcome file stat failed file=%s errno=%d",
                 file->file_name, errno);
        ret = ESP_FAIL;
        goto cleanup;
    }
    if (file_stat.st_size <= 0 ||
        (uint64_t)file_stat.st_size > FACTORY_RESET_WELCOME_MAX_COMPRESSED_SIZE) {
        ESP_LOGE(TAG, "welcome file size invalid file=%s size=%lld",
                 file->file_name, (long long)file_stat.st_size);
        ret = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    file->size = (size_t)file_stat.st_size;
    file->data = (uint8_t *)MemoryPolicy_AllocBuffer(file->size);
    if (file->data == NULL) {
        ESP_LOGE(TAG, "welcome buffer alloc failed size=%u",
                 (unsigned int)file->size);
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    input = fopen(file_path, "rb");
    if (input == NULL) {
        ESP_LOGE(TAG, "welcome file open failed file=%s errno=%d",
                 file->file_name, errno);
        ret = ESP_FAIL;
        goto cleanup;
    }
    if (fread(file->data, 1, file->size, input) != file->size) {
        ESP_LOGE(TAG, "welcome file read failed file=%s", file->file_name);
        ret = ESP_FAIL;
        goto cleanup;
    }
    ret = ESP_OK;

cleanup:
    if (input != NULL && fclose(input) != 0 && ret == ESP_OK) {
        ESP_LOGE(TAG, "welcome file close failed file=%s errno=%d",
                 file->file_name, errno);
        ret = ESP_FAIL;
    }
    TdxSharedSpi_Unlock();
    if (ret != ESP_OK) {
        FactoryResetWelcome_Release(file);
    }
    return ret;
}

void FactoryResetWelcome_Release(factory_reset_welcome_file_t *file)
{
    if (file == NULL) {
        return;
    }
    free(file->data);
    memset(file, 0, sizeof(*file));
}
