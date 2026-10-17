/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#include "ui/notification_message.h"

#include <string.h>

#include "app_config.h"
#include "ui/notification_emoji.h"
#include "ui/notification_markup.h"
#include "ui/text_font.h"

static void clear_spans(lv_obj_t *obj)
{
    if (!obj) return;
    while (lv_spangroup_get_span_count(obj)) lv_spangroup_delete_span(obj, lv_spangroup_get_child(obj, 0));
}

void notification_message_set(lv_obj_t *label, lv_obj_t **spans, const char *text)
{
    char plain[APP_NOTIFICATION_MAX_MESSAGE_LEN];
    notif_markup_run_t runs[NOTIF_MARKUP_MAX_RUNS];
    const notif_markup_result_t parsed = notif_markup_parse(text, plain, sizeof(plain), runs, NOTIF_MARKUP_MAX_RUNS);
    lv_label_set_text(label, plain);
    const lv_text_align_t align = strchr(plain, '\n') ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER;
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
    clear_spans(*spans);
    lv_obj_set_hidden(label, false);
    if (*spans) lv_obj_set_hidden(*spans, true);
    if (!parsed.bold) return;

    if (!*spans) {
        *spans = lv_spangroup_create(lv_obj_get_parent(label));
        if (!*spans) return;
        lv_obj_set_hidden(*spans, true);
        lv_obj_remove_style_all(*spans);
        lv_obj_set_width(*spans, LV_PCT(100));
        lv_obj_set_height(*spans, LV_SIZE_CONTENT);
        lv_obj_set_scrollable(*spans, false);
        lv_obj_set_clickable(*spans, false); /* the card still dismisses on tap */
        lv_obj_move_to_index(*spans, lv_obj_get_index(label));
    }
    const lv_font_t *normal_emoji = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    const lv_font_t *base = normal_emoji;
    while (!ui_font_text_px_of(base) && base->fallback) base = base->fallback;
    const lv_font_t *bold = ui_font_emphasis(base);
    const lv_font_t *bold_emoji = notification_emoji_font(bold, ui_font_text_px_of(bold));
    lv_obj_set_style_text_font(*spans, normal_emoji, LV_PART_MAIN);
    lv_obj_set_style_text_color(*spans, lv_obj_get_style_text_color(label, LV_PART_MAIN), LV_PART_MAIN);
    lv_obj_set_style_text_align(*spans, align, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(*spans, lv_obj_get_style_text_line_space(label, LV_PART_MAIN), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(*spans, lv_obj_get_style_text_letter_space(label, LV_PART_MAIN), LV_PART_MAIN);
    for (size_t i = 0; i < parsed.count; i++) {
        lv_span_t *span = lv_spangroup_add_span(*spans);
        if (!span) { clear_spans(*spans); return; }
        const size_t end = runs[i].offset + runs[i].length;
        const char saved = plain[end];
        plain[end] = '\0';
        lv_span_set_text(span, plain + runs[i].offset);
        plain[end] = saved;
        lv_style_set_text_font(lv_span_get_style(span), runs[i].bold ? bold_emoji : normal_emoji);
    }
    lv_spangroup_refresh(*spans);
    lv_obj_set_hidden(label, true);
    lv_obj_set_hidden(*spans, false);
}
