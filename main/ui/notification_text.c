/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/notification_text.h"

#include <string.h>

static const uint32_t k_supported_emoji[] = {
#include "ui/fonts/emoji_codepoints.inc"
};

#define ELLIPSIS "\xE2\x80\xA6"
#define ELLIPSIS_BYTES 3U
#define WORD_BOUNDARY_WINDOW 24U

bool notif_text_emoji_supported(uint32_t cp)
{
    size_t low = 0, high = sizeof(k_supported_emoji) / sizeof(k_supported_emoji[0]);
    while (low < high) {
        const size_t mid = low + (high - low) / 2;
        if (k_supported_emoji[mid] == cp) return true;
        if (k_supported_emoji[mid] < cp) low = mid + 1;
        else high = mid;
    }
    return false;
}

/* Strict UTF-8: returns the length of the sequence at s (at most `left` bytes), 0 for an invalid byte. */
static size_t decode(const unsigned char *s, size_t left, uint32_t *cp)
{
    if (left == 0) return 0;
    if (s[0] < 0x80U) { *cp = s[0]; return 1; }
    if (s[0] >= 0xC2U && s[0] <= 0xDFU && left >= 2 && (s[1] & 0xC0U) == 0x80U) {
        *cp = ((uint32_t)(s[0] & 0x1FU) << 6) | (uint32_t)(s[1] & 0x3FU);
        return 2;
    }
    if ((s[0] & 0xF0U) == 0xE0U && left >= 3 && (s[1] & 0xC0U) == 0x80U && (s[2] & 0xC0U) == 0x80U) {
        const uint32_t v = ((uint32_t)(s[0] & 0x0FU) << 12) | ((uint32_t)(s[1] & 0x3FU) << 6) | (uint32_t)(s[2] & 0x3FU);
        if (v < 0x800U || (v >= 0xD800U && v <= 0xDFFFU)) return 0;
        *cp = v;
        return 3;
    }
    if (s[0] >= 0xF0U && s[0] <= 0xF4U && left >= 4 && (s[1] & 0xC0U) == 0x80U && (s[2] & 0xC0U) == 0x80U &&
        (s[3] & 0xC0U) == 0x80U) {
        const uint32_t v = ((uint32_t)(s[0] & 0x07U) << 18) | ((uint32_t)(s[1] & 0x3FU) << 12) |
                           ((uint32_t)(s[2] & 0x3FU) << 6) | (uint32_t)(s[3] & 0x3FU);
        if (v < 0x10000U || v > 0x10FFFFU) return 0;
        *cp = v;
        return 4;
    }
    return 0;
}

static bool is_modifier(uint32_t cp) { return cp >= 0x1F3FBU && cp <= 0x1F3FFU; }
static bool is_regional(uint32_t cp) { return cp >= 0x1F1E6U && cp <= 0x1F1FFU; }
static bool is_tag(uint32_t cp) { return cp >= 0xE0020U && cp <= 0xE007FU; }
static bool is_selector(uint32_t cp) { return cp == 0xFE0EU || cp == 0xFE0FU; }

/* Codepoints that start an emoji or a pictograph (not the plain Latin-1 symbols the text font draws itself). */
static bool is_emoji_candidate(uint32_t cp)
{
    return (cp >= 0x1F000U && cp <= 0x1FAFFU) || (cp >= 0x2600U && cp <= 0x27BFU) || cp == 0x203CU || cp == 0x2049U ||
           cp == 0x2139U || (cp >= 0x231AU && cp <= 0x231BU) || (cp >= 0x23E9U && cp <= 0x23F3U) ||
           (cp >= 0x23F8U && cp <= 0x23FAU) || cp == 0x25B6U || cp == 0x25C0U || (cp >= 0x2B05U && cp <= 0x2B07U) ||
           cp == 0x2B1BU || cp == 0x2B1CU || cp == 0x2B50U || cp == 0x2B55U || cp == 0x21A9U || cp == 0x21AAU ||
           cp == 0x2934U || cp == 0x2935U || cp == 0x3030U || cp == 0x303DU || cp == 0x3297U || cp == 0x3299U;
}

/* Arrows are not part of the emoji font; they were always dropped, only the double arrow became a tilde. */
static bool is_plain_arrow(uint32_t cp) { return cp >= 0x2190U && cp <= 0x21FFU && !notif_text_emoji_supported(cp); }

/* Symbols the text font cannot draw (a missing glyph is an empty box) but notifications use for layout and marks. Box-drawing
 * lines become dashes and bars, so a separator line stays a line; the check mark has no glyph in the emoji font, its heavy twin
 * has; the Greek delta (a difference, "Δ 0.7") becomes a plus-minus sign, which Poppins has. */
static const char *plain_replacement(uint32_t cp)
{
    if (cp == 0x2713U) return "\xE2\x9C\x94";   /* U+2713 -> U+2714 (heavy check mark, in the emoji font) */
    if (cp == 0x0394U) return "\xC2\xB1";        /* U+0394 -> U+00B1 */
    if (cp >= 0x2500U && cp <= 0x257FU) {
        switch (cp) {
        case 0x2500U: case 0x2501U: case 0x2504U: case 0x2505U: case 0x2508U: case 0x2509U: case 0x254CU: case 0x254DU:
        case 0x2550U:
            return "\xE2\x80\x93";              /* an en dash: about the width of one box-drawing cell */
        case 0x2502U: case 0x2503U: case 0x2506U: case 0x2507U: case 0x250AU: case 0x250BU: case 0x254EU: case 0x254FU:
        case 0x2551U:
            return "|";
        default:
            return "+";                          /* corners, junctions and the rest of the box-drawing block */
        }
    }
    return NULL;
}

typedef struct {
    char *dst;
    size_t capacity;      /* includes the terminator */
    size_t length;
    size_t word_cut;      /* output length at the last space (the cut point of a word boundary), 0 if none */
    bool full;
    bool at_line_start;
    bool pending_space;   /* a space is owed before the next visible token (it followed a dropped symbol) */
} writer_t;

/* A token is written whole or not at all; it keeps room for the ellipsis the text may need when it is cut. */
static bool emit(writer_t *w, const char *token, size_t n)
{
    if (w->full) return false;
    if (w->length + n + 1U > w->capacity) { w->full = true; return false; }
    memcpy(w->dst + w->length, token, n);
    w->length += n;
    w->dst[w->length] = '\0';
    return true;
}

static void emit_space(writer_t *w)
{
    if (w->full || w->at_line_start) return;
    if (w->length > 0 && (w->dst[w->length - 1U] == ' ' || w->dst[w->length - 1U] == '\n')) return;
    if (emit(w, " ", 1)) w->word_cut = w->length - 1U;
}

/* Measures the emoji unit that starts at s and returns its length in bytes (0 if s does not start an emoji unit).
 * simple: the unit is one supported base emoji, optionally with a selector or a skin tone (shown as the base glyph).
 * base: that base codepoint. */
static size_t emoji_unit(const unsigned char *s, size_t left, bool *simple, uint32_t *base, bool *had_modifier)
{
    *simple = false;
    *had_modifier = false;
    uint32_t cp = 0;
    size_t len = decode(s, left, &cp);
    if (len == 0) return 0;
    size_t used = len;
    uint32_t next = 0;
    size_t next_len;

    /* keycap: [0-9#*] FE0F? 20E3 */
    if ((cp >= '0' && cp <= '9') || cp == '#' || cp == '*') {
        size_t k = used;
        next_len = decode(s + k, left - k, &next);
        if (next_len && next == 0xFE0FU) { k += next_len; next_len = decode(s + k, left - k, &next); }
        if (next_len && next == 0x20E3U) return k + next_len;
        return 0;
    }
    if (is_regional(cp)) { /* a flag: two regional indicators; a single one is a stray symbol */
        next_len = decode(s + used, left - used, &next);
        if (next_len && is_regional(next)) used += next_len;
        return used;
    }
    if (cp == 0x1F3F4U) { /* black flag, possibly a tag sequence (subdivision flags) */
        size_t k = used;
        bool tags = false;
        for (;;) {
            next_len = decode(s + k, left - k, &next);
            if (!next_len || !is_tag(next)) break;
            tags = true; k += next_len;
        }
        if (tags) return k;
    }
    if (is_modifier(cp) || cp == 0x200DU || is_selector(cp) || is_tag(cp) || cp == 0x20E3U) return len; /* stray: dropped by the caller */
    if (!is_emoji_candidate(cp)) return 0;

    /* a base emoji: selectors, a skin tone and ZWJ joins follow */
    bool joined = false;
    for (;;) {
        next_len = decode(s + used, left - used, &next);
        if (!next_len) break;
        if (is_selector(next)) { used += next_len; continue; }
        if (is_modifier(next)) { *had_modifier = true; used += next_len; continue; }
        if (next == 0x200DU) { /* join with the next emoji */
            uint32_t after = 0;
            const size_t after_len = decode(s + used + next_len, left - used - next_len, &after);
            if (after_len && (is_emoji_candidate(after) || is_regional(after))) { joined = true; used += next_len + after_len; continue; }
            used += next_len; /* a dangling joiner is part of the unit */
            break;
        }
        break;
    }
    *base = cp;
    *simple = !joined && notif_text_emoji_supported(cp);
    return used;
}

size_t notif_text_fit(const char *src, size_t src_length, size_t max_bytes)
{
    if (src == NULL) return 0;
    size_t n = 0;
    while (n < src_length && src[n] != '\0') n++;
    if (n <= max_bytes) return n;
    const unsigned char *s = (const unsigned char *)src;
    size_t i = 0;
    while (i < n) {
        bool simple = false, modified = false;
        uint32_t base = 0, cp = 0;
        size_t step = emoji_unit(s + i, n - i, &simple, &base, &modified);
        if (step == 0) step = decode(s + i, n - i, &cp);
        if (step == 0) step = 1; /* an invalid byte is copied as it is: ingress does not validate */
        if (i + step > max_bytes) break;
        i += step;
    }
    return i;
}

size_t notif_text_copy(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) return 0;
    const size_t len = (src != NULL) ? notif_text_fit(src, strlen(src), dst_size - 1U) : 0;
    if (len > 0) memcpy(dst, src, len);
    dst[len] = '\0';
    return len;
}

notif_text_result_t notif_text_sanitize(const char *src, size_t src_length, char *dst, size_t dst_capacity)
{
    notif_text_result_t result = {0, false, 0, 0};
    if (dst == NULL || dst_capacity == 0) return result;
    dst[0] = '\0';
    if (src == NULL) return result;
    size_t n = 0;
    while (n < src_length && src[n] != '\0') n++;
    const unsigned char *s = (const unsigned char *)src;

    writer_t w = {dst, dst_capacity, 0, 0, false, true, false};
    size_t i = 0;
    while (i < n && !w.full) {
        uint32_t cp = 0;
        const size_t len = decode(s + i, n - i, &cp);
        if (len == 0) { i++; continue; } /* invalid byte */

        const char *replacement = plain_replacement(cp);
        if (replacement != NULL) {
            if (w.pending_space) { emit_space(&w); w.pending_space = false; }
            if (emit(&w, replacement, strlen(replacement))) w.at_line_start = false;
            i += len;
            continue;
        }

        bool simple = false, modified = false;
        uint32_t base = 0;
        const size_t unit = emoji_unit(s + i, n - i, &simple, &base, &modified);
        if (unit > 0) {
            const bool stray = !simple && (is_modifier(cp) || cp == 0x200DU || is_selector(cp) || is_tag(cp) || cp == 0x20E3U);
            if (stray) { i += unit; continue; }
            if (w.pending_space) { emit_space(&w); w.pending_space = false; }
            if (simple) {
                /* the base glyph is the first codepoint of the unit */
                if (emit(&w, (const char *)s + i, len)) { w.at_line_start = false; if (modified) result.simplified++; }
            } else if (is_regional(cp) && unit == len) { /* a lone regional indicator: drop it */
            } else {
                if (emit(&w, NOTIF_TEXT_EMOJI_MARKER, sizeof(NOTIF_TEXT_EMOJI_MARKER) - 1U)) { w.at_line_start = false; result.replaced++; }
            }
            i += unit;
            continue;
        }
        if (is_plain_arrow(cp)) {
            if (cp == 0x2194U) { if (w.pending_space) emit_space(&w); if (emit(&w, "~", 1)) w.at_line_start = false; }
            else w.pending_space = !w.at_line_start;
            i += len;
            continue;
        }
        if (cp == '\r') { i += len; continue; }
        if (cp == '\n') {
            while (w.length > 0 && w.dst[w.length - 1U] == ' ') { w.length--; w.dst[w.length] = '\0'; }
            if (w.word_cut > w.length) w.word_cut = 0;
            if (emit(&w, "\n", 1)) { w.at_line_start = true; w.pending_space = false; w.word_cut = w.length; }
            i += len;
            continue;
        }
        if (cp == ' ' || cp == '\t') {
            if (!w.at_line_start) emit_space(&w);
            i += len;
            continue;
        }
        if (cp < 0x20U || cp == 0x7FU || (cp >= 0x80U && cp < 0xA0U)) { i += len; continue; } /* controls */
        if (w.pending_space) { emit_space(&w); w.pending_space = false; }
        if (emit(&w, (const char *)s + i, len)) w.at_line_start = false;
        i += len;
    }
    result.truncated = w.full || i < n;
    /* trailing spaces and newlines */
    while (w.length > 0 && (w.dst[w.length - 1U] == ' ' || w.dst[w.length - 1U] == '\n')) { w.length--; }
    if (w.word_cut > w.length) w.word_cut = 0;
    dst[w.length] = '\0';

    if (result.truncated) {
        /* end at a recent word boundary when the text is prose, then add the ellipsis when it fits */
        size_t keep = w.length;
        if (w.word_cut > 0 && w.length - w.word_cut <= WORD_BOUNDARY_WINDOW) keep = w.word_cut;
        while (keep > 0 && (dst[keep - 1U] == ' ' || dst[keep - 1U] == '\n')) keep--;
        if (keep + ELLIPSIS_BYTES + 1U > dst_capacity) {
            /* make room by dropping whole trailing tokens (a UTF-8 sequence or the marker) until the ellipsis fits */
            while (keep > 0 && keep + ELLIPSIS_BYTES + 1U > dst_capacity) {
                const size_t marker = sizeof(NOTIF_TEXT_EMOJI_MARKER) - 1U;
                if (keep >= marker && memcmp(dst + keep - marker, NOTIF_TEXT_EMOJI_MARKER, marker) == 0) { keep -= marker; continue; }
                keep--;
                while (keep > 0 && ((unsigned char)dst[keep] & 0xC0U) == 0x80U) keep--; /* back to the start of the UTF-8 sequence */
            }
        }
        if (keep + ELLIPSIS_BYTES + 1U <= dst_capacity) {
            memcpy(dst + keep, ELLIPSIS, ELLIPSIS_BYTES);
            w.length = keep + ELLIPSIS_BYTES;
        } else {
            w.length = keep; /* no room for the ellipsis: the longest valid output */
        }
        dst[w.length] = '\0';
    }
    result.length = w.length;
    return result;
}
