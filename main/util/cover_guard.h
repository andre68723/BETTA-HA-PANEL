/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Checks for the cover/camera image fetcher that decide what is trusted: the image bytes and URL come from
 * Home Assistant attributes, which in turn can point at any web server. Pure functions, host-tested.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* True when `url` lies on the Home Assistant origin `base_url` ("http://host:8123"): it starts with the whole base
 * and continues with nothing, '/', '?' or '#'. Only such URLs may carry the access token, the Host override and the
 * pinned TLS name; "http://host:8123.evil.example/" and "http://host:81234/" are other origins. */
bool cover_url_on_origin(const char *url, const char *base_url);

typedef enum {
    COVER_HTTP_ANONYMOUS,
    COVER_HTTP_SIGNED_URL,
    COVER_HTTP_BEARER,
} cover_http_auth_t;
/* Recognizes HA's camera proxy paths, including absolute URLs; this is not an origin trust check. */
bool cover_url_is_camera_proxy(const char *url);
/* Full URL, modified in place: same-origin camera requests discard query tokens and use Bearer.
 * Other signed URLs and all foreign URLs stay unchanged. A BEARER result requires a nonempty
 * credential and disabled redirects; the caller must not make an anonymous fallback request. */
cover_http_auth_t cover_prepare_image_auth(char *url, const char *base_url);

/* True when a PNG chunk of `chunk_len` data bytes starting at `pos` (its length field) lies fully inside the first
 * `len` bytes: 4 length + 4 type + data + 4 CRC. Written so that a 32-bit size_t cannot wrap: a length of 0xFFFFFFF5
 * must not pass as "1 byte". */
bool cover_png_chunk_fits(size_t pos, size_t len, uint32_t chunk_len);

/* Width x height within `max_pixels` and non-zero. */
bool cover_image_size_ok(uint32_t w, uint32_t h, uint32_t max_pixels);
