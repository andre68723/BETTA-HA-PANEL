/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Text of a notification (title, message, action label) made safe for the panel's labels: pure C, host-tested.
 *
 * Emoji: the panel renders monochrome Noto Emoji outlines for the codepoints of ui/fonts/emoji_codepoints.txt. A standalone supported
 * emoji stays (a text or emoji presentation selector is dropped); a supported emoji with a skin-tone modifier is shown as its base glyph;
 * everything the panel cannot shape - joined sequences (ZWJ), flags, keycaps, tag sequences and unsupported emoji - becomes the short
 * marker "[Emoji]" once per sequence, so no meaning is lost silently. Stray modifiers, joiners and selectors are dropped.
 *
 * Symbols outside the text font that notifications use for layout are replaced by what it can draw: box-drawing lines by dashes and
 * bars ("━━━" becomes a line of en dashes), the check mark U+2713 by the heavy check mark U+2714 and the Greek delta by a
 * plus-minus sign.
 *
 * Output rules: a CR is dropped, a tab is a space, other control characters are dropped, a newline stays (spaces before it and at the start
 * of a line are dropped), invalid UTF-8 bytes are skipped, trailing spaces and newlines are trimmed. The result is always NUL-terminated
 * (when the capacity is at least 1), never splits a UTF-8 sequence, a marker or an emoji, and fits the capacity. When the text does not
 * fit, it ends at a recent word boundary (within 24 bytes) or otherwise at a complete token, followed by an ellipsis (U+2026, 3 bytes) when
 * there is room for it. Source and destination must not overlap.
 *
 * Ingress: notif_text_fit()/notif_text_copy() bound the raw text of a field by its capacity before any of the above. They cut before the
 * whole codepoint or emoji unit (base, selector, skin tone, ZWJ join, flag, keycap, tag run) that does not fit, so a later renderer never
 * sees half a sequence, and they change nothing else: no selector is dropped and no marker is added.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NOTIF_TEXT_EMOJI_MARKER "[Emoji]"

typedef struct {
    size_t length;           /* bytes written, without the terminator */
    bool truncated;          /* the text did not fit */
    unsigned replaced;       /* sequences and unsupported emoji replaced by the marker */
    unsigned simplified;     /* supported emoji shown as their base glyph (a skin tone dropped) */
} notif_text_result_t;

/* True for a codepoint of the panel's emoji font (ui/fonts/emoji_codepoints.txt). */
bool notif_text_emoji_supported(uint32_t codepoint);

/* Bytes of src (at most src_length, a NUL ends the text earlier) to keep so that at most max_bytes remain and no codepoint or emoji unit is
 * split. Returns the whole length when it fits. */
size_t notif_text_fit(const char *src, size_t src_length, size_t max_bytes);

/* Copies src into dst (dst_size counts the terminator) cut by notif_text_fit(); returns the bytes copied. A NULL src gives an empty string. */
size_t notif_text_copy(char *dst, size_t dst_size, const char *src);

/* src_length bytes of src (a NUL ends the text earlier); dst_capacity counts the terminator. */
notif_text_result_t notif_text_sanitize(const char *src, size_t src_length, char *dst, size_t dst_capacity);
