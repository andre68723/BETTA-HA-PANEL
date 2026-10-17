/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * GET /api/log: the most recent log lines of the device as plain text.
 */
#include "api/api_routes.h"

#include "esp_http_server.h"
#include "util/log_buffer.h"

esp_err_t api_log_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char chunk[512];
    size_t offset = 0;
    for (;;) {
        size_t n = log_buffer_read(offset, chunk, sizeof(chunk));
        if (n == 0) {
            break;
        }
        if (httpd_resp_send_chunk(req, chunk, (ssize_t)n) != ESP_OK) {
            return ESP_FAIL;
        }
        offset += n;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}
