#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t receive_data_redirect_handler(httpd_req_t *req);
esp_err_t server_network_sta_net_data_register_handlers(httpd_handle_t server, const char *base_path);

#ifdef __cplusplus
}
#endif
