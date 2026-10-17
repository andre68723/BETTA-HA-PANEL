/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "util/sha256_soft.h"
#define OTA_APP_DESC_OFFSET (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t))
#define OTA_HEADER_CHECK_LEN (OTA_APP_DESC_OFFSET + sizeof(esp_app_desc_t))
typedef struct {
    const esp_partition_t *partition;
    esp_ota_handle_t handle;
    bool begun;
    bool header_checked;
    uint8_t header_buf[OTA_HEADER_CHECK_LEN];
    size_t header_len;
    size_t written;
    size_t total;
    char version[32];
    char project_name[32];
    char error[160];
    bool verify;                /* compare the SHA-256 of every byte with expected_sha256 before switching */
    char expected_sha256[65];
    sha256_soft_t sha;          /* software SHA-256 of every byte: the chip's SHA engine returned wrong digests for this long stream (2026-10-02) */
} api_ota_stream_t;

void ota_stream_set_error(api_ota_stream_t *stream, const char *message);
esp_err_t ota_stream_finish(api_ota_stream_t *stream);
void ota_stream_abort(api_ota_stream_t *stream);
/* Bounded idle and total receive times; caller owns the returned body. */
char *ota_read_request_body(httpd_req_t *req, size_t max_len);
