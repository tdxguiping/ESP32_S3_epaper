#include "usb_console_saved_images.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "tdx_cfg.h"
#include "tdx_shared_spi.h"
#include "usb_console_common.h"
#include "usb_console_transport.h"

static const char *TAG = "usb_console_saved_images";

static bool has_jpg_extension(const char *name)
{
    size_t len = name != NULL ? strlen(name) : 0;
    return len > 4 && (strcmp(name + len - 4, ".jpg") == 0 || strcmp(name + len - 4, ".JPG") == 0);
}

static esp_err_t send_thumb_file(const char *uri, usb_console_http_response_t *response)
{
    char path[SERVER_NETWORK_STA_DATAUP_BASE_PATH_MAX + SERVER_NETWORK_STA_DATAUP_FILE_NAME_MAX + 24];
    char header[192];
    char scratch[1024];
    struct stat st = {0};
    const char *name = uri + strlen(SERVER_NETWORK_STA_THUMB_URI_PREFIX);

    if (!UsbConsoleCommon_FileNameIsSafe(name) || !has_jpg_extension(name)) {
        return UsbConsoleCommon_SetJsonf(response,
                                         400,
                                         "Bad Request",
                                         "{\"func\":\"thumb_result\",\"result\":%d,\"message\":\"invalid thumbnail name\"}",
                                         TDX_JSON_RESULT_THUMB_NAME_INVALID);
    }

    snprintf(path, sizeof(path), "%s/jpg_img/%s", USB_CONSOLE_BASE_PATH, name);
    esp_err_t lock_ret = TdxSharedSpi_Lock(portMAX_DELAY);
    if (lock_ret != ESP_OK) {
        return lock_ret;
    }
    if (stat(path, &st) != 0 || st.st_size <= 0) {
        TdxSharedSpi_Unlock();
        return UsbConsoleCommon_SetJsonf(response,
                                         404,
                                         "Not Found",
                                         "{\"func\":\"thumb_result\",\"result\":%d,\"message\":\"thumbnail not found\"}",
                                         TDX_JSON_RESULT_THUMB_NOT_FOUND);
    }

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        TdxSharedSpi_Unlock();
        return UsbConsoleCommon_SetJsonf(response,
                                         404,
                                         "Not Found",
                                         "{\"func\":\"thumb_result\",\"result\":%d,\"message\":\"thumbnail not found\"}",
                                         TDX_JSON_RESULT_THUMB_NOT_FOUND);
    }

    int header_len = snprintf(header,
                              sizeof(header),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: image/jpeg\r\n"
                              "Content-Length: %lu\r\n"
                              "Connection: keep-alive\r\n"
                              "\r\n",
                              (unsigned long)st.st_size);
    if (header_len <= 0 || header_len >= (int)sizeof(header)) {
        fclose(fp);
        TdxSharedSpi_Unlock();
        return ESP_ERR_INVALID_SIZE;
    }

    // Send thumbnail bytes directly so USB saved_images can fetch SD card images like the HTTP side.
    // Chinese note: thumbnail uses direct USB chunks, matching the network side SD-card image fetch behavior.
    esp_err_t write_ret = UsbConsoleTransport_WriteAll(USB_CONSOLE_FRAME_HEAD,
                                                       strlen(USB_CONSOLE_FRAME_HEAD),
                                                       pdMS_TO_TICKS(USB_CONSOLE_WRITE_TIMEOUT_MS));
    if (write_ret != ESP_OK) {
        fclose(fp);
        TdxSharedSpi_Unlock();
        return write_ret;
    }
    write_ret = UsbConsoleTransport_WriteAll(header,
                                             (size_t)header_len,
                                             pdMS_TO_TICKS(USB_CONSOLE_WRITE_TIMEOUT_MS));
    if (write_ret != ESP_OK) {
        fclose(fp);
        TdxSharedSpi_Unlock();
        return write_ret;
    }
    size_t read_len = 0;
    while ((read_len = fread(scratch, 1, sizeof(scratch), fp)) > 0) {
        write_ret = UsbConsoleTransport_WriteAll(scratch,
                                                 read_len,
                                                 pdMS_TO_TICKS(USB_CONSOLE_WRITE_TIMEOUT_MS));
        if (write_ret != ESP_OK) {
            fclose(fp);
            TdxSharedSpi_Unlock();
            return write_ret;
        }
    }
    fclose(fp);
    TdxSharedSpi_Unlock();
    ESP_RETURN_ON_ERROR(UsbConsoleTransport_WriteAll(USB_CONSOLE_FRAME_TAIL,
                                                     strlen(USB_CONSOLE_FRAME_TAIL),
                                                     pdMS_TO_TICKS(USB_CONSOLE_WRITE_TIMEOUT_MS)),
                        TAG, "write thumbnail frame tail failed");
    response->status = 0;
    return ESP_OK;
}

esp_err_t UsbConsoleSavedImages_Handle(const usb_console_http_request_t *request,
                                       usb_console_http_response_t *response)
{
    return UsbConsoleCommon_SubmitAsyncRequest(request, response, "saved_images", UsbConsoleSavedImages_Process);
}

esp_err_t UsbConsoleSavedImages_Process(const usb_console_http_request_t *request,
                                       usb_console_http_response_t *response)
{
    if (request == NULL || response == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strncmp(request->path,
                SERVER_NETWORK_STA_THUMB_URI_PREFIX,
                strlen(SERVER_NETWORK_STA_THUMB_URI_PREFIX)) == 0) {
        return send_thumb_file(request->path, response);
    }

    if (!UsbConsoleCommon_JsonFuncEquals(request->body, "get_saved_images")) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    size_t used = 0;
    esp_err_t ret = UsbConsoleCommon_SetJsonf(response,
                                             200,
                                             "OK",
                                             "{\"func\":\"get_saved_images_result\",\"result\":0,");
    if (ret != ESP_OK) {
        return ret;
    }
    used = strlen(response->body);
    ret = UsbConsoleCommon_ListSavedImages(response->body, sizeof(response->body), &used);
    if (ret == ESP_OK && used + 2 < sizeof(response->body)) {
        response->body[used++] = '}';
        response->body[used] = '\0';
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "get_saved_images failed ret=%s", esp_err_to_name(ret));
        return UsbConsoleCommon_SetJsonf(response,
                                         200,
                                         "OK",
                                         "{\"func\":\"get_saved_images_result\",\"result\":%d,\"message\":\"read saved images failed\"}",
                                         TDX_JSON_RESULT_IMAGES_READ_FAILED);
    }
    return ESP_OK;
}
