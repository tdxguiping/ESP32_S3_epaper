#include "factory_welcome_storage.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "factory_welcome_config.h"
#include "tdx_shared_spi.h"

static const char *TAG = "factory_welcome_storage";

#define FACTORY_WELCOME_PATH_MAX_SIZE 320U

static bool build_directory_path(const char *base_path,
                                 char *path,
                                 size_t path_size)
{
    if (base_path == NULL || base_path[0] == '\0' || path == NULL ||
        path_size == 0U) {
        return false;
    }
    int length = snprintf(path, path_size, "%s/%s",
                          base_path, FACTORY_WELCOME_DIRECTORY_NAME);
    return length > 0 && (size_t)length < path_size;
}

static bool build_file_path(const char *base_path,
                            const char *file_name,
                            char *path,
                            size_t path_size)
{
    char directory[FACTORY_WELCOME_PATH_MAX_SIZE] = {0};
    if (file_name == NULL || file_name[0] == '\0' ||
        !build_directory_path(base_path, directory, sizeof(directory))) {
        return false;
    }
    int length = snprintf(path, path_size, "%s/%s", directory, file_name);
    return length > 0 && (size_t)length < path_size;
}

esp_err_t FactoryWelcomeStorage_EnsureDirectory(const char *base_path)
{
    char path[FACTORY_WELCOME_PATH_MAX_SIZE] = {0};
    if (!build_directory_path(base_path, path, sizeof(path))) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = TdxSharedSpi_Lock(0);
    if (ret != ESP_OK) {
        return ret;
    }

    struct stat st = {0};
    if (stat(path, &st) == 0) {
        ret = S_ISDIR(st.st_mode) ? ESP_OK : ESP_FAIL;
    } else if (mkdir(path, 0775) != 0 && errno != EEXIST) {
        ret = ESP_FAIL;
    }
    TdxSharedSpi_Unlock();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "directory unavailable path=%s ret=%s errno=%d",
                 path, esp_err_to_name(ret), errno);
    }
    return ret;
}

esp_err_t FactoryWelcomeStorage_ReadManifest(const char *base_path,
                                             char *buffer,
                                             size_t buffer_capacity,
                                             size_t *manifest_size)
{
    char path[FACTORY_WELCOME_PATH_MAX_SIZE] = {0};
    if (buffer == NULL || buffer_capacity < 2U || manifest_size == NULL ||
        !build_file_path(base_path,
                         FACTORY_WELCOME_MANIFEST_FILE_NAME,
                         path,
                         sizeof(path))) {
        return ESP_ERR_INVALID_ARG;
    }
    *manifest_size = 0U;
    esp_err_t ret = TdxSharedSpi_Lock(0);
    if (ret != ESP_OK) {
        return ret;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ret = errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    } else {
        size_t read_size = fread(buffer, 1, buffer_capacity - 1U, file);
        int extra = fgetc(file);
        bool read_failed = ferror(file) != 0;
        int close_result = fclose(file);
        if (read_failed || extra != EOF || close_result != 0 || read_size == 0U) {
            ret = extra != EOF ? ESP_ERR_INVALID_SIZE : ESP_FAIL;
        } else {
            buffer[read_size] = '\0';
            *manifest_size = read_size;
        }
    }
    TdxSharedSpi_Unlock();
    return ret;
}

esp_err_t FactoryWelcomeStorage_FileHasSize(const char *base_path,
                                            const char *file_name,
                                            size_t expected_size,
                                            bool *matches)
{
    char path[FACTORY_WELCOME_PATH_MAX_SIZE] = {0};
    if (expected_size == 0U || matches == NULL ||
        !build_file_path(base_path, file_name, path, sizeof(path))) {
        return ESP_ERR_INVALID_ARG;
    }
    *matches = false;
    esp_err_t ret = TdxSharedSpi_Lock(0);
    if (ret != ESP_OK) {
        return ret;
    }
    struct stat st = {0};
    if (stat(path, &st) == 0) {
        *matches = S_ISREG(st.st_mode) && (size_t)st.st_size == expected_size;
    } else if (errno != ENOENT) {
        ret = ESP_FAIL;
    }
    TdxSharedSpi_Unlock();
    return ret;
}

esp_err_t FactoryWelcomeStorage_ReadFileExact(const char *base_path,
                                              const char *file_name,
                                              uint8_t *buffer,
                                              size_t expected_size)
{
    char path[FACTORY_WELCOME_PATH_MAX_SIZE] = {0};
    if (buffer == NULL || expected_size == 0U ||
        !build_file_path(base_path, file_name, path, sizeof(path))) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = TdxSharedSpi_Lock(0);
    if (ret != ESP_OK) {
        return ret;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ret = errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    } else {
        size_t read_size = fread(buffer, 1, expected_size, file);
        int extra = fgetc(file);
        bool read_failed = ferror(file) != 0;
        int close_result = fclose(file);
        if (read_failed || close_result != 0 || read_size != expected_size ||
            extra != EOF) {
            ret = ESP_ERR_INVALID_SIZE;
        }
    }
    TdxSharedSpi_Unlock();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "file read failed path=%s expected=%u ret=%s",
                 path, (unsigned int)expected_size, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t write_atomic(const char *base_path,
                              const char *file_name,
                              const uint8_t *data,
                              size_t data_size)
{
    char path[FACTORY_WELCOME_PATH_MAX_SIZE] = {0};
    char temporary_path[FACTORY_WELCOME_PATH_MAX_SIZE + 4U] = {0};
    char backup_path[FACTORY_WELCOME_PATH_MAX_SIZE + 4U] = {0};
    if (data == NULL || data_size == 0U ||
        !build_file_path(base_path, file_name, path, sizeof(path))) {
        return ESP_ERR_INVALID_ARG;
    }
    int temporary_length = snprintf(temporary_path,
                                    sizeof(temporary_path),
                                    "%s.tmp",
                                    path);
    if (temporary_length <= 0 ||
        (size_t)temporary_length >= sizeof(temporary_path)) {
        return ESP_ERR_INVALID_ARG;
    }
    int backup_length = snprintf(backup_path,
                                 sizeof(backup_path),
                                 "%s.bak",
                                 path);
    if (backup_length <= 0 || (size_t)backup_length >= sizeof(backup_path)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = TdxSharedSpi_Lock(0);
    if (ret != ESP_OK) {
        return ret;
    }
    unlink(temporary_path);
    FILE *file = fopen(temporary_path, "wb");
    if (file == NULL) {
        ret = ESP_FAIL;
    } else {
        size_t written = fwrite(data, 1, data_size, file);
        int close_result = fclose(file);
        struct stat st = {0};
        if (written != data_size || close_result != 0 ||
            stat(temporary_path, &st) != 0 ||
            !S_ISREG(st.st_mode) || (size_t)st.st_size != data_size) {
            ret = ESP_FAIL;
        }
    }

    if (ret == ESP_OK) {
        bool had_original = access(path, F_OK) == 0;
        unlink(backup_path);
        if (had_original && rename(path, backup_path) != 0) {
            ret = ESP_FAIL;
        }
        if (ret == ESP_OK && rename(temporary_path, path) != 0) {
            ret = ESP_FAIL;
            if (had_original && rename(backup_path, path) != 0) {
                ESP_LOGE(TAG, "backup restore failed path=%s errno=%d",
                         path, errno);
            }
        }
        if (ret == ESP_OK && had_original) {
            unlink(backup_path);
        }
    }
    if (ret != ESP_OK) {
        int saved_errno = errno;
        unlink(temporary_path);
        ESP_LOGE(TAG, "atomic write failed path=%s size=%u errno=%d",
                 path, (unsigned int)data_size, saved_errno);
    }
    TdxSharedSpi_Unlock();
    return ret;
}

esp_err_t FactoryWelcomeStorage_WriteFileAtomic(const char *base_path,
                                                const char *file_name,
                                                const uint8_t *data,
                                                size_t data_size)
{
    return write_atomic(base_path, file_name, data, data_size);
}

esp_err_t FactoryWelcomeStorage_WriteManifestAtomic(const char *base_path,
                                                    const char *json,
                                                    size_t json_size)
{
    return write_atomic(base_path,
                        FACTORY_WELCOME_MANIFEST_FILE_NAME,
                        (const uint8_t *)json,
                        json_size);
}
