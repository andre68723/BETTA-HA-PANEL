/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/ui_widget_factory.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_i18n.h"
#include "ui/ui_bindings.h"
#include "ui/ui_memory.h"

typedef enum {
    W_SLIDER_DIR_AUTO = 0,
    W_SLIDER_DIR_LEFT_TO_RIGHT,
    W_SLIDER_DIR_RIGHT_TO_LEFT,
    W_SLIDER_DIR_BOTTOM_TO_TOP,
    W_SLIDER_DIR_TOP_TO_BOTTOM,
} w_slider_direction_t;

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    lv_obj_t *card;
    lv_obj_t *title_label;
    lv_obj_t *state_label;
    lv_obj_t *value_label;
    lv_obj_t *slider;
    w_slider_direction_t direction_cfg;
    w_slider_direction_t direction_effective;
    lv_color_t accent_color;
    int value;
    bool is_on;
    bool unavailable;
    bool dragging;
    bool suppress_event;
    bool is_cover;            /* cover.* entity: the state is Open / Closed / Opening / Closing */
    char cover_state[16];
    int last_sent_value;
} w_slider_ctx_t;

static const uint32_t W_SLIDER_FILL_OFF_HEX = 0x8C98A4;
static const uint32_t W_SLIDER_TRACK_HEX = 0x3A3E43;

static int clamp_percent(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return value;
}

static bool slider_state_is_unavailable(const char *state)
{
    if (state == NULL) {
        return false;
    }
    return strcmp(state, "unavailable") == 0 || strcmp(state, "unknown") == 0;
}

static bool slider_state_is_on_text(const char *state)
{
    if (state == NULL) {
        return false;
    }
    return strcmp(state, "on") == 0 || strcmp(state, "open") == 0 || strcmp(state, "playing") == 0 ||
           strcmp(state, "home") == 0;
}

static bool slider_parse_percent_text(const char *text, int *out_value)
{
    if (text == NULL || out_value == NULL || text[0] == '\0') {
        return false;
    }

    char *end = NULL;
    double parsed = strtod(text, &end);
    if (end == text) {
        return false;
    }

    while (*end != '\0' && isspace((unsigned char)*end)) {
        end++;
    }
    if (*end == '%') {
        end++;
    }
    while (*end != '\0' && isspace((unsigned char)*end)) {
        end++;
    }
    if (*end != '\0') {
        return false;
    }

    int value = (int)(parsed + (parsed >= 0.0 ? 0.5 : -0.5));
    *out_value = clamp_percent(value);
    return true;
}

static int slider_extract_percent_value(const ha_state_t *state, bool *out_has_numeric)
{
    if (out_has_numeric != NULL) {
        *out_has_numeric = false;
    }
    if (state == NULL) {
        return 0;
    }

    int value = 0;
    if (slider_parse_percent_text(state->state, &value)) {
        if (out_has_numeric != NULL) {
            *out_has_numeric = true;
        }
        return value;
    }

    cJSON *attrs = cJSON_Parse(state->attributes_json);
    if (attrs != NULL) {
        cJSON *brightness_pct = cJSON_GetObjectItemCaseSensitive(attrs, "brightness_pct");
        cJSON *brightness = cJSON_GetObjectItemCaseSensitive(attrs, "brightness");
        cJSON *position = cJSON_GetObjectItemCaseSensitive(attrs, "current_position"); /* covers */
        cJSON *volume = cJSON_GetObjectItemCaseSensitive(attrs, "volume_level");       /* media players, 0..1 */
        if (cJSON_IsNumber(position)) {
            value = clamp_percent((int)(position->valuedouble + 0.5));
            if (out_has_numeric != NULL) {
                *out_has_numeric = true;
            }
        } else if (cJSON_IsNumber(volume)) {
            value = clamp_percent((int)(volume->valuedouble * 100.0 + 0.5));
            if (out_has_numeric != NULL) {
                *out_has_numeric = true;
            }
        } else if (cJSON_IsNumber(brightness_pct)) {
            value = clamp_percent((int)(brightness_pct->valuedouble + 0.5));
            if (out_has_numeric != NULL) {
                *out_has_numeric = true;
            }
        } else if (cJSON_IsNumber(brightness)) {
            int raw_255 = (int)(brightness->valuedouble + 0.5);
            if (raw_255 < 0) {
                raw_255 = 0;
            }
            if (raw_255 > 255) {
                raw_255 = 255;
            }
            value = (raw_255 * 100 + 127) / 255;
            if (out_has_numeric != NULL) {
                *out_has_numeric = true;
            }
        }
        cJSON_Delete(attrs);
    }

    if (out_has_numeric != NULL && *out_has_numeric) {
        return clamp_percent(value);
    }
    return slider_state_is_on_text(state->state) ? 100 : 0;
}

static bool slider_is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int slider_hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

static bool slider_parse_hex_color(const char *text, lv_color_t *out)
{
    if (text == NULL || out == NULL || text[0] == '\0') {
        return false;
    }

    const char *p = text;
    if (p[0] == '#') {
        p++;
    } else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }

    if (strlen(p) != 6) {
        return false;
    }
    for (size_t i = 0; i < 6; i++) {
        if (!slider_is_hex_digit(p[i])) {
            return false;
        }
    }

    int r_hi = slider_hex_nibble(p[0]);
    int r_lo = slider_hex_nibble(p[1]);
    int g_hi = slider_hex_nibble(p[2]);
    int g_lo = slider_hex_nibble(p[3]);
    int b_hi = slider_hex_nibble(p[4]);
    int b_lo = slider_hex_nibble(p[5]);
    if (r_hi < 0 || r_lo < 0 || g_hi < 0 || g_lo < 0 || b_hi < 0 || b_lo < 0) {
        return false;
    }

    uint32_t rgb = (uint32_t)(((r_hi << 4) | r_lo) << 16) | (uint32_t)(((g_hi << 4) | g_lo) << 8) |
                   (uint32_t)((b_hi << 4) | b_lo);
    *out = lv_color_hex(rgb);
    return true;
}

static const char *slider_translate_status_text(const char *status_text)
{
    if (status_text == NULL || status_text[0] == '\0') {
        return ui_i18n_get("common.off", "OFF");
    }
    if (strcmp(status_text, "ON") == 0 || strcmp(status_text, "on") == 0) {
        return ui_i18n_get("common.on", "ON");
    }
    if (strcmp(status_text, "OFF") == 0 || strcmp(status_text, "off") == 0) {
        return ui_i18n_get("common.off", "OFF");
    }
    if (strcmp(status_text, "unavailable") == 0) {
        return ui_i18n_get("common.unavailable", "unavailable");
    }
    return status_text;
}

static w_slider_direction_t slider_direction_from_text(const char *direction)
{
    if (direction == NULL || direction[0] == '\0' || strcmp(direction, "auto") == 0) {
        return W_SLIDER_DIR_AUTO;
    }
    if (strcmp(direction, "left_to_right") == 0) {
        return W_SLIDER_DIR_LEFT_TO_RIGHT;
    }
    if (strcmp(direction, "right_to_left") == 0) {
        return W_SLIDER_DIR_RIGHT_TO_LEFT;
    }
    if (strcmp(direction, "bottom_to_top") == 0) {
        return W_SLIDER_DIR_BOTTOM_TO_TOP;
    }
    if (strcmp(direction, "top_to_bottom") == 0) {
        return W_SLIDER_DIR_TOP_TO_BOTTOM;
    }
    return W_SLIDER_DIR_AUTO;
}

static bool slider_direction_is_vertical(w_slider_direction_t direction)
{
    return direction == W_SLIDER_DIR_BOTTOM_TO_TOP || direction == W_SLIDER_DIR_TOP_TO_BOTTOM;
}

static bool slider_direction_is_reversed(w_slider_direction_t direction)
{
    return direction == W_SLIDER_DIR_RIGHT_TO_LEFT || direction == W_SLIDER_DIR_TOP_TO_BOTTOM;
}

static w_slider_direction_t slider_effective_direction(const w_slider_ctx_t *ctx, lv_obj_t *card)
{
    if (ctx == NULL) {
        return W_SLIDER_DIR_LEFT_TO_RIGHT;
    }
    if (ctx->direction_cfg != W_SLIDER_DIR_AUTO) {
        return ctx->direction_cfg;
    }
    if (card != NULL && lv_obj_get_width(card) < lv_obj_get_height(card)) {
        return W_SLIDER_DIR_BOTTOM_TO_TOP;
    }
    return W_SLIDER_DIR_LEFT_TO_RIGHT;
}

static void slider_apply_native_orientation(w_slider_ctx_t *ctx)
{
    if (ctx == NULL || ctx->slider == NULL) {
        return;
    }

    const bool vertical = slider_direction_is_vertical(ctx->direction_effective);
    const bool reversed = slider_direction_is_reversed(ctx->direction_effective);

    lv_slider_set_orientation(
        ctx->slider, vertical ? LV_SLIDER_ORIENTATION_VERTICAL : LV_SLIDER_ORIENTATION_HORIZONTAL);
    lv_obj_set_style_base_dir(ctx->slider, LV_BASE_DIR_LTR, LV_PART_MAIN);
    lv_slider_set_range(ctx->slider, reversed ? 100 : 0, reversed ? 0 : 100);
}

static void slider_set_value_label(lv_obj_t *label, int value)
{
    if (label == NULL) {
        return;
    }
    char text[24] = {0};
    snprintf(text, sizeof(text), "%d %%", clamp_percent(value));
    lv_label_set_text(label, text);
}

/* Split anatomy (ui_style_traits()->split_wide_tiles) from this content width on: title and state
 * in a left column, a broad track and the value column right of it. */
#define SLIDER_SPLIT_MIN_W 420

static void slider_layout_split_info(w_slider_ctx_t *ctx, lv_coord_t content_w, lv_coord_t content_h)
{
    const lv_coord_t info_w = content_w / 4;
    lv_obj_set_hidden(ctx->state_label, false);
    lv_obj_set_align(ctx->title_label, LV_ALIGN_TOP_LEFT);
    lv_obj_set_style_text_align(ctx->title_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_width(ctx->title_label, info_w);
    ui_widget_name_one_line(ctx->title_label);
    lv_obj_set_align(ctx->state_label, LV_ALIGN_TOP_LEFT);
    lv_obj_set_style_text_font(ctx->state_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_width(ctx->state_label, info_w);
    /* name and state stacked by their line heights around the middle */
    const lv_coord_t title_h = lv_obj_get_height(ctx->title_label);
    const lv_coord_t state_h = lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    if (ui_widget_name_enabled(ctx->title_label)) {
        const lv_coord_t top = (content_h - title_h - 4 - state_h) / 2;
        lv_obj_set_pos(ctx->title_label, 0, top);
        lv_obj_set_pos(ctx->state_label, 0, top + title_h + 4);
    } else {
        lv_obj_set_pos(ctx->state_label, 0, (content_h - state_h) / 2);
    }
}

static void slider_apply_layout(lv_obj_t *card, w_slider_ctx_t *ctx)
{
    if (card == NULL || ctx == NULL || ctx->state_label == NULL || ctx->title_label == NULL || ctx->slider == NULL) {
        return;
    }

    /* The state word ("Offen"/"Geschlossen"/"An"/"Aus") and the value used to share a cramped top
     * row, leaving too little height for the slider itself (it read as a plain rectangle rather
     * than a fill bar on a narrow tile) and fighting over width on top of that -- found via the
     * widget gallery. The state word is dropped from the visible layout entirely (still updated
     * in ctx->state_label for the rare case something needs it later, e.g. distinguishing a
     * cover's "opening"/"closing" from a static percentage); only the value is shown now,
     * centered on the slider itself instead of in a separate row, so the slider gets almost the
     * full tile height. */
    lv_obj_set_hidden(ctx->state_label, true);
    /* A short tile (near the 100 px minimum) cannot spare the margins sized for a large one. */
    lv_obj_update_layout(card);
    const bool compact = lv_obj_get_content_height(card) < 120;
    lv_obj_align(ctx->title_label, LV_ALIGN_BOTTOM_MID, 0, compact ? 0 : -12);

    const lv_coord_t top_gap = compact ? 2 : 10;
    const lv_coord_t bottom_gap = compact ? 4 : 12;
    ctx->direction_effective = slider_effective_direction(ctx, card);
    const bool vertical = slider_direction_is_vertical(ctx->direction_effective);
    /* The area above the title the track may use at least. A vertical track needs real length; a
     * horizontal one only its thickness. With the vertical 50 px for both, a horizontal slider in a
     * short tile (105 px, the widget gallery) grew over its own title: hidden behind the opaque
     * track in flat and glow, showing through the translucent one in glass. */
    const lv_coord_t min_h = vertical ? 50 : (ui_style_traits()->touch_tracks ? 32 : 16);

    lv_coord_t content_w =
        lv_obj_get_width(card) - lv_obj_get_style_pad_left(card, LV_PART_MAIN) - lv_obj_get_style_pad_right(card, LV_PART_MAIN);
    lv_coord_t content_h =
        lv_obj_get_height(card) - lv_obj_get_style_pad_top(card, LV_PART_MAIN) - lv_obj_get_style_pad_bottom(card, LV_PART_MAIN);
    if (content_w < 24) {
        content_w = 24;
    }
    if (content_h < 24) {
        content_h = 24;
    }

    /* The value label is capped to one line (not just given a width): without an explicit
     * height, LV_LABEL_LONG_DOT wraps to as many lines as the label's auto height allows before
     * ever truncating, instead of dotting a single line. It now spans the full content width and
     * is centered on the slider below, so it no longer needs to fight anything else for room. */
    const lv_font_t *value_font = compact ? ui_font_role(UI_TEXT_META) : ui_font_role(UI_TEXT_META);
    lv_obj_set_style_text_font(ctx->value_label, value_font, LV_PART_MAIN);
    const lv_coord_t label_h = lv_font_get_line_height(value_font);
    lv_obj_set_width(ctx->value_label, content_w);
    lv_obj_set_height(ctx->value_label, label_h);

    lv_obj_update_layout(card);

    lv_coord_t top = top_gap;
    lv_coord_t bottom = ui_widget_name_enabled(ctx->title_label) ? lv_obj_get_y(ctx->title_label) - bottom_gap : content_h - bottom_gap;
    if (top < 0) {
        top = 0;
    }
    if (bottom > content_h) {
        bottom = content_h;
    }
    /* The minimum track length gives way to the title below it on a small tile: a shorter track
     * beats one drawn over the title. */
    if (bottom < (top + min_h) && lv_obj_is_hidden(ctx->title_label)) {
        bottom = top + min_h;
        if (bottom > content_h) {
            bottom = content_h;
            top = bottom - min_h;
            if (top < 0) {
                top = 0;
            }
        }
    }
    if (bottom < top + 8) {
        bottom = top + 8;
    }

    lv_coord_t area_h = bottom - top;
    lv_coord_t area_w = content_w;

    lv_coord_t slider_x = 0;
    lv_coord_t slider_y = top;
    lv_coord_t slider_w = area_w;
    lv_coord_t slider_h = area_h;
    lv_coord_t target_thickness = (content_w < content_h) ? content_w : content_h;
    if (target_thickness < 2) {
        target_thickness = 2;
    }

    if (vertical) {
        slider_w = target_thickness;
        if (slider_w > area_w) {
            slider_w = area_w;
        }
        if (slider_w < 2) {
            slider_w = 2;
        }
        slider_h = area_h;
        slider_x = (area_w - slider_w) / 2;
    } else {
        slider_w = area_w;
        slider_h = target_thickness;
        if (slider_h > area_h) {
            slider_h = area_h;
        }
        if (slider_h < 2) {
            slider_h = 2;
        }
        slider_y = top + (area_h - slider_h) / 2;
    }

    /* A horizontal track thinner than the value text cannot carry it: "42 %" sat across the
     * fill edge (widget gallery). Give the value its own column right of the track instead,
     * sized for the widest value so the track does not change length with the number. */
    lv_coord_t value_col_w = 0;
    if (!vertical && (ui_style_traits()->value_beside_slider || slider_h < label_h + 4)) {
        lv_point_t widest = {0};
        lv_text_get_size(&widest, "100 %", ui_font_role(UI_TEXT_META), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        value_col_w = widest.x + 10;
        if (value_col_w < area_w / 2) {
            slider_w = area_w - value_col_w;
        } else {
            value_col_w = 0;
        }
    }
    /* Too narrow for that column as well: the value goes above the track, which moves down. */
    bool value_above = false;
    if (!vertical && slider_h < label_h + 4 && value_col_w == 0 && area_h >= slider_h + label_h + 2) {
        value_above = true;
        slider_y = top + area_h - slider_h;
    }
    if (ui_style_traits()->split_wide_tiles && !vertical && content_w >= SLIDER_SPLIT_MIN_W) {
        slider_layout_split_info(ctx, content_w, content_h);
        const lv_coord_t info_w = content_w / 4;
        value_col_w = 72;
        slider_x = info_w + 16;
        slider_w = content_w - slider_x - value_col_w - 8;
        slider_h = 48;
        slider_y = (content_h - slider_h) / 2;
        value_above = false;
    }

    lv_obj_set_pos(ctx->slider, slider_x, slider_y);
    lv_obj_set_size(ctx->slider, slider_w, slider_h);
    lv_coord_t thickness = vertical ? slider_w : slider_h;
    if (thickness < 2) {
        thickness = 2;
    }
    /* Pill in glow and glass, flat's square ends in flat (set again after every resize). */
    const lv_coord_t radius = ui_style_track_radius();
    lv_obj_set_style_radius(ctx->slider, radius, LV_PART_MAIN);
    lv_obj_set_style_radius(ctx->slider, radius, LV_PART_INDICATOR);
    lv_obj_set_style_radius(ctx->slider, radius, LV_PART_KNOB);

    /* Centered on the slider itself, not the card: on a vertical slider the slider is narrower
     * than the card (target_thickness above) and horizontally centered within it, so centering
     * the label on the card instead would already happen to match here, but on a horizontal
     * slider the bar can be shorter than the card too (thickness capped, vertically centered) --
     * centering on the slider is correct in both cases, the card only coincidentally so in one.
     * The label was created before the slider (see the widget's creation order), so LVGL would
     * otherwise draw the slider over it; move it back in front every layout pass. */
    if (value_above) {
        lv_obj_set_style_text_align(ctx->value_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align_to(ctx->value_label, ctx->slider, LV_ALIGN_OUT_TOP_MID, 0, -2);
    } else if (value_col_w > 0) {
        lv_obj_set_width(ctx->value_label, value_col_w);
        lv_obj_set_style_text_align(ctx->value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_align_to(ctx->value_label, ctx->slider, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
    } else {
        lv_obj_set_style_text_align(ctx->value_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align_to(ctx->value_label, ctx->slider, LV_ALIGN_CENTER, 0, 0);
    }
    lv_obj_move_foreground(ctx->value_label);

    slider_apply_native_orientation(ctx);
}

static void slider_apply_visual(w_slider_ctx_t *ctx)
{
    if (ctx == NULL || ctx->card == NULL || ctx->title_label == NULL || ctx->state_label == NULL ||
        ctx->value_label == NULL || ctx->slider == NULL) {
        return;
    }

    lv_obj_t *card = ctx->card;

    const lv_color_t card_bg = lv_color_hex(ctx->is_on && !ctx->unavailable ? APP_UI_COLOR_CARD_BG_ON : APP_UI_COLOR_CARD_BG_OFF);
    lv_color_t indicator_color = ctx->unavailable
                                     ? lv_color_hex(APP_UI_COLOR_CARD_BORDER)
                                     : (ctx->is_on ? ctx->accent_color : lv_color_hex(W_SLIDER_FILL_OFF_HEX));
    if (!ctx->unavailable) {
        indicator_color = ui_style_slider_fill(ctx->is_on, indicator_color, lv_color_hex(W_SLIDER_TRACK_HEX));
    }
    /* Now that the value sits centered on top of the slider itself (previously it sat on the
     * card, with the card's own on/off tinting), "is_on" here would have picked ctx->accent_color
     * -- the exact same color as the slider's own filled indicator right underneath it, making
     * the text invisible wherever it overlapped the filled portion. A fixed light color reads
     * against both the filled (accent) and unfilled (dark track / W_SLIDER_FILL_OFF_HEX) halves
     * instead. */
    const lv_color_t value_color = ctx->unavailable ? lv_color_hex(APP_UI_COLOR_TEXT_MUTED)
        : ui_style_value_on_fill(lv_color_white());
    const lv_color_t state_color = ctx->unavailable
                                       ? lv_color_hex(APP_UI_COLOR_TEXT_MUTED)
                                       : (ctx->is_on ? lv_color_hex(APP_UI_COLOR_STATE_ON) : lv_color_hex(APP_UI_COLOR_STATE_OFF));

    lv_obj_set_style_bg_color(card, card_bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    ui_style_card_set_active(card, ctx->is_on && !ctx->unavailable);
    /* An unavailable entity cannot take a value: the slider stops answering touch until HA reports a state again. */
    if (ctx->unavailable) {
        lv_obj_add_state(ctx->slider, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(ctx->slider, LV_STATE_DISABLED);
    }
    lv_obj_set_style_text_color(ctx->title_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->state_label, state_color, LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->value_label, value_color, LV_PART_MAIN);

    /* Glow gets a raised track, glass a translucent bordered one, and matte a dark flat one
     * (ui_style_inset_track()); flat keeps this widget's own fixed track color, so it
     * is not overwritten again below. Border stays off for flat and glow (a flush track); glass
     * manages its own border width entirely inside ui_style_inset_track(). */
    ui_style_inset_track(ctx->slider, lv_color_hex(W_SLIDER_TRACK_HEX));
    if (ui_style_is_flat()) {
        lv_obj_set_style_bg_color(ctx->slider, lv_color_hex(W_SLIDER_TRACK_HEX), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ctx->slider, lv_color_hex(W_SLIDER_TRACK_HEX), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(ctx->slider, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(ctx->slider, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    }
    if (!ui_style_is_glass()) {
        lv_obj_set_style_border_width(ctx->slider, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(ctx->slider, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    }
    lv_obj_set_style_pad_all(ctx->slider, 0, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(ctx->slider, true, LV_PART_MAIN);

    lv_obj_set_style_bg_color(ctx->slider, indicator_color, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ctx->slider, indicator_color, LV_PART_INDICATOR | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(ctx->slider, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ctx->slider, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(ctx->slider, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ctx->slider, 0, LV_PART_INDICATOR | LV_STATE_PRESSED);

    /* Keep native knob hit-testing but render it transparent in all interaction states. */
    ui_style_inset_effects(ctx->slider);
    lv_obj_set_style_bg_opa(ctx->slider, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ctx->slider, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_PRESSED);
    lv_obj_set_style_border_opa(ctx->slider, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ctx->slider, LV_OPA_TRANSP, LV_PART_KNOB | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(ctx->slider, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ctx->slider, 0, LV_PART_KNOB | LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(ctx->slider, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(ctx->slider, 0, LV_PART_KNOB | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(ctx->slider, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ctx->slider, 0, LV_PART_KNOB | LV_STATE_PRESSED);
    lv_obj_set_style_pad_left(ctx->slider, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_right(ctx->slider, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_top(ctx->slider, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_bottom(ctx->slider, 0, LV_PART_KNOB);

    slider_apply_layout(card, ctx);

    ctx->suppress_event = true;
    lv_slider_set_value(ctx->slider, clamp_percent(ctx->value), LV_ANIM_OFF);
    ctx->suppress_event = false;

    slider_set_value_label(ctx->value_label, ctx->value);
    if (ctx->is_cover && !ctx->unavailable) {
        const char *text = ui_i18n_get("cover.open", "Open");
        if (strcmp(ctx->cover_state, "closed") == 0) {
            text = ui_i18n_get("cover.closed", "Closed");
        } else if (strcmp(ctx->cover_state, "opening") == 0) {
            text = ui_i18n_get("cover.opening", "Opening");
        } else if (strcmp(ctx->cover_state, "closing") == 0) {
            text = ui_i18n_get("cover.closing", "Closing");
        }
        lv_label_set_text(ctx->state_label, text);
    } else {
        lv_label_set_text(
            ctx->state_label,
            slider_translate_status_text(ctx->unavailable ? "unavailable" : (ctx->is_on ? "ON" : "OFF")));
    }
}

static void w_slider_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    w_slider_ctx_t *ctx = (w_slider_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL) {
        return;
    }
    if (code == LV_EVENT_DELETE) {
        free(ctx);
        return;
    }

    if (ctx->suppress_event) {
        return;
    }

    lv_obj_t *slider = lv_event_get_target(event);
    if (slider == NULL) {
        return;
    }

    if (ctx->unavailable) {
        /* The entity went unavailable (also mid-drag): send nothing, keep the flag until HA reports a state, and put the knob back. */
        ctx->dragging = false;
        slider_apply_visual(ctx);
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        ctx->dragging = true;
        slider_apply_visual(ctx);
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        ctx->value = clamp_percent(lv_slider_get_value(slider));
        ctx->dragging = true;
        ctx->unavailable = false;
        ctx->is_on = ctx->value > 0;
        slider_apply_visual(ctx);
    } else if (code == LV_EVENT_RELEASED) {
        int prev_value = ctx->value;
        bool prev_is_on = ctx->is_on;
        bool prev_unavailable = ctx->unavailable;
        int next_value = clamp_percent(lv_slider_get_value(slider));
        bool next_is_on = next_value > 0;
        ctx->dragging = false;
        if (next_value != ctx->last_sent_value) {
            esp_err_t err = ui_bindings_set_slider_value(ctx->entity_id, next_value);
            if (err != ESP_OK) {
                ctx->value = prev_value;
                ctx->is_on = prev_is_on;
                ctx->unavailable = prev_unavailable;
                slider_apply_visual(ctx);
                return;
            }
            ctx->last_sent_value = next_value;
        }
        ctx->value = next_value;
        ctx->is_on = next_is_on;
        ctx->unavailable = false;
        slider_apply_visual(ctx);
    } else if (code == LV_EVENT_PRESS_LOST) {
        ctx->dragging = false;
        slider_apply_visual(ctx);
    }
}

esp_err_t w_slider_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, def->x, def->y);
    lv_obj_set_size(card, def->w, def->h);
    lv_obj_set_scrollable(card, false);
    lv_obj_set_style_radius(card, APP_UI_CARD_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 16, LV_PART_MAIN);

    lv_obj_t *title = ui_widget_name_create(card, !def->hide_name);
    const char *title_text = def->title[0] ? def->title : def->id;
    ui_style_title_fit(title, title_text);
    lv_obj_set_width(title, def->w - 32);
    lv_obj_set_height(title, lv_font_get_line_height(ui_font_role(UI_TEXT_META)));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(title, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_BOTTOM_MID, 0, -12);

    lv_obj_t *state = lv_label_create(card);
    lv_label_set_text(state, ui_i18n_get("common.off", "OFF"));
    lv_obj_set_style_text_font(state, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_label_set_long_mode(state, LV_LABEL_LONG_DOT);
    lv_obj_align(state, LV_ALIGN_TOP_LEFT, 0, 2);

    lv_obj_t *value = lv_label_create(card);
    slider_set_value_label(value, 0);
    lv_obj_set_style_text_font(value, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    /* Leftover from the old top-right corner position: right-aligned text in a label now sized
     * to the full content width and centered on the slider (slider_apply_layout()) rendered the
     * text hugging the right edge of that wide box instead of in the middle -- found via the
     * widget gallery ("70 %" sitting well into the horizontal slider's unfilled portion instead
     * of over its own center). */
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t *slider = lv_slider_create(card);
    lv_obj_set_size(slider, def->w - 32, def->h - 84);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, 0, LV_ANIM_OFF);
    lv_obj_set_event_bubble(slider, false);

    w_slider_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(w_slider_ctx_t));
    if (ctx == NULL) {
        lv_obj_del(card);
        return ESP_ERR_NO_MEM;
    }
    snprintf(ctx->entity_id, sizeof(ctx->entity_id), "%s", def->entity_id);
    ctx->is_cover = strncmp(ctx->entity_id, "cover.", 6) == 0;
    ctx->card = card;
    ctx->title_label = title;
    ctx->state_label = state;
    ctx->value_label = value;
    ctx->slider = slider;
    ctx->direction_cfg = slider_direction_from_text(def->slider_direction);
    ctx->direction_effective = slider_effective_direction(ctx, card);
    ctx->accent_color = ctx->is_cover ? ui_style_control_on(lv_color_hex(APP_UI_COLOR_NAV_TAB_ACTIVE))
                                                 : ui_style_accent(lv_color_hex(APP_UI_COLOR_NAV_TAB_ACTIVE));
    ctx->value = 0;
    ctx->is_on = false;
    ctx->unavailable = false;
    ctx->dragging = false;
    ctx->suppress_event = false;
    ctx->last_sent_value = -1;

    lv_color_t parsed_color = lv_color_hex(0);
    if (slider_parse_hex_color(def->slider_accent_color, &parsed_color)) {
        ctx->accent_color = parsed_color;
    }

    lv_obj_add_event_cb(slider, w_slider_event_cb, LV_EVENT_PRESSED, ctx);
    lv_obj_add_event_cb(slider, w_slider_event_cb, LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_add_event_cb(slider, w_slider_event_cb, LV_EVENT_RELEASED, ctx);
    lv_obj_add_event_cb(slider, w_slider_event_cb, LV_EVENT_PRESS_LOST, ctx);
    lv_obj_add_event_cb(slider, w_slider_event_cb, LV_EVENT_DELETE, ctx);

    slider_apply_visual(ctx);
    out_instance->obj = card;
    out_instance->ctx = ctx;
    return ESP_OK;
}

void w_slider_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->obj == NULL || state == NULL) {
        return;
    }

    w_slider_ctx_t *ctx = (w_slider_ctx_t *)instance->ctx;
    if (ctx == NULL) {
        return;
    }

    if (slider_state_is_unavailable(state->state)) {
        ctx->value = 0;
        ctx->is_on = false;
        ctx->unavailable = true;
        ctx->dragging = false;
        slider_apply_visual(ctx);
        return;
    }

    bool has_numeric = false;
    int value = slider_extract_percent_value(state, &has_numeric);
    bool on_from_text = slider_state_is_on_text(state->state);
    bool is_on = has_numeric ? (value > 0 || on_from_text) : on_from_text;

    ctx->value = clamp_percent(value);
    ctx->is_on = is_on;
    ctx->unavailable = false;
    ctx->dragging = false;
    if (ctx->is_cover) {
        strlcpy(ctx->cover_state, state->state, sizeof(ctx->cover_state));
    }
    slider_apply_visual(ctx);
}

void w_slider_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->obj == NULL) {
        return;
    }

    w_slider_ctx_t *ctx = (w_slider_ctx_t *)instance->ctx;
    if (ctx == NULL) {
        return;
    }

    ctx->value = 0;
    ctx->is_on = false;
    ctx->unavailable = true;
    ctx->dragging = false;
    slider_apply_visual(ctx);
}
