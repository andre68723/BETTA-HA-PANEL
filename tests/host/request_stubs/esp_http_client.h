/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define ESP_ERR_HTTP_BASE 0x7000
#define HTTP_METHOD_GET 0
typedef void *esp_http_client_handle_t;
typedef struct {
    const char *url, *common_name;
    int method, timeout_ms, buffer_size, buffer_size_tx;
    bool keep_alive_enable;
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *key, const char *value);
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int size);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c);
int esp_http_client_get_status_code(esp_http_client_handle_t c);
int esp_http_client_read(esp_http_client_handle_t c, char *data, int size);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c);
esp_err_t esp_http_client_close(esp_http_client_handle_t c);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c);
