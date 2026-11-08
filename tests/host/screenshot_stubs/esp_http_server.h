/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Host stub for tests/host/test_screenshot.c: just enough of ESP-IDF, HTTP server and LVGL for main/api/api_screenshot.c.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include "esp_err.h"
typedef struct httpd_req { int unused; } httpd_req_t;
typedef void *httpd_handle_t;
esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *field, const char *value);
esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status);
esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type);
esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *str);
esp_err_t httpd_resp_send_chunk(httpd_req_t *req, const char *buf, ssize_t len);
esp_err_t httpd_resp_send(httpd_req_t *req, const char *buf, ssize_t len);
