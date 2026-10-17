/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/cover_guard.h"

#include <string.h>

bool cover_url_on_origin(const char *url, const char *base_url)
{
    if (url == NULL || base_url == NULL || base_url[0] == '\0') {
        return false;
    }
    const size_t n = strlen(base_url);
    if (strncmp(url, base_url, n) != 0) {
        return false;
    }
    const char next = url[n];
    return next == '\0' || next == '/' || next == '?' || next == '#';
}

bool cover_png_chunk_fits(size_t pos, size_t len, uint32_t chunk_len)
{
    if (pos > len || len - pos < 12U) {
        return false;
    }
    return (size_t)chunk_len <= len - pos - 12U;
}

bool cover_image_size_ok(uint32_t w, uint32_t h, uint32_t max_pixels)
{
    if (w == 0U || h == 0U) {
        return false;
    }
    return (uint64_t)w * h <= max_pixels;
}
