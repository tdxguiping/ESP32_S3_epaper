#include "local_image_browsing.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "epd_display_app.h"
#include "epd_display_mode.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "server_network_sta_daily_image.h"
#include "server_network_sta_slideshow.h"
#include "server_network_sta_slideshow_control.h"
#include "tdx_cfg.h"
#include "tdx_shared_spi.h"

static const char *TAG = "local_image_browsing";

typedef struct {
    uint32_t version;
    uint32_t crc32;
    uint8_t transaction_state;
    uint8_t reserved[3];
    char last_displayed_file[LOCAL_IMAGE_BROWSING_FILE_NAME_MAX_LEN];
    char pending_file[LOCAL_IMAGE_BROWSING_FILE_NAME_MAX_LEN];
    char next_file[LOCAL_IMAGE_BROWSING_FILE_NAME_MAX_LEN];
    uint32_t successful_display_count;
} local_image_browsing_persisted_state_t;

typedef struct {
    char selected_file[LOCAL_IMAGE_BROWSING_FILE_NAME_MAX_LEN];
    char next_file[LOCAL_IMAGE_BROWSING_FILE_NAME_MAX_LEN];
    size_t file_count;
} local_image_browsing_scan_result_t;

typedef struct {
    local_image_browsing_trigger_t trigger;
    uint16_t protocol_seq;
    epd_display_reservation_t reservation;
} local_image_browsing_request_t;

static QueueHandle_t s_request_queue;
static TaskHandle_t s_worker_task;
static SemaphoreHandle_t s_state_mutex;
static portMUX_TYPE s_startup_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_initialized;
static local_image_browsing_request_t
    s_startup_requests[LOCAL_IMAGE_BROWSING_STARTUP_QUEUE_LENGTH];
static size_t s_startup_request_head;
static size_t s_startup_request_count;
static char s_base_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX];
static local_image_browsing_persisted_state_t s_state;

static const char *trigger_to_string(local_image_browsing_trigger_t trigger)
{
    return trigger == LOCAL_IMAGE_BROWSING_TRIGGER_KEY_EVENT_PB2_PRESS
               ? "KEY_EVENT"
               : "DEVICE_INFO";
}

static uint32_t state_crc32(const local_image_browsing_persisted_state_t *state)
{
    if (state == NULL) {
        return 0;
    }

    local_image_browsing_persisted_state_t copy = *state;
    copy.crc32 = 0;
    const uint8_t *data = (const uint8_t *)&copy;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < sizeof(copy); ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

static void state_set_default(local_image_browsing_persisted_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->version = LOCAL_IMAGE_BROWSING_STATE_VERSION;
    state->transaction_state = LOCAL_IMAGE_BROWSING_TRANSACTION_IDLE;
    state->crc32 = state_crc32(state);
}

static bool state_string_valid(const char *value, size_t value_size)
{
    return value != NULL && memchr(value, '\0', value_size) != NULL;
}

static bool state_valid(const local_image_browsing_persisted_state_t *state)
{
    return state != NULL &&
           state->version == LOCAL_IMAGE_BROWSING_STATE_VERSION &&
           (state->transaction_state == LOCAL_IMAGE_BROWSING_TRANSACTION_IDLE ||
            state->transaction_state == LOCAL_IMAGE_BROWSING_TRANSACTION_PREPARED) &&
           state_string_valid(state->last_displayed_file,
                              sizeof(state->last_displayed_file)) &&
           state_string_valid(state->pending_file, sizeof(state->pending_file)) &&
           state_string_valid(state->next_file, sizeof(state->next_file)) &&
           state->crc32 == state_crc32(state);
}

static esp_err_t save_state_locked(void)
{
    s_state.version = LOCAL_IMAGE_BROWSING_STATE_VERSION;
    s_state.crc32 = state_crc32(&s_state);
    esp_err_t ret = app_nvs_write_blob(LOCAL_IMAGE_BROWSING_NVS_STATE_KEY,
                                       &s_state,
                                       sizeof(s_state));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "state save failed ret=%s", esp_err_to_name(ret));
        return ret;
    }

    local_image_browsing_persisted_state_t verify = {0};
    ret = app_nvs_read_blob(LOCAL_IMAGE_BROWSING_NVS_STATE_KEY,
                            &verify,
                            sizeof(verify));
    if (ret != ESP_OK || memcmp(&verify, &s_state, sizeof(verify)) != 0) {
        ESP_LOGE(TAG, "state verify failed ret=%s", esp_err_to_name(ret));
        return ret != ESP_OK ? ret : ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t load_state(void)
{
    local_image_browsing_persisted_state_t loaded = {0};
    esp_err_t ret = app_nvs_read_blob(LOCAL_IMAGE_BROWSING_NVS_STATE_KEY,
                                      &loaded,
                                      sizeof(loaded));
    if (ret == ESP_OK && state_valid(&loaded)) {
        s_state = loaded;
        ESP_LOGI(TAG,
                 "state restored transaction=%u pending=%s next=%s last=%s count=%lu",
                 (unsigned int)s_state.transaction_state,
                 s_state.pending_file[0] != '\0' ? s_state.pending_file : "(empty)",
                 s_state.next_file[0] != '\0' ? s_state.next_file : "(empty)",
                 s_state.last_displayed_file[0] != '\0'
                     ? s_state.last_displayed_file
                     : "(empty)",
                 (unsigned long)s_state.successful_display_count);
        return ESP_OK;
    }

    if (ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "saved state invalid ret=%s, reset cursor", esp_err_to_name(ret));
    }
    state_set_default(&s_state);
    return save_state_locked();
}

static int file_name_compare(const void *left, const void *right)
{
    const char *left_name = (const char *)left;
    const char *right_name = (const char *)right;
    int insensitive = strcasecmp(left_name, right_name);
    return insensitive != 0 ? insensitive : strcmp(left_name, right_name);
}

static bool bin_entry_name_valid(const char *entry_name,
                                 char *base_name,
                                 size_t base_name_size)
{
    if (entry_name == NULL || base_name == NULL || base_name_size == 0 ||
        strstr(entry_name, "..") != NULL || strchr(entry_name, '/') != NULL ||
        strchr(entry_name, '\\') != NULL) {
        return false;
    }

    size_t name_len = strlen(entry_name);
    size_t extension_len = strlen(LOCAL_IMAGE_BROWSING_FILE_EXTENSION);
    if (name_len <= extension_len ||
        strcmp(entry_name + name_len - extension_len,
               LOCAL_IMAGE_BROWSING_FILE_EXTENSION) != 0) {
        return false;
    }

    size_t base_len = name_len - extension_len;
    if (base_len == 0 || base_len >= base_name_size) {
        return false;
    }
    for (size_t i = 0; i < base_len; ++i) {
        unsigned char c = (unsigned char)entry_name[i];
        if (c < 0x20U || c > 0x7EU) {
            return false;
        }
    }
    memcpy(base_name, entry_name, base_len);
    base_name[base_len] = '\0';
    return true;
}

static bool build_bin_path(const char *file_name,
                           const char *suffix,
                           char *path,
                           size_t path_size)
{
    if (file_name == NULL || file_name[0] == '\0' || suffix == NULL ||
        path == NULL || path_size == 0U) {
        return false;
    }

    size_t used = strlcpy(path, LOCAL_IMAGE_BROWSING_BIN_DIRECTORY_PATH, path_size);
    if (used >= path_size || strlcat(path, "/", path_size) >= path_size ||
        strlcat(path, file_name, path_size) >= path_size ||
        strlcat(path, suffix, path_size) >= path_size) {
        path[0] = '\0';
        return false;
    }
    return true;
}

static void update_two_smallest(const char *file_name,
                                char *smallest,
                                char *second_smallest)
{
    if (smallest[0] == '\0' || file_name_compare(file_name, smallest) < 0) {
        strlcpy(second_smallest,
                smallest,
                LOCAL_IMAGE_BROWSING_FILE_NAME_MAX_LEN);
        strlcpy(smallest,
                file_name,
                LOCAL_IMAGE_BROWSING_FILE_NAME_MAX_LEN);
    } else if (strcmp(file_name, smallest) != 0 &&
               (second_smallest[0] == '\0' ||
                file_name_compare(file_name, second_smallest) < 0)) {
        strlcpy(second_smallest,
                file_name,
                LOCAL_IMAGE_BROWSING_FILE_NAME_MAX_LEN);
    }
}

static esp_err_t scan_next_file(const char *reference_file,
                                bool include_reference,
                                local_image_browsing_scan_result_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));

    char directory_path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + 24];
    snprintf(directory_path,
             sizeof(directory_path),
             "%s",
             LOCAL_IMAGE_BROWSING_BIN_DIRECTORY_PATH);

    esp_err_t lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        ESP_LOGE(TAG, "list scan SPI lock failed ret=%s", esp_err_to_name(lock_ret));
        return lock_ret;
    }

    DIR *directory = opendir(directory_path);
    if (directory == NULL) {
        int saved_errno = errno;
        TdxSharedSpi_Unlock();
        ESP_LOGE(TAG, "BIN directory open failed path=%s errno=%d",
                 directory_path, saved_errno);
        return ESP_ERR_NOT_FOUND;
    }

    /*
     * Keep only two global minima and two minima after the persisted cursor.
     * This preserves stable sorted traversal without a RAM directory list.
     */
    char global_first[LOCAL_IMAGE_BROWSING_FILE_NAME_MAX_LEN] = {0};
    char global_second[LOCAL_IMAGE_BROWSING_FILE_NAME_MAX_LEN] = {0};
    char eligible_first[LOCAL_IMAGE_BROWSING_FILE_NAME_MAX_LEN] = {0};
    char eligible_second[LOCAL_IMAGE_BROWSING_FILE_NAME_MAX_LEN] = {0};
    esp_err_t scan_ret = ESP_OK;
    struct dirent *entry = NULL;
    while (true) {
        errno = 0;
        entry = readdir(directory);
        if (entry == NULL) {
            if (errno != 0) {
                ESP_LOGE(TAG, "BIN directory read failed path=%s errno=%d",
                         directory_path, errno);
                scan_ret = ESP_FAIL;
            }
            break;
        }
        char base_name[LOCAL_IMAGE_BROWSING_FILE_NAME_MAX_LEN] = {0};
        if (!bin_entry_name_valid(entry->d_name, base_name, sizeof(base_name))) {
            continue;
        }

        char path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX +
                  LOCAL_IMAGE_BROWSING_FILE_NAME_MAX_LEN + 24];
        struct stat file_stat = {0};
        if (!build_bin_path(base_name,
                            LOCAL_IMAGE_BROWSING_FILE_EXTENSION,
                            path,
                            sizeof(path))) {
            ESP_LOGE(TAG, "BIN file path is too long file=%s", base_name);
            scan_ret = ESP_ERR_INVALID_SIZE;
            break;
        }
        errno = 0;
        if (stat(path, &file_stat) != 0) {
            int saved_errno = errno;
            if (saved_errno == ENOENT) {
                ESP_LOGW(TAG, "BIN file disappeared during scan path=%s", path);
                continue;
            }
            ESP_LOGE(TAG, "BIN file stat failed path=%s errno=%d",
                     path, saved_errno);
            scan_ret = ESP_FAIL;
            break;
        }
        if (!S_ISREG(file_stat.st_mode) || file_stat.st_size <= 0) {
            continue;
        }
        result->file_count++;
        update_two_smallest(base_name, global_first, global_second);

        bool eligible = reference_file == NULL || reference_file[0] == '\0';
        if (!eligible) {
            int compare = file_name_compare(base_name, reference_file);
            eligible = compare > 0 || (include_reference && compare == 0);
        }
        if (eligible) {
            update_two_smallest(base_name, eligible_first, eligible_second);
        }
    }
    errno = 0;
    if (closedir(directory) != 0 && scan_ret == ESP_OK) {
        ESP_LOGE(TAG, "BIN directory close failed path=%s errno=%d",
                 directory_path, errno);
        scan_ret = ESP_FAIL;
    }
    TdxSharedSpi_Unlock();

    if (scan_ret != ESP_OK) {
        return scan_ret;
    }

    if (result->file_count == 0U) {
        return ESP_ERR_NOT_FOUND;
    }
    const char *selected = eligible_first[0] != '\0'
                               ? eligible_first
                               : global_first;
    const char *next = eligible_first[0] != '\0' &&
                               eligible_second[0] != '\0'
                           ? eligible_second
                           : global_first;
    if (strcmp(next, selected) == 0 && global_second[0] != '\0') {
        next = global_second;
    }
    strlcpy(result->selected_file, selected, sizeof(result->selected_file));
    strlcpy(result->next_file, next, sizeof(result->next_file));
    ESP_LOGI(TAG, "BIN scan completed count=%u selected=%s next=%s",
             (unsigned int)result->file_count,
             result->selected_file,
             result->next_file);
    return ESP_OK;
}

static esp_err_t save_prepared_file(const char *file_name)
{
    s_state.transaction_state = LOCAL_IMAGE_BROWSING_TRANSACTION_PREPARED;
    strlcpy(s_state.pending_file, file_name, sizeof(s_state.pending_file));
    return save_state_locked();
}

static esp_err_t save_skipped_file(const char *next_file)
{
    s_state.transaction_state = LOCAL_IMAGE_BROWSING_TRANSACTION_IDLE;
    s_state.pending_file[0] = '\0';
    strlcpy(s_state.next_file, next_file, sizeof(s_state.next_file));
    return save_state_locked();
}

static esp_err_t save_display_success(const char *displayed_file,
                                      const char *next_file)
{
    s_state.transaction_state = LOCAL_IMAGE_BROWSING_TRANSACTION_IDLE;
    strlcpy(s_state.last_displayed_file,
            displayed_file,
            sizeof(s_state.last_displayed_file));
    s_state.pending_file[0] = '\0';
    strlcpy(s_state.next_file, next_file, sizeof(s_state.next_file));
    s_state.successful_display_count++;
    return save_state_locked();
}

static esp_err_t load_file(const char *file_name, uint8_t **buffer, size_t *size)
{
    if (file_name == NULL || buffer == NULL || size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *buffer = NULL;
    *size = 0;

    char path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX +
              LOCAL_IMAGE_BROWSING_FILE_NAME_MAX_LEN + 24];
    if (!build_bin_path(file_name,
                        LOCAL_IMAGE_BROWSING_FILE_EXTENSION,
                        path,
                        sizeof(path))) {
        ESP_LOGE(TAG, "BIN file path is too long file=%s", file_name);
        return ESP_ERR_INVALID_SIZE;
    }

    struct stat file_stat = {0};
    esp_err_t lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }
    errno = 0;
    if (stat(path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode) ||
        file_stat.st_size <= 0) {
        int saved_errno = errno;
        TdxSharedSpi_Unlock();
        ESP_LOGW(TAG, "BIN file missing or invalid, skip path=%s errno=%d",
                 path, saved_errno);
        return ESP_ERR_NOT_FOUND;
    }
    TdxSharedSpi_Unlock();

    uint8_t *loaded = (uint8_t *)heap_caps_malloc(
        (size_t)file_stat.st_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (loaded == NULL) {
        loaded = (uint8_t *)heap_caps_malloc((size_t)file_stat.st_size,
                                             MALLOC_CAP_8BIT);
    }
    if (loaded == NULL) {
        ESP_LOGE(TAG, "BIN file allocation failed path=%s size=%u",
                 path, (unsigned int)file_stat.st_size);
        return ESP_ERR_NO_MEM;
    }

    lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        heap_caps_free(loaded);
        return lock_ret;
    }
    errno = 0;
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        int saved_errno = errno;
        TdxSharedSpi_Unlock();
        heap_caps_free(loaded);
        if (saved_errno == ENOENT) {
            ESP_LOGW(TAG, "BIN file disappeared before read, skip path=%s", path);
            return ESP_ERR_NOT_FOUND;
        }
        ESP_LOGE(TAG, "BIN file open failed path=%s errno=%d", path, saved_errno);
        return ESP_FAIL;
    }
    size_t read_size = fread(loaded, 1, (size_t)file_stat.st_size, file);
    bool read_failed = ferror(file) != 0;
    errno = 0;
    int close_ret = fclose(file);
    int close_errno = errno;
    TdxSharedSpi_Unlock();
    if (read_failed || read_size != (size_t)file_stat.st_size || close_ret != 0) {
        heap_caps_free(loaded);
        ESP_LOGE(TAG,
                 "BIN file read failed path=%s expected=%u actual=%u close_errno=%d",
                 path,
                 (unsigned int)file_stat.st_size,
                 (unsigned int)read_size,
                 close_ret != 0 ? close_errno : 0);
        return ESP_FAIL;
    }

    *buffer = loaded;
    *size = read_size;
    return ESP_OK;
}

static esp_err_t stop_scheduled_modes(void)
{
    uint8_t mode = EpdDisplayMode_Get();
    if (mode == USER_EPD_DISPLAY_MODE_LOCAL_IMAGE_BROWSING) {
        return ESP_OK;
    }

    esp_err_t ret = ServerNetworkStaDailyImage_StopAndWait();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "daily stop wait failed ret=%s", esp_err_to_name(ret));
        return ret;
    }

    ret = ESP_OK;
    if (mode == USER_EPD_DISPLAY_MODE_SLIDESHOW) {
        ret = ServerNetworkStaSlideshowControl_Disable(s_base_path);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "slideshow control stop failed ret=%s",
                     esp_err_to_name(ret));
            return ret;
        }
    }
    ret = ServerNetworkStaSlideshow_StopAndWait();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "slideshow stop wait failed ret=%s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

static esp_err_t process_request(local_image_browsing_request_t *request)
{
    esp_err_t ret = stop_scheduled_modes();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = EpdDisplayMode_Set(USER_EPD_DISPLAY_MODE_LOCAL_IMAGE_BROWSING);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LOCAL_IMAGE_BROWSING mode save failed ret=%s",
                 esp_err_to_name(ret));
        return ret;
    }

    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    const char *reference_file = s_state.last_displayed_file;
    bool include_reference = false;
    if (s_state.transaction_state == LOCAL_IMAGE_BROWSING_TRANSACTION_PREPARED &&
        s_state.pending_file[0] != '\0') {
        reference_file = s_state.pending_file;
        include_reference = true;
    } else if (s_state.next_file[0] != '\0') {
        reference_file = s_state.next_file;
        include_reference = true;
    }

    local_image_browsing_scan_result_t scan = {0};
    ret = scan_next_file(reference_file, include_reference, &scan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "no usable local BIN image ret=%s", esp_err_to_name(ret));
        xSemaphoreGive(s_state_mutex);
        return ret;
    }

    size_t maximum_attempts = scan.file_count;
    bool image_displayed = false;
    for (size_t attempt = 0; attempt < maximum_attempts; ++attempt) {
        const char *file_name = scan.selected_file;
        ESP_LOGI(TAG,
                 "selected file=%s total=%u trigger=%s seq=%u",
                 file_name,
                 (unsigned int)scan.file_count,
                 trigger_to_string(request->trigger),
                 (unsigned int)request->protocol_seq);

        ret = save_prepared_file(file_name);
        if (ret != ESP_OK) {
            break;
        }

        uint8_t *buffer = NULL;
        size_t size = 0;
        ret = load_file(file_name, &buffer, &size);
        if (ret == ESP_ERR_NOT_FOUND) {
            char skipped_file[LOCAL_IMAGE_BROWSING_FILE_NAME_MAX_LEN];
            strlcpy(skipped_file, file_name, sizeof(skipped_file));
            local_image_browsing_scan_result_t next_scan = {0};
            ret = scan_next_file(skipped_file, false, &next_scan);
            if (ret != ESP_OK) {
                break;
            }
            ret = save_skipped_file(next_scan.selected_file);
            if (ret != ESP_OK) {
                break;
            }
            ESP_LOGW(TAG, "file skipped file=%s next=%s",
                     skipped_file, next_scan.selected_file);
            scan = next_scan;
            continue;
        }
        if (ret != ESP_OK) {
            break;
        }

        ret = ServerNetworkStaEpdDisplay_QueueReservedToScreenAndWait(
            &request->reservation, buffer, size, 1);
        heap_caps_free(buffer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "display failed file=%s ret=%s",
                     file_name, esp_err_to_name(ret));
            break;
        }

        image_displayed = true;
        ret = save_display_success(file_name, scan.next_file);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "display completed file=%s next=%s count=%lu",
                     file_name,
                     s_state.next_file,
                     (unsigned long)s_state.successful_display_count);
        }
        break;
    }
    if (!image_displayed && ret == ESP_OK) {
        ESP_LOGE(TAG, "all scanned BIN files disappeared or became invalid");
        ret = ESP_ERR_NOT_FOUND;
    }
    xSemaphoreGive(s_state_mutex);
    return ret;
}

static void local_image_browsing_worker(void *argument)
{
    (void)argument;
    local_image_browsing_request_t request = {0};
    for (;;) {
        if (xQueueReceive(s_request_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        esp_err_t ret = process_request(&request);
        if (request.reservation.valid) {
            ServerNetworkStaEpdDisplay_ReleaseReservation(&request.reservation);
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "request failed trigger=%s seq=%u ret=%s",
                     trigger_to_string(request.trigger),
                     (unsigned int)request.protocol_seq,
                     esp_err_to_name(ret));
        }
        UBaseType_t stack_free = uxTaskGetStackHighWaterMark(NULL);
        if (stack_free < LOCAL_IMAGE_BROWSING_STACK_WARNING_BYTES) {
            ESP_LOGW(TAG, "worker low stack watermark=%u bytes",
                     (unsigned int)stack_free);
        }
        memset(&request, 0, sizeof(request));
    }
}

static esp_err_t submit_initialized_request(local_image_browsing_trigger_t trigger,
                                            uint16_t protocol_seq)
{
    local_image_browsing_request_t request = {
        .trigger = trigger,
        .protocol_seq = protocol_seq,
    };
    esp_err_t ret = ServerNetworkStaEpdDisplay_TryReserveIdle(&request.reservation);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "request rejected EPD=BUSY trigger=%s seq=%u ret=%s",
                 trigger_to_string(trigger),
                 (unsigned int)protocol_seq,
                 esp_err_to_name(ret));
        return ret;
    }
    if (xQueueSend(s_request_queue, &request, 0) != pdTRUE) {
        ServerNetworkStaEpdDisplay_ReleaseReservation(&request.reservation);
        ESP_LOGE(TAG, "request queue full trigger=%s seq=%u",
                 trigger_to_string(trigger), (unsigned int)protocol_seq);
        return ESP_ERR_TIMEOUT;
    }
    ESP_LOGI(TAG, "request accepted trigger=%s seq=%u",
             trigger_to_string(trigger), (unsigned int)protocol_seq);
    return ESP_OK;
}

esp_err_t LocalImageBrowsing_Init(const char *base_path)
{
    if (base_path == NULL || strcmp(base_path, LOCAL_IMAGE_BROWSING_BASE_PATH) != 0 ||
        strlcpy(s_base_path, base_path, sizeof(s_base_path)) >= sizeof(s_base_path)) {
        ESP_LOGE(TAG, "init rejected non-SD base path=%s",
                 base_path != NULL ? base_path : "(null)");
        return ESP_ERR_INVALID_ARG;
    }
    if (s_state_mutex == NULL) {
        s_state_mutex = xSemaphoreCreateMutex();
        if (s_state_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_request_queue == NULL) {
        s_request_queue = xQueueCreate(LOCAL_IMAGE_BROWSING_QUEUE_LENGTH,
                                       sizeof(local_image_browsing_request_t));
        if (s_request_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_worker_task == NULL) {
        if (xTaskCreate(local_image_browsing_worker,
                        "local_img",
                        LOCAL_IMAGE_BROWSING_TASK_STACK_SIZE,
                        NULL,
                        LOCAL_IMAGE_BROWSING_TASK_PRIORITY,
                        &s_worker_task) != pdPASS) {
            s_worker_task = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = load_state();
    xSemaphoreGive(s_state_mutex);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t deferred_count = 0;
    for (;;) {
        local_image_browsing_request_t deferred = {0};
        portENTER_CRITICAL(&s_startup_mux);
        if (s_startup_request_count == 0U) {
            s_initialized = true;
            portEXIT_CRITICAL(&s_startup_mux);
            break;
        }
        deferred = s_startup_requests[s_startup_request_head];
        s_startup_request_head =
            (s_startup_request_head + 1U) % LOCAL_IMAGE_BROWSING_STARTUP_QUEUE_LENGTH;
        s_startup_request_count--;
        portEXIT_CRITICAL(&s_startup_mux);

        deferred_count++;
        (void)submit_initialized_request(deferred.trigger, deferred.protocol_seq);
    }
    ESP_LOGI(TAG, "initialized mode=%u active=%d deferred=%u",
             (unsigned int)EpdDisplayMode_Get(),
             LocalImageBrowsing_IsActive() ? 1 : 0,
             (unsigned int)deferred_count);
    return ESP_OK;
}

esp_err_t LocalImageBrowsing_RequestNext(local_image_browsing_trigger_t trigger,
                                         uint16_t protocol_seq)
{
    if (trigger != LOCAL_IMAGE_BROWSING_TRIGGER_DEVICE_INFO_KEY_PB2 &&
        trigger != LOCAL_IMAGE_BROWSING_TRIGGER_KEY_EVENT_PB2_PRESS) {
        return ESP_ERR_INVALID_ARG;
    }

    bool deferred = false;
    bool startup_queue_full = false;
    portENTER_CRITICAL(&s_startup_mux);
    if (!s_initialized) {
        if (s_startup_request_count < LOCAL_IMAGE_BROWSING_STARTUP_QUEUE_LENGTH) {
            size_t tail = (s_startup_request_head + s_startup_request_count) %
                          LOCAL_IMAGE_BROWSING_STARTUP_QUEUE_LENGTH;
            s_startup_requests[tail].trigger = trigger;
            s_startup_requests[tail].protocol_seq = protocol_seq;
            s_startup_request_count++;
            deferred = true;
        } else {
            startup_queue_full = true;
        }
    }
    portEXIT_CRITICAL(&s_startup_mux);

    if (startup_queue_full) {
        ESP_LOGE(TAG, "startup request queue full trigger=%s seq=%u",
                 trigger_to_string(trigger), (unsigned int)protocol_seq);
        return ESP_ERR_NO_MEM;
    }
    if (deferred) {
        ESP_LOGW(TAG, "PB2 deferred until module init trigger=%s seq=%u",
                 trigger_to_string(trigger), (unsigned int)protocol_seq);
        return ESP_OK;
    }
    return submit_initialized_request(trigger, protocol_seq);
}

bool LocalImageBrowsing_IsActive(void)
{
    return EpdDisplayMode_Get() == USER_EPD_DISPLAY_MODE_LOCAL_IMAGE_BROWSING;
}

esp_err_t LocalImageBrowsing_ResetState(void)
{
    if (s_state_mutex != NULL &&
        xSemaphoreTake(s_state_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    state_set_default(&s_state);
    esp_err_t ret = app_nvs_erase_key(LOCAL_IMAGE_BROWSING_NVS_STATE_KEY);
    if (s_state_mutex != NULL) {
        xSemaphoreGive(s_state_mutex);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "state reset failed ret=%s", esp_err_to_name(ret));
    }
    return ret;
}
