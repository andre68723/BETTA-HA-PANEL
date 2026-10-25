/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Stocks & ETFs (stock_quotes): up to eight market quotes of Yahoo Finance or Easy Stock sensors (ha/ha_quote.h), mixed in
 * any order. entity_id is the first quote, extra_entity_id[0 .. 6] the others. The provider of each comes from its registry
 * entry; a sensor of another integration is named as such instead of being read. Two views (display_mode):
 *   list   rows of name (and, with stock_ticker, the symbol), price and the last session's change;
 *   chart  a section per quote: price, the change over the chosen period against its reference, and a small chart; one
 *          row of period buttons (1D, 1W, 30D, YTD, 1Y, option stock_selector, on by default) applies to every chart of the tile.
 * stock_period is the charts' first period and the one the detail popup opens with; stock_delta shows the change in
 * percent, as an amount or both. Rows scroll when they do not fit; a tap (not a drag) opens the detail popup.
 * Histories come from ui/quote_history_feed.c, only for charts on screen.
 */
#include "ui/ui_widget_factory.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"

#include "ha/ha_display_precision.h"
#include "ha/ha_quote.h"
#include "ha/ha_quote_series.h"
#include "ui/quote_history_feed.h"
#include "ui/text_font.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/theme/ui_typography.h"
#include "ui/ui_history_popup.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"
#include "ui/ui_widget_name.h"

#define QS_MAX UI_STOCK_MAX_QUOTES
#define QS_SPARK_MAX 120
#define QS_PAD 12
#define QS_GAP 8
/* A chart's span never drops below this share of its price: rounding noise must not look like a crash. */
#define QS_MIN_SPAN 0.004f

typedef enum { QS_DELTA_PERCENT = 0, QS_DELTA_ABSOLUTE, QS_DELTA_BOTH } qs_delta_t;

struct w_qs_ctx;

typedef struct {
    struct w_qs_ctx *ctx;
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    ha_quote_provider_t provider;
    bool has_state;
    ha_quote_t quote;

    void *feed;                         /* chart view: the history slot */
    quote_feed_status_t feed_status;
    ha_quote_series_kind_t kind;
    bool partial;
    bool has_change;                    /* the period change */
    ha_quote_change_t change;
    int64_t cover_from;                 /* the first value's time, for a partial history */
    uint16_t point_count;
    ha_quote_point_t point[QS_SPARK_MAX];

    lv_obj_t *row;                      /* a button: the tap target */
    lv_obj_t *name;
    lv_obj_t *ticker;
    lv_obj_t *price;
    lv_obj_t *delta;
    lv_obj_t *delta2;                   /* list view with stock_delta both: the amount under the percentage */
    lv_obj_t *sub;                      /* chart view: reference or source under the change */
    lv_obj_t *spark;
} qs_item_t;

typedef struct w_qs_ctx {
    char title[APP_MAX_NAME_LEN];
    bool show_name;
    bool chart;
    bool ticker;
    bool selector;                      /* chart view: the period buttons (stock_selector) */
    qs_delta_t delta_mode;
    ui_text_role_t number_role;         /* list view: the price and change columns, LABEL or the smaller META */
    ha_quote_period_t period;
    int count;
    qs_item_t item[QS_MAX];
    lv_obj_t *card;
    lv_obj_t *title_label;
    lv_obj_t *chip[HA_QUOTE_PERIOD_COUNT];
    lv_obj_t *list;
} w_qs_ctx_t;

static const char *const QS_PERIOD_KEY[HA_QUOTE_PERIOD_COUNT] = {
    "stock.period.1d", "stock.period.1w", "stock.period.30d", "stock.period.ytd", "stock.period.1y"};
static const char *const QS_PERIOD_TEXT[HA_QUOTE_PERIOD_COUNT] = {"1D", "1W", "30D", "YTD", "1Y"};

static bool qs_german(void)
{
    const char *lang = ui_i18n_get_language();
    return lang != NULL && strncmp(lang, "de", 2) == 0;
}

static lv_obj_t *qs_label(lv_obj_t *parent, ui_text_role_t role, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    ui_text_set(l, role, true);
    lv_obj_set_style_text_color(l, lv_color_hex(color), LV_PART_MAIN);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_clickable(l, false);
    return l;
}

static uint32_t qs_sign_color(bool known, double v)
{
    if (!known || v == 0.0) return APP_UI_COLOR_TEXT_SOFT;
    return v > 0.0 ? APP_UI_COLOR_OK : APP_UI_COLOR_ERROR;
}

static lv_coord_t qs_text_w(const char *text, const lv_font_t *font)
{
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

static void qs_set_text(lv_obj_t *label, const char *text)
{
    const char *now = lv_label_get_text(label);
    if (now == NULL || strcmp(now, text) != 0) lv_label_set_text(label, text);
}

static void qs_place(lv_obj_t *obj, int x, int y, int w, int h)
{
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w > 1 ? w : 1, h > 1 ? h : 1);
}

/* "208,65 €", "1.234,50 GBp" */
static void qs_price_text(const ha_quote_t *q, char *out, size_t cap)
{
    char num[40];
    ha_quote_format_number(num, sizeof(num), q->price, ha_quote_price_decimals(q), qs_german(), false);
    const char *sign = ha_quote_currency_sign(q->currency);
    snprintf(out, cap, sign[0] != '\0' ? "%s %s" : "%s", num, sign);
}

static void qs_pct_text(double pct, char *out, size_t cap)
{
    char num[32];
    ha_quote_format_number(num, sizeof(num), pct, 2, qs_german(), true);
    snprintf(out, cap, "%s %%", num);
}

static void qs_amount_text(const ha_quote_t *q, double amount, char *out, size_t cap)
{
    char num[40];
    ha_quote_format_number(num, sizeof(num), amount, ha_quote_price_decimals(q), qs_german(), true);
    const char *sign = ha_quote_currency_sign(q->currency);
    snprintf(out, cap, sign[0] != '\0' ? "%s %s" : "%s", num, sign);
}

/* What a row says instead of a price while its quote cannot be read. NULL when it can. */
static const char *qs_status_text(const qs_item_t *it)
{
    switch (it->provider) {
    case HA_QUOTE_PROVIDER_PENDING: return "\xE2\x80\xA6"; /* the registry has not answered yet */
    case HA_QUOTE_PROVIDER_FAILED: return ui_i18n_get("stock.lookup_failed", "Source unknown");
    case HA_QUOTE_PROVIDER_UNSUPPORTED: return ui_i18n_get("stock.unsupported", "Not a quote");
    default: break;
    }
    if (!it->has_state || !it->quote.available) return "--";
    return NULL;
}

static void qs_name_text(const qs_item_t *it, char *out, size_t cap)
{
    const char *name = it->quote.name[0] != '\0' ? it->quote.name : it->quote.symbol[0] != '\0' ? it->quote.symbol : it->entity_id;
    snprintf(out, cap, "%s", name);
}

/* ---- the small chart of the chart view ---- */

static void qs_spark_draw_cb(lv_event_t *e)
{
    qs_item_t *it = (qs_item_t *)lv_event_get_user_data(e);
    if (it == NULL || it->point_count < 2) return;
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    const int w = lv_area_get_width(&area), h = lv_area_get_height(&area);
    if (w < 8 || h < 6) return;
    float lo = INFINITY, hi = -INFINITY;
    for (uint16_t i = 0; i < it->point_count; i++) {
        const float v = it->point[i].v;
        if (!isfinite(v)) continue;
        lo = fminf(lo, v);
        hi = fmaxf(hi, v);
    }
    if (it->change.from_reference && isfinite((float)it->change.base)) {
        lo = fminf(lo, (float)it->change.base);
        hi = fmaxf(hi, (float)it->change.base);
    }
    if (!isfinite(lo) || !isfinite(hi)) return;
    /* a small minimum span around the middle: a flat day draws flat, not as a storm of rounding steps */
    const float mid = (lo + hi) * 0.5f, min_span = fabsf(mid) * QS_MIN_SPAN;
    if (hi - lo < min_span) {
        lo = mid - min_span * 0.5f;
        hi = mid + min_span * 0.5f;
    }
    const float span = hi - lo > 0.0f ? hi - lo : 1.0f;
    const int64_t t0 = it->point[0].t, t1 = it->point[it->point_count - 1].t;
    const double tspan = t1 > t0 ? (double)(t1 - t0) : 1.0;
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.width = 2;
    line.round_start = 1;
    line.round_end = 1;
    line.color = lv_color_hex(qs_sign_color(it->has_change, it->change.change));
    if (it->change.from_reference) {
        /* the reference close as a faint line: where the period's change starts */
        lv_draw_line_dsc_t ref = line;
        ref.width = 1;
        ref.opa = LV_OPA_40;
        ref.color = lv_color_hex(APP_UI_COLOR_TEXT_MUTED);
        ref.dash_width = 4;
        ref.dash_gap = 4;
        const int32_t y = area.y2 - 1 - (int32_t)(((float)it->change.base - lo) / span * (float)(h - 3) + 0.5f);
        ref.p1.x = area.x1;
        ref.p1.y = y;
        ref.p2.x = area.x2;
        ref.p2.y = y;
        lv_draw_line(layer, &ref);
    }
    for (uint16_t i = 0; i + 1 < it->point_count; i++) {
        const float a = it->point[i].v, b = it->point[i + 1].v;
        if (!isfinite(a) || !isfinite(b)) continue; /* a gap stays a gap */
        line.p1.x = area.x1 + (int32_t)((double)(it->point[i].t - t0) / tspan * (double)(w - 1));
        line.p1.y = area.y2 - 1 - (int32_t)((a - lo) / span * (float)(h - 3) + 0.5f);
        line.p2.x = area.x1 + (int32_t)((double)(it->point[i + 1].t - t0) / tspan * (double)(w - 1));
        line.p2.y = area.y2 - 1 - (int32_t)((b - lo) / span * (float)(h - 3) + 0.5f);
        lv_draw_line(layer, &line);
    }
}

/* ---- layout ---- */

static int qs_line_h(ui_text_role_t role)
{
    return (int)lv_font_get_line_height(ui_font_role(role));
}

static void qs_layout_row_list(w_qs_ctx_t *ctx, qs_item_t *it, int y, int w, int price_w, int delta_w)
{
    const int label_h = qs_line_h(UI_TEXT_LABEL), meta_h = qs_line_h(UI_TEXT_META);
    const int number_h = qs_line_h(ctx->number_role), number_y = 5 + (label_h - number_h) / 2;
    const bool two_lines = ctx->ticker || ctx->delta_mode == QS_DELTA_BOTH;
    const int row_h = (two_lines ? label_h + meta_h : label_h) + 10;
    qs_place(it->row, 0, y, w, row_h);
    char text[96];
    qs_name_text(it, text, sizeof(text));
    qs_set_text(it->name, text);
    const int name_w = w - price_w - delta_w - 2 * QS_GAP - 8;
    qs_place(it->name, 4, 5, name_w, label_h);
    lv_obj_set_hidden(it->ticker, !ctx->ticker);
    if (ctx->ticker) {
        qs_set_text(it->ticker, it->quote.symbol);
        qs_place(it->ticker, 4, 5 + label_h, name_w, meta_h);
    }
    const char *status = qs_status_text(it);
    const bool known = status == NULL;
    if (known) qs_price_text(&it->quote, text, sizeof(text));
    qs_set_text(it->price, known ? text : status);
    lv_obj_set_style_text_color(it->price, lv_color_hex(known ? APP_UI_COLOR_TEXT_PRIMARY : APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    /* a quote that cannot be read says why across both number columns, smaller */
    const bool message = !known && strcmp(status, "--") != 0;
    ui_text_set(it->price, message ? UI_TEXT_META : ctx->number_role, true);
    lv_obj_set_hidden(it->delta, message);
    if (message) {
        qs_place(it->price, w - delta_w - QS_GAP - price_w - 4, 5 + (label_h - meta_h) / 2, price_w + QS_GAP + delta_w, meta_h);
        lv_obj_set_hidden(it->delta2, true);
        lv_obj_set_hidden(it->sub, true);
        lv_obj_set_hidden(it->spark, true);
        return;
    }
    qs_place(it->price, w - delta_w - QS_GAP - price_w - 4, number_y, price_w, number_h);

    const ha_quote_t *q = &it->quote;
    const bool pct = known && q->has_change_pct, amount = known && q->has_change;
    char first[48], second[48];
    if (ctx->delta_mode == QS_DELTA_ABSOLUTE) {
        if (amount) qs_amount_text(q, q->change, first, sizeof(first));
        else snprintf(first, sizeof(first), "--");
    } else if (pct) {
        qs_pct_text(q->change_pct, first, sizeof(first));
    } else {
        snprintf(first, sizeof(first), "--");
    }
    const double sign_of = ctx->delta_mode == QS_DELTA_ABSOLUTE ? q->change : q->has_change_pct ? q->change_pct : q->change;
    qs_set_text(it->delta, first);
    lv_obj_set_style_text_color(it->delta, lv_color_hex(qs_sign_color(pct || amount, sign_of)), LV_PART_MAIN);
    qs_place(it->delta, w - delta_w - 4, number_y, delta_w, number_h);
    lv_obj_set_hidden(it->delta2, ctx->delta_mode != QS_DELTA_BOTH);
    if (ctx->delta_mode == QS_DELTA_BOTH) {
        if (amount) qs_amount_text(q, q->change, second, sizeof(second));
        else snprintf(second, sizeof(second), "--");
        qs_set_text(it->delta2, second);
        lv_obj_set_style_text_color(it->delta2, lv_color_hex(qs_sign_color(amount, q->change)), LV_PART_MAIN);
        qs_place(it->delta2, w - delta_w - 4, 5 + label_h, delta_w, meta_h);
    }
    lv_obj_set_hidden(it->sub, true);
    lv_obj_set_hidden(it->spark, true);
}

static const char *qs_kind_text(ha_quote_series_kind_t kind)
{
    switch (kind) {
    case HA_QUOTE_SERIES_CLOSES: return ui_i18n_get("stock.source.closes", "Closes");
    case HA_QUOTE_SERIES_STATES: return ui_i18n_get("stock.source.recorded", "Recorded");
    default: return ui_i18n_get("stock.source.averages", "HA averages");
    }
}

static void qs_date_text(int64_t epoch, char *out, size_t cap)
{
    const time_t at = (time_t)epoch;
    struct tm local;
    if (localtime_r(&at, &local) == NULL) {
        snprintf(out, cap, "?");
        return;
    }
    if (qs_german()) snprintf(out, cap, "%d.%d.%02d", local.tm_mday, local.tm_mon + 1, local.tm_year % 100);
    else snprintf(out, cap, "%d/%d/%02d", local.tm_mon + 1, local.tm_mday, local.tm_year % 100);
}

static int qs_layout_row_chart(w_qs_ctx_t *ctx, qs_item_t *it, int y, int w, int chart_h)
{
    const int label_h = qs_line_h(UI_TEXT_LABEL), meta_h = qs_line_h(UI_TEXT_META);
    const int row_h = label_h + meta_h + chart_h + 16;
    qs_place(it->row, 0, y, w, row_h);
    char text[96];
    const char *status = qs_status_text(it);
    const bool known = status == NULL;
    if (known) qs_price_text(&it->quote, text, sizeof(text));
    qs_set_text(it->price, known ? text : status);
    lv_obj_set_style_text_color(it->price, lv_color_hex(known ? APP_UI_COLOR_TEXT_PRIMARY : APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    const int price_w = qs_text_w(lv_label_get_text(it->price), ui_font_role(UI_TEXT_LABEL)) + 2;
    qs_place(it->price, w - price_w - 4, 4, price_w, label_h);
    qs_name_text(it, text, sizeof(text));
    if (ctx->ticker && it->quote.symbol[0] != '\0') {
        const size_t used = strlen(text);
        snprintf(text + used, sizeof(text) - used, " \xC2\xB7 %s", it->quote.symbol);
    }
    qs_set_text(it->name, text);
    qs_place(it->name, 4, 4, w - price_w - QS_GAP - 8, label_h);
    lv_obj_set_hidden(it->ticker, true);
    lv_obj_set_hidden(it->delta2, true);

    /* the period change and where it comes from */
    char first[96] = "--", second[128] = "";
    uint32_t color = APP_UI_COLOR_TEXT_SOFT;
    if (!known) {
        snprintf(first, sizeof(first), " ");
    } else if (it->feed_status == QUOTE_FEED_LOADING) {
        snprintf(second, sizeof(second), "%s", ui_i18n_get("history.loading", "Loading history..."));
    } else if (it->feed_status == QUOTE_FEED_FAILED) {
        snprintf(second, sizeof(second), "%s", ui_i18n_get("stock.history_failed", "History not available"));
    } else if (it->feed_status == QUOTE_FEED_EMPTY || !it->has_change) {
        snprintf(second, sizeof(second), "%s", ui_i18n_get("stock.history_empty", "No history for this period"));
    } else {
        char pct[32], amount[48];
        qs_pct_text(it->change.change_pct, pct, sizeof(pct));
        qs_amount_text(&it->quote, it->change.change, amount, sizeof(amount));
        if (ctx->delta_mode == QS_DELTA_ABSOLUTE) snprintf(first, sizeof(first), "%s", amount);
        else if (ctx->delta_mode == QS_DELTA_BOTH) snprintf(first, sizeof(first), "%s  %s", amount, pct);
        else snprintf(first, sizeof(first), "%s", pct);
        color = qs_sign_color(true, it->change.change);
        /* means are a recorded trend, not a market return; a close series names its reference */
        char date[16], base[40];
        if (it->change.from_reference) {
            /* the reference's date is in the detail popup: the tile names the price */
            ha_quote_t ref = it->quote;
            ref.price = it->change.base;
            qs_price_text(&ref, base, sizeof(base));
            snprintf(second, sizeof(second), "%s %s", ui_i18n_get("stock.since", "since"), base);
        } else if (it->partial) {
            qs_date_text(it->cover_from, date, sizeof(date));
            snprintf(second, sizeof(second), "%s \xC2\xB7 %s %s", qs_kind_text(it->kind), ui_i18n_get("stock.from", "from"), date);
        } else {
            snprintf(second, sizeof(second), "%s", qs_kind_text(it->kind));
        }
    }
    qs_set_text(it->delta, first);
    lv_obj_set_style_text_color(it->delta, lv_color_hex(color), LV_PART_MAIN);
    const int delta_w = qs_text_w(first, ui_font_role(UI_TEXT_META)) + 2;
    qs_place(it->delta, 4, 4 + label_h, delta_w, meta_h);
    lv_obj_set_style_text_font(it->delta, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_hidden(it->sub, false);
    qs_set_text(it->sub, second);
    qs_place(it->sub, 4 + delta_w + QS_GAP, 4 + label_h, w - delta_w - QS_GAP - 8, meta_h);
    lv_obj_set_hidden(it->spark, !known);
    qs_place(it->spark, 4, 8 + label_h + meta_h, w - 8, chart_h);
    lv_obj_invalidate(it->spark);
    return row_h;
}

/* The widest price and change of the list in a text role (a reading's status counts as a price). */
static void qs_number_widths(const w_qs_ctx_t *ctx, ui_text_role_t role, int *price_w, int *delta_w)
{
    const lv_font_t *font = ui_font_role(role), *meta = ui_font_role(UI_TEXT_META);
    char text[64];
    *price_w = 0;
    *delta_w = qs_text_w("+00,00 %", font) + 2;
    for (int i = 0; i < ctx->count; i++) {
        const qs_item_t *it = &ctx->item[i];
        const char *status = qs_status_text(it);
        if (status != NULL && strcmp(status, "--") != 0) continue; /* a message spans both columns */
        if (status == NULL) qs_price_text(&it->quote, text, sizeof(text));
        const int pw = qs_text_w(status != NULL ? status : text, font) + 2;
        if (pw > *price_w) *price_w = pw;
        if (ctx->delta_mode != QS_DELTA_PERCENT && status == NULL && it->quote.has_change) {
            qs_amount_text(&it->quote, it->quote.change, text, sizeof(text));
            const int dw = qs_text_w(text, ctx->delta_mode == QS_DELTA_BOTH ? meta : font) + 2;
            if (dw > *delta_w) *delta_w = dw;
        }
    }
}

static void qs_layout(w_qs_ctx_t *ctx)
{
    const int w = (int)lv_obj_get_width(ctx->card), h = (int)lv_obj_get_height(ctx->card);
    const int inner_w = w - 2 * QS_PAD;
    int y = QS_PAD;
    lv_obj_set_hidden(ctx->title_label, !ctx->show_name);
    const int meta_h = qs_line_h(UI_TEXT_META);
    if (ctx->show_name) {
        qs_place(ctx->title_label, QS_PAD, y, inner_w, meta_h);
        y += meta_h + 4;
    }
    const bool buttons = ctx->chart && ctx->selector;
    for (int p = 0; p < HA_QUOTE_PERIOD_COUNT; p++) lv_obj_set_hidden(ctx->chip[p], !buttons);
    if (buttons) {
        /* the period buttons share the width; the chosen one is filled */
        const int chip_h = meta_h + 8, chip_w = (inner_w - (HA_QUOTE_PERIOD_COUNT - 1) * 6) / HA_QUOTE_PERIOD_COUNT;
        for (int p = 0; p < HA_QUOTE_PERIOD_COUNT; p++) {
            const bool on = p == (int)ctx->period;
            qs_set_text(ctx->chip[p], ui_i18n_get(QS_PERIOD_KEY[p], QS_PERIOD_TEXT[p]));
            qs_place(ctx->chip[p], QS_PAD + p * (chip_w + 6), y, chip_w, chip_h);
            const lv_color_t bg = ui_style_control_color(on ? UI_CONTROL_SELECTED : UI_CONTROL_ACTION, true);
            ui_style_action_button(ctx->chip[p], NULL, on ? UI_CONTROL_SELECTED : UI_CONTROL_ACTION, true);
            lv_obj_set_style_text_color(ctx->chip[p], ui_style_button_icon_color(bg), LV_PART_MAIN);
        }
        y += chip_h + 6;
    }
    qs_place(ctx->list, QS_PAD - 4, y, inner_w + 8, h - y - QS_PAD + 4);
    const int list_w = inner_w + 8;

    if (!ctx->chart) {
        /* One column width for every price and every change, so the numbers line up. Numbers are never cut: when they do
         * not fit beside a few letters of the name, they take the smaller text. */
        int price_w = 0, delta_w = 0;
        ui_text_role_t role = UI_TEXT_LABEL;
        qs_number_widths(ctx, role, &price_w, &delta_w);
        const int name_min = 3 * qs_line_h(UI_TEXT_LABEL);
        if (price_w + delta_w > list_w - 8 - 2 * QS_GAP - name_min) {
            role = UI_TEXT_META;
            qs_number_widths(ctx, role, &price_w, &delta_w);
        }
        ctx->number_role = role;
        int row_y = 0;
        for (int i = 0; i < ctx->count; i++) {
            qs_item_t *it = &ctx->item[i];
            ui_text_set(it->delta, role, true);
            qs_layout_row_list(ctx, it, row_y, list_w, price_w, delta_w);
            row_y += (int)lv_obj_get_height(it->row) + 2;
        }
        return;
    }
    /* chart sections: as tall as the list allows for a readable chart, scrolling below a minimum */
    const int list_h = (int)lv_obj_get_height(ctx->list);
    const int text_h = qs_line_h(UI_TEXT_LABEL) + meta_h + 16;
    int chart_h = ctx->count > 0 ? (list_h - 4 * (ctx->count - 1)) / ctx->count - text_h : 40;
    if (chart_h < 36) chart_h = 36;
    if (chart_h > 120) chart_h = 120;
    int row_y = 0;
    for (int i = 0; i < ctx->count; i++) {
        row_y += qs_layout_row_chart(ctx, &ctx->item[i], row_y, list_w, chart_h) + 4;
    }
}

static void qs_refresh(w_qs_ctx_t *ctx)
{
    if (ctx == NULL || ctx->card == NULL) return;
    ui_fonts_tile_begin("stock_quotes", (int)lv_obj_get_width(ctx->card), (int)lv_obj_get_height(ctx->card));
    qs_layout(ctx);
    ui_fonts_tile_end();
}

/* ---- histories ---- */

static void qs_feed_cb(void *user, quote_feed_status_t status, const ha_quote_series_t *series)
{
    qs_item_t *it = (qs_item_t *)user;
    if (it == NULL) return;
    it->feed_status = status;
    it->has_change = false;
    it->point_count = 0;
    if (status == QUOTE_FEED_READY && series != NULL) {
        it->kind = series->kind;
        it->partial = series->partial;
        it->cover_from = series->count > 0 ? series->point[0].t : 0;
        it->point_count = (uint16_t)ha_quote_series_reduce(series, it->point, QS_SPARK_MAX);
        it->has_change = ha_quote_series_period_change(series, &it->quote, &it->change);
    }
    qs_refresh(it->ctx);
}

static void qs_feed_update(qs_item_t *it)
{
    if (it->feed == NULL) return;
    quote_history_feed_set(it->feed, it->entity_id, it->provider, it->quote.symbol, it->ctx->period, it->quote.price);
}

/* ---- events ---- */

static void qs_row_clicked(lv_event_t *e)
{
    qs_item_t *it = (qs_item_t *)lv_event_get_user_data(e);
    if (it == NULL || (it->provider != HA_QUOTE_PROVIDER_YAHOO && it->provider != HA_QUOTE_PROVIDER_EASY_STOCK)) return;
    ui_history_popup_open_quote(it->entity_id, it->provider, &it->quote, it->ctx->period);
}

static void qs_chip_clicked(lv_event_t *e)
{
    w_qs_ctx_t *ctx = (w_qs_ctx_t *)lv_event_get_user_data(e);
    lv_obj_t *target = lv_event_get_target(e);
    if (ctx == NULL) return;
    for (int p = 0; p < HA_QUOTE_PERIOD_COUNT; p++) {
        if (target != ctx->chip[p] || (int)ctx->period == p) continue;
        ctx->period = (ha_quote_period_t)p;
        for (int i = 0; i < ctx->count; i++) {
            ctx->item[i].feed_status = QUOTE_FEED_LOADING;
            ctx->item[i].point_count = 0;
            ctx->item[i].has_change = false;
            qs_feed_update(&ctx->item[i]);
        }
        qs_refresh(ctx);
        break;
    }
}

static void qs_event_cb(lv_event_t *e)
{
    w_qs_ctx_t *ctx = (w_qs_ctx_t *)lv_event_get_user_data(e);
    if (ctx == NULL) return;
    if (lv_event_get_code(e) == LV_EVENT_DELETE) {
        for (int i = 0; i < ctx->count; i++) quote_history_feed_unregister(ctx->item[i].feed);
        free(ctx);
    } else if (lv_event_get_code(e) == LV_EVENT_SIZE_CHANGED) {
        qs_refresh(ctx);
    }
}

static void qs_item_create(w_qs_ctx_t *ctx, qs_item_t *it, const char *entity_id)
{
    it->ctx = ctx;
    strlcpy(it->entity_id, entity_id, sizeof(it->entity_id));
    it->provider = ha_quote_provider_from_platform(ha_display_precision_platform(entity_id));
    it->feed_status = QUOTE_FEED_LOADING;
    it->row = lv_btn_create(ctx->list);
    lv_obj_remove_style_all(it->row);
    lv_obj_set_style_radius(it->row, ui_style_radius(8), LV_PART_MAIN);
    lv_obj_set_style_bg_color(it->row, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(it->row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(it->row, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_set_scrollable(it->row, false);
    lv_obj_add_event_cb(it->row, qs_row_clicked, LV_EVENT_CLICKED, it);
    it->name = qs_label(it->row, UI_TEXT_LABEL, APP_UI_COLOR_TEXT_PRIMARY);
    lv_label_set_long_mode(it->name, LV_LABEL_LONG_DOT);
    it->ticker = qs_label(it->row, UI_TEXT_META, APP_UI_COLOR_TEXT_MUTED);
    it->price = qs_label(it->row, UI_TEXT_LABEL, APP_UI_COLOR_TEXT_PRIMARY);
    lv_obj_set_style_text_align(it->price, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    it->delta = qs_label(it->row, UI_TEXT_LABEL, APP_UI_COLOR_TEXT_SOFT);
    lv_obj_set_style_text_align(it->delta, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    it->delta2 = qs_label(it->row, UI_TEXT_META, APP_UI_COLOR_TEXT_SOFT);
    lv_obj_set_style_text_align(it->delta2, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    it->sub = qs_label(it->row, UI_TEXT_META, APP_UI_COLOR_TEXT_MUTED);
    lv_label_set_long_mode(it->sub, LV_LABEL_LONG_DOT);
    it->spark = lv_obj_create(it->row);
    lv_obj_remove_style_all(it->spark);
    lv_obj_set_scrollable(it->spark, false);
    lv_obj_set_clickable(it->spark, false);
    lv_obj_add_event_cb(it->spark, qs_spark_draw_cb, LV_EVENT_DRAW_POST_END, it);
    if (ctx->chart) {
        it->feed = quote_history_feed_register(it->row, false, qs_feed_cb, it);
        lv_obj_set_style_text_align(it->delta, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    }
}

esp_err_t w_stock_quotes_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) return ESP_ERR_INVALID_ARG;
    w_qs_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx));
    if (ctx == NULL) return ESP_ERR_NO_MEM;
    ctx->show_name = !def->hide_name;
    ctx->chart = strcmp(def->display_mode, "chart") == 0;
    ctx->ticker = def->stock_ticker;
    ctx->selector = def->stock_selector;
    ctx->delta_mode = strcmp(def->stock_delta, "absolute") == 0 ? QS_DELTA_ABSOLUTE
                      : strcmp(def->stock_delta, "both") == 0 ? QS_DELTA_BOTH : QS_DELTA_PERCENT;
    if (!ha_quote_period_from_code(def->stock_period, &ctx->period)) ctx->period = HA_QUOTE_PERIOD_30D;
    strlcpy(ctx->title, def->title[0] ? def->title : ui_i18n_get("stock.title", "Stocks & ETFs"), sizeof(ctx->title));

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

    ctx->title_label = ui_widget_name_create(card, ctx->show_name);
    lv_label_set_text(ctx->title_label, ctx->title);
    ui_style_title_fit(ctx->title_label, ctx->title);
    ui_text_set(ctx->title_label, UI_TEXT_META, true);
    lv_obj_set_style_text_color(ctx->title_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->title_label, LV_LABEL_LONG_DOT);

    for (int p = 0; p < HA_QUOTE_PERIOD_COUNT; p++) {
        lv_obj_t *chip = qs_label(card, UI_TEXT_META, APP_UI_COLOR_TEXT_SOFT);
        lv_obj_set_style_text_align(chip, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_radius(chip, ui_style_radius(8), LV_PART_MAIN);
        lv_obj_set_style_pad_top(chip, 4, LV_PART_MAIN);
        lv_obj_set_clickable(chip, true);
        lv_obj_add_event_cb(chip, qs_chip_clicked, LV_EVENT_CLICKED, ctx);
        ctx->chip[p] = chip;
    }

    ctx->list = lv_obj_create(card);
    lv_obj_remove_style_all(ctx->list);
    lv_obj_set_scroll_dir(ctx->list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ctx->list, LV_SCROLLBAR_MODE_AUTO);

    const char *ids[QS_MAX] = {def->entity_id};
    for (int i = 0; i < QS_MAX - 1; i++) ids[i + 1] = def->extra_entity_id[i];
    for (int i = 0; i < QS_MAX; i++) {
        if (ids[i] == NULL || ids[i][0] == '\0') continue;
        qs_item_create(ctx, &ctx->item[ctx->count], ids[i]);
        ctx->count++;
    }

    lv_obj_add_event_cb(card, qs_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, qs_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    qs_refresh(ctx);
    return ESP_OK;
}

static bool qs_quote_equal(const ha_quote_t *a, const ha_quote_t *b)
{
    return a->available == b->available && a->price == b->price && a->price_decimals == b->price_decimals &&
        a->has_change == b->has_change && a->change == b->change &&
        a->has_change_pct == b->has_change_pct && a->change_pct == b->change_pct &&
        a->has_prev_close == b->has_prev_close && a->prev_close == b->prev_close &&
        a->market == b->market && a->has_quote_time == b->has_quote_time && a->quote_time == b->quote_time &&
        a->has_live == b->has_live && a->price_is_live == b->price_is_live &&
        strcmp(a->currency, b->currency) == 0 && strcmp(a->symbol, b->symbol) == 0 &&
        strcmp(a->name, b->name) == 0 && strcmp(a->long_name, b->long_name) == 0;
}

void w_stock_quotes_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->ctx == NULL || state == NULL) return;
    w_qs_ctx_t *ctx = (w_qs_ctx_t *)instance->ctx;
    bool changed = false;
    /* the same sensor may be listed twice */
    for (int i = 0; i < ctx->count; i++) {
        qs_item_t *it = &ctx->item[i];
        if (strcmp(state->entity_id, it->entity_id) != 0) continue;
        const ha_quote_t previous = it->quote;
        const bool had_state = it->has_state;
        const ha_quote_provider_t provider = it->provider;
        it->provider = ha_quote_provider_from_platform(ha_display_precision_platform(it->entity_id));
        it->has_state = ha_quote_parse(it->provider, state->state, state->attributes_json, &it->quote);
        if (!it->has_state) {
            /* not a quote this widget reads, but the row still says which sensor it is */
            memset(&it->quote, 0, sizeof(it->quote));
            cJSON *attrs = cJSON_Parse(state->attributes_json);
            const cJSON *name = cJSON_GetObjectItemCaseSensitive(attrs, "friendly_name");
            if (cJSON_IsString(name) && name->valuestring != NULL) strlcpy(it->quote.name, name->valuestring, sizeof(it->quote.name));
            cJSON_Delete(attrs);
        }
        if (provider == it->provider && had_state == it->has_state && qs_quote_equal(&previous, &it->quote)) continue;
        if (it->has_change && it->point_count > 0) {
            /* a new price moves the end of the period change, not its history */
            ha_quote_change_t c = it->change;
            if (it->quote.available && (it->kind == HA_QUOTE_SERIES_CLOSES || it->kind == HA_QUOTE_SERIES_STATES) && c.base != 0.0) {
                c.end = it->quote.price;
                c.change = c.end - c.base;
                c.change_pct = c.change / fabs(c.base) * 100.0;
                it->change = c;
            }
        }
        qs_feed_update(it);
        changed = true;
    }
    if (changed) qs_refresh(ctx);
}

void w_stock_quotes_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) return;
    w_qs_ctx_t *ctx = (w_qs_ctx_t *)instance->ctx;
    for (int i = 0; i < ctx->count; i++) {
        ctx->item[i].has_state = false;
        ctx->item[i].quote.available = false;
    }
    qs_refresh(ctx);
}
