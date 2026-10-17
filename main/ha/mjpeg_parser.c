/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#include "ha/mjpeg_parser.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = '\0';
    return s;
}

bool mjpeg_camera_path(const char *id, char *out, size_t capacity)
{
    if (!id || strncmp(id, "camera.", 7) || !id[7] || strlen(id) >= 96 || !out) return false;
    for (const char *s = id + 7; *s; s++) {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_')) return false;
    }
    int n = snprintf(out, capacity, "/api/camera_proxy_stream/%s", id);
    return n > 0 && (size_t)n < capacity;
}

bool mjpeg_parser_init(mjpeg_parser_t *p, const char *type, uint8_t *frame, size_t capacity)
{
    if (!p) return false;
    memset(p, 0, sizeof(*p));
    p->failed = true;
    if (!type || !frame || !capacity || strlen(type) > MJPEG_HEADER_MAX) return false;
    const char *end = strchr(type, ';');
    if (!end || (size_t)(end - type) != strlen("multipart/x-mixed-replace") ||
        strncasecmp(type, "multipart/x-mixed-replace", (size_t)(end - type))) return false;
    /* Parameter names are case insensitive; quoted boundaries may contain '='. */
    const char *s = end + 1;
    while (*s) {
        while (*s == ' ' || *s == '\t' || *s == ';') s++;
        const char *key = s;
        while (*s && *s != '=' && *s != ';') s++;
        if (*s != '=') return false;
        const char *key_end = s++;
        while (key_end > key && isspace((unsigned char)key_end[-1])) key_end--;
        while (*s == ' ' || *s == '\t') s++;
        bool quoted = *s == '"';
        if (quoted) s++;
        const char *value = s;
        while (*s && (quoted ? *s != '"' : *s != ';' && *s != ' ' && *s != '\t')) s++;
        size_t n = (size_t)(s - value);
        if (quoted && *s++ != '"') return false;
        if ((size_t)(key_end - key) == 8 && !strncasecmp(key, "boundary", 8)) {
            if (!n || n > MJPEG_BOUNDARY_MAX) return false;
            for (size_t i = 0; i < n; i++) if ((unsigned char)value[i] < 33 || value[i] == 127) return false;
            memcpy(p->boundary, "--", 2);
            memcpy(p->boundary + 2, value, n);
            p->boundary[n + 2] = '\0';
            p->frame = frame;
            p->capacity = capacity < MJPEG_FRAME_MAX ? capacity : MJPEG_FRAME_MAX;
            p->failed = false;
            return true;
        }
        while (*s == ' ' || *s == '\t') s++;
        if (*s && *s != ';') return false;
    }
    return false;
}

static bool line_done(mjpeg_parser_t *p)
{
    char *line = trim(p->line);
    if (!p->state) {
        size_t n = strlen(p->boundary);
        /* HA's still-to-MJPEG endpoint includes the leading '--' in its
         * boundary parameter as well as in its delimiter. Accept both forms. */
        const char *boundary = !strncmp(p->boundary, "----", 4) &&
            !strncmp(line, p->boundary + 2, n - 2) ? p->boundary + 2 : p->boundary;
        n = strlen(boundary);
        if (!strcmp(line, boundary)) {
            p->state = 1;
            p->jpeg = p->have_length = false;
            p->length = p->received = p->header_bytes = 0;
        } else if (!strncmp(line, boundary, n) && !strcmp(line + n, "--")) {
            p->ended = true;
        } else if (*line) return false;
        return true;
    }
    if (!*line) {
        if (!p->jpeg || !p->have_length || p->length < 4) return false;
        p->state = 2;
        return true;
    }
    char *colon = strchr(line, ':');
    if (!colon) return false;
    *colon = '\0';
    char *value = trim(colon + 1);
    if (!strcasecmp(trim(line), "Content-Type")) {
        if (p->jpeg || strcasecmp(value, "image/jpeg")) return false;
        p->jpeg = true;
    } else if (!strcasecmp(trim(line), "Content-Length")) {
        if (p->have_length || !*value) return false;
        size_t n = 0;
        for (const char *s = value; *s; s++) {
            if (*s < '0' || *s > '9' || n > p->capacity / 10) return false;
            n = n * 10 + (size_t)(*s - '0');
            if (n > p->capacity) return false;
        }
        p->length = n;
        p->have_length = true;
    }
    return true;
}

bool mjpeg_parser_feed(mjpeg_parser_t *p, const uint8_t *data, size_t length,
                       mjpeg_frame_cb_t completed, void *user)
{
    if (!p || p->failed || (!data && length) || !completed) return false;
    while (length && !p->ended) {
        if (p->state == 2) {
            size_t n = p->length - p->received;
            if (n > length) n = length;
            memcpy(p->frame + p->received, data, n);
            data += n; length -= n; p->received += n;
            if (p->received == p->length) {
                if (p->frame[0] != 0xff || p->frame[1] != 0xd8 ||
                    p->frame[p->length - 2] != 0xff || p->frame[p->length - 1] != 0xd9 ||
                    !completed(user, p->frame, p->length)) goto fail;
                p->state = 0;
                p->header_bytes = 0;
            }
        } else {
            uint8_t c = *data++; length--;
            if (++p->header_bytes > MJPEG_HEADER_MAX || !c) goto fail;
            if (c == '\n') {
                p->line[p->line_length] = '\0';
                if (!line_done(p)) goto fail;
                p->line_length = 0;
            } else {
                if (p->line_length == MJPEG_HEADER_MAX) goto fail;
                p->line[p->line_length++] = (char)c;
            }
        }
    }
    return true;
fail:
    p->failed = true;
    return false;
}
