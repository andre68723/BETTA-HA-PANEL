/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Energy price forecast (energy_price_forecast): the electricity price of a day as one bar per interval, for a sensor whose attribute lists the prices of
 * today and, once published, tomorrow (see ha/ha_price_series.h for the accepted shapes: ha_epex_spot, the Day Ahead Optimizer, Nord Pool and
 * Energy Price Forecast sensors, Tibber and Nord Pool templates).
 *
 * The behaviour follows the Home Assistant community's price timeline card (Neisi/ha-price-timeline-card): a bar is orange when its price,
 * rounded to whole cents, is above the average and green otherwise; hours that are over are dimmed and the running interval is marked; the
 * average is the mean of the day's prices (in tenths of a cent) rounded to a cent, or a fixed value or a sensor of the layout; the cheap times
 * are the lowest quarter of each day's prices (rounded to 0.001 EUR/kWh), neighbours joined, at least an hour long and not over yet. The
 * options are the card's: average (fixed value or sensor), selector (Today/Tomorrow buttons, option price_selector) and cheap_times (shade and name the cheap
 * times). Nothing is copied from the card; this is the panel's own drawing and code.
 */
#include "ui/ui_widget_factory.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ha/ha_history_stats.h"
#include "ha/ha_price_series.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/theme/ui_typography.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"
#include "ui/ui_widget_name.h"

#define PF_AXIS_LABELS 4
#define PF_TICK_PERIOD_MS 30000
#define PF_PLAUSIBLE_EPOCH 1600000000LL /* 2020-09-13: an unsynchronised clock starts near 1970 */
#define PF_COLOR_CHEAP 0x4FCB7E
#define PF_COLOR_DEAR 0xF2A14E

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    char title[APP_MAX_NAME_LEN];
    ha_price_series_t series;
    bool has_series;
    int day_offset; /* 0 today, 1 tomorrow */
    /* the layout's options (the price timeline card's average, day_switch and cheap_times) */
    bool day_switch;
    bool cheap_times;
    bool line_mode;                           /* option price_line: a stepped line instead of filled bars */
    double avg_fixed;                         /* EUR/kWh, 0 = none */
    char avg_entity[APP_MAX_ENTITY_ID_LEN];   /* optional sensor whose state is the average, EUR/kWh */
    bool avg_sensor_valid;
    double avg_sensor;

    /* derived by pf_update() */
    int64_t now_s;
    int64_t day_start_s;
    int64_t day_end_s;
    int first;
    int count;
    int cur_slot;   /* slot of the series that is running now, -1 when none */
    int shown_slot; /* the slot named in the header: the running one today, the first of another day (no marker there) */
    double avg_cent; /* what the bars are compared with: the day's average, the fixed value or the sensor, in cent */
    ha_price_stats_t stats;
    ha_price_window_t windows[HA_PRICE_MAX_WINDOWS];     /* the ones not over yet */
    size_t window_count;
    const char *status; /* non-NULL: nothing to draw, this is why */

    lv_obj_t *card;
    lv_obj_t *title_label;
    lv_obj_t *price_label;
    lv_obj_t *slot_label;
    lv_obj_t *stats_label;
    lv_obj_t *unit_label;
    lv_obj_t *btn_today;
    lv_obj_t *btn_tomorrow;
    lv_obj_t *plot;
    lv_obj_t *axis_label[PF_AXIS_LABELS];
    int axis_x[PF_AXIS_LABELS]; /* pixel offset of the tick in the plot, -1 = no tick */
    int axis_hour[PF_AXIS_LABELS];
    lv_obj_t *cheap_label;
    lv_obj_t *status_label;
    lv_timer_t *timer;
} w_pf_ctx_t;

static void pf_comma(char *s)
{
    for (; *s != '\0'; s++) {
        if (*s == '.') *s = ',';
    }
}

static void pf_format_cent(char *out, size_t cap, double eur_per_kwh)
{
    snprintf(out, cap, "%.1f", eur_per_kwh * 100.0);
    pf_comma(out);
}

static void pf_format_clock(char *out, size_t cap, int64_t epoch_s)
{
    time_t t = (time_t)epoch_s;
    struct tm tm;
    if (localtime_r(&t, &tm) == NULL) {
        snprintf(out, cap, "--:--");
        return;
    }
    snprintf(out, cap, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

/* Recomputes what the widget shows from the series and the clock; never touches LVGL. */
static void pf_update(w_pf_ctx_t *ctx)
{
    ctx->now_s = (int64_t)time(NULL);
    ctx->status = NULL;
    ctx->count = 0;
    ctx->first = 0;
    ctx->cur_slot = -1;
    ctx->shown_slot = -1;
    ctx->window_count = 0;
    ctx->avg_cent = NAN;
    memset(&ctx->stats, 0, sizeof(ctx->stats));
    if (!ctx->day_switch) ctx->day_offset = 0; /* without the buttons only today is shown */
    if (!ctx->has_series) {
        ctx->status = ui_i18n_get("price.no_data", "No prices");
        return;
    }
    if (ctx->now_s < PF_PLAUSIBLE_EPOCH) {
        ctx->status = ui_i18n_get("price.no_clock", "No clock");
        return;
    }
    if (ctx->day_switch) {
        ctx->day_start_s = ha_stats_local_day_start(ctx->now_s, ctx->day_offset);
        ctx->day_end_s = ha_stats_local_day_start(ctx->now_s, ctx->day_offset + 1);
    } else {
        /* Without the day buttons the view is the 24 hours around now: the running interval sits in the middle, the past to its left and
         * the coming hours (into tomorrow once published) to its right. */
        const int64_t step = ctx->has_series && ctx->series.interval_s > 0 ? ctx->series.interval_s : 3600;
        ctx->day_start_s = ctx->now_s - ctx->now_s % step + step / 2 - 12 * 3600;
        /* A list that starts with today (the price sensors drop yesterday) leaves the left of that window empty while the late hours at its
         * right are cut off: let the window begin at the first priced interval instead. The running interval stays inside it, because that
         * start lies less than 12 hours before now. */
        for (int i = 0; i < ctx->series.count; i++) {
            if (ctx->series.price[i] == HA_PRICE_MISSING) continue;
            const int64_t first_s = ha_price_slot_start(&ctx->series, i);
            if (ctx->day_start_s < first_s && first_s <= ctx->now_s) ctx->day_start_s = first_s;
            break;
        }
        ctx->day_end_s = ctx->day_start_s + 24 * 3600;
    }
    if (ctx->day_start_s < 0 || ctx->day_end_s <= ctx->day_start_s) {
        ctx->status = ui_i18n_get("price.no_clock", "No clock");
        return;
    }
    ha_price_slot_range(&ctx->series, ctx->day_start_s, ctx->day_end_s, &ctx->first, &ctx->count);
    ctx->stats = ha_price_stats(&ctx->series, ctx->first, ctx->count);
    if (!ctx->stats.valid) {
        ctx->count = 0;
        ctx->status = ctx->day_offset > 0 ? ui_i18n_get("price.no_tomorrow", "Tomorrow not published yet")
                                          : ui_i18n_get("price.no_today", "No prices for today");
        return;
    }
    ctx->cur_slot = ha_price_slot_at(&ctx->series, ctx->now_s);

    /* The header names the running interval today and the first priced one of another day (which carries no marker), as the card does. */
    if (ctx->day_offset == 0 && ctx->cur_slot >= ctx->first && ctx->cur_slot < ctx->first + ctx->count &&
        !isnan(ha_price_value(&ctx->series, ctx->cur_slot))) {
        ctx->shown_slot = ctx->cur_slot;
    } else {
        for (int i = ctx->first; i < ctx->first + ctx->count; i++) {
            if (!isnan(ha_price_value(&ctx->series, i))) {
                ctx->shown_slot = i;
                break;
            }
        }
    }

    /* What the bars are compared with: the sensor, else the fixed value, else the day's own average (whole cents). */
    if (ctx->avg_sensor_valid) {
        ctx->avg_cent = ctx->avg_sensor * 100.0;
    } else if (ctx->avg_fixed > 0.0) {
        ctx->avg_cent = ctx->avg_fixed * 100.0;
    } else {
        ctx->avg_cent = ha_price_average_cent(&ctx->series, ctx->first, ctx->count);
    }

    if (ctx->cheap_times) {
        ctx->window_count = ha_price_cheap_windows(&ctx->series, ctx->now_s, ctx->windows, HA_PRICE_MAX_WINDOWS);
    }
}

static lv_coord_t pf_text_w(const char *text, const lv_font_t *font)
{
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

static void pf_button_style(lv_obj_t *btn, bool active)
{
    const ui_control_role_t role = active ? UI_CONTROL_SELECTED : UI_CONTROL_ACTION;
    ui_style_action_button(btn, NULL, role, true);
    lv_obj_set_style_text_color(btn, ui_style_button_icon_color(ui_style_control_color(role, true)), LV_PART_MAIN);
}

/* The cheap window to name: the first not yet over that touches the shown day, else the first one ahead on any day. */
static const ha_price_window_t *pf_pick_window(const w_pf_ctx_t *ctx, bool *other_day)
{
    *other_day = false;
    const ha_price_window_t *ahead = NULL;
    for (size_t i = 0; i < ctx->window_count; i++) {
        const ha_price_window_t *w = &ctx->windows[i];
        if (ahead == NULL) ahead = w;
        if (w->end_s > ctx->day_start_s && w->start_s < ctx->day_end_s) return w;
    }
    *other_day = ahead != NULL;
    return ahead;
}

/* Sizes and positions follow the line heights of the fonts in use; what does not fit is left out, not squeezed. */
static void pf_layout(w_pf_ctx_t *ctx)
{
    const lv_coord_t w = lv_obj_get_width(ctx->card);
    const lv_coord_t h = lv_obj_get_height(ctx->card);
    const lv_coord_t pad = 12, gap = 4;
    const lv_font_t *meta = ui_font_role(UI_TEXT_META);
    const lv_coord_t meta_h = lv_font_get_line_height(meta);
    lv_coord_t y = 8;

    lv_obj_set_width(ctx->title_label, w - 2 * pad);
    lv_obj_set_height(ctx->title_label, meta_h); /* DOT needs a one-line box; an auto-height label can wrap over the price. */
    lv_obj_align(ctx->title_label, LV_ALIGN_TOP_LEFT, pad, y);
    y += ui_widget_name_height(ctx->title_label, ui_font_role(UI_TEXT_META)) + ui_widget_name_gap(ctx->title_label, gap);

    /* Day buttons (option day_switch): both on a wide tile; on a narrow one a single button naming the day shown, a tap shows the other. */
    const char *today = ui_i18n_get("price.today", "Today");
    const char *tomorrow = ui_i18n_get("price.tomorrow", "Tomorrow");
    const lv_coord_t bpad = 8;
    const lv_coord_t bh = meta_h + 6;
    const bool single = w < 360;
    lv_coord_t buttons_w;
    if (!ctx->day_switch) {
        lv_obj_set_hidden(ctx->btn_today, true);
        lv_obj_set_hidden(ctx->btn_tomorrow, true);
        buttons_w = 0;
    } else if (single) {
        lv_obj_set_hidden(ctx->btn_today, false);
        const char *shown = ctx->day_offset == 0 ? today : tomorrow;
        const lv_coord_t bw = (pf_text_w(today, meta) > pf_text_w(tomorrow, meta) ? pf_text_w(today, meta) : pf_text_w(tomorrow, meta)) + 2 * bpad;
        lv_label_set_text(ctx->btn_today, shown);
        lv_obj_set_size(ctx->btn_today, bw, bh);
        lv_obj_align(ctx->btn_today, LV_ALIGN_TOP_RIGHT, -pad, y);
        lv_obj_set_hidden(ctx->btn_tomorrow, true);
        buttons_w = bw;
    } else {
        lv_obj_set_hidden(ctx->btn_today, false);
        const lv_coord_t b1 = pf_text_w(today, meta) + 2 * bpad;
        const lv_coord_t b2 = pf_text_w(tomorrow, meta) + 2 * bpad;
        lv_label_set_text(ctx->btn_today, today);
        lv_obj_set_size(ctx->btn_today, b1, bh);
        lv_obj_set_size(ctx->btn_tomorrow, b2, bh);
        lv_obj_align(ctx->btn_tomorrow, LV_ALIGN_TOP_RIGHT, -pad, y);
        lv_obj_align(ctx->btn_today, LV_ALIGN_TOP_RIGHT, -pad - b2 - 4, y);
        lv_obj_set_hidden(ctx->btn_tomorrow, false);
        buttons_w = b1 + b2 + 4;
    }

    /* The price now (or the day's average), its unit beside it and the interval under it. */
    char price_text[24] = "--";
    char slot_text[64] = "";
    const char *unit_text = "ct/kWh";
    if (ctx->stats.valid) {
        pf_format_cent(price_text, sizeof(price_text), ha_price_value(&ctx->series, ctx->shown_slot));
        char a[8], b[8];
        pf_format_clock(a, sizeof(a), ha_price_slot_start(&ctx->series, ctx->shown_slot));
        pf_format_clock(b, sizeof(b), ha_price_slot_start(&ctx->series, ctx->shown_slot) + ctx->series.interval_s);
        snprintf(slot_text, sizeof(slot_text), "%s-%s", a, b);
    }
    lv_label_set_text(ctx->price_label, price_text);
    lv_label_set_text(ctx->slot_label, slot_text);
    lv_label_set_text(ctx->unit_label, unit_text);
    lv_coord_t left_w = w - 2 * pad - buttons_w - (buttons_w > 0 ? 8 : 0);
    if (left_w < 40) left_w = 40;
    const lv_coord_t unit_w = pf_text_w(unit_text, meta);
    const lv_font_t *key_font = ui_font_role(UI_TEXT_KEY);
    const bool reserve_unit = pf_text_w(price_text, key_font) + 6 + unit_w <= left_w;
    const lv_font_t *price_font = ui_text_fit(price_text, key_font, reserve_unit ? left_w - unit_w - 6 : left_w, 64);
    lv_obj_set_style_text_font(ctx->price_label, price_font, LV_PART_MAIN);
    const lv_coord_t price_w = pf_text_w(price_text, price_font);
    const lv_coord_t ph = lv_font_get_line_height(price_font);
    lv_obj_set_width(ctx->price_label, price_w + 2);
    lv_obj_align(ctx->price_label, LV_ALIGN_TOP_LEFT, pad, y);
    const bool unit_fits = price_w + 6 + unit_w <= left_w;
    lv_obj_set_hidden(ctx->unit_label, !(unit_fits && ctx->stats.valid));
    lv_obj_set_width(ctx->unit_label, unit_w + 2);
    lv_obj_align(ctx->unit_label, LV_ALIGN_TOP_LEFT, pad + price_w + 6, y + ph - meta_h - 3);
    /* The interval gets its full row below both the price and the day buttons, even on the narrowest tile. */
    const lv_coord_t head_h = buttons_w > 0 && bh > ph ? bh : ph;
    lv_obj_set_width(ctx->slot_label, w - 2 * pad);
    lv_obj_align(ctx->slot_label, LV_ALIGN_TOP_LEFT, pad, y + head_h);
    y += head_h + meta_h;
    y += gap;

    /* Bottom block: axis labels, and one row with the next cheap window and the day's minimum and maximum. */
    char cheap_text[64] = "";
    char stats_text[48] = "";
    if (ctx->stats.valid && ctx->cheap_times) {
        bool other_day;
        const ha_price_window_t *win = pf_pick_window(ctx, &other_day);
        if (win != NULL) {
            char a[8], b[8];
            const int64_t ws = win->start_s < ctx->day_start_s && !other_day ? ctx->day_start_s : win->start_s;
            const int64_t we = win->end_s > ctx->day_end_s && !other_day ? ctx->day_end_s : win->end_s;
            pf_format_clock(a, sizeof(a), ws);
            pf_format_clock(b, sizeof(b), we);
            if (other_day) {
                snprintf(cheap_text, sizeof(cheap_text), "%s %s %s-%s", ui_i18n_get("price.cheap", "Cheap"),
                         ctx->day_offset == 0 ? ui_i18n_get("price.tomorrow", "Tomorrow") : ui_i18n_get("price.today", "Today"), a, b);
            } else {
                snprintf(cheap_text, sizeof(cheap_text), "%s %s-%s", ui_i18n_get("price.cheap", "Cheap"), a, b);
            }
        } else {
            snprintf(cheap_text, sizeof(cheap_text), "%s", ui_i18n_get("price.no_cheap", "No cheap window left"));
        }
    }
    if (ctx->stats.valid) {
        char lo[16], hi[16];
        pf_format_cent(lo, sizeof(lo), ctx->stats.min);
        pf_format_cent(hi, sizeof(hi), ctx->stats.max);
        snprintf(stats_text, sizeof(stats_text), "%s %s  %s %s", ui_i18n_get("price.min", "min"), lo, ui_i18n_get("price.max", "max"), hi);
    }
    const lv_coord_t avail_plot = h - y - pad;
    const bool show_row = (cheap_text[0] != '\0' || stats_text[0] != '\0') && avail_plot >= 5 * meta_h;
    bool show_axis = ctx->stats.valid && avail_plot >= (show_row ? 4 : 3) * meta_h;
    lv_coord_t bottom = pad;
    lv_label_set_text(ctx->cheap_label, cheap_text);
    lv_label_set_text(ctx->stats_label, stats_text);
    lv_obj_set_hidden(ctx->cheap_label, !show_row || cheap_text[0] == '\0');
    const lv_coord_t cheap_w = pf_text_w(cheap_text, meta);
    const lv_coord_t stats_w = pf_text_w(stats_text, meta);
    const bool show_stats = show_row && stats_text[0] != '\0' && (cheap_text[0] == '\0' || cheap_w + stats_w + 16 <= w - 2 * pad);
    lv_obj_set_hidden(ctx->stats_label, !show_stats);
    if (show_row) {
        lv_obj_set_width(ctx->cheap_label, w - 2 * pad - (show_stats ? stats_w + 12 : 0));
        lv_obj_align(ctx->cheap_label, LV_ALIGN_BOTTOM_LEFT, pad, -8);
        lv_obj_set_width(ctx->stats_label, stats_w + 2);
        lv_obj_align(ctx->stats_label, LV_ALIGN_BOTTOM_RIGHT, -pad, -8);
        bottom = 8 + meta_h + gap;
    }
    lv_coord_t axis_y = h - bottom - meta_h;
    if (show_axis) bottom += meta_h + 2;
    lv_coord_t plot_h = h - y - bottom;
    if (plot_h < 14) {
        show_axis = false;
        lv_obj_set_hidden(ctx->cheap_label, true);
        lv_obj_set_hidden(ctx->stats_label, true);
        plot_h = h - y - pad;
    }
    const lv_coord_t plot_w = w - 2 * pad;
    const bool plot_ok = ctx->stats.valid && plot_h >= 14 && plot_w >= 40;
    lv_obj_set_size(ctx->plot, plot_w, plot_h > 0 ? plot_h : 1);
    lv_obj_align(ctx->plot, LV_ALIGN_TOP_LEFT, pad, y);
    lv_obj_set_hidden(ctx->plot, !plot_ok);

    /* Axis labels at the local hours 00, 06, 12 and 18 inside the view, placed by time: a day that starts late or has 23 or 25 hours keeps
     * them where they belong, and the 24 hours around now show the four ticks in the order they come. */
    for (int i = 0; i < PF_AXIS_LABELS; i++) {
        ctx->axis_x[i] = -1;
        ctx->axis_hour[i] = 0;
    }
    if (plot_ok && show_axis && ctx->day_end_s > ctx->day_start_s) {
        time_t base = (time_t)ctx->day_start_s;
        struct tm day;
        int n = 0;
        if (localtime_r(&base, &day) != NULL) {
            for (int d = 0; d < 2 && n < PF_AXIS_LABELS; d++) {
                for (int t = 0; t < 4 && n < PF_AXIS_LABELS; t++) {
                    struct tm tick = day;
                    tick.tm_mday += d;
                    tick.tm_hour = t * 6;
                    tick.tm_min = 0;
                    tick.tm_sec = 0;
                    tick.tm_isdst = -1;
                    const int64_t at = (int64_t)mktime(&tick);
                    const int x = (int)((at - ctx->day_start_s) * plot_w / (ctx->day_end_s - ctx->day_start_s));
                    if (at >= ctx->day_start_s && at < ctx->day_end_s && x + pf_text_w("00", meta) <= plot_w) { /* a label that would run past the plot is left out */
                        ctx->axis_x[n] = x;
                        ctx->axis_hour[n] = t * 6;
                        n++;
                    }
                }
            }
        }
    }
    for (int i = 0; i < PF_AXIS_LABELS; i++) {
        const bool on = ctx->axis_x[i] >= 0;
        lv_obj_set_hidden(ctx->axis_label[i], !on);
        if (!on) continue;
        char text[8];
        snprintf(text, sizeof(text), "%02d", ctx->axis_hour[i]);
        lv_label_set_text(ctx->axis_label[i], text);
        lv_obj_align(ctx->axis_label[i], LV_ALIGN_TOP_LEFT, pad + ctx->axis_x[i], axis_y);
    }

    lv_label_set_text(ctx->status_label, ctx->status != NULL ? ctx->status : "");
    lv_obj_set_hidden(ctx->status_label, ctx->status == NULL);
    if (ctx->status != NULL) {
        lv_obj_set_width(ctx->status_label, w - 2 * pad);
        lv_obj_set_style_text_align(ctx->status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(ctx->status_label, LV_ALIGN_CENTER, 0, meta_h);
    }
    lv_obj_set_hidden(ctx->price_label, !ctx->stats.valid);
    lv_obj_set_hidden(ctx->slot_label, !ctx->stats.valid);
    pf_button_style(ctx->btn_today, single ? true : ctx->day_offset == 0);
    pf_button_style(ctx->btn_tomorrow, ctx->day_offset == 1);
    lv_obj_invalidate(ctx->plot);
}

static void pf_refresh(w_pf_ctx_t *ctx, bool changed)
{
    if (ctx == NULL || ctx->card == NULL) return;
    const int64_t start = ctx->day_start_s, end = ctx->day_end_s;
    const int current = ctx->cur_slot, shown = ctx->shown_slot;
    const size_t windows = ctx->window_count;
    const char *status = ctx->status;
    pf_update(ctx);
    /* Data/options/geometry changes force layout. With the same inputs, only
     * interval/day boundaries, expiring cheap windows or status can change it. */
    if (!changed && start == ctx->day_start_s && end == ctx->day_end_s &&
        current == ctx->cur_slot && shown == ctx->shown_slot &&
        windows == ctx->window_count && status == ctx->status) return;
    /* The timer, a button and a size change run outside the factory's calls: take the tile's text-size cap here as well. */
    ui_fonts_tile_begin("energy_price_forecast", (int)lv_obj_get_width(ctx->card), (int)lv_obj_get_height(ctx->card));
    pf_layout(ctx);
    ui_fonts_tile_end();
}

static void pf_draw_cb(lv_event_t *e)
{
    w_pf_ctx_t *ctx = (w_pf_ctx_t *)lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    if (ctx == NULL || layer == NULL || ctx->count <= 0 || !ctx->stats.valid) return;
    lv_area_t area;
    lv_obj_get_coords(ctx->plot, &area);
    const int32_t w = lv_area_get_width(&area);
    const int32_t h = lv_area_get_height(&area);
    const int64_t day_len = ctx->day_end_s - ctx->day_start_s;
    if (w < 24 || h < 8 || day_len <= 0) return;

    const double lo = ctx->stats.min < 0.0 ? ctx->stats.min : 0.0;
    double hi = ctx->stats.max > 0.0 ? ctx->stats.max : 0.0;
    if (hi - lo < 1e-9) hi = lo + 0.01;
    const double px_per_eur = (double)(h - 2) / (hi - lo);
    const int32_t zero_y = area.y2 - 1 - (int32_t)((0.0 - lo) * px_per_eur + 0.5);

    /* Bars and bands sit at their time of the day, so a day that is only partly priced leaves the rest empty. */
#define PF_X(t) (area.x1 + (int32_t)(((int64_t)((t) - ctx->day_start_s) * w) / day_len))

    if (ctx->cheap_times) {
        lv_draw_rect_dsc_t band;
        lv_draw_rect_dsc_init(&band);
        band.bg_color = lv_color_hex(PF_COLOR_CHEAP);
        band.bg_opa = LV_OPA_20;
        band.radius = 0;
        for (size_t k = 0; k < ctx->window_count; k++) {
            int64_t ws = ctx->windows[k].start_s, we = ctx->windows[k].end_s;
            if (we <= ctx->day_start_s || ws >= ctx->day_end_s) continue;
            if (ws < ctx->day_start_s) ws = ctx->day_start_s;
            if (we > ctx->day_end_s) we = ctx->day_end_s;
            const lv_area_t area_band = {(lv_coord_t)PF_X(ws), area.y1, (lv_coord_t)(PF_X(we) - 1), area.y2};
            lv_draw_rect(layer, &band, &area_band);
        }
    }

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = 0;
    dsc.border_width = 0;
    int32_t prev_y = 0;
    int prev_i = -2;
    for (int i = ctx->first; i < ctx->first + ctx->count; i++) {
        const double v = ha_price_value(&ctx->series, i);
        if (isnan(v)) continue;
        const int64_t st = ha_price_slot_start(&ctx->series, i);
        const int64_t en = st + ctx->series.interval_s;
        const int32_t x1 = PF_X(st);
        int32_t x2 = PF_X(en) - (PF_X(en) - x1 >= 4 ? 2 : 1);
        if (x2 < x1) x2 = x1;
        const int32_t bar_h = (int32_t)(fabs(v) * px_per_eur + 0.5);
        int32_t y1 = v >= 0 ? zero_y - (bar_h > 0 ? bar_h : 1) + 1 : zero_y;
        int32_t y2 = v >= 0 ? zero_y : zero_y + (bar_h > 0 ? bar_h : 1) - 1;
        if (y1 < area.y1) y1 = area.y1;
        if (y2 > area.y2) y2 = area.y2;
        /* The card's rules: orange above the average (the price in whole cents against it), green otherwise; the intervals before the
         * running one are dimmed, and only today has a running one. */
        const bool above = ha_price_cent(ctx->series.price[i]) > ctx->avg_cent;
        const bool over = ctx->day_offset == 0 && ctx->cur_slot >= 0 && i < ctx->cur_slot;
        const bool running = ctx->day_offset == 0 && i == ctx->cur_slot;
        dsc.bg_color = lv_color_hex(above ? PF_COLOR_DEAR : PF_COLOR_CHEAP);
        dsc.bg_opa = over ? LV_OPA_40 : LV_OPA_COVER;
        if (ctx->line_mode) {
            /* A stepped line: a level over the interval and a riser to the one before when it is the neighbour. */
            const int32_t yl = v >= 0 ? y1 : y2;
            const int32_t xe = PF_X(en);
            const lv_area_t level = {(lv_coord_t)x1, (lv_coord_t)(yl - 1), (lv_coord_t)(xe > x1 ? xe : x1), (lv_coord_t)yl};
            lv_draw_rect(layer, &dsc, &level);
            if (prev_i == i - 1) {
                const lv_area_t riser = {(lv_coord_t)(x1 - 1), (lv_coord_t)(yl < prev_y ? yl : prev_y), (lv_coord_t)x1,
                                         (lv_coord_t)(yl < prev_y ? prev_y : yl)};
                lv_draw_rect(layer, &dsc, &riser);
            }
            prev_y = yl;
            prev_i = i;
        } else {
            const lv_area_t bar = {(lv_coord_t)x1, (lv_coord_t)y1, (lv_coord_t)x2, (lv_coord_t)y2};
            lv_draw_rect(layer, &dsc, &bar);
        }
        if (running) {
            lv_draw_rect_dsc_t mark;
            lv_draw_rect_dsc_init(&mark);
            mark.bg_opa = LV_OPA_TRANSP;
            mark.border_color = lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY);
            mark.border_width = 2;
            mark.border_opa = LV_OPA_COVER;
            mark.radius = 0;
            const lv_area_t outline = {(lv_coord_t)(x1 - 1), (lv_coord_t)area.y1, (lv_coord_t)(x2 + 1), (lv_coord_t)area.y2};
            lv_draw_rect(layer, &mark, &outline);
        }
    }

    /* The average as a thin dashed line (when it lies within the bars' range), the zero axis when prices go negative. */
    lv_draw_rect_dsc_t line;
    lv_draw_rect_dsc_init(&line);
    line.bg_color = lv_color_hex(APP_UI_COLOR_TEXT_MUTED);
    line.bg_opa = LV_OPA_60;
    const double avg_eur = ctx->avg_cent / 100.0;
    if (!isnan(avg_eur) && avg_eur >= lo && avg_eur <= hi) {
        const int32_t avg_y = area.y2 - 1 - (int32_t)((avg_eur - lo) * px_per_eur + 0.5);
        for (int32_t x = area.x1; x < area.x2; x += 6) {
            const lv_area_t dash = {(lv_coord_t)x, (lv_coord_t)avg_y, (lv_coord_t)(x + 2 < area.x2 ? x + 2 : area.x2), (lv_coord_t)avg_y};
            lv_draw_rect(layer, &line, &dash);
        }
    }
    if (ctx->stats.min < 0.0) {
        const lv_area_t axis = {area.x1, (lv_coord_t)zero_y, area.x2, (lv_coord_t)zero_y};
        lv_draw_rect(layer, &line, &axis);
    }
#undef PF_X
}

static void pf_day_clicked(lv_event_t *e)
{
    w_pf_ctx_t *ctx = (w_pf_ctx_t *)lv_event_get_user_data(e);
    if (ctx == NULL || !ctx->day_switch) return;
    /* A narrow tile has one button that names the day shown: it switches to the other one. */
    const bool single = lv_obj_is_hidden(ctx->btn_tomorrow);
    const int want = single ? 1 - ctx->day_offset : (lv_event_get_target(e) == ctx->btn_tomorrow ? 1 : 0);
    if (want == ctx->day_offset) return;
    ctx->day_offset = want;
    pf_refresh(ctx, true);
}

static void pf_tick_cb(lv_timer_t *timer)
{
    pf_refresh((w_pf_ctx_t *)lv_timer_get_user_data(timer), false);
}

static void pf_event_cb(lv_event_t *e)
{
    w_pf_ctx_t *ctx = (w_pf_ctx_t *)lv_event_get_user_data(e);
    if (ctx == NULL) return;
    if (lv_event_get_code(e) == LV_EVENT_DELETE) {
        if (ctx->timer != NULL) lv_timer_delete(ctx->timer);
        free(ctx);
    } else if (lv_event_get_code(e) == LV_EVENT_SIZE_CHANGED) {
        pf_refresh(ctx, true);
    }
}

static lv_obj_t *pf_label(lv_obj_t *parent, ui_text_role_t role, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    ui_text_set(l, role, true);
    lv_obj_set_style_text_color(l, lv_color_hex(color), LV_PART_MAIN);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    return l;
}

static lv_obj_t *pf_day_button(w_pf_ctx_t *ctx, const char *text)
{
    lv_obj_t *b = lv_label_create(ctx->card);
    ui_text_set(b, UI_TEXT_META, true);
    lv_label_set_text(b, text);
    lv_label_set_long_mode(b, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_radius(b, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_top(b, 3, LV_PART_MAIN);
    lv_obj_set_clickable(b, true);
    lv_obj_add_event_cb(b, pf_day_clicked, LV_EVENT_CLICKED, ctx);
    return b;
}

esp_err_t w_energy_price_forecast_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    w_pf_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    strlcpy(ctx->entity_id, def->entity_id, sizeof(ctx->entity_id));
    strlcpy(ctx->avg_entity, def->secondary_entity_id, sizeof(ctx->avg_entity));
    ctx->day_switch = def->price_selector;
    ctx->cheap_times = def->price_cheap_times;
    ctx->line_mode = def->price_line;
    ctx->avg_fixed = def->price_average > 0.0f ? (double)def->price_average : 0.0;
    strlcpy(ctx->title, def->title[0] ? def->title : def->entity_id, sizeof(ctx->title));

    lv_obj_t *card = lv_obj_create(parent);
    ctx->card = card;
    out_instance->obj = card;
    out_instance->ctx = ctx;
    out_instance->visible = true;
    lv_obj_set_pos(card, def->x, def->y);
    lv_obj_set_size(card, def->w, def->h);
    theme_default_style_card(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(card, false);

    ctx->title_label = ui_widget_name_create(card, !def->hide_name);
    lv_label_set_text(ctx->title_label, ctx->title);
    ui_style_title_fit(ctx->title_label, ctx->title);
    ui_text_set(ctx->title_label, UI_TEXT_META, true);
    lv_obj_set_style_text_color(ctx->title_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->title_label, LV_LABEL_LONG_DOT);

    ctx->price_label = pf_label(card, UI_TEXT_KEY, APP_UI_COLOR_TEXT_PRIMARY);
    ctx->slot_label = pf_label(card, UI_TEXT_META, APP_UI_COLOR_TEXT_SOFT);
    ctx->unit_label = pf_label(card, UI_TEXT_META, APP_UI_COLOR_TEXT_SOFT);
    ctx->stats_label = pf_label(card, UI_TEXT_META, APP_UI_COLOR_TEXT_SOFT);
    ctx->btn_today = pf_day_button(ctx, ui_i18n_get("price.today", "Today"));
    ctx->btn_tomorrow = pf_day_button(ctx, ui_i18n_get("price.tomorrow", "Tomorrow"));
    ctx->cheap_label = pf_label(card, UI_TEXT_META, PF_COLOR_CHEAP);
    ctx->status_label = pf_label(card, UI_TEXT_LABEL, APP_UI_COLOR_TEXT_SOFT);
    lv_label_set_long_mode(ctx->status_label, LV_LABEL_LONG_WRAP);

    ctx->plot = lv_obj_create(card);
    lv_obj_remove_style_all(ctx->plot);
    lv_obj_set_scrollable(ctx->plot, false);
    lv_obj_set_clickable(ctx->plot, false);
    lv_obj_add_event_cb(ctx->plot, pf_draw_cb, LV_EVENT_DRAW_POST_END, ctx);
    for (int i = 0; i < PF_AXIS_LABELS; i++) {
        ctx->axis_label[i] = pf_label(card, UI_TEXT_META, APP_UI_COLOR_TEXT_MUTED);
        ctx->axis_x[i] = -1;
    }

    lv_obj_add_event_cb(card, pf_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, pf_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    ctx->timer = lv_timer_create(pf_tick_cb, PF_TICK_PERIOD_MS, ctx);
    pf_refresh(ctx, true);
    return ESP_OK;
}

void w_energy_price_forecast_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->ctx == NULL || state == NULL) return;
    w_pf_ctx_t *ctx = (w_pf_ctx_t *)instance->ctx;
    if (ctx->avg_entity[0] != '\0' && strcmp(state->entity_id, ctx->avg_entity) == 0) {
        /* The optional average sensor: its state is the average price in EUR/kWh. Anything else leaves the day's own average in use. */
        char *end = NULL;
        const double v = strtod(state->state, &end);
        const bool valid = end != state->state && isfinite(v) && v > 0.0 && v < 5.0;
        const double average = valid ? v : 0.0;
        const bool changed = valid != ctx->avg_sensor_valid || average != ctx->avg_sensor;
        ctx->avg_sensor_valid = valid;
        ctx->avg_sensor = average;
        pf_refresh(ctx, changed);
        return;
    }
    ha_price_series_t *next = ui_calloc_prefer_psram(1, sizeof(*next));
    if (next == NULL) return;
    bool changed = ctx->has_series;
    if (ha_price_series_from_json(state->attributes_json, next)) {
        changed = !ctx->has_series || ctx->series.start_s != next->start_s ||
            ctx->series.interval_s != next->interval_s || ctx->series.count != next->count ||
            memcmp(ctx->series.price, next->price, next->count * sizeof(next->price[0])) != 0;
        ctx->series = *next;
        ctx->has_series = true;
    } else {
        ctx->has_series = false;
    }
    free(next);
    pf_refresh(ctx, changed);
}

void w_energy_price_forecast_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) return;
    w_pf_ctx_t *ctx = (w_pf_ctx_t *)instance->ctx;
    const bool changed = ctx->has_series || ctx->avg_sensor_valid;
    ctx->has_series = false;
    ctx->avg_sensor_valid = false;
    pf_refresh(ctx, changed);
}
