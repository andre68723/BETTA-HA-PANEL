/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * A single sensor as a live gauge with a configurable range (gauge_min/gauge_max), for values
 * that do not read naturally as a percentage (e.g. a wallbox's charging power, 0..11000 W) and
 * where the plain "sensor" tile's number is not visual enough. Read-only: unlike the slider tile
 * this never calls a service, it only displays ha_client's state updates.
 *
 * Three styles (style_variant, the same generic field the heating tile uses for its own variant):
 * - "linear" (default): a vertical fill bar, gold (empty) to green (full), like a thermometer.
 * - "horizontal": the same bar lying down, filling left to right, with the title and the value in
 *   one row above it -- for a wide, flat tile.
 * - "radial": a single arc gauge (Instrument-style, moving colour arc) with the primary entity's
 *   value at the centre and up to two more short readouts underneath it, from
 *   secondary_entity_id / tertiary_entity_id (optional; hidden when not set) -- for a related
 *   value or two worth showing alongside without a second widget.
 */
#include "ui/ui_widget_factory.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "app_config.h"
#include "layout/widget_types.h"
#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"
#include "ha/ha_display_precision.h"
#include "util/distance_units.h"

/* Same gold (empty) to green (full) gradient as the solar forecast bars, so a fill-level bar or
 * arc looks the same wherever the app uses one. */
#define GAUGE_BAR_GREEN (ui_style_has_depth() ? 0x4FCB7E : 0x22D164)
#define GAUGE_BAR_GOLD (ui_style_has_depth() ? 0xF2C14E : 0xFFD500)

typedef enum {
    GAUGE_STYLE_LINEAR = 0,
    GAUGE_STYLE_HORIZONTAL,
    GAUGE_STYLE_RADIAL,
} gauge_style_t;

/* Color coding by value, on top of the default fill-level gold->green (ui_style_fill_level_color()):
 * - GAUGE_COLOR_GRADIENT: a custom two-color version of the same idea, e.g. cold blue to hot red
 *   across the full gauge_min..gauge_max range (a room temperature).
 * - GAUGE_COLOR_RANGE: a target band highlighted distinctly from outside it, e.g. a humidity
 *   "normal" range in one color, too dry/too humid in another (not a continuous gradient -- the
 *   "good" value is in the middle, not at either end, which GAUGE_COLOR_GRADIENT cannot express). */
typedef enum {
    GAUGE_COLOR_FILL = 0,
    GAUGE_COLOR_GRADIENT,
    GAUGE_COLOR_RANGE,
} gauge_color_mode_t;

static gauge_color_mode_t gauge_color_mode_from_text(const char *text)
{
    if (text != NULL && strcmp(text, "gradient") == 0) {
        return GAUGE_COLOR_GRADIENT;
    }
    if (text != NULL && strcmp(text, "range") == 0) {
        return GAUGE_COLOR_RANGE;
    }
    return GAUGE_COLOR_FILL;
}

static bool gauge_is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int gauge_hex_nibble(char c)
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

/* Same hex-RGB parser as the other widgets with a configurable accent color (slider_accent_color,
 * button_accent_color, graph_line_color); kept as its own copy rather than a new shared helper,
 * matching how each of those already has its own, rather than refactoring all of them here. */
static bool gauge_parse_hex_color(const char *text, lv_color_t *out)
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
        if (!gauge_is_hex_digit(p[i])) {
            return false;
        }
    }
    int r_hi = gauge_hex_nibble(p[0]);
    int r_lo = gauge_hex_nibble(p[1]);
    int g_hi = gauge_hex_nibble(p[2]);
    int g_lo = gauge_hex_nibble(p[3]);
    int b_hi = gauge_hex_nibble(p[4]);
    int b_lo = gauge_hex_nibble(p[5]);
    if (r_hi < 0 || r_lo < 0 || g_hi < 0 || g_lo < 0 || b_hi < 0 || b_lo < 0) {
        return false;
    }
    uint32_t rgb = (uint32_t)(((r_hi << 4) | r_lo) << 16) | (uint32_t)(((g_hi << 4) | g_lo) << 8) |
                   (uint32_t)((b_hi << 4) | b_lo);
    *out = lv_color_hex(rgb);
    return true;
}

/* Segmented linear style: LED-like blocks instead of one continuous bar, gauge_segment_count of
 * them (GAUGE_SEGMENT_COUNT_* in layout/widget_types.h). A column too short for all of them at
 * GAUGE_SEGMENT_MIN_H shows fewer, so a block never shrinks to a line. */
#define GAUGE_SEGMENT_MIN_H 4

typedef struct {
    lv_obj_t *card;
    lv_obj_t *title_label;
    lv_obj_t *value_label;
    lv_obj_t *bar;         /* linear, smooth sub-style only */
    lv_obj_t *segment[GAUGE_SEGMENT_COUNT_MAX]; /* linear, segmented sub-style only */
    int segment_count;  /* created blocks */
    int segments_shown; /* blocks that fit the column, <= segment_count */
    int32_t segment_value; /* last value shown, to relight the blocks when fewer or more fit */
    char value_text[96];   /* the value label's full text: LV_LABEL_LONG_DOT shortens the label's own copy */
    lv_obj_t *arc;         /* radial only */
    char info_text[2][96]; /* Preserve full readings when LVGL replaces text with dots. */
    lv_obj_t *info_label[2]; /* radial and horizontal: secondary_entity_id, tertiary_entity_id */
    gauge_style_t style;
    bool segmented; /* linear only: gauge_bar_style == "segmented" */
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    char extra_entity_id[2][APP_MAX_ENTITY_ID_LEN]; /* secondary, tertiary */
    float range_min;
    float range_max;
    gauge_color_mode_t color_mode;
    lv_color_t color_low;    /* gradient: at range_min; range: below target_low */
    lv_color_t color_high;   /* gradient: at range_max; range: above target_high */
    lv_color_t target_color; /* range only: within [target_low, target_high] */
    float target_low;
    float target_high;
    bool unavailable;
} w_gauge_ctx_t;

static gauge_style_t gauge_style_from_text(const char *text)
{
    if (text != NULL && strcmp(text, "radial") == 0) {
        return GAUGE_STYLE_RADIAL;
    }
    if (text != NULL && strcmp(text, "horizontal") == 0) {
        return GAUGE_STYLE_HORIZONTAL;
    }
    return GAUGE_STYLE_LINEAR;
}

static bool gauge_state_is_unavailable(const char *state_text)
{
    if (state_text == NULL || state_text[0] == '\0') {
        return true;
    }
    return strcmp(state_text, "unavailable") == 0 || strcmp(state_text, "unknown") == 0;
}

static bool gauge_parse_number(const char *text, double *out_value)
{
    if (text == NULL || text[0] == '\0' || out_value == NULL) {
        return false;
    }
    char *end = NULL;
    double value = strtod(text, &end);
    if (end == text) {
        return false;
    }
    *out_value = value;
    return true;
}

static int32_t gauge_clamp_i32(double value, int32_t min, int32_t max)
{
    if (value < (double)min) {
        return min;
    }
    if (value > (double)max) {
        return max;
    }
    return (int32_t)lround(value);
}

/* state -> "state unit" text, reused for the main value and the two radial info labels alike. */
static void gauge_format_state_text(const ha_state_t *state, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (state == NULL) {
        return;
    }
    const char *unit = NULL;
    const char *device_class = NULL;
    cJSON *attrs = cJSON_Parse(state->attributes_json);
    if (attrs != NULL) {
        cJSON *unit_item = cJSON_GetObjectItemCaseSensitive(attrs, "unit_of_measurement");
        if (cJSON_IsString(unit_item) && unit_item->valuestring != NULL) {
            unit = unit_item->valuestring;
        }
        cJSON *class_item = cJSON_GetObjectItemCaseSensitive(attrs, "device_class");
        if (cJSON_IsString(class_item) && class_item->valuestring != NULL) {
            device_class = class_item->valuestring;
        }
    }
    char rounded[APP_MAX_STATE_LEN];
    const char *value = ha_display_precision_format(state->entity_id, state->state, rounded, sizeof(rounded))
        ? rounded : state->state;
    if (distance_readout_km(device_class, unit, state->state, out, out_len)) {
        /* A range in metres (66000 m) reads as kilometres. */
    } else if (unit != NULL && unit[0] != '\0') {
        snprintf(out, out_len, "%s %s", value, unit);
    } else {
        snprintf(out, out_len, "%s", value);
    }
    if (attrs != NULL) {
        cJSON_Delete(attrs);
    }
}

static int32_t gauge_range_min_i32(const w_gauge_ctx_t *ctx)
{
    float min = ctx->range_min;
    float max = ctx->range_max;
    if (!(max > min)) {
        return 0;
    }
    return gauge_clamp_i32(min, INT32_MIN, INT32_MAX);
}

static int32_t gauge_range_max_i32(const w_gauge_ctx_t *ctx)
{
    float min = ctx->range_min;
    float max = ctx->range_max;
    if (!(max > min)) {
        return 100;
    }
    return gauge_clamp_i32(max, INT32_MIN, INT32_MAX);
}

/* The color for one point on the gauge (a segment, the arc, the whole bar's indicator), by
 * ctx->color_mode: GAUGE_COLOR_FILL keeps the existing gold->green fill-level look
 * (ui_style_fill_level_color(), shared with the solar forecast bars and the car tile's rings) so
 * a gauge that never sets gauge_color_mode renders exactly as before this feature. GAUGE_COLOR_GRADIENT
 * linearly mixes ctx->color_low (at min) to ctx->color_high (at max). GAUGE_COLOR_RANGE picks
 * ctx->color_low below ctx->target_low, ctx->target_color within [target_low, target_high], and
 * ctx->color_high above target_high -- a step function, not a gradient, since the "good" value
 * sits in the middle rather than at either end (a humidity target band: too dry, in range, too
 * humid). `value` is clamped to [min, max] internally, same as ui_style_fill_level_color(). */
static lv_color_t gauge_value_color(const w_gauge_ctx_t *ctx, int32_t value, int32_t min, int32_t max)
{
    if (ctx->color_mode == GAUGE_COLOR_FILL) {
        return ui_style_fill_level_color(value, min, max);
    }
    if (value < min) {
        value = min;
    }
    if (value > max) {
        value = max;
    }
    if (ctx->color_mode == GAUGE_COLOR_RANGE) {
        if ((float)value < ctx->target_low) {
            return ctx->color_low;
        }
        if ((float)value > ctx->target_high) {
            return ctx->color_high;
        }
        return ctx->target_color;
    }
    /* GAUGE_COLOR_GRADIENT: lv_color_mix's second weight (0..255) is the share of the FIRST
     * color kept, same convention as ui_style_fill_level_color(). */
    if (max <= min) {
        return ctx->color_low;
    }
    uint8_t high_share = (uint8_t)(((int64_t)(value - min) * 255) / (max - min));
    return lv_color_mix(ctx->color_high, ctx->color_low, high_share);
}

static void gauge_apply_range(w_gauge_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    const int32_t min = gauge_range_min_i32(ctx);
    const int32_t max = gauge_range_max_i32(ctx);
    if (ctx->bar != NULL) {
        lv_bar_set_range(ctx->bar, min, max);
    }
    if (ctx->arc != NULL) {
        lv_arc_set_range(ctx->arc, min, max);
    }
}

/* GAUGE_COLOR_FILL and GAUGE_COLOR_GRADIENT both paint the whole indicator with one static
 * LVGL bar gradient at creation time (gold->green, or the two configured colors) -- the bar
 * simply reveals more or less of the same strip as the value changes, no per-update recolor
 * needed. GAUGE_COLOR_RANGE cannot be expressed this way: LVGL's bg_grad is a single two-stop
 * gradient, but a target band needs three zones (below/within/above), with the "good" color in
 * the middle rather than at either end. For that mode the indicator is left a plain, ungraded
 * fill instead, recolored to a solid color on every value update in gauge_apply_state() below --
 * closer to a traffic-light indicator than a gradient, which matches what a target band actually
 * means (is the current reading good or not), rather than approximating a shape LVGL cannot
 * draw. */
static void gauge_style_bar(w_gauge_ctx_t *ctx, lv_obj_t *bar)
{
    ui_style_inset_track(
        bar, lv_color_mix(lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 26));
    if (ui_style_is_flat()) {
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x3A3F47), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    }
    if (ctx->color_mode == GAUGE_COLOR_RANGE) {
        lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_NONE, LV_PART_INDICATOR);
    } else {
        const lv_color_t low = ctx->color_mode == GAUGE_COLOR_GRADIENT ? ctx->color_low : lv_color_hex(GAUGE_BAR_GOLD);
        const lv_color_t high = ctx->color_mode == GAUGE_COLOR_GRADIENT ? ctx->color_high : lv_color_hex(GAUGE_BAR_GREEN);
        lv_obj_set_style_bg_color(bar, low, LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_color(bar, high, LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_dir(
            bar, ctx->style == GAUGE_STYLE_HORIZONTAL ? LV_GRAD_DIR_HOR : LV_GRAD_DIR_VER, LV_PART_INDICATOR);
    }
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_bar_set_orientation(
        bar, ctx->style == GAUGE_STYLE_HORIZONTAL ? LV_BAR_ORIENTATION_HORIZONTAL : LV_BAR_ORIENTATION_VERTICAL);
}

static void gauge_style_arc(lv_obj_t *arc)
{
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_clickable(arc, false);
    ui_style_gauge_arc(arc, 18, 14,
        lv_color_mix(lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 30),
        lv_color_hex(APP_UI_COLOR_CARD_BORDER), lv_color_hex(GAUGE_BAR_GOLD));
}

static void gauge_style_segments(w_gauge_ctx_t *ctx, lv_obj_t *card)
{
    for (int i = 0; i < ctx->segment_count; i++) {
        lv_obj_t *seg = lv_obj_create(card);
        lv_obj_set_scrollable(seg, false);
        lv_obj_set_style_radius(seg, 3, LV_PART_MAIN);
        lv_obj_set_style_border_width(seg, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, LV_PART_MAIN);
        ctx->segment[i] = seg;
    }
}

/* Colours and dims each segment for the current value: filled from the bottom (index 0) up,
 * each lit segment coloured by its own position (green low, gold high) via
 * ui_style_fill_level_color, same colour language as the smooth bar's gradient and the radial
 * arc. Called on every state update, not just on layout, since it is the only thing besides
 * lv_bar_set_value the smooth style needs. */
static void gauge_update_segments(w_gauge_ctx_t *ctx, int32_t value)
{
    if (ctx == NULL) {
        return;
    }
    const int32_t min = gauge_range_min_i32(ctx);
    const int32_t max = gauge_range_max_i32(ctx);
    const int32_t span = max - min;
    ctx->segment_value = value;
    const int count = ctx->segments_shown;
    if (count <= 0) {
        return;
    }
    int lit = span > 0 ? (int)(((int64_t)(value - min) * count + span / 2) / span) : 0;
    if (lit < 0) lit = 0;
    if (lit > count) lit = count;

    /* an unlit block is the style's resting control colour: Matte's card is lighter than CARD_BG_OFF, so the old mix vanished on it */
    const lv_color_t track = ui_style_control_bg(
        lv_color_mix(lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 22));
    for (int i = 0; i < count; i++) {
        if (ctx->segment[i] == NULL) {
            continue;
        }
        /* index 0 is the bottom-most segment (lit first). */
        bool lit_seg = i < lit;
        if (lit_seg) {
            const int32_t seg_value = min + (int32_t)(((int64_t)i * 2 + 1) * span / (2 * count));
            lv_obj_set_style_bg_color(ctx->segment[i], gauge_value_color(ctx, seg_value, min, max), LV_PART_MAIN);
        } else {
            lv_obj_set_style_bg_color(ctx->segment[i], track, LV_PART_MAIN);
        }
    }
}

static void gauge_apply_layout_linear(w_gauge_ctx_t *ctx, lv_coord_t content_w, lv_coord_t content_h)
{
    const lv_coord_t gap = content_h < 100 ? 2 : 8;
    lv_coord_t bar_top = lv_obj_get_height(ctx->value_label) + gap;
    lv_coord_t bar_bottom = content_h - (ui_widget_name_enabled(ctx->title_label) ? lv_obj_get_height(ctx->title_label) : 0) - gap;
    if (bar_bottom < bar_top + 8) bar_bottom = bar_top + 8;
#if UI_TEXT_ADAPTIVE
    lv_coord_t bar_w = content_w * 2 / 3;
#else
    lv_coord_t bar_w = content_w / 3;
#endif
    if (bar_w < 14) {
        bar_w = 14;
    }
#if !UI_TEXT_ADAPTIVE
    if (bar_w > 60) bar_w = 60;
#endif
    if (ctx->segmented) {
        /* Many blocks get narrower gaps, so the column stays mostly light. */
        const lv_coord_t seg_gap = ctx->segment_count > GAUGE_SEGMENT_COUNT_DEFAULT ? 2 : 3;
        const lv_coord_t total_h = bar_bottom - bar_top;
        int count = ctx->segment_count;
        const int fit = (int)((total_h + seg_gap) / (GAUGE_SEGMENT_MIN_H + seg_gap));
        if (count > fit) {
            count = fit > 1 ? fit : 1;
        }
        const bool count_changed = count != ctx->segments_shown;
        ctx->segments_shown = count;
        const lv_coord_t seg_h = (total_h - seg_gap * (count - 1)) / count;
        lv_coord_t x = (content_w - bar_w) / 2;
        for (int i = 0; i < ctx->segment_count; i++) {
            if (ctx->segment[i] == NULL) {
                continue;
            }
            lv_obj_set_hidden(ctx->segment[i], i >= count);
            if (i >= count) {
                continue;
            }
            /* index 0 (bottom-most) sits at the bottom of the column, drawn last-to-first. */
            lv_coord_t y = bar_bottom - seg_h - i * (seg_h + seg_gap);
            lv_obj_set_size(ctx->segment[i], bar_w, seg_h);
            lv_obj_set_pos(ctx->segment[i], x, y);
        }
        if (count_changed) {
            gauge_update_segments(ctx, ctx->segment_value);
        }
        return;
    }
    lv_obj_set_size(ctx->bar, bar_w, bar_bottom - bar_top);
    lv_obj_set_pos(ctx->bar, (content_w - bar_w) / 2, bar_top);
    lv_obj_set_style_radius(ctx->bar, ui_style_track_radius(), LV_PART_MAIN);
    lv_obj_set_style_radius(ctx->bar, ui_style_track_radius(), LV_PART_INDICATOR);
}

static void gauge_apply_layout_radial(w_gauge_ctx_t *ctx, lv_coord_t content_w, lv_coord_t content_h)
{
    const bool info_1 = ctx->extra_entity_id[0][0] != '\0';
    const bool info_2 = ctx->extra_entity_id[1][0] != '\0';
    /* The title sits in the ring's open segment, so the ring takes the whole card. */
    lv_coord_t title_h = lv_obj_get_height(ctx->title_label);
    lv_coord_t arc_size = (content_w < content_h) ? content_w : content_h;
    /* A narrow, tall tile (the 80x160 minimum): the ring alone is too small to hold the title in
     * its open segment, so the title goes below it when there is the height for it. */
    const lv_coord_t title_line_h = lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    const bool title_below = ui_widget_name_enabled(ctx->title_label) && arc_size < 130 && content_h >= arc_size + title_line_h + 4;
    lv_coord_t arc_x = (content_w - arc_size) / 2;
    lv_coord_t arc_y = 0;
    lv_obj_set_size(ctx->arc, arc_size, arc_size);
    lv_obj_set_pos(ctx->arc, arc_x, arc_y);

    lv_coord_t center_y = arc_y + arc_size / 2;
    /* The reading as large as the car tile's charge level where the ring leaves room for it. */
    lv_obj_set_style_text_font(ctx->value_label,
                               arc_size >= 140 ? ui_font_role(UI_TEXT_VALUE) : (arc_size >= 90 ? ui_font_role(UI_TEXT_META) : ui_font_role(UI_TEXT_META)),
                               LV_PART_MAIN);
#if UI_TEXT_ADAPTIVE
    lv_obj_set_style_text_font(ctx->value_label, ui_text_fit(ctx->value_text, ui_font_role(UI_TEXT_KEY), arc_size - 32, arc_size / 3), LV_PART_MAIN);
#endif
    const lv_coord_t value_h = lv_font_get_line_height(lv_obj_get_style_text_font(ctx->value_label, LV_PART_MAIN));
    lv_obj_set_width(ctx->value_label, arc_size - 16);
    lv_obj_set_height(ctx->value_label, value_h);
    /* The reading and the extra readouts (META, one line each) as one block centred in the ring; the readouts used a
     * 20 px step from the 16 px text and overlapped at today's sizes. */
    const lv_coord_t info_step = lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    const int info_count = (info_1 ? 1 : 0) + (info_2 ? 1 : 0);
    const lv_coord_t value_y = center_y - (value_h + info_count * info_step) / 2;
    lv_obj_align(ctx->value_label, LV_ALIGN_TOP_LEFT, arc_x + 8, value_y);

    for (int i = 0, row = 0; i < 2; i++) {
        lv_obj_t *label = ctx->info_label[i];
        bool has = ctx->extra_entity_id[i][0] != '\0';
        lv_obj_set_hidden(label, !has);
        if (has) {
            ui_text_set(label, UI_TEXT_META, true);
            lv_obj_set_size(label, arc_size - 16, info_step);
            lv_obj_align(label, LV_ALIGN_TOP_LEFT, arc_x + 8, value_y + value_h + row * info_step);
            row++;
        }
    }
    if (title_below) {
        lv_obj_set_style_text_font(ctx->title_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
        lv_obj_set_height(ctx->title_label, title_line_h);
        lv_obj_set_width(ctx->title_label, content_w);
        lv_obj_align(ctx->title_label, LV_ALIGN_TOP_MID, 0, arc_y + arc_size + 4);
        ui_widget_name_set_hidden(ctx->title_label, false);
        return;
    }
    /* The open segment is about 1.3 r wide at 0.74 r below the centre; below the info lines. */
    const lv_coord_t r = arc_size / 2;
    lv_obj_set_style_text_font(ctx->title_label, ui_font_role(UI_TEXT_LABEL), LV_PART_MAIN);
    title_h = lv_font_get_line_height(lv_obj_get_style_text_font(ctx->title_label, LV_PART_MAIN));
    lv_coord_t title_y = center_y + r * 37 / 50 - title_h / 2;
    const lv_coord_t info_bottom = value_y + value_h + info_count * info_step;
    if ((info_1 || info_2) && title_y < info_bottom) {
        title_y = info_bottom;
    }
    if (title_y > content_h - title_h) {
        title_y = content_h - title_h;
    }
    /* a ring filled by the reading and its extras (larger text) leaves the name out rather than over the extras */
    ui_widget_name_set_hidden(ctx->title_label, (info_1 || info_2) && title_y < info_bottom);
    lv_obj_set_width(ctx->title_label, r * 13 / 10);
    lv_obj_align(ctx->title_label, LV_ALIGN_TOP_MID, 0, title_y);
}

static void gauge_set_value_text(w_gauge_ctx_t *ctx, const char *text)
{
    snprintf(ctx->value_text, sizeof(ctx->value_text), "%s", text);
    lv_label_set_text(ctx->value_label, ctx->value_text);
}

/* Horizontal style: title (left) and value (right) share the top row, the bar or the blocks fill
 * the width below, filling left to right. */
static void gauge_apply_layout_horizontal(w_gauge_ctx_t *ctx, lv_coord_t content_w, lv_coord_t content_h)
{
#if UI_TEXT_ADAPTIVE
    const lv_coord_t gap = content_h < 80 ? 4 : 8;
#else
    const lv_coord_t gap = 8;
#endif
    const lv_coord_t row_h = lv_font_get_line_height(lv_obj_get_style_text_font(ctx->value_label, LV_PART_MAIN));
    /* One row: the value gets the width it needs, the title the rest, and gives way entirely on a
     * tile too narrow for both. */
    lv_point_t value_size;
    lv_text_get_size(&value_size, ctx->value_text, lv_obj_get_style_text_font(ctx->value_label, LV_PART_MAIN),
                     0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_coord_t value_w = value_size.x + 2;
    if (value_w > content_w) {
        value_w = content_w;
    }
    const lv_coord_t title_w = content_w - value_w - gap;
    ui_widget_name_set_hidden(ctx->title_label, title_w < 40); /* narrower shows little more than "..." */
    lv_obj_set_size(ctx->value_label, value_w, lv_font_get_line_height(lv_obj_get_style_text_font(ctx->value_label, LV_PART_MAIN)));
    lv_obj_set_width(ctx->title_label, title_w < 40 ? 40 : title_w);
    lv_obj_set_style_text_align(ctx->title_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_align(ctx->title_label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_align(ctx->value_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    const lv_coord_t area_top = row_h + gap;
    lv_coord_t area_h = content_h - area_top;
    if (area_h < 6) area_h = 6;
    /* Extra readouts in one row under the bar (info 1 left, info 2 right) when the tile has room. */
    const bool info_1 = ctx->extra_entity_id[0][0] != '\0';
    const bool info_2 = ctx->extra_entity_id[1][0] != '\0';
    const lv_coord_t info_h = lv_font_get_line_height(ui_font_role(UI_TEXT_VALUE));
#if UI_TEXT_ADAPTIVE
    if ((info_1 || info_2) && content_h < 80) {
        lv_coord_t right = value_w + gap;
        for (int i = 1; i >= 0; i--) {
            const bool has = ctx->extra_entity_id[i][0] != '\0';
            lv_obj_set_hidden(ctx->info_label[i], !has);
            if (!has) continue;
            const char *text = ctx->info_text[i];
            lv_label_set_text(ctx->info_label[i], text);
            const lv_font_t *font = ui_text_fit(text, ui_font_role(UI_TEXT_VALUE), content_w / 3, row_h);
            lv_point_t size; lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            lv_obj_set_style_text_font(ctx->info_label[i], font, LV_PART_MAIN);
            lv_obj_set_size(ctx->info_label[i], size.x + 2, font->line_height);
            lv_obj_align(ctx->info_label[i], LV_ALIGN_TOP_RIGHT, -right, (row_h - font->line_height) / 2);
            right += size.x + 2 + gap;
        }
        lv_obj_set_width(ctx->title_label, content_w - right - gap);
    }
#endif
    const bool info_row = (info_1 || info_2) && area_h >= 14 + 6 + info_h;
    for (int i = 0; i < 2; i++) {
        const bool shown = info_row && (i == 0 ? info_1 : info_2);
#if UI_TEXT_ADAPTIVE
        if (content_h >= 80) lv_obj_set_hidden(ctx->info_label[i], !shown);
#else
        lv_obj_set_hidden(ctx->info_label[i], !shown);
#endif
        if (!shown) {
            continue;
        }
        const lv_coord_t w = (info_1 && info_2) ? (content_w - gap) / 2 : content_w;
        lv_obj_set_size(ctx->info_label[i], w, info_h);
        lv_obj_set_style_text_align(ctx->info_label[i], i == 0 && info_2 ? LV_TEXT_ALIGN_LEFT
                                    : (i == 1 && info_1 ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER), LV_PART_MAIN);
        lv_obj_align(ctx->info_label[i], i == 1 && info_1 ? LV_ALIGN_BOTTOM_RIGHT : LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
    if (info_row) {
        area_h -= info_h + 6;
    }
    lv_coord_t bar_h = area_h / 2;
    if (bar_h < 14) {
        bar_h = area_h < 14 ? area_h : 14;
    }
    if (bar_h > 40) {
        bar_h = 40;
    }
    const lv_coord_t bar_y = area_top + (area_h - bar_h) / 2;

    if (ctx->segmented) {
        const lv_coord_t seg_gap = ctx->segment_count > GAUGE_SEGMENT_COUNT_DEFAULT ? 2 : 3;
        int count = ctx->segment_count;
        const int fit = (int)((content_w + seg_gap) / (GAUGE_SEGMENT_MIN_H + seg_gap));
        if (count > fit) {
            count = fit > 1 ? fit : 1;
        }
        const bool count_changed = count != ctx->segments_shown;
        ctx->segments_shown = count;
        const lv_coord_t seg_w = (content_w - seg_gap * (count - 1)) / count;
        /* Centre the blocks: the integer width leaves a few pixels over. */
        const lv_coord_t x0 = (content_w - (seg_w * count + seg_gap * (count - 1))) / 2;
        for (int i = 0; i < ctx->segment_count; i++) {
            if (ctx->segment[i] == NULL) {
                continue;
            }
            lv_obj_set_hidden(ctx->segment[i], i >= count);
            if (i >= count) {
                continue;
            }
            /* index 0 (lit first) is the left-most block. */
            lv_obj_set_size(ctx->segment[i], seg_w, bar_h);
            lv_obj_set_pos(ctx->segment[i], x0 + i * (seg_w + seg_gap), bar_y);
        }
        if (count_changed) {
            gauge_update_segments(ctx, ctx->segment_value);
        }
        return;
    }
    lv_obj_set_size(ctx->bar, content_w, bar_h);
    lv_obj_set_pos(ctx->bar, 0, bar_y);
    lv_obj_set_style_radius(ctx->bar, ui_style_track_radius(), LV_PART_MAIN);
    lv_obj_set_style_radius(ctx->bar, ui_style_track_radius(), LV_PART_INDICATOR);
}

static void gauge_apply_layout(w_gauge_ctx_t *ctx)
{
    if (ctx == NULL || ctx->card == NULL || ctx->title_label == NULL || ctx->value_label == NULL) {
        return;
    }

    lv_obj_t *card = ctx->card;
    lv_obj_update_layout(card);
    lv_coord_t content_w =
        lv_obj_get_width(card) - lv_obj_get_style_pad_left(card, LV_PART_MAIN) - lv_obj_get_style_pad_right(card, LV_PART_MAIN);
    lv_coord_t content_h =
        lv_obj_get_height(card) - lv_obj_get_style_pad_top(card, LV_PART_MAIN) - lv_obj_get_style_pad_bottom(card, LV_PART_MAIN);
    if (content_w < 24) {
        content_w = 24;
    }
    if (content_h < 40) {
        content_h = 40;
    }

    lv_obj_set_width(ctx->title_label, content_w);
    /* A reading wider than the narrow bar column puts its unit on a second line, on every board ("1142.0 W" was cut on
     * the 10-inch boards). */
    lv_label_set_text(ctx->value_label, ctx->value_text);
    if (ctx->style == GAUGE_STYLE_LINEAR) {
        lv_point_t natural; lv_text_get_size(&natural, ctx->value_text, ui_font_role(UI_TEXT_META), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (natural.x > content_w) {
            char split[sizeof(ctx->value_text)]; snprintf(split, sizeof(split), "%s", ctx->value_text);
            char *unit = strchr(split, ' '); if (unit) *unit = '\n';
            lv_label_set_text(ctx->value_label, split);
        }
        lv_label_set_long_mode(ctx->value_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_height(ctx->value_label, LV_SIZE_CONTENT);
    }
#if UI_TEXT_ADAPTIVE
    const lv_font_t *preferred = ui_font_role(UI_TEXT_KEY);
    lv_coord_t value_room = ctx->style == GAUGE_STYLE_HORIZONTAL && content_h < 80 ? content_h - 14 : (content_h < 80 ? 25 : content_h / 2);
    const lv_font_t *value_font = ui_text_fit(lv_label_get_text(ctx->value_label), preferred, content_w, value_room);
    lv_obj_set_style_text_font(ctx->value_label, value_font, LV_PART_MAIN);
    ui_text_compact(ctx->value_label, value_font == ui_font_role(UI_TEXT_META));
    ui_text_set(ctx->title_label, UI_TEXT_LABEL, false);
    if (ctx->style == GAUGE_STYLE_LINEAR && content_h < 100) lv_obj_set_style_text_font(ctx->title_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_height(ctx->title_label, lv_obj_get_style_text_font(ctx->title_label, LV_PART_MAIN)->line_height);
#endif
    lv_obj_set_style_text_align(ctx->title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_update_layout(ctx->title_label);

    if (ctx->style == GAUGE_STYLE_RADIAL) {
        lv_obj_set_style_text_align(ctx->value_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        gauge_apply_layout_radial(ctx, content_w, content_h);
    } else if (ctx->style == GAUGE_STYLE_HORIZONTAL) {
        gauge_apply_layout_horizontal(ctx, content_w, content_h);
    } else {
        lv_obj_set_width(ctx->value_label, content_w);
        lv_obj_set_style_text_align(ctx->value_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(ctx->value_label, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_align(ctx->title_label, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_update_layout(ctx->value_label);
        gauge_apply_layout_linear(ctx, content_w, content_h);
    }
}

static void gauge_set_unavailable_visual(w_gauge_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    gauge_set_value_text(ctx, ui_i18n_get("common.unavailable", "unavailable"));
    const int32_t min = gauge_range_min_i32(ctx);
    if (ctx->bar != NULL) {
        lv_bar_set_value(ctx->bar, min, LV_ANIM_OFF);
    }
    if (ctx->arc != NULL) {
        lv_arc_set_value(ctx->arc, min);
        /* Deliberately not gauge_value_color(): "unavailable" has no real value to color-code,
         * and showing it in whatever the configured gradient/range mode maps to the low end
         * (which could be a "good" green in range mode) would misleadingly suggest a real
         * reading. Always the same neutral, empty-looking gold as the default fill-level look. */
        lv_obj_set_style_arc_color(ctx->arc, ui_style_fill_level_color(min, min, gauge_range_max_i32(ctx)), LV_PART_INDICATOR);
    }
    if (ctx->segmented) {
        gauge_update_segments(ctx, min);
    }
}

static void w_gauge_event_cb(lv_event_t *event)
{
    if (event == NULL) {
        return;
    }
    w_gauge_ctx_t *ctx = (w_gauge_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DELETE) {
        free(ctx);
    } else if (code == LV_EVENT_SIZE_CHANGED) {
        gauge_apply_layout(ctx);
    }
}

esp_err_t w_gauge_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, def->x, def->y);
    lv_obj_set_size(card, def->w, def->h);
    lv_obj_set_scrollable(card, false);
    theme_default_style_card(card);
    lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);

    w_gauge_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(w_gauge_ctx_t));
    if (ctx == NULL) {
        lv_obj_del(card);
        return ESP_ERR_NO_MEM;
    }
    ctx->card = card;
    ctx->style = gauge_style_from_text(def->style_variant);
    snprintf(ctx->entity_id, sizeof(ctx->entity_id), "%s", def->entity_id);
    snprintf(ctx->extra_entity_id[0], sizeof(ctx->extra_entity_id[0]), "%s", def->secondary_entity_id);
    snprintf(ctx->extra_entity_id[1], sizeof(ctx->extra_entity_id[1]), "%s", def->tertiary_entity_id);
    ctx->range_min = def->gauge_min;
    ctx->range_max = def->gauge_max;
    ctx->segmented = strcmp(def->gauge_bar_style, "segmented") == 0;
    ctx->segment_count = def->gauge_segment_count >= GAUGE_SEGMENT_COUNT_MIN &&
                                 def->gauge_segment_count <= GAUGE_SEGMENT_COUNT_MAX
                             ? def->gauge_segment_count
                             : GAUGE_SEGMENT_COUNT_DEFAULT;
    ctx->segment_value = gauge_range_min_i32(ctx);
    ctx->color_mode = gauge_color_mode_from_text(def->gauge_color_mode);
    /* Defaults if the color fields are unset: gradient defaults to the common case this was
     * built for (cold blue to hot red, e.g. a room temperature); range defaults to a green
     * "in range" against red "out of range" on both sides (e.g. a humidity target band). An
     * invalid/empty stored color falls back to the same defaults, not to black. */
    if (!gauge_parse_hex_color(def->gauge_color_low, &ctx->color_low)) {
        ctx->color_low = lv_color_hex(0x3B82F6); /* blue */
    }
    if (!gauge_parse_hex_color(def->gauge_color_high, &ctx->color_high)) {
        ctx->color_high = lv_color_hex(0xEF4444); /* red */
    }
    if (!gauge_parse_hex_color(def->gauge_target_color, &ctx->target_color)) {
        ctx->target_color = lv_color_hex(0x4FCB7E); /* green, same as the fill-level gradient's "full" */
    }
    ctx->target_low = def->gauge_target_low;
    ctx->target_high = def->gauge_target_high;
    if (ctx->target_high <= ctx->target_low) {
        ctx->target_low = ctx->range_min;
        ctx->target_high = ctx->range_max;
    }
    ctx->unavailable = false;

    lv_obj_t *title = ui_widget_name_create(card, !def->hide_name);
    lv_label_set_text(title, def->title[0] ? def->title : def->id);
    lv_obj_set_style_text_color(title, theme_default_color_text_muted(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    /* LV_LABEL_LONG_DOT only truncates to one line if the label also has an explicit
     * single-line height -- without it, LVGL wraps across as many lines as an auto-sized
     * height allows before ever truncating (the same bug found and fixed in w_slider.c). A
     * narrow vertical gauge with a long title ("Ladeleistung") wrapped into two lines instead
     * of truncating, found on a real dashboard, not just the widget gallery's own short test
     * titles ("Smooth"/"Segmented"/"Radial") which happened not to trigger it. The wrapped
     * height was also read back below to size the bar above it, so the bug compounded into a
     * wrong bar height too. */
    lv_obj_set_height(title, lv_font_get_line_height(ui_font_role(UI_TEXT_META)));
    ctx->title_label = title;

    lv_obj_t *value = lv_label_create(card);
    lv_label_set_text(value, "--");
    lv_obj_set_style_text_color(value, theme_default_color_text_primary(), LV_PART_MAIN);
    lv_obj_set_style_text_font(value, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    ui_text_record(value, UI_TEXT_KEY, true);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    ctx->value_label = value;
    snprintf(ctx->value_text, sizeof(ctx->value_text), "--");

    /* The radial and horizontal looks have room for two extra readouts. */
    for (int i = 0; ctx->style != GAUGE_STYLE_LINEAR && i < 2; i++) {
        lv_obj_t *label = lv_label_create(card);
        lv_obj_set_style_text_color(label, theme_default_color_text_muted(), LV_PART_MAIN);
        ui_text_set(label, UI_TEXT_VALUE, true);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_label_set_text(label, "--");
        lv_obj_set_hidden(label, true);
        ctx->info_label[i] = label;
        snprintf(ctx->info_text[i], sizeof(ctx->info_text[i]), "--");
    }
    if (ctx->style == GAUGE_STYLE_RADIAL) {
        ctx->arc = lv_arc_create(card);
        gauge_style_arc(ctx->arc);
    } else if (ctx->segmented) {
        gauge_style_segments(ctx, card);
    } else {
        ctx->bar = lv_bar_create(card);
        gauge_style_bar(ctx, ctx->bar);
    }

    gauge_apply_range(ctx);
    const int32_t min = gauge_range_min_i32(ctx);
    if (ctx->bar != NULL) {
        lv_bar_set_value(ctx->bar, min, LV_ANIM_OFF);
    }
    if (ctx->arc != NULL) {
        lv_arc_set_value(ctx->arc, min);
        /* Same neutral placeholder color as gauge_set_unavailable_visual(), for the same reason:
         * no real value has arrived yet at creation time. */
        lv_obj_set_style_arc_color(ctx->arc, ui_style_fill_level_color(min, min, gauge_range_max_i32(ctx)), LV_PART_INDICATOR);
    }
    if (ctx->segmented) {
        gauge_update_segments(ctx, min);
    }

    lv_obj_add_event_cb(card, w_gauge_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, w_gauge_event_cb, LV_EVENT_SIZE_CHANGED, ctx);

    gauge_apply_layout(ctx);

    out_instance->obj = card;
    out_instance->ctx = ctx;
    return ESP_OK;
}

void w_gauge_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->obj == NULL || state == NULL) {
        return;
    }
    w_gauge_ctx_t *ctx = (w_gauge_ctx_t *)instance->ctx;
    if (ctx == NULL) {
        return;
    }

    /* Radial mode's secondary_entity_id / tertiary_entity_id are informational text only: they
     * never drive the arc and never mark the whole tile unavailable. */
    for (int i = 0; i < 2; i++) {
        if (ctx->info_label[i] != NULL && ctx->extra_entity_id[i][0] != '\0' &&
            strncmp(state->entity_id, ctx->extra_entity_id[i], APP_MAX_ENTITY_ID_LEN) == 0) {
            char text[64] = {0};
            if (gauge_state_is_unavailable(state->state)) {
                snprintf(text, sizeof(text), "%s", ui_i18n_get("common.unavailable", "unavailable"));
            } else {
                gauge_format_state_text(state, text, sizeof(text));
            }
            snprintf(ctx->info_text[i], sizeof(ctx->info_text[i]), "%s", text[0] ? text : "--");
            lv_label_set_text(ctx->info_label[i], ctx->info_text[i]);
            gauge_apply_layout(ctx);
            return;
        }
    }

    if (gauge_state_is_unavailable(state->state)) {
        ctx->unavailable = true;
        gauge_set_unavailable_visual(ctx);
        gauge_apply_layout(ctx);
        return;
    }

    double numeric = 0.0;
    bool has_numeric = gauge_parse_number(state->state, &numeric);
    char text[96] = {0};
    gauge_format_state_text(state, text, sizeof(text));

    ctx->unavailable = false;
    gauge_set_value_text(ctx, text[0] ? text : "--");
    const int32_t clamped =
        has_numeric ? gauge_clamp_i32(numeric, gauge_range_min_i32(ctx), gauge_range_max_i32(ctx)) : gauge_range_min_i32(ctx);
    if (ctx->bar != NULL) {
        lv_bar_set_value(ctx->bar, clamped, has_numeric ? LV_ANIM_ON : LV_ANIM_OFF);
        /* Only GAUGE_COLOR_RANGE needs this: fill/gradient are one static bar gradient set once
         * at creation (gauge_style_bar()) that just reveals more or less of itself as the value
         * changes, unlike the arc and segments above which are always recolored per update. */
        if (ctx->color_mode == GAUGE_COLOR_RANGE) {
            lv_obj_set_style_bg_color(
                ctx->bar, gauge_value_color(ctx, clamped, gauge_range_min_i32(ctx), gauge_range_max_i32(ctx)), LV_PART_INDICATOR);
        }
    }
    if (ctx->arc != NULL) {
        lv_arc_set_value(ctx->arc, clamped);
        lv_obj_set_style_arc_color(
            ctx->arc, gauge_value_color(ctx, clamped, gauge_range_min_i32(ctx), gauge_range_max_i32(ctx)), LV_PART_INDICATOR);
    }
    if (ctx->segmented) {
        gauge_update_segments(ctx, clamped);
    }
    gauge_apply_layout(ctx);
}

void w_gauge_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->obj == NULL) {
        return;
    }
    w_gauge_ctx_t *ctx = (w_gauge_ctx_t *)instance->ctx;
    if (ctx == NULL) {
        return;
    }
    ctx->unavailable = true;
    gauge_set_unavailable_visual(ctx);
    gauge_apply_layout(ctx);
}
