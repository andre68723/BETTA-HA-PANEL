/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Energy timeline (energy_timeline): rings for home use, feed-in, solar production and the battery, above a 24 hour
 * timeline of 96 bars of 15 minutes (home use stacked by source above the axis, feed-in and battery charging below,
 * an optional solar forecast as a line). Independent of the Energy page: it reads its own Recorder statistics through
 * ha_energy_timeline.c; only the battery's state of charge and the forecast are live entity states. A tap opens the
 * details popup. style_variant "nested" draws the rings inside each other (after the reference's style 4) with the values
 * beside them, and the timeline below when the card is tall enough. It scales from a 300x200 card to a whole page.
 * Visual idea: aharren/ios-energy-widget (MIT); this is a fresh LVGL implementation.
 */
#include "ui/ui_widget_factory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ha/ha_energy_timeline.h"
#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_energy_timeline_popup.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"

#define ETL_PAD 10
#define ETL_TICKS 4
#define ETL_POLL_MS 2000
#define ETL_RING_COUNT 4
#define ETL_SEGMENTS 3
#define ETL_CHIPS 6
#define ETL_ROWS 8
#define ETL_ROWS_DEFAULT 5 /* the rows beside a ring or two, in the default look */

typedef enum { ETL_RING_HOME = 0, ETL_RING_EXPORT, ETL_RING_SOLAR, ETL_RING_BATTERY } etl_ring_kind_t;

struct w_etl_ctx;
typedef struct {
    struct w_etl_ctx *ctx;
    int index;
} etl_ring_ref_t;

typedef struct w_etl_ctx {
    char widget_id[APP_MAX_WIDGET_ID_LEN];
    ui_energy_timeline_popup_info_t info;
    ha_energy_timeline_snapshot_t *snap;
    uint32_t shown_version;
    bool has_snapshot;
    bool has_soc;
    float soc;
    char forecast_attrs[APP_HA_ATTRS_MAX_LEN];
    et_forecast_t forecast;
    lv_timer_t *timer;
    lv_obj_t *card;
    lv_obj_t *title_label;
    lv_obj_t *status_label;
    /* rings: which of them exist follows the configuration; segments are set by render */
    bool ring_used[ETL_RING_COUNT];
    float segment[ETL_RING_COUNT][ETL_SEGMENTS];
    uint32_t segment_color[ETL_RING_COUNT][ETL_SEGMENTS];
    etl_ring_ref_t ring_ref[ETL_RING_COUNT];
    lv_obj_t *ring[ETL_RING_COUNT];
    lv_obj_t *ring_value[ETL_RING_COUNT];
    lv_obj_t *ring_unit[ETL_RING_COUNT];
    lv_obj_t *ring_caption[ETL_RING_COUNT];
    lv_obj_t *row_dot[ETL_ROWS];
    lv_obj_t *row_label[ETL_ROWS];
    lv_obj_t *chip_dot[ETL_CHIPS];
    lv_obj_t *chip_label[ETL_CHIPS];
    lv_obj_t *plot;
    lv_obj_t *tick[ETL_TICKS];
    bool rows_mode;
    bool legend_row;
    bool nested;        /* style_variant "nested": the rings inside each other */
    bool narrow;        /* nested, but the rows beside the rings are too narrow for full names */
    int rows_fit;       /* how many value rows the layout has room for */
    lv_obj_t *nest;     /* the one object that draws them */
} w_etl_ctx_t;

static void etl_format_kwh(char *out, size_t len, float kwh)
{
    snprintf(out, len, kwh >= 100.0f ? "%.0f" : "%.1f", (double)kwh);
}

/* ---- the 96 bars ------------------------------------------------------------------------------------------ */

static void etl_fill(lv_layer_t *layer, lv_draw_rect_dsc_t *dsc, uint32_t color, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    if (y2 < y1) return;
    dsc->bg_color = lv_color_hex(color);
    const lv_area_t bar = {(lv_coord_t)x1, (lv_coord_t)y1, (lv_coord_t)x2, (lv_coord_t)y2};
    lv_draw_rect(layer, dsc, &bar);
}

static void etl_plot_draw_cb(lv_event_t *e)
{
    w_etl_ctx_t *ctx = (w_etl_ctx_t *)lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    if (ctx == NULL || ctx->snap == NULL || layer == NULL || !ctx->has_snapshot) {
        return;
    }
    lv_area_t area;
    lv_obj_get_coords(ctx->plot, &area);
    const int32_t w = lv_area_get_width(&area);
    const int32_t h = lv_area_get_height(&area);
    if (w < ET_SLOTS || h < 8) {
        return;
    }
    const et_series_t *series = &ctx->snap->series;
    et_slot_t slots[ET_SLOTS];
    float max_up = 0.0f, max_down = 0.0f;
    for (size_t i = 0; i < ET_SLOTS; i++) {
        et_slot(series, i, &slots[i]);
        if (slots[i].state == ET_SLOT_EMPTY) continue;
        if (slots[i].home_kwh > max_up) max_up = slots[i].home_kwh;
        const float down = slots[i].export_kwh + slots[i].charge_kwh;
        if (down > max_down) max_down = down;
    }
    if (ctx->forecast.valid) {
        for (size_t i = 0; i < ET_SLOTS; i++) {
            if (ctx->forecast.has[i] && ctx->forecast.kwh[i] > max_up) max_up = ctx->forecast.kwh[i];
        }
    }
    float span = max_up + max_down;
    if (!(span > 0.0f)) span = 1.0f;
    const float px_per_kwh = (float)(h - 2) / span;
    const int32_t axis_y = area.y1 + 1 + (int32_t)(max_up * px_per_kwh + 0.5f);

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = 0;
    dsc.border_width = 0;
    lv_draw_rect_dsc_t axis;
    lv_draw_rect_dsc_init(&axis);
    axis.bg_color = lv_color_hex(APP_UI_COLOR_TEXT_MUTED);
    axis.bg_opa = LV_OPA_40;
    const lv_area_t axis_area = {area.x1, (lv_coord_t)axis_y, area.x2, (lv_coord_t)axis_y};
    lv_draw_rect(layer, &axis, &axis_area);

    for (size_t i = 0; i < ET_SLOTS; i++) {
        if (slots[i].state == ET_SLOT_EMPTY) continue;
        const int32_t x1 = area.x1 + (int32_t)(i * (size_t)w / ET_SLOTS);
        int32_t x2 = area.x1 + (int32_t)((i + 1) * (size_t)w / ET_SLOTS) - 2; /* one pixel between bars */
        if (x2 < x1) x2 = x1;
        dsc.bg_opa = slots[i].state == ET_SLOT_COMPLETE ? LV_OPA_COVER : LV_OPA_60; /* a partial slot is lighter */
        const float up[3] = {slots[i].home_from_solar, slots[i].home_from_battery, slots[i].home_from_grid};
        const uint32_t up_color[3] = {ctx->info.palette.value[ETL_SOLAR], ctx->info.palette.value[ETL_BATTERY_OUT], ctx->info.palette.value[ETL_GRID]};
        int32_t y = axis_y;
        for (int p = 0; p < 3; p++) {
            const int32_t bar_h = (int32_t)(up[p] * px_per_kwh + 0.5f);
            if (bar_h <= 0) continue;
            etl_fill(layer, &dsc, up_color[p], x1, y - bar_h, x2, y - 1);
            y -= bar_h;
        }
        const float down[2] = {slots[i].export_kwh, slots[i].charge_kwh};
        const uint32_t down_color[2] = {ctx->info.palette.value[ETL_EXPORT], ctx->info.palette.value[ETL_BATTERY_IN]};
        y = axis_y + 1;
        for (int p = 0; p < 2; p++) {
            const int32_t bar_h = (int32_t)(down[p] * px_per_kwh + 0.5f);
            if (bar_h <= 0) continue;
            etl_fill(layer, &dsc, down_color[p], x1, y, x2, y + bar_h - 1);
            y += bar_h;
        }
    }

    /* The forecast: a line through the slots it covers; a hole in the forecast breaks the line. */
    if (ctx->forecast.valid) {
        lv_draw_line_dsc_t line;
        lv_draw_line_dsc_init(&line);
        line.color = lv_color_hex(ctx->info.palette.value[ETL_FORECAST]);
        line.width = w >= 500 ? 3 : 2;
        line.opa = LV_OPA_COVER;
        for (size_t i = 0; i + 1 < ET_SLOTS; i++) {
            if (!ctx->forecast.has[i] || !ctx->forecast.has[i + 1]) continue;
            line.p1.x = area.x1 + (int32_t)(i * (size_t)w / ET_SLOTS) + w / (2 * ET_SLOTS);
            line.p1.y = axis_y - (int32_t)(ctx->forecast.kwh[i] * px_per_kwh + 0.5f);
            line.p2.x = area.x1 + (int32_t)((i + 1) * (size_t)w / ET_SLOTS) + w / (2 * ET_SLOTS);
            line.p2.y = axis_y - (int32_t)(ctx->forecast.kwh[i + 1] * px_per_kwh + 0.5f);
            lv_draw_line(layer, &line);
        }
    }
}

/* ---- the rings -------------------------------------------------------------------------------------------- */

static void etl_ring_draw_cb(lv_event_t *e)
{
    etl_ring_ref_t *ref = (etl_ring_ref_t *)lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    if (ref == NULL || layer == NULL) {
        return;
    }
    w_etl_ctx_t *ctx = ref->ctx;
    lv_obj_t *obj = ctx->ring[ref->index];
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    const int32_t d = lv_area_get_width(&area);
    const int32_t width = d >= 150 ? 16 : (d >= 90 ? 12 : 8);
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center.x = area.x1 + d / 2;
    arc.center.y = area.y1 + d / 2;
    arc.radius = (uint16_t)(d / 2);
    arc.width = (uint16_t)width;
    arc.opa = LV_OPA_COVER;
    arc.color = lv_color_hex(APP_UI_COLOR_CARD_BORDER); /* the track */
    arc.start_angle = 0;
    arc.end_angle = 359;
    lv_draw_arc(layer, &arc);
    float start = 270.0f;
    for (int i = 0; i < ETL_SEGMENTS; i++) {
        const float share = ctx->segment[ref->index][i];
        if (!(share > 0.001f)) continue;
        float end = start + share * 360.0f;
        if (end > start + 359.0f) end = start + 359.0f;
        arc.color = lv_color_hex(ctx->segment_color[ref->index][i]);
        arc.start_angle = (lv_value_precise_t)start;
        arc.end_angle = (lv_value_precise_t)end;
        lv_draw_arc(layer, &arc);
        start = end;
    }
}

/* Nested rings, outermost first: battery level, feed-in, solar production, home use (the reference's order). */
static int etl_nested_kinds(const w_etl_ctx_t *ctx, int kinds[ETL_RING_COUNT])
{
    static const int order[ETL_RING_COUNT] = {ETL_RING_BATTERY, ETL_RING_EXPORT, ETL_RING_SOLAR, ETL_RING_HOME};
    int n = 0;
    for (int i = 0; i < ETL_RING_COUNT; i++) {
        if (ctx->ring_used[order[i]]) kinds[n++] = order[i];
    }
    return n;
}

static void etl_nested_geometry(lv_coord_t d, int n, lv_coord_t *width, lv_coord_t *step, lv_coord_t *hole)
{
    const lv_coord_t r = d / 2;
    const lv_coord_t s = n > 0 ? (r - r * 32 / 100) / n : r;
    lv_coord_t wd = s * 58 / 100;
    if (wd < 4) wd = 4;
    *width = wd;
    *step = s;
    *hole = r - n * s; /* radius of the empty centre, measured to the inside of the innermost ring's slot */
}

static void etl_nested_draw_cb(lv_event_t *e)
{
    w_etl_ctx_t *ctx = (w_etl_ctx_t *)lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    if (ctx == NULL || layer == NULL) {
        return;
    }
    lv_area_t area;
    lv_obj_get_coords(ctx->nest, &area);
    const int32_t d = lv_area_get_width(&area);
    int kinds[ETL_RING_COUNT];
    const int n = etl_nested_kinds(ctx, kinds);
    lv_coord_t width, step, hole;
    etl_nested_geometry((lv_coord_t)d, n, &width, &step, &hole);
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center.x = area.x1 + d / 2;
    arc.center.y = area.y1 + d / 2;
    arc.width = (uint16_t)width;
    arc.opa = LV_OPA_COVER;
    for (int i = 0; i < n; i++) {
        arc.radius = (uint16_t)(d / 2 - i * step);
        arc.color = lv_color_hex(APP_UI_COLOR_CARD_BORDER); /* the track */
        arc.start_angle = 0;
        arc.end_angle = 359;
        lv_draw_arc(layer, &arc);
        float start = 270.0f;
        for (int k = 0; k < ETL_SEGMENTS; k++) {
            const float share = ctx->segment[kinds[i]][k];
            if (!(share > 0.001f)) continue;
            float end = start + share * 360.0f;
            if (end > start + 359.0f) end = start + 359.0f;
            arc.color = lv_color_hex(ctx->segment_color[kinds[i]][k]);
            arc.start_angle = (lv_value_precise_t)start;
            arc.end_angle = (lv_value_precise_t)end;
            lv_draw_arc(layer, &arc);
            start = end;
        }
    }
}

/* ---- layout ----------------------------------------------------------------------------------------------- */

/* style_variant "nested": the rings inside each other at the left, one row per value beside them, and the timeline
 * below when the card is tall enough for it. */
static void etl_layout_nested(w_etl_ctx_t *ctx, lv_coord_t w, lv_coord_t h, int rings)
{
    const lv_coord_t pad = ETL_PAD;
    const lv_font_t *font16 = ui_font_role(UI_TEXT_META);
    const lv_coord_t line = lv_font_get_line_height(font16);
    const lv_coord_t title_h = ui_widget_name_height(ctx->title_label, font16);
    const lv_coord_t top = pad + (title_h > 0 ? title_h : line) + 4;
    const lv_coord_t inner_h = h - top - pad;
    const lv_font_t *row_font = h >= 380 ? ui_font_role(UI_TEXT_META) : (h >= 300 ? ui_font_role(UI_TEXT_META) : font16);
    const lv_coord_t row_h = lv_font_get_line_height(row_font) + 2;
    const bool with_plot = inner_h >= 330;
    lv_coord_t block_h = with_plot ? inner_h * 55 / 100 : inner_h;
    lv_coord_t d = block_h;
    if (d > w * (w < 400 ? 40 : 46) / 100) d = w * (w < 400 ? 40 : 46) / 100;
    if (d < 60) d = 60;
    lv_obj_set_hidden(ctx->nest, false);
    lv_obj_set_size(ctx->nest, d, d);
    lv_obj_set_pos(ctx->nest, pad, top + (block_h - d) / 2);

    /* the four separate rings are not used here; the home use total sits in the centre when it fits */
    lv_coord_t width, step, hole;
    etl_nested_geometry(d, rings, &width, &step, &hole);
    for (int i = 0; i < ETL_RING_COUNT; i++) {
        lv_obj_set_hidden(ctx->ring[i], true);
        lv_obj_set_hidden(ctx->ring_caption[i], true);
        const bool centre = i == ETL_RING_HOME && hole >= 30;
        lv_obj_set_hidden(ctx->ring_value[i], !centre);
        lv_obj_set_hidden(ctx->ring_unit[i], !centre || hole < 44);
    }
    const lv_font_t *value_font = hole >= 70 ? ui_font_role(UI_TEXT_VALUE) : (hole >= 44 ? ui_font_role(UI_TEXT_META) : font16);
    const lv_coord_t vh = lv_font_get_line_height(value_font), uh = hole >= 44 ? line : 0;
    const lv_coord_t cx = pad + d / 2, cy = top + block_h / 2;
    lv_obj_set_style_text_font(ctx->ring_value[ETL_RING_HOME], value_font, LV_PART_MAIN);
    lv_obj_set_size(ctx->ring_value[ETL_RING_HOME], 2 * hole, vh);
    lv_obj_set_pos(ctx->ring_value[ETL_RING_HOME], cx - hole, cy - (vh + uh) / 2);
    lv_obj_set_size(ctx->ring_unit[ETL_RING_HOME], 2 * hole, uh > 0 ? uh : 1);
    lv_obj_set_pos(ctx->ring_unit[ETL_RING_HOME], cx - hole, cy - (vh + uh) / 2 + vh);

    /* one row per value, as many as fit */
    const lv_coord_t rows_x = pad + d + 2 * pad;
    const lv_coord_t rows_w = w - rows_x - pad;
    ctx->narrow = rows_w < 210;
    int fit = (int)(block_h / row_h);
    if (fit > ETL_ROWS) fit = ETL_ROWS;
    ctx->rows_fit = fit;
    const lv_coord_t dot = row_h / 2 - 2;
    for (int i = 0; i < ETL_ROWS; i++) {
        const bool shown = i < fit;
        lv_obj_set_hidden(ctx->row_dot[i], !shown);
        lv_obj_set_hidden(ctx->row_label[i], !shown);
        if (!shown) continue;
        const lv_coord_t y = top + (block_h - fit * row_h) / 2 + i * row_h;
        lv_obj_set_size(ctx->row_dot[i], dot, dot);
        lv_obj_set_pos(ctx->row_dot[i], rows_x, y + (row_h - dot) / 2);
        lv_obj_set_style_text_font(ctx->row_label[i], row_font, LV_PART_MAIN);
        lv_obj_set_size(ctx->row_label[i], rows_w - dot - 6 > 40 ? rows_w - dot - 6 : 40, row_h);
        lv_obj_set_pos(ctx->row_label[i], rows_x + dot + 6, y);
    }
    ctx->rows_mode = true;
    ctx->legend_row = false;
    for (int i = 0; i < ETL_CHIPS; i++) {
        lv_obj_set_hidden(ctx->chip_dot[i], true);
        lv_obj_set_hidden(ctx->chip_label[i], true);
    }

    lv_obj_set_hidden(ctx->plot, !with_plot);
    const lv_coord_t plot_y = top + block_h + 6;
    for (int i = 0; i < ETL_TICKS; i++) lv_obj_set_hidden(ctx->tick[i], !with_plot);
    if (!with_plot) return;
    const lv_coord_t plot_w = w - 2 * pad;
    lv_obj_set_size(ctx->plot, plot_w, inner_h - block_h - 6 - line - 2);
    lv_obj_set_pos(ctx->plot, pad, plot_y);
    for (int i = 0; i < ETL_TICKS; i++) {
        lv_obj_set_size(ctx->tick[i], 64, line);
        lv_obj_set_pos(ctx->tick[i], pad + (lv_coord_t)((int32_t)plot_w * i * (ET_SLOTS / ETL_TICKS) / ET_SLOTS),
                       top + inner_h - line);
    }
}

static void etl_layout(w_etl_ctx_t *ctx)
{
    lv_obj_update_layout(ctx->card);
    const lv_coord_t w = lv_obj_get_width(ctx->card);
    const lv_coord_t h = lv_obj_get_height(ctx->card);
    const lv_coord_t pad = ETL_PAD;
    const lv_font_t *font16 = ui_font_role(UI_TEXT_META);
    const lv_coord_t line = lv_font_get_line_height(font16);

    const lv_coord_t title_h = ui_widget_name_height(ctx->title_label, font16);
    const lv_coord_t status_w = w * 45 / 100;
    lv_obj_set_size(ctx->title_label, w - 2 * pad - status_w - 6, title_h > 0 ? title_h : 1);
    lv_obj_set_pos(ctx->title_label, pad, pad);
    lv_obj_set_style_text_align(ctx->status_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_size(ctx->status_label, status_w, line);
    lv_obj_set_pos(ctx->status_label, w - pad - status_w, pad);

    int rings = 0;
    for (int i = 0; i < ETL_RING_COUNT; i++) rings += ctx->ring_used[i] ? 1 : 0;
    if (rings < 1) rings = 1;
    ctx->rows_mode = rings <= 2;
    if (ctx->nested) {
        etl_layout_nested(ctx, w, h, rings);
        return;
    }
    lv_obj_set_hidden(ctx->nest, true);
    lv_obj_set_hidden(ctx->plot, false);
    for (int i = 0; i < ETL_TICKS; i++) lv_obj_set_hidden(ctx->tick[i], false);

    const lv_coord_t top = pad + (title_h > 0 ? title_h : line) + 4;
    const lv_coord_t inner_h = h - top - pad;
    const lv_font_t *row_font = h >= 380 ? ui_font_role(UI_TEXT_META) : (h >= 300 ? ui_font_role(UI_TEXT_META) : font16);
    const lv_coord_t row_h = lv_font_get_line_height(row_font) + 2;
    const lv_coord_t tick_h = line;
    const bool legend_row = !ctx->rows_mode && (h >= 260 || (w >= 500 && h >= 220));
    ctx->legend_row = legend_row;
    const lv_coord_t legend_h = legend_row ? line + 4 : 0;
    const lv_coord_t caption_h = ctx->rows_mode ? 0 : line;

    lv_coord_t block_h = inner_h * 45 / 100;
    if (block_h < 60 + caption_h) block_h = 60 + caption_h;
    if (block_h > 300) block_h = 300;
    lv_coord_t plot_total = inner_h - block_h - legend_h - 6;
    if (plot_total < tick_h + 24) { /* very short card: give the plot what the rings can spare */
        block_h -= (tick_h + 24) - plot_total;
        plot_total = tick_h + 24;
    }
    const lv_coord_t ring_area_h = block_h - caption_h;

    lv_coord_t cell_w, ring_d;
    if (ctx->rows_mode) {
        ring_d = ring_area_h;
        if (ring_d > w * 38 / 100 / rings) ring_d = w * 38 / 100 / rings;
        if (ring_d < 56) ring_d = 56;
        cell_w = ring_d + 6;
    } else {
        cell_w = (w - 2 * pad) / rings;
        ring_d = cell_w - 10;
        if (ring_d > ring_area_h) ring_d = ring_area_h;
        if (ring_d < 44) ring_d = 44;
    }
    const lv_font_t *value_font = ring_d >= 190 ? ui_font_role(UI_TEXT_KEY)
                                : (ring_d >= 110 ? ui_font_role(UI_TEXT_VALUE) : (ring_d >= 70 ? ui_font_role(UI_TEXT_META) : font16));
    const lv_font_t *unit_font = ring_d >= 110 ? ui_font_role(UI_TEXT_META) : font16;
    const lv_coord_t arc_w = ring_d >= 150 ? 16 : (ring_d >= 90 ? 12 : 8);
    const lv_coord_t vh = lv_font_get_line_height(value_font);
    const lv_coord_t uh = ring_d >= 70 ? lv_font_get_line_height(unit_font) : 0;

    int slot = 0;
    for (int i = 0; i < ETL_RING_COUNT; i++) {
        const bool used = ctx->ring_used[i];
        lv_obj_set_hidden(ctx->ring[i], !used);
        lv_obj_set_hidden(ctx->ring_value[i], !used);
        lv_obj_set_hidden(ctx->ring_unit[i], !used || uh == 0);
        lv_obj_set_hidden(ctx->ring_caption[i], !used || ctx->rows_mode);
        if (!used) continue;
        const lv_coord_t cx = pad + slot * cell_w + cell_w / 2;
        const lv_coord_t rx = cx - ring_d / 2;
        const lv_coord_t ry = top + (ring_area_h - ring_d) / 2;
        lv_obj_set_size(ctx->ring[i], ring_d, ring_d);
        lv_obj_set_pos(ctx->ring[i], rx, ry);
        const lv_coord_t inner = ring_d - 2 * arc_w - 2;
        lv_obj_set_style_text_font(ctx->ring_value[i], value_font, LV_PART_MAIN);
        lv_obj_set_style_text_font(ctx->ring_unit[i], unit_font, LV_PART_MAIN);
        const lv_coord_t text_top = ry + (ring_d - (vh + uh)) / 2;
        lv_obj_set_size(ctx->ring_value[i], inner, vh);
        lv_obj_set_pos(ctx->ring_value[i], rx + arc_w + 1, text_top);
        lv_obj_set_size(ctx->ring_unit[i], inner, uh > 0 ? uh : 1);
        lv_obj_set_pos(ctx->ring_unit[i], rx + arc_w + 1, text_top + vh);
        lv_obj_set_size(ctx->ring_caption[i], cell_w - 2, line);
        lv_obj_set_pos(ctx->ring_caption[i], cx - (cell_w - 2) / 2, top + ring_area_h + 1);
        slot++;
    }

    /* rows mode: the values to the right of the rings, as many rows as fit */
    const lv_coord_t rows_x = pad + slot * cell_w + pad;
    const lv_coord_t rows_w = w - rows_x - pad;
    int fit = (int)(block_h / row_h);
    if (fit > ETL_ROWS_DEFAULT) fit = ETL_ROWS_DEFAULT;
    ctx->rows_fit = fit;
    const lv_coord_t dot = row_h / 2 - 2;
    for (int i = 0; i < ETL_ROWS; i++) {
        const bool shown = ctx->rows_mode && i < fit;
        lv_obj_set_hidden(ctx->row_dot[i], !shown);
        lv_obj_set_hidden(ctx->row_label[i], !shown);
        if (!shown) continue;
        const lv_coord_t y = top + (block_h - fit * row_h) / 2 + i * row_h;
        lv_obj_set_size(ctx->row_dot[i], dot, dot);
        lv_obj_set_pos(ctx->row_dot[i], rows_x, y + (row_h - dot) / 2);
        lv_obj_set_style_text_font(ctx->row_label[i], row_font, LV_PART_MAIN);
        lv_obj_set_size(ctx->row_label[i], rows_w - dot - 6 > 40 ? rows_w - dot - 6 : 40, row_h);
        lv_obj_set_pos(ctx->row_label[i], rows_x + dot + 6, y);
    }

    /* rings mode: the colour legend row under the rings (its chips are placed by render) */
    for (int i = 0; i < ETL_CHIPS; i++) {
        lv_obj_set_hidden(ctx->chip_dot[i], !legend_row);
        lv_obj_set_hidden(ctx->chip_label[i], !legend_row);
    }

    const lv_coord_t plot_y = top + block_h + legend_h + 6;
    lv_obj_set_size(ctx->plot, w - 2 * pad, plot_total - tick_h - 2);
    lv_obj_set_pos(ctx->plot, pad, plot_y);
    const lv_coord_t plot_w = w - 2 * pad;
    for (int i = 0; i < ETL_TICKS; i++) {
        lv_obj_set_size(ctx->tick[i], 64, tick_h);
        lv_obj_set_pos(ctx->tick[i], pad + (lv_coord_t)((int32_t)plot_w * i * (ET_SLOTS / ETL_TICKS) / ET_SLOTS),
                       plot_y + plot_total - tick_h);
    }
}

/* ---- render ----------------------------------------------------------------------------------------------- */

static void etl_set_segments(w_etl_ctx_t *ctx, int ring, const float parts[ETL_SEGMENTS],
                             const uint32_t colors[ETL_SEGMENTS], float total)
{
    for (int i = 0; i < ETL_SEGMENTS; i++) {
        ctx->segment[ring][i] = et_share(parts[i], total);
        ctx->segment_color[ring][i] = colors[i];
    }
}

static void etl_render(w_etl_ctx_t *ctx)
{
    const lv_color_t muted = lv_color_hex(APP_UI_COLOR_TEXT_MUTED);
    const lv_color_t primary = lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY);
    const bool no_data = !ctx->has_snapshot || ctx->snap->state == HA_ET_LOADING || ctx->snap->state == HA_ET_NO_STATISTICS;
    et_totals_t t;
    memset(&t, 0, sizeof(t));
    char buf[80], value[16];

    /* the rings that exist: home always, the others as configured */
    const et_series_t *series = &ctx->snap->series;
    const bool has_export = ctx->info.stat_id[ET_CH_GRID_EXPORT][0] != '\0';
    const bool has_solar = ctx->info.stat_id[ET_CH_SOLAR][0] != '\0';
    const bool has_battery = ctx->info.stat_id[ET_CH_BATTERY_CHARGE][0] != '\0';
    const bool used[ETL_RING_COUNT] = {true, has_export, has_solar, ctx->info.soc_id[0] != '\0'};
    bool changed = false;
    for (int i = 0; i < ETL_RING_COUNT; i++) {
        changed |= ctx->ring_used[i] != used[i];
        ctx->ring_used[i] = used[i];
    }
    if (changed) etl_layout(ctx);

    lv_label_set_text(ctx->status_label, "");
    if (no_data) {
        lv_label_set_text(ctx->status_label, ctx->has_snapshot && ctx->snap->state == HA_ET_NO_STATISTICS
                                                 ? ui_i18n_get("energytl.nostats", "No statistics")
                                                 : ui_i18n_get("energytl.loading", "Loading..."));
    } else {
        et_totals(series, &t);
        if (ctx->snap->state == HA_ET_STALE) {
            lv_label_set_text(ctx->status_label, ui_i18n_get("energytl.stale", "Outdated"));
        } else {
            struct tm local;
            time_t at = (time_t)ctx->snap->updated_epoch;
            if (ctx->snap->updated_epoch > 0 && localtime_r(&at, &local)) {
                char clock[8];
                strftime(clock, sizeof(clock), "%H:%M", &local);
                snprintf(buf, sizeof(buf), ui_i18n_get("energytl.updated", "Updated %s"), clock);
                lv_label_set_text(ctx->status_label, buf);
            }
        }
    }
    lv_obj_set_style_text_color(ctx->status_label,
                                (!no_data && ctx->snap->state == HA_ET_STALE) ? lv_color_hex(ETL_COLOR_EXPORT) : muted,
                                LV_PART_MAIN);

    memset(ctx->segment, 0, sizeof(ctx->segment));
    const float home_parts[3] = {t.home_from_solar, t.home_from_battery, t.home_from_grid};
    const uint32_t home_colors[3] = {ctx->info.palette.value[ETL_SOLAR], ctx->info.palette.value[ETL_BATTERY_OUT], ctx->info.palette.value[ETL_GRID]};
    etl_set_segments(ctx, ETL_RING_HOME, home_parts, home_colors, t.home_kwh);
    const float export_parts[3] = {t.export_from_solar, t.export_from_battery, 0.0f};
    const uint32_t export_colors[3] = {ctx->info.palette.value[ETL_SOLAR], ctx->info.palette.value[ETL_BATTERY_OUT], ctx->info.palette.value[ETL_EXPORT]};
    etl_set_segments(ctx, ETL_RING_EXPORT, export_parts, export_colors, t.export_from_solar + t.export_from_battery);
    const float solar_parts[3] = {t.home_from_solar, t.battery_from_solar, t.export_from_solar};
    const uint32_t solar_colors[3] = {ctx->info.palette.value[ETL_HOME_SOLAR_DEST], ctx->info.palette.value[ETL_BATTERY_IN], ctx->info.palette.value[ETL_EXPORT]};
    etl_set_segments(ctx, ETL_RING_SOLAR, solar_parts, solar_colors,
                     t.home_from_solar + t.battery_from_solar + t.export_from_solar);
    const float soc_parts[3] = {ctx->has_soc ? ctx->soc : 0.0f, 0.0f, 0.0f};
    const uint32_t soc_colors[3] = {ctx->info.palette.value[ETL_BATTERY_OUT], ctx->info.palette.value[ETL_BATTERY_OUT], ctx->info.palette.value[ETL_BATTERY_OUT]};
    etl_set_segments(ctx, ETL_RING_BATTERY, soc_parts, soc_colors, 100.0f);

    const float totals[ETL_RING_COUNT] = {t.home_kwh, t.export_kwh, t.solar_kwh, 0.0f};
    const char *captions[ETL_RING_COUNT] = {
        ui_i18n_get("energytl.home_s", "Home"), ui_i18n_get("energytl.export_s", "Feed-in"),
        ui_i18n_get("energytl.solar_s", "Solar"), ui_i18n_get("energytl.battery_s", "Battery")};
    for (int i = 0; i < ETL_RING_COUNT; i++) {
        lv_label_set_text(ctx->ring_caption[i], captions[i]);
        if (i == ETL_RING_BATTERY) {
            if (ctx->has_soc) {
                snprintf(value, sizeof(value), "%.0f", (double)ctx->soc);
                lv_label_set_text(ctx->ring_value[i], value);
            } else {
                lv_label_set_text(ctx->ring_value[i], "--");
            }
            lv_label_set_text(ctx->ring_unit[i], "%");
        } else {
            if (no_data) {
                lv_label_set_text(ctx->ring_value[i], "--");
            } else {
                etl_format_kwh(value, sizeof(value), totals[i]);
                lv_label_set_text(ctx->ring_value[i], value);
            }
            lv_label_set_text(ctx->ring_unit[i], "kWh");
        }
        lv_obj_invalidate(ctx->ring[i]);
    }

    /* rows mode: the values next to the rings, in a fixed priority so a short card keeps the most useful */
    struct {
        const char *key, *fallback;
        const char *short_key, *short_fallback; /* used when the rows are narrow */
        float value;
        const char *unit;
        uint32_t color;
        bool show;
    } rows[ETL_ROWS] = {
        {"energytl.home", "Home use", "energytl.home_s", "Home", t.home_kwh, "kWh", APP_UI_COLOR_TEXT_PRIMARY, true},
        {"energytl.solar", "From solar", "energytl.c_solar", "Solar", t.home_from_solar, "kWh", ctx->info.palette.value[ETL_SOLAR], has_solar},
        {"energytl.grid", "From grid", "energytl.c_grid", "Grid", t.home_from_grid, "kWh", ctx->info.palette.value[ETL_GRID], true},
        {"energytl.export", "Feed-in", "energytl.c_export", "Feed-in", t.export_kwh, "kWh", ctx->info.palette.value[ETL_EXPORT], has_export},
        {"energytl.battery", "From battery", "energytl.c_battery", "Battery", t.home_from_battery, "kWh", ctx->info.palette.value[ETL_BATTERY_OUT], has_battery},
        /* only the nested look has room for these, and not when it is narrow */
        {"energytl.solar_meter", "Solar production", "energytl.solar_s", "Solar", t.solar_kwh, "kWh", ctx->info.palette.value[ETL_HOME_SOLAR_DEST],
         ctx->nested && !ctx->narrow && has_solar},
        {"energytl.charge", "Battery charge", "energytl.c_charge", "Charging", t.charge_kwh, "kWh", ctx->info.palette.value[ETL_BATTERY_IN],
         ctx->nested && !ctx->narrow && has_battery},
        {"energytl.soc", "Battery level", "energytl.battery_s", "Battery", ctx->soc, "%", ctx->info.palette.value[ETL_BATTERY_OUT],
         ctx->nested && ctx->has_soc},
    };
    int shown = 0;
    for (int i = 0; i < ETL_ROWS; i++) {
        if (!rows[i].show) continue;
        if (no_data && rows[i].unit[0] == 'k') {
            lv_label_set_text(ctx->row_label[shown], "");
        } else {
            etl_format_kwh(value, sizeof(value), rows[i].value);
            snprintf(buf, sizeof(buf), "%s %s %s",
                     ctx->narrow ? ui_i18n_get(rows[i].short_key, rows[i].short_fallback) : ui_i18n_get(rows[i].key, rows[i].fallback),
                     value, rows[i].unit);
            lv_label_set_text(ctx->row_label[shown], buf);
        }
        lv_obj_set_style_text_color(ctx->row_label[shown], primary, LV_PART_MAIN);
        lv_obj_set_style_bg_color(ctx->row_dot[shown], lv_color_hex(rows[i].color), LV_PART_MAIN);
        shown++;
    }
    for (int i = shown; i < ETL_ROWS; i++) lv_label_set_text(ctx->row_label[i], "");
    for (int i = 0; i < ETL_ROWS; i++) {
        lv_obj_set_hidden(ctx->row_dot[i], !(ctx->rows_mode && i < shown && i < ctx->rows_fit));
    }

    /* rings mode: legend chips, left to right until the width is used */
    if (!ctx->rows_mode) {
        const struct {
            const char *key, *fallback;
            uint32_t color;
            bool show;
        } chips[ETL_CHIPS] = {
            {"energytl.c_solar", "Solar", ctx->info.palette.value[ETL_SOLAR], has_solar},
            {"energytl.c_battery", "Battery", ctx->info.palette.value[ETL_BATTERY_OUT], has_battery},
            {"energytl.c_grid", "Grid", ctx->info.palette.value[ETL_GRID], true},
            {"energytl.c_export", "Feed-in", ctx->info.palette.value[ETL_EXPORT], has_export},
            {"energytl.c_charge", "Charging", ctx->info.palette.value[ETL_BATTERY_IN], has_battery},
            {"energytl.c_forecast", "Forecast", ctx->info.palette.value[ETL_FORECAST], ctx->forecast.valid},
        };
        const lv_coord_t w = lv_obj_get_width(ctx->card);
        const lv_coord_t line = lv_font_get_line_height(ui_font_role(UI_TEXT_META));
        lv_coord_t x = ETL_PAD;
        const lv_coord_t y = lv_obj_get_y(ctx->plot) - line - 6;
        for (int i = 0; i < ETL_CHIPS; i++) {
            lv_obj_set_hidden(ctx->chip_dot[i], true);
            lv_obj_set_hidden(ctx->chip_label[i], true);
            if (!chips[i].show || !ctx->legend_row) continue;
            lv_label_set_text(ctx->chip_label[i], ui_i18n_get(chips[i].key, chips[i].fallback));
            lv_point_t size;
            lv_text_get_size(&size, lv_label_get_text(ctx->chip_label[i]), ui_font_role(UI_TEXT_META), 0, 0, LV_COORD_MAX,
                             LV_TEXT_FLAG_NONE);
            if (x + 14 + size.x > w - ETL_PAD) continue;
            lv_obj_set_size(ctx->chip_dot[i], 10, 10);
            lv_obj_set_pos(ctx->chip_dot[i], x, y + line / 2 - 5);
            lv_obj_set_style_bg_color(ctx->chip_dot[i], lv_color_hex(chips[i].color), LV_PART_MAIN);
            lv_obj_set_size(ctx->chip_label[i], size.x + 2, line);
            lv_obj_set_pos(ctx->chip_label[i], x + 14, y);
            lv_obj_set_hidden(ctx->chip_dot[i], false);
            lv_obj_set_hidden(ctx->chip_label[i], false);
            x += 14 + size.x + 14;
        }
    }

    for (int i = 0; i < ETL_TICKS; i++) {
        if (no_data) {
            lv_label_set_text(ctx->tick[i], "");
            continue;
        }
        const time_t at = (time_t)(series->window_start + (int64_t)i * (ET_SLOTS / ETL_TICKS) * ET_SLOT_SECONDS);
        struct tm local;
        if (localtime_r(&at, &local)) {
            strftime(buf, sizeof(buf), "%H:%M", &local);
            lv_label_set_text(ctx->tick[i], buf);
        }
    }

    /* the popup's view of the live values */
    ctx->info.has_soc = ctx->has_soc;
    ctx->info.soc_percent = ctx->soc;
    ctx->info.has_forecast = ctx->forecast.valid;
    ctx->info.forecast_kwh = 0.0f;
    for (size_t i = 0; ctx->forecast.valid && i < ET_SLOTS; i++) {
        if (ctx->forecast.has[i]) ctx->info.forecast_kwh += ctx->forecast.kwh[i];
    }
    lv_obj_invalidate(ctx->plot);
    lv_obj_invalidate(ctx->nest);
}

/* ---- events and updates ----------------------------------------------------------------------------------- */

static void etl_refresh_forecast(w_etl_ctx_t *ctx)
{
    if (ctx->has_snapshot && ctx->forecast_attrs[0] != '\0') {
        et_forecast_from_attrs(ctx->forecast_attrs, ctx->snap->series.window_start, &ctx->forecast);
    } else {
        memset(&ctx->forecast, 0, sizeof(ctx->forecast));
    }
}

static void etl_poll_cb(lv_timer_t *timer)
{
    w_etl_ctx_t *ctx = (w_etl_ctx_t *)lv_timer_get_user_data(timer);
    ha_energy_timeline_snapshot_t next;
    if (!ha_energy_timeline_get(ctx->widget_id, &next)) {
        return;
    }
    if (ctx->has_snapshot && next.version == ctx->shown_version && next.state == ctx->snap->state) {
        return;
    }
    *ctx->snap = next;
    ctx->shown_version = next.version;
    ctx->has_snapshot = true;
    etl_refresh_forecast(ctx);
    etl_render(ctx);
}

static void etl_clicked_cb(lv_event_t *event)
{
    w_etl_ctx_t *ctx = (w_etl_ctx_t *)lv_event_get_user_data(event);
    if (ctx != NULL && ctx->has_snapshot) {
        ui_energy_timeline_popup_open(&ctx->info, ctx->snap);
    }
}

static void etl_event_cb(lv_event_t *event)
{
    w_etl_ctx_t *ctx = (w_etl_ctx_t *)lv_event_get_user_data(event);
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        if (ctx->timer != NULL) {
            lv_timer_delete(ctx->timer);
        }
        ha_energy_timeline_unregister(ctx->widget_id);
        free(ctx->snap);
        free(ctx);
    } else if (lv_event_get_code(event) == LV_EVENT_SIZE_CHANGED) {
        etl_layout(ctx);
        etl_render(ctx);
    }
}

static lv_obj_t *etl_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return label;
}

static lv_obj_t *etl_dot(lv_obj_t *parent)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_clickable(dot, false);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    return dot;
}

esp_err_t w_energy_timeline_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    w_etl_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx));
    ha_energy_timeline_snapshot_t *snap = ui_calloc_prefer_psram(1, sizeof(*snap));
    if (ctx == NULL || snap == NULL) {
        free(ctx);
        free(snap);
        return ESP_ERR_NO_MEM;
    }
    ctx->snap = snap;
    ctx->info.palette = def->energy_palette.initialized ? def->energy_palette : etl_palette_defaults();
    strlcpy(ctx->widget_id, def->id, sizeof(ctx->widget_id));
    strlcpy(ctx->info.title, def->title[0] ? def->title : "Energy", sizeof(ctx->info.title));
    strlcpy(ctx->info.stat_id[ET_CH_GRID_IMPORT], def->entity_id, sizeof(ctx->info.stat_id[0]));
    strlcpy(ctx->info.stat_id[ET_CH_GRID_EXPORT], def->secondary_entity_id, sizeof(ctx->info.stat_id[0]));
    strlcpy(ctx->info.stat_id[ET_CH_SOLAR], def->tertiary_entity_id, sizeof(ctx->info.stat_id[0]));
    strlcpy(ctx->info.stat_id[ET_CH_BATTERY_CHARGE], def->extra_entity_id[UI_ETL_EXTRA_BATTERY_CHARGE],
            sizeof(ctx->info.stat_id[0]));
    strlcpy(ctx->info.stat_id[ET_CH_BATTERY_DISCHARGE], def->extra_entity_id[UI_ETL_EXTRA_BATTERY_DISCHARGE],
            sizeof(ctx->info.stat_id[0]));
    strlcpy(ctx->info.soc_id, def->extra_entity_id[UI_ETL_EXTRA_BATTERY_SOC], sizeof(ctx->info.soc_id));
    strlcpy(ctx->info.forecast_id, def->forecast_today_entity_id, sizeof(ctx->info.forecast_id));
    /* Only with both battery counters is the battery part of the balance. */
    if (ctx->info.stat_id[ET_CH_BATTERY_CHARGE][0] == '\0' || ctx->info.stat_id[ET_CH_BATTERY_DISCHARGE][0] == '\0') {
        ctx->info.stat_id[ET_CH_BATTERY_CHARGE][0] = '\0';
        ctx->info.stat_id[ET_CH_BATTERY_DISCHARGE][0] = '\0';
    }

    ha_energy_timeline_config_t config = {0};
    for (int c = 0; c < ET_CH_COUNT; c++) {
        config.stat_id[c] = ctx->info.stat_id[c];
    }
    if (ha_energy_timeline_register(ctx->widget_id, &config) != ESP_OK) {
        free(ctx);
        free(snap);
        return ESP_ERR_NO_MEM;
    }

    lv_obj_t *card = lv_obj_create(parent);
    ctx->card = card;
    out_instance->obj = card;
    out_instance->ctx = ctx;
    out_instance->visible = true;
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, def->w, def->h);
    lv_obj_set_pos(card, def->x, def->y);
    lv_obj_set_scrollable(card, false);
    lv_obj_set_clickable(card, true);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_radius(card, APP_UI_CARD_RADIUS, LV_PART_MAIN);

    ctx->title_label = ui_widget_name_create(card, !def->hide_name);
    ui_style_title_fit(ctx->title_label, ctx->info.title);
    lv_obj_set_style_text_font(ctx->title_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->title_label, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->title_label, LV_LABEL_LONG_DOT);
    ctx->status_label = etl_label(card, ui_font_role(UI_TEXT_META), lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_TEXT_ALIGN_RIGHT);

    for (int i = 0; i < ETL_RING_COUNT; i++) {
        ctx->ring_ref[i] = (etl_ring_ref_t){ctx, i};
        ctx->ring[i] = lv_obj_create(card);
        lv_obj_remove_style_all(ctx->ring[i]);
        lv_obj_set_clickable(ctx->ring[i], false);
        lv_obj_add_event_cb(ctx->ring[i], etl_ring_draw_cb, LV_EVENT_DRAW_POST_END, &ctx->ring_ref[i]);
        ctx->ring_value[i] = etl_label(card, ui_font_role(UI_TEXT_META), lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_TEXT_ALIGN_CENTER);
        ctx->ring_unit[i] = etl_label(card, ui_font_role(UI_TEXT_META), lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_TEXT_ALIGN_CENTER);
        ctx->ring_caption[i] = etl_label(card, ui_font_role(UI_TEXT_META), lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_TEXT_ALIGN_CENTER);
    }
    for (int i = 0; i < ETL_ROWS; i++) {
        ctx->row_dot[i] = etl_dot(card);
        ctx->row_label[i] = etl_label(card, ui_font_role(UI_TEXT_META), lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_TEXT_ALIGN_LEFT);
    }
    for (int i = 0; i < ETL_CHIPS; i++) {
        ctx->chip_dot[i] = etl_dot(card);
        ctx->chip_label[i] = etl_label(card, ui_font_role(UI_TEXT_META), lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_TEXT_ALIGN_LEFT);
        lv_label_set_long_mode(ctx->chip_label[i], LV_LABEL_LONG_CLIP);
    }
    ctx->nested = strcmp(def->style_variant, "nested") == 0;
    ctx->nest = lv_obj_create(card);
    lv_obj_remove_style_all(ctx->nest);
    lv_obj_set_clickable(ctx->nest, false);
    lv_obj_set_hidden(ctx->nest, true);
    lv_obj_add_event_cb(ctx->nest, etl_nested_draw_cb, LV_EVENT_DRAW_POST_END, ctx);
    ctx->plot = lv_obj_create(card);
    lv_obj_remove_style_all(ctx->plot);
    lv_obj_set_clickable(ctx->plot, false);
    lv_obj_set_scrollable(ctx->plot, false);
    lv_obj_add_event_cb(ctx->plot, etl_plot_draw_cb, LV_EVENT_DRAW_POST_END, ctx);
    for (int i = 0; i < ETL_TICKS; i++) {
        ctx->tick[i] = etl_label(card, ui_font_role(UI_TEXT_META), lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_TEXT_ALIGN_LEFT);
    }
    ctx->ring_used[ETL_RING_HOME] = true;
    ctx->ring_used[ETL_RING_EXPORT] = ctx->info.stat_id[ET_CH_GRID_EXPORT][0] != '\0';
    ctx->ring_used[ETL_RING_SOLAR] = ctx->info.stat_id[ET_CH_SOLAR][0] != '\0';
    ctx->ring_used[ETL_RING_BATTERY] = ctx->info.soc_id[0] != '\0';

    lv_obj_add_event_cb(card, etl_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, etl_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    lv_obj_add_event_cb(card, etl_clicked_cb, LV_EVENT_CLICKED, ctx);
    etl_layout(ctx);
    etl_render(ctx);
    ctx->timer = lv_timer_create(etl_poll_cb, ETL_POLL_MS, ctx);
    etl_poll_cb(ctx->timer); /* the simulator's snapshot is there at once */
    return ESP_OK;
}

void w_energy_timeline_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->ctx == NULL || state == NULL) {
        return;
    }
    w_etl_ctx_t *ctx = (w_etl_ctx_t *)instance->ctx;
    if (ctx->info.soc_id[0] != '\0' && strncmp(state->entity_id, ctx->info.soc_id, APP_MAX_ENTITY_ID_LEN) == 0) {
        char *end = NULL;
        const float value = strtof(state->state, &end);
        ctx->has_soc = end != state->state && *end == '\0' && value >= 0.0f && value <= 100.0f;
        ctx->soc = ctx->has_soc ? value : 0.0f;
    } else if (ctx->info.forecast_id[0] != '\0' &&
               strncmp(state->entity_id, ctx->info.forecast_id, APP_MAX_ENTITY_ID_LEN) == 0) {
        strlcpy(ctx->forecast_attrs, state->attributes_json, sizeof(ctx->forecast_attrs));
        etl_refresh_forecast(ctx);
    } else {
        return;
    }
    etl_render(ctx);
}

void w_energy_timeline_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_etl_ctx_t *ctx = (w_etl_ctx_t *)instance->ctx;
    ctx->has_soc = false;
    ctx->forecast_attrs[0] = '\0';
    etl_refresh_forecast(ctx);
    etl_render(ctx);
}
