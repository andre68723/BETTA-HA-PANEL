/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * GET /api/coredump?id=<sha256>: one raw panic dump from the two-report archive.
 * GET without an ID selects the newest. It needs the matching firmware .elf and the
 * esp-coredump tool to turn into a backtrace, see docs/coredump.md.
 * POST /api/coredump/erase?id=<sha256>: deletes that report; no ID selects the newest.
 *
 */
#include "esp_http_server.h"

#include "api/api_routes.h"

#include <stdio.h>
#include <string.h>

#include "util/coredump_archive.h"

/* IDs select immutable reports, never list positions. Reject malformed selectors rather than
 * accidentally downloading/deleting the newest report when a stale link no longer exists. */
static bool request_id(httpd_req_t *req, char id[65])
{
    id[0] = '\0';
    size_t n = httpd_req_get_url_query_len(req);
    if (!n) return true;
    char query[80];
    return n < sizeof(query) && httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
           httpd_query_key_value(query, "id", id, 65) == ESP_OK && coredump_store_id_valid(id);
}

esp_err_t api_coredump_get_handler(httpd_req_t *req)
{
    char id[65];
    if (!request_id(req, id)) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid crash report ID");
    coredump_record_t record;
    if (!coredump_archive_get(id, &record)) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Crash report not found");
    httpd_resp_set_type(req, "application/octet-stream");
    char disposition[112];
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"coredump-%.64s.bin\"", record.id);
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    uint8_t chunk[1024];
    for (size_t offset = 0; offset < record.size;) {
        size_t n = record.size - offset < sizeof(chunk) ? record.size - offset : sizeof(chunk);
        if (!coredump_archive_read(record.id, offset, chunk, n) ||
            httpd_resp_send_chunk(req, (const char *)chunk, (ssize_t)n) != ESP_OK) return ESP_FAIL;
        offset += n;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

esp_err_t api_coredump_erase_post_handler(httpd_req_t *req)
{
    char id[65];
    if (!request_id(req, id)) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid crash report ID");
    esp_err_t err = coredump_archive_delete(id);
    if (err == ESP_ERR_NOT_FOUND) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Crash report not found");
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}
