/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/cover_guard.h"

#include <string.h>

static const char *url_path(const char *url)
{
    if (url == NULL) return "";
    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
        const char *host = strstr(url, "://") + 3;
        const char *path = host + strcspn(host, "/?#");
        return *path == '/' ? path : "";
    }
    return url;
}

bool cover_url_is_camera_proxy(const char *url)
{
    const char *path = url_path(url);
    return strncmp(path, "/api/camera_proxy/camera.", 25) == 0 ||
           strncmp(path, "/api/camera_proxy_stream/camera.", 32) == 0;
}

static bool query_token(const char *part, size_t len)
{
    return len >= 5 && memcmp(part, "token", 5) == 0 && (len == 5 || part[5] == '=');
}

cover_http_auth_t cover_prepare_image_auth(char *url, const char *base_url)
{
    if (!cover_url_on_origin(url, base_url)) return COVER_HTTP_ANONYMOUS;
    char *query = strchr(url, '?');
    char *fragment = strchr(url, '#');
    if (!query || (fragment && fragment < query)) return COVER_HTTP_BEARER;
    const bool camera = cover_url_is_camera_proxy(url);
    char *read = query + 1, *write = query;
    bool have_param = false, signed_url = false;
    while (*read && *read != '#') {
        size_t len = strcspn(read, "&#");
        bool token = query_token(read, len);
        signed_url |= token;
        if (camera && len && !token) {
            *write++ = have_param ? '&' : '?';
            memmove(write, read, len);
            write += len;
            have_param = true;
        }
        read += len;
        if (*read == '&') read++;
    }
    if (camera) {
        memmove(write, read, strlen(read) + 1); /* preserve any fragment */
        return COVER_HTTP_BEARER;
    }
    return signed_url ? COVER_HTTP_SIGNED_URL : COVER_HTTP_BEARER;
}

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
