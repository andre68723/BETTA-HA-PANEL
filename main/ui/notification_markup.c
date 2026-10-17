/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#include "ui/notification_markup.h"

#include <string.h>

#include "ui/notification_text.h"

static bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static bool word(unsigned char c)
{
    return c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static bool escaped(const char *src, size_t pos)
{
    size_t n = 0;
    while (pos && src[--pos] == '\\') n++;
    return (n & 1U) != 0;
}

static bool marker(const char *src, size_t length, size_t pos, char c)
{
    return pos + 1 < length && src[pos] == c && src[pos + 1] == c &&
           (!pos || src[pos - 1] != c) && (pos + 2 == length || src[pos + 2] != c) &&
           !escaped(src, pos);
}

static size_t closing(const char *src, size_t length, size_t open)
{
    const char c = src[open];
    if (open + 2 >= length || space(src[open + 2]) ||
        (c == '_' && open && word((unsigned char)src[open - 1]))) return length;
    for (size_t i = open + 3; i + 1 < length; i++) {
        if (src[i] == '\n' && src[i + 1] == '\n') break; /* paragraph boundary */
        if (marker(src, length, i, c) && !space(src[i - 1]) &&
            (c != '_' || i + 2 == length || !word((unsigned char)src[i + 2]))) return i;
    }
    return length;
}

static bool append(notif_markup_result_t *out, char *dst, size_t capacity,
                   notif_markup_run_t *runs, size_t run_capacity,
                   const char *text, size_t length, bool bold)
{
    if (!length) return true;
    const size_t n = notif_text_fit(text, length, capacity - out->length - 1);
    if (!n) return true;
    notif_markup_run_t *run = out->count ? &runs[out->count - 1] : NULL;
    if (!run || run->bold != bold) {
        if (out->count == run_capacity) return false;
        run = &runs[out->count++];
        *run = (notif_markup_run_t){ .offset = out->length, .bold = bold };
    }
    memcpy(dst + out->length, text, n);
    out->length += n;
    run->length += n;
    out->bold |= bold;
    dst[out->length] = '\0';
    return true;
}

notif_markup_result_t notif_markup_parse(const char *src, char *dst, size_t capacity,
                                        notif_markup_run_t *runs, size_t run_capacity)
{
    notif_markup_result_t out = {0};
    if (!dst || !capacity) return out;
    if (capacity > UINT16_MAX) capacity = UINT16_MAX;
    dst[0] = '\0';
    if (!src || !runs || !run_capacity) {
        if (src) out.length = notif_text_copy(dst, capacity, src);
        return out;
    }
    const size_t length = strlen(src);
    bool code = false;
    for (size_t i = 0; i < length && out.length < capacity - 1;) {
        size_t n = 1, start = i;
        bool bold = false;
        if (src[i] == '\\' && i + 1 < length && strchr("*_\\`", src[i + 1])) {
            start = ++i; /* escaped punctuation remains literal */
        } else if (src[i] == '`') {
            code = !code;
        } else if (!code && (src[i] == '*' || src[i] == '_') && marker(src, length, i, src[i])) {
            const size_t end = closing(src, length, i);
            if (end < length) {
                start = i + 2;
                n = end - start;
                i = end + 2;
                bold = true;
            }
        }
        if (!bold) {
            /* Copy a complete UTF-8 codepoint, not one byte of a multibyte
             * character: notif_text_fit() rejects incomplete units. */
            const unsigned char first = (unsigned char)src[start];
            n = first >= 0xf0 ? 4 : first >= 0xe0 ? 3 : first >= 0xc0 ? 2 : 1;
            if (n > length - start) n = length - start;
            i = start + n;
        }
        const bool truncated = notif_text_fit(src + start, n, capacity - out.length - 1) < n;
        if (!append(&out, dst, capacity, runs, run_capacity, src + start, n, bold)) {
            out = (notif_markup_result_t){ .length = notif_text_copy(dst, capacity, src) };
            if (out.length) { out.count = 1; runs[0] = (notif_markup_run_t){ .length = out.length }; }
            return out;
        }
        if (truncated) break;
    }
    return out;
}
