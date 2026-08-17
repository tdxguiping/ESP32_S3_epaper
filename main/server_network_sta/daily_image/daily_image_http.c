#include "daily_image_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "tdx_cfg.h"

static const char *TAG = "daily_image_http";

#ifdef CONFIG_MBEDTLS_HARDWARE_AES
#define DAILY_IMAGE_TLS_AES_MODE "hardware"
#else
#define DAILY_IMAGE_TLS_AES_MODE "software"
#endif

static bool https_url_is_valid(const char *url)
{
    return url != NULL && strncmp(url, "https://", 8) == 0;
}

static void log_tls_heap(const char *operation)
{
    ESP_LOGI(TAG,
             "TLS heap before %s internal_free=%u internal_largest=%u dma_free=%u dma_largest=%u psram_free=%u psram_largest=%u aes=%s",
             operation != NULL ? operation : "request",
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                                   MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_largest_free_block(
                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM |
                                                   MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_largest_free_block(
                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             DAILY_IMAGE_TLS_AES_MODE);
}

static esp_err_t write_all(esp_http_client_handle_t client,
                           const char *data,
                           size_t data_size)
{
    size_t offset = 0;
    while (offset < data_size) {
        int written = esp_http_client_write(client,
                                            data + offset,
                                            (int)(data_size - offset));
        if (written <= 0) {
            return ESP_FAIL;
        }
        offset += (size_t)written;
    }
    return ESP_OK;
}

static esp_err_t open_and_check_response(esp_http_client_handle_t client,
                                         int write_size,
                                         const char *request_body)
{
    esp_err_t ret = esp_http_client_open(client, write_size);
    if (ret != ESP_OK) {
        return ret;
    }
    if (write_size > 0) {
        ret = write_all(client, request_body, (size_t)write_size);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    int64_t content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0 && !esp_http_client_is_chunked_response(client)) {
        return ESP_FAIL;
    }

    int status = esp_http_client_get_status_code(client);
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "HTTP status rejected status=%d", status);
        return ESP_ERR_HTTP_BASE;
    }

    char final_url[USER_DAILY_IMAGE_DOWNLOAD_URL_BUFFER_SIZE] = {0};
    ret = esp_http_client_get_url(client, final_url, (int)sizeof(final_url));
    if (ret != ESP_OK || !https_url_is_valid(final_url)) {
        ESP_LOGE(TAG, "final URL rejected ret=%s url=%s",
                 esp_err_to_name(ret),
                 final_url[0] != '\0' ? final_url : "<empty>");
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static esp_http_client_handle_t init_https_client(const char *url,
                                                  esp_http_client_method_t method)
{
    log_tls_heap(method == HTTP_METHOD_POST ? "POST" : "GET");
    esp_http_client_config_t http_config = {
        .url = url,
        .method = method,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = USER_DAILY_IMAGE_HTTP_TIMEOUT_MS,
        .disable_auto_redirect = true,
        .keep_alive_enable = false,
    };
    return esp_http_client_init(&http_config);
}

esp_err_t DailyImageHttp_SelectDownloadUrl(
    const daily_image_config_t *config,
    char *download_url,
    size_t download_url_size,
    daily_image_continue_fn_t should_continue)
{
    if (config == NULL || download_url == NULL || download_url_size == 0 ||
        !https_url_is_valid(config->api_url)) {
        return ESP_ERR_INVALID_ARG;
    }
    download_url[0] = '\0';

    if (should_continue != NULL && !should_continue()) {
        return ESP_ERR_INVALID_STATE;
    }

    char request_body[USER_DAILY_IMAGE_QUERY_BODY_SIZE];
    int request_len = snprintf(request_body,
                               sizeof(request_body),
                               "{\"imageHeight\":%lu,\"imageWidth\":%lu,\"orientation\":%ld}",
                               (unsigned long)config->image_height,
                               (unsigned long)config->image_width,
                               (long)config->orientation);
    if (request_len < 0 || (size_t)request_len >= sizeof(request_body)) {
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "select POST url=%s body=%s", config->api_url, request_body);
    esp_http_client_handle_t client = init_https_client(config->api_url, HTTP_METHOD_POST);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = esp_http_client_set_header(client, "Content-Type", "application/json");
    if (ret == ESP_OK) {
        ret = open_and_check_response(client, request_len, request_body);
    }

    char *response = NULL;
    size_t total = 0;
    if (ret == ESP_OK) {
        int64_t content_length = esp_http_client_get_content_length(client);
        if (content_length >= (int64_t)USER_DAILY_IMAGE_QUERY_RESPONSE_SIZE) {
            ret = ESP_ERR_INVALID_SIZE;
        }
    }
    if (ret == ESP_OK) {
        response = (char *)calloc(1, USER_DAILY_IMAGE_QUERY_RESPONSE_SIZE);
        if (response == NULL) {
            ret = ESP_ERR_NO_MEM;
        }
    }

    while (ret == ESP_OK && total < USER_DAILY_IMAGE_QUERY_RESPONSE_SIZE - 1U) {
        if (should_continue != NULL && !should_continue()) {
            ret = ESP_ERR_INVALID_STATE;
            break;
        }
        int read_len = esp_http_client_read(client,
                                            response + total,
                                            (int)(USER_DAILY_IMAGE_QUERY_RESPONSE_SIZE -
                                                  1U - total));
        if (read_len < 0) {
            ret = ESP_FAIL;
            break;
        }
        if (read_len == 0) {
            break;
        }
        total += (size_t)read_len;
    }
    if (ret == ESP_OK && !esp_http_client_is_complete_data_received(client)) {
        ret = ESP_ERR_INVALID_RESPONSE;
    }

    if (ret == ESP_OK) {
        response[total] = '\0';
        ESP_LOGI(TAG, "select response bytes=%u body=%s",
                 (unsigned int)total, response);

        cJSON *root = cJSON_ParseWithLength(response, total);
        cJSON *code = root != NULL ?
            cJSON_GetObjectItemCaseSensitive(root, "code") : NULL;
        cJSON *data = root != NULL ?
            cJSON_GetObjectItemCaseSensitive(root, "data") : NULL;
        cJSON *url = cJSON_IsObject(data) ?
            cJSON_GetObjectItemCaseSensitive(data, "dailyImageUrl") : NULL;

        if (!cJSON_IsString(code) || code->valuestring == NULL ||
            strcmp(code->valuestring, "1") != 0 ||
            !cJSON_IsString(url) || url->valuestring == NULL ||
            !https_url_is_valid(url->valuestring) ||
            strlen(url->valuestring) >= download_url_size) {
            ESP_LOGE(TAG, "select response invalid code=%s",
                     cJSON_IsString(code) && code->valuestring != NULL ?
                     code->valuestring : "<missing>");
            ret = ESP_ERR_INVALID_RESPONSE;
        } else {
            strlcpy(download_url, url->valuestring, download_url_size);
            ESP_LOGI(TAG, "selected dailyImageUrl=%s", download_url);
        }
        cJSON_Delete(root);
    }

    free(response);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "select failed ret=%s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t DailyImageHttp_Download(
    const char *download_url,
    uint8_t *buffer,
    size_t buffer_size,
    bool exact_size_required,
    size_t *downloaded_size,
    daily_image_continue_fn_t should_continue)
{
    if (!https_url_is_valid(download_url) || buffer == NULL ||
        buffer_size == 0 || downloaded_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *downloaded_size = 0;

    ESP_LOGI(TAG, "download GET url=%s capacity=%u exact=%d",
             download_url,
             (unsigned int)buffer_size,
             exact_size_required ? 1 : 0);
    esp_http_client_handle_t client = init_https_client(download_url, HTTP_METHOD_GET);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = open_and_check_response(client, 0, NULL);
    if (ret == ESP_OK) {
        int64_t content_length = esp_http_client_get_content_length(client);
        if (content_length >= 0 &&
            ((exact_size_required && content_length != (int64_t)buffer_size) ||
             (!exact_size_required &&
              (content_length <= 0 || content_length > (int64_t)buffer_size)))) {
            ESP_LOGE(TAG, "download Content-Length invalid actual=%lld capacity=%u exact=%d",
                     (long long)content_length,
                     (unsigned int)buffer_size,
                     exact_size_required ? 1 : 0);
            ret = ESP_ERR_INVALID_SIZE;
        } else if (content_length < 0) {
            ESP_LOGW(TAG, "download has no Content-Length, validate accumulated bytes");
        }
    }

    uint32_t next_progress_step = 1U;
    while (ret == ESP_OK && *downloaded_size < buffer_size) {
        if (should_continue != NULL && !should_continue()) {
            ret = ESP_ERR_INVALID_STATE;
            break;
        }
        int read_len = esp_http_client_read(client,
                                            (char *)buffer + *downloaded_size,
                                            (int)(buffer_size - *downloaded_size));
        if (read_len < 0) {
            ret = ESP_FAIL;
            break;
        }
        if (read_len == 0) {
            break;
        }
        *downloaded_size += (size_t)read_len;
        if (next_progress_step < USER_DAILY_IMAGE_PROGRESS_STEP_COUNT &&
            *downloaded_size * USER_DAILY_IMAGE_PROGRESS_STEP_COUNT >=
                buffer_size * next_progress_step) {
            ESP_LOGI(TAG, "download progress=%u%% bytes=%u/%u",
                     (unsigned int)(next_progress_step * 100U /
                                    USER_DAILY_IMAGE_PROGRESS_STEP_COUNT),
                     (unsigned int)*downloaded_size,
                     (unsigned int)buffer_size);
            ++next_progress_step;
        }
    }

    bool complete = esp_http_client_is_complete_data_received(client);
    if (ret == ESP_OK &&
        (*downloaded_size == 0 ||
         (exact_size_required && *downloaded_size != buffer_size) ||
         !complete)) {
        ESP_LOGE(TAG, "download size invalid actual=%u capacity=%u exact=%d complete=%d",
                 (unsigned int)*downloaded_size,
                 (unsigned int)buffer_size,
                 exact_size_required ? 1 : 0,
                 complete ? 1 : 0);
        ret = ESP_ERR_INVALID_SIZE;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "download complete bytes=%u", (unsigned int)*downloaded_size);
    } else {
        ESP_LOGE(TAG, "download failed ret=%s bytes=%u",
                 esp_err_to_name(ret),
                 (unsigned int)*downloaded_size);
    }
    return ret;
}
