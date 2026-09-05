#include "factory_welcome_http.h"

#include <limits.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "factory_welcome_config.h"

static const char *TAG = "factory_welcome_http";

static bool url_is_allowed(const char *url)
{
    return url != NULL &&
           (strcmp(url, FACTORY_WELCOME_MANIFEST_URL) == 0 ||
            strcmp(url, FACTORY_WELCOME_CANONICAL_MANIFEST_URL) == 0 ||
            strncmp(url,
                    FACTORY_WELCOME_LOCAL_FILE_URL_PREFIX,
                    sizeof(FACTORY_WELCOME_LOCAL_FILE_URL_PREFIX) - 1U) == 0 ||
            strncmp(url,
                    FACTORY_WELCOME_LOCAL_CANONICAL_FILE_URL_PREFIX,
                    sizeof(FACTORY_WELCOME_LOCAL_CANONICAL_FILE_URL_PREFIX) - 1U) == 0);
}

static bool can_continue(factory_welcome_continue_fn_t should_continue,
                         void *context)
{
    return should_continue == NULL || should_continue(context);
}

esp_err_t FactoryWelcomeHttp_Get(const char *url,
                                 uint8_t *buffer,
                                 size_t buffer_capacity,
                                 size_t expected_size,
                                 size_t *received_size,
                                 uint32_t timeout_ms,
                                 factory_welcome_continue_fn_t should_continue,
                                 void *continue_context)
{
    if (!url_is_allowed(url) || buffer == NULL || buffer_capacity == 0U ||
        received_size == NULL || expected_size > buffer_capacity ||
        timeout_ms == 0U || timeout_ms > INT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    *received_size = 0U;
    if (!can_continue(should_continue, continue_context)) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = (int)timeout_ms,
        .disable_auto_redirect = true,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = esp_http_client_open(client, 0);
    if (ret == ESP_OK) {
        int64_t content_length = esp_http_client_fetch_headers(client);
        if (content_length < 0 && !esp_http_client_is_chunked_response(client)) {
            ret = ESP_ERR_INVALID_RESPONSE;
        } else if (content_length >= 0 &&
                   ((expected_size > 0U &&
                     content_length != (int64_t)expected_size) ||
                    (expected_size == 0U &&
                     (content_length == 0 ||
                      content_length > (int64_t)buffer_capacity)))) {
            ret = ESP_ERR_INVALID_SIZE;
        }
    }
    if (ret == ESP_OK && esp_http_client_get_status_code(client) != 200) {
        ret = ESP_ERR_HTTP_BASE;
    }
    if (ret == ESP_OK) {
        char final_url[FACTORY_WELCOME_URL_MAX_SIZE] = {0};
        ret = esp_http_client_get_url(client, final_url, (int)sizeof(final_url));
        if (ret == ESP_OK && !url_is_allowed(final_url)) {
            ret = ESP_ERR_INVALID_RESPONSE;
        }
    }

    while (ret == ESP_OK && *received_size < buffer_capacity) {
        if (!can_continue(should_continue, continue_context)) {
            ret = ESP_ERR_INVALID_STATE;
            break;
        }
        int read_size = esp_http_client_read(
            client,
            (char *)buffer + *received_size,
            (int)(buffer_capacity - *received_size));
        if (read_size < 0) {
            ret = ESP_FAIL;
            break;
        }
        if (read_size == 0) {
            break;
        }
        *received_size += (size_t)read_size;
    }

    if (ret == ESP_OK && *received_size == buffer_capacity) {
        char extra = 0;
        int extra_size = esp_http_client_read(client, &extra, 1);
        if (extra_size != 0) {
            ret = extra_size < 0 ? ESP_FAIL : ESP_ERR_INVALID_SIZE;
        }
    }
    if (ret == ESP_OK &&
        (!esp_http_client_is_complete_data_received(client) ||
         *received_size == 0U ||
         (expected_size > 0U && *received_size != expected_size))) {
        ret = ESP_ERR_INVALID_SIZE;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "GET failed url=%s received=%u expected=%u ret=%s",
                 url,
                 (unsigned int)*received_size,
                 (unsigned int)expected_size,
                 esp_err_to_name(ret));
    }
    return ret;
}
