/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/notification_emoji.h"

#include <stddef.h>

#include "ui/notification_emoji_data.h"

#define MAX_FONTS 8U /* Medium and Bold at each of the four notification text sizes. */

static const int k_sizes[NOTIFICATION_EMOJI_SIZES] = {20, 24, 28, 32};

int notification_emoji_index(uint32_t codepoint)
{
    unsigned low = 0, high = k_notification_emoji_count;
    while (low < high) {
        const unsigned mid = low + (high - low) / 2U;
        if (k_notification_emoji[mid].codepoint == codepoint) return (int)mid;
        if (k_notification_emoji[mid].codepoint < codepoint) low = mid + 1U;
        else high = mid;
    }
    return -1;
}

int notification_emoji_size_index(int text_px)
{
    int best = 0;
    for (int i = 1; i < NOTIFICATION_EMOJI_SIZES; i++) {
        const int d = text_px > k_sizes[i] ? text_px - k_sizes[i] : k_sizes[i] - text_px;
        const int b = text_px > k_sizes[best] ? text_px - k_sizes[best] : k_sizes[best] - text_px;
        if (d < b) best = i;
    }
    return best;
}

#if LV_USE_IMGFONT
typedef struct {
    const lv_font_t *base;
    lv_font_t *font;
    int size_index;
} emoji_font_t;

static emoji_font_t s_fonts[MAX_FONTS];

static const void *emoji_path_cb(const lv_font_t *font, uint32_t unicode, uint32_t unicode_next, int32_t *offset_y, void *user_data)
{
    (void)font;
    (void)unicode_next;
    const emoji_font_t *entry = user_data;
    const int index = notification_emoji_index(unicode);
    if (index < 0) return NULL; /* not a color motif: the wrapped font draws it */
    *offset_y = 0;
    return &k_notification_emoji[index].image[entry->size_index];
}

const lv_font_t *notification_emoji_font(const lv_font_t *base, int text_px)
{
    if (base == NULL) return NULL;
    unsigned free_slot = MAX_FONTS;
    for (unsigned i = 0; i < MAX_FONTS; i++) {
        if (s_fonts[i].font != NULL && s_fonts[i].base == base) return s_fonts[i].font;
        if (s_fonts[i].font == NULL && free_slot == MAX_FONTS) free_slot = i;
    }
    if (free_slot == MAX_FONTS) return base;
    emoji_font_t *entry = &s_fonts[free_slot];
    entry->base = base;
    entry->size_index = notification_emoji_size_index(text_px);
    entry->font = lv_imgfont_create((uint16_t)base->line_height, emoji_path_cb, entry);
    if (entry->font == NULL) return base;
    entry->font->base_line = base->base_line;
    entry->font->fallback = base;
    return entry->font;
}
#else
const lv_font_t *notification_emoji_font(const lv_font_t *base, int text_px)
{
    (void)text_px;
    return base;
}
#endif
