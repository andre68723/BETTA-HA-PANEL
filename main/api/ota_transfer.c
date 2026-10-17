/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "api/ota_transfer.h"
#include "util/flash_gate.h"
#include "util/transfer_deadline.h"
#include "net/update_check_parse.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define OTA_FLASH_GATE_WAIT_MS 15000U
#define TAG_API_OTA "api_ota"

void ota_stream_set_error(api_ota_stream_t *stream, const char *message)
{
    if (stream == NULL) {
        return;
    }
    strlcpy(stream->error, message != NULL ? message : "OTA stream failed", sizeof(stream->error));
}

esp_err_t ota_stream_finish(api_ota_stream_t *stream)
{
    if (stream == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!stream->header_checked) {
        ota_stream_set_error(stream, "OTA image is too small or incomplete");
        return ESP_ERR_INVALID_SIZE;
    }
    if (stream->total > 0 && stream->written != stream->total) {
        ota_stream_set_error(stream, "OTA transfer ended before all bytes were received");
        return ESP_ERR_INVALID_SIZE;
    }

    /* esp_ota_end() remaps flash pages to verify the image; a background flash write in that window can reset the chip. */
    const bool gated = flash_gate_enter(OTA_FLASH_GATE_WAIT_MS);
    if (!gated) {
        ota_stream_set_error(stream, "Flash busy: OTA validation deferred");
        return ESP_ERR_TIMEOUT; /* Keep begun set so the caller aborts the handle. */
    }
    esp_err_t err = esp_ota_end(stream->handle);
    flash_gate_leave();
    stream->begun = false;
    if (err != ESP_OK) {
        ota_stream_set_error(stream, "OTA image validation failed");
        return err;
    }

    if (stream->verify) {
        uint8_t digest[32];
        stream->verify = false;
        sha256_soft_final(&stream->sha, digest);
        if (!update_check_sha256_matches(stream->expected_sha256, digest)) {
            char got[65] = {0};
            for (size_t i = 0; i < sizeof(digest); i++) {
                snprintf(got + 2 * i, sizeof(got) - 2 * i, "%02x", digest[i]);
            }
            ESP_LOGE(TAG_API_OTA, "SHA-256 mismatch: expected %s, computed %s (%u of %u bytes)", stream->expected_sha256, got,
                (unsigned)stream->written, (unsigned)stream->total);
            ota_stream_set_error(stream, "The downloaded image does not match the SHA-256 GitHub published; nothing was installed");
            return ESP_ERR_INVALID_CRC;
        }
    }

    err = esp_ota_set_boot_partition(stream->partition);
    if (err != ESP_OK) {
        ota_stream_set_error(stream, "Failed to select OTA boot partition");
        return err;
    }
    return ESP_OK;
}

void ota_stream_abort(api_ota_stream_t *stream)
{
    if (stream != NULL && stream->verify) {
        stream->verify = false;
    }
    if (stream != NULL && stream->begun) {
        (void)esp_ota_abort(stream->handle);
        stream->begun = false;
    }
}

char *ota_read_request_body(httpd_req_t *req, size_t max_len)
{
    if (req == NULL || req->content_len <= 0 || (size_t)req->content_len > max_len) {
        return NULL;
    }
    char *buf = calloc((size_t)req->content_len + 1U, sizeof(char));
    if (buf == NULL) {
        return NULL;
    }

    transfer_deadline_t deadline;
    transfer_deadline_init(&deadline, esp_timer_get_time() / 1000, 30000, 180000);
    int received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, buf + received, req->content_len - received);
        if (transfer_deadline_expired(&deadline, esp_timer_get_time() / 1000)) r = 0;
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            free(buf);
            return NULL;
        }
        received += r;
        transfer_deadline_progress(&deadline, esp_timer_get_time() / 1000);
    }
    buf[received] = '\0';
    return buf;
}
