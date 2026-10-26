/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Portfolio (parqet_portfolio): the value of an investment portfolio and how it does, from the Parqet Companion integration
 * (cubinet-code/ha-parqet-companion; see ha/parqet_portfolio.h and ha/parqet_performance.h). entity_id is the total value sensor, whose attributes
 * name the interval and the largest holdings. Optional sensors: secondary_entity_id the unrealised gain, tertiary_entity_id the XIRR,
 * dividends_entity_id the dividends and return_entity_id the unrealised return in percent. The sensors report one interval chosen in the integration
 * (Max here: it is the sensors' own figures). Another interval (1D, 1W, 1M, YTD) comes from the integration's WebSocket command through
 * ha/parqet_request.c. Two views: Performance (value, gain, XIRR, TTWROR, dividends, a value curve) and Holdings (the largest holdings with their
 * return in the interval). The option parqet_selector (on by default) shows tabs and interval chips to change both by touch; parqet_view and parqet_interval
 * set the start. parqet_hide_amounts shows percentages only.
 */
#include "ui/ui_widget_factory.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ha/parqet_performance.h"
#include "ha/parqet_portfolio.h"
#include "ha/parqet_request.h"
#include "ui/text_font.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/theme/ui_typography.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"
#include "ui/ui_widget_name.h"

#define PQ_HIDDEN_TEXT "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2" /* three bullets */
#define PQ_BAR_H 3
#define PQ_ROWS PARQET_PERF_RANK
#define PQ_SORTS 3
#define PQ_KPIS 4
#define PQ_INTERVALS 5
#define PQ_MAX_INDEX 4 /* the last interval, "max": the sensors' own figures */

static const char *const PQ_INTERVAL_CODE[PQ_INTERVALS] = {"1d", "1w", "1m", "ytd", "max"};
static const char *const PQ_INTERVAL_TEXT[PQ_INTERVALS] = {"1D", "1W", "1M", "YTD", "MAX"};

typedef enum { PQ_PERFORMANCE = 0, PQ_HOLDINGS } pq_view_t;
/* which holdings the Holdings view lists: the largest by value, the best or the worst by return in the interval (a touch choice, not saved) */
typedef enum { PQ_LARGEST = 0, PQ_TOP, PQ_FLOP } pq_sort_t;
typedef enum { PQ_NO_REQUEST = 0, PQ_LOADING, PQ_READY, PQ_FAILED } pq_load_t;

typedef struct {
    lv_obj_t *name;
    lv_obj_t *right;
    lv_obj_t *bar;
} pq_row_t;

typedef struct {
    lv_obj_t *name;
    lv_obj_t *value;
} pq_kpi_t;

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    char gain_entity[APP_MAX_ENTITY_ID_LEN];
    char xirr_entity[APP_MAX_ENTITY_ID_LEN];
    char div_entity[APP_MAX_ENTITY_ID_LEN];
    char ret_entity[APP_MAX_ENTITY_ID_LEN];
    char title[APP_MAX_NAME_LEN];
    bool hide_amounts;
    bool selector;
    pq_view_t view;
    pq_sort_t sort;
    int interval;     /* index into PQ_INTERVAL_CODE */

    bool has_value, has_gain, has_xirr, has_div, has_ret, has_attrs;
    char gain_unit[8], xirr_unit[8], div_unit[8], ret_unit[8]; /* each optional sensor's own unit_of_measurement, "" when it has none */
    double value, gain, xirr, dividends, ret;
    parqet_attrs_t attrs;

    pq_load_t load;           /* the request for the shown interval, when it is not the sensors' */
    int loaded_interval;      /* the interval `perf` belongs to, -1 none */
    esp_err_t load_status;
    parqet_perf_t perf;

    lv_obj_t *card;
    lv_obj_t *title_label;
    lv_obj_t *interval_label;
    lv_obj_t *tab[2];
    lv_obj_t *sort_chip[PQ_SORTS];
    lv_obj_t *chip[PQ_INTERVALS];
    lv_obj_t *hero_label;
    lv_obj_t *gain_label;
    pq_kpi_t kpi[PQ_KPIS];
    lv_obj_t *spark;
    lv_obj_t *status_label;
    pq_row_t row[PQ_ROWS];

    /* the curve to draw, set by pq_layout() */
    float spark_min, spark_max;
    bool spark_on;
} w_pq_ctx_t;

static bool pq_german(void)
{
    const char *lang = ui_i18n_get_language();
    return lang != NULL && strncmp(lang, "de", 2) == 0;
}

static lv_obj_t *pq_label(lv_obj_t *parent, ui_text_role_t role, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    ui_text_set(l, role, true);
    lv_obj_set_style_text_color(l, lv_color_hex(color), LV_PART_MAIN);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    return l;
}

static uint32_t pq_sign_color(double v)
{
    if (v > 0.0) return APP_UI_COLOR_OK;
    if (v < 0.0) return APP_UI_COLOR_ERROR;
    return APP_UI_COLOR_TEXT_SOFT;
}

static void pq_money(const w_pq_ctx_t *ctx, char *out, size_t cap, double v, bool plus)
{
    char num[40];
    parqet_format_number(num, sizeof(num), v, 0, pq_german(), plus);
    snprintf(out, cap, "%s %s", num, parqet_currency_symbol(ctx->attrs.unit[0] != '\0' ? ctx->attrs.unit : "EUR"));
}

/* An optional amount sensor must be in the primary sensor's currency; a percentage sensor must be in percent. A missing unit is not
 * a mismatch (the states of such sensors are read as they come), and neither is a primary sensor that has not reported yet. */
static bool pq_money_unit_ok(const w_pq_ctx_t *ctx, const char *unit)
{
    return unit[0] == '\0' || ctx->attrs.unit[0] == '\0' || strcmp(unit, ctx->attrs.unit) == 0;
}

static bool pq_percent_unit_ok(const char *unit)
{
    return unit[0] == '\0' || strcmp(unit, "%") == 0;
}

static void pq_percent(char *out, size_t cap, double v, int decimals, bool plus)
{
    char num[32];
    parqet_format_number(num, sizeof(num), v, decimals, pq_german(), plus);
    snprintf(out, cap, "%s %%", num);
}

static void pq_set_hidden(lv_obj_t *obj, bool hidden)
{
    if (obj != NULL) lv_obj_set_hidden(obj, hidden);
}

static lv_coord_t pq_text_w(const char *text, const lv_font_t *font)
{
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

static void pq_chip_style(lv_obj_t *chip, bool active)
{
    const ui_control_role_t role = active ? UI_CONTROL_SELECTED : UI_CONTROL_ACTION;
    ui_style_action_button(chip, NULL, role, true);
    lv_obj_set_style_text_color(chip, ui_style_button_icon_color(ui_style_control_color(role, true)), LV_PART_MAIN);
}

/* The figures come from the integration's WebSocket command when another interval than the sensors' (Max) is shown. */
static bool pq_ws_mode(const w_pq_ctx_t *ctx)
{
    return ctx->interval != PQ_MAX_INDEX;
}

static void pq_refresh(w_pq_ctx_t *ctx);

static void pq_perf_cb(void *user, esp_err_t status, const parqet_perf_t *perf)
{
    w_pq_ctx_t *ctx = (w_pq_ctx_t *)user;
    if (ctx == NULL) return;
    int idx = -1;
    for (int i = 0; i < PQ_INTERVALS && perf != NULL; i++) {
        if (strcmp(perf->interval, PQ_INTERVAL_CODE[i]) == 0) idx = i;
    }
    if (status == ESP_OK && perf != NULL && idx >= 0) {
        ctx->perf = *perf;
        ctx->loaded_interval = idx;
        if (idx == ctx->interval) ctx->load = PQ_READY;
    } else if (ctx->load == PQ_LOADING) {
        ctx->load = PQ_FAILED;
        ctx->load_status = status;
    }
    pq_refresh(ctx);
}

/* Asks for the shown interval once the portfolio is known (the sensor's attributes carry the ids). */
static void pq_request(w_pq_ctx_t *ctx)
{
    if (!pq_ws_mode(ctx)) return;
    if (ctx->attrs.entry_id[0] == '\0' || ctx->attrs.portfolio_id[0] == '\0') return;
    if (ctx->load == PQ_LOADING) return;
    if (ctx->loaded_interval != ctx->interval) ctx->load = PQ_LOADING; /* an older answer of this interval stays on screen while it is renewed */
    const esp_err_t err = parqet_request_perf(ctx->attrs.entry_id, ctx->attrs.portfolio_id, PQ_INTERVAL_CODE[ctx->interval], ctx, pq_perf_cb);
    if (err != ESP_OK) {
        ctx->load = PQ_FAILED;
        ctx->load_status = err;
    }
}

static const char *pq_status_text(const w_pq_ctx_t *ctx)
{
    if (ctx->load == PQ_FAILED) {
        return ctx->load_status == ESP_ERR_TIMEOUT ? ui_i18n_get("parqet.no_answer", "No answer (the reply may be too large)")
                                                    : ui_i18n_get("parqet.failed", "Not available");
    }
    return ui_i18n_get("parqet.loading", "Loading");
}

/* ---- layout ---------------------------------------------------------------------------------------------- */

static void pq_hide_body(w_pq_ctx_t *ctx)
{
    pq_set_hidden(ctx->hero_label, true);
    pq_set_hidden(ctx->gain_label, true);
    for (int i = 0; i < PQ_KPIS; i++) {
        pq_set_hidden(ctx->kpi[i].name, true);
        pq_set_hidden(ctx->kpi[i].value, true);
    }
    pq_set_hidden(ctx->spark, true);
    ctx->spark_on = false;
    for (int i = 0; i < PQ_ROWS; i++) {
        pq_set_hidden(ctx->row[i].name, true);
        pq_set_hidden(ctx->row[i].right, true);
        pq_set_hidden(ctx->row[i].bar, true);
    }
}

typedef struct {
    char name[24];
    char value[48];
    uint32_t color;
    bool colored;
} pq_kpi_item_t;

/* Up to four figures in two columns; a row is only drawn when it fits above `limit`. Returns the new y. */
static lv_coord_t pq_place_kpis(w_pq_ctx_t *ctx, const pq_kpi_item_t *items, int count, lv_coord_t y, lv_coord_t limit, lv_coord_t w,
                                lv_coord_t pad, lv_coord_t gap, lv_coord_t meta_h, lv_coord_t lab_h)
{
    const lv_coord_t block = meta_h + lab_h + gap;
    const lv_coord_t col_w = count > 1 ? (w - 2 * pad - gap) / 2 : w - 2 * pad;
    int shown = 0;
    for (int i = 0; i < count && i < PQ_KPIS; i++) {
        const int row = i / 2;
        const int col = i % 2;
        const lv_coord_t top = y + row * (block + gap);
        if (top + block > limit) break;
        const lv_coord_t x = pad + col * (col_w + gap);
        lv_label_set_text(ctx->kpi[i].name, items[i].name);
        lv_label_set_text(ctx->kpi[i].value, items[i].value);
        lv_obj_set_style_text_color(ctx->kpi[i].value, lv_color_hex(items[i].colored ? items[i].color : APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
        lv_obj_set_size(ctx->kpi[i].name, col_w, meta_h);
        lv_obj_set_size(ctx->kpi[i].value, col_w, lab_h);
        lv_obj_align(ctx->kpi[i].name, LV_ALIGN_TOP_LEFT, x, top);
        lv_obj_align(ctx->kpi[i].value, LV_ALIGN_TOP_LEFT, x, top + meta_h);
        pq_set_hidden(ctx->kpi[i].name, false);
        pq_set_hidden(ctx->kpi[i].value, false);
        shown = i + 1;
    }
    if (shown == 0) return y;
    const int rows = (shown + 1) / 2;
    return y + rows * block + (rows - 1) * gap + gap;
}

/* The holdings as rows: name, a right-hand figure and a weight bar. `ws` rows carry the interval's return, sensor rows the weight. */
static void pq_place_rows(w_pq_ctx_t *ctx, bool ws, lv_coord_t y, lv_coord_t limit, lv_coord_t w, lv_coord_t pad, lv_coord_t gap,
                          lv_coord_t meta_h)
{
    const lv_coord_t row_h = meta_h + PQ_BAR_H + 10;
    const parqet_perf_holding_t *list = ctx->sort == PQ_TOP ? ctx->perf.top : ctx->sort == PQ_FLOP ? ctx->perf.flop : ctx->perf.largest;
    const int list_count = ctx->sort == PQ_TOP ? ctx->perf.top_count : ctx->sort == PQ_FLOP ? ctx->perf.flop_count : ctx->perf.largest_count;
    const int avail = ws ? list_count : ctx->attrs.count;
    int rows = avail;
    while (rows > 0 && y + rows * row_h > limit) rows--;
    if (rows < 2 && avail >= 2) rows = 0; /* a single holding says little: leave the space empty */
    double max_weight = 0.0;
    for (int i = 0; i < rows; i++) {
        const double weight = ws ? list[i].weight : ctx->attrs.holding[i].weight;
        if (weight > max_weight) max_weight = weight;
    }
    const lv_font_t *meta = ui_font_role(UI_TEXT_META);
    for (int i = 0; i < PQ_ROWS; i++) {
        const bool on = i < rows;
        pq_set_hidden(ctx->row[i].name, !on);
        pq_set_hidden(ctx->row[i].right, !on);
        pq_set_hidden(ctx->row[i].bar, !on);
        if (!on) continue;
        const char *name = ws ? list[i].name : ctx->attrs.holding[i].name;
        const double weight = ws ? list[i].weight : ctx->attrs.holding[i].weight;
        char right[32];
        uint32_t color = APP_UI_COLOR_TEXT_SOFT;
        if (ws) {
            if (list[i].has_return) {
                pq_percent(right, sizeof(right), list[i].ret, 1, true);
                color = pq_sign_color(list[i].ret);
            } else {
                snprintf(right, sizeof(right), "--");
            }
        } else {
            pq_percent(right, sizeof(right), weight, 1, false);
        }
        const lv_coord_t rw = pq_text_w(right, meta) + 2;
        lv_label_set_text(ctx->row[i].right, right);
        lv_obj_set_style_text_color(ctx->row[i].right, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_size(ctx->row[i].right, rw, meta_h);
        lv_obj_align(ctx->row[i].right, LV_ALIGN_TOP_RIGHT, -pad, y);
        lv_label_set_text(ctx->row[i].name, name);
        lv_obj_set_size(ctx->row[i].name, w - 2 * pad - rw - gap, meta_h);
        lv_obj_align(ctx->row[i].name, LV_ALIGN_TOP_LEFT, pad, y);
        const lv_coord_t full = w - 2 * pad;
        lv_coord_t bw = max_weight > 0.0 ? (lv_coord_t)(full * (weight / max_weight)) : 0;
        if (bw < 2) bw = 2;
        lv_obj_set_size(ctx->row[i].bar, bw, PQ_BAR_H);
        lv_obj_align(ctx->row[i].bar, LV_ALIGN_TOP_LEFT, pad, y + meta_h + 1);
        y += row_h;
    }
}

static void pq_layout(w_pq_ctx_t *ctx)
{
    const lv_coord_t w = lv_obj_get_width(ctx->card);
    const lv_coord_t h = lv_obj_get_height(ctx->card);
    const lv_coord_t pad = 12, gap = 4, limit = h - 6;
    const lv_font_t *meta = ui_font_role(UI_TEXT_META);
    const lv_font_t *key = ui_font_role(UI_TEXT_KEY);
    const lv_font_t *lab = ui_font_role(UI_TEXT_LABEL);
    const lv_coord_t meta_h = lv_font_get_line_height(meta);
    const lv_coord_t key_h = lv_font_get_line_height(key);
    const lv_coord_t lab_h = lv_font_get_line_height(lab);
    const bool ws = pq_ws_mode(ctx);
    lv_coord_t y = 8;
    pq_hide_body(ctx);

    /* the interval at the top right (the chips name it when they are shown) */
    const bool chips = ctx->selector && h >= 200;
    const bool tabs = ctx->selector && h >= 260;
    char interval[12];
    if (ws) snprintf(interval, sizeof(interval), "%s", PQ_INTERVAL_TEXT[ctx->interval]);
    else parqet_interval_label(ctx->attrs.interval, interval, sizeof(interval));
    lv_coord_t interval_w = 0;
    if (!chips && interval[0] != '\0') {
        lv_label_set_text(ctx->interval_label, interval);
        interval_w = pq_text_w(interval, meta) + 2;
        lv_obj_set_size(ctx->interval_label, interval_w, meta_h);
        lv_obj_align(ctx->interval_label, LV_ALIGN_TOP_RIGHT, -pad, y);
    }
    pq_set_hidden(ctx->interval_label, interval_w == 0);

    lv_obj_set_width(ctx->title_label, w - 2 * pad - (interval_w > 0 ? interval_w + gap : 0));
    lv_obj_set_height(ctx->title_label, meta_h);
    lv_obj_align(ctx->title_label, LV_ALIGN_TOP_LEFT, pad, y);
    y += ui_widget_name_height(ctx->title_label, meta) + ui_widget_name_gap(ctx->title_label, gap);

    /* tabs and interval chips (option parqet_selector) */
    const lv_coord_t chip_h = meta_h + 8;
    if (tabs) {
        const char *names[2] = {ui_i18n_get("parqet.performance", "Performance"), ui_i18n_get("parqet.holdings", "Holdings")};
        lv_coord_t x = pad;
        /* A narrow tile or larger text (German names at text step 2) can need more than the row has: the longer tab gives way, the
         * label clips instead of reaching outside the tile. */
        lv_coord_t tab_w[2] = {pq_text_w(names[0], meta) + 16, pq_text_w(names[1], meta) + 16};
        const lv_coord_t room = w - 2 * pad - gap;
        if (tab_w[0] + tab_w[1] > room) {
            const lv_coord_t half = room / 2;
            if (tab_w[0] <= half) tab_w[1] = room - tab_w[0];
            else if (tab_w[1] <= half) tab_w[0] = room - tab_w[1];
            else tab_w[0] = tab_w[1] = half;
        }
        for (int i = 0; i < 2; i++) {
            const lv_coord_t cw = tab_w[i];
            lv_label_set_text(ctx->tab[i], names[i]);
            lv_obj_set_size(ctx->tab[i], cw, chip_h);
            lv_obj_align(ctx->tab[i], LV_ALIGN_TOP_LEFT, x, y);
            pq_chip_style(ctx->tab[i], (int)ctx->view == i);
            x += cw + gap;
        }
        y += chip_h + gap;
    }
    for (int i = 0; i < 2; i++) pq_set_hidden(ctx->tab[i], !tabs);
    if (chips) {
        lv_coord_t total = 0;
        for (int i = 0; i < PQ_INTERVALS; i++) total += pq_text_w(PQ_INTERVAL_TEXT[i], meta) + 16 + gap;
        const lv_coord_t shrink = total > w - 2 * pad ? (total - (w - 2 * pad)) / PQ_INTERVALS + 1 : 0; /* a narrow tile trims the chip padding */
        lv_coord_t x = pad;
        for (int i = 0; i < PQ_INTERVALS; i++) {
            const lv_coord_t cw = pq_text_w(PQ_INTERVAL_TEXT[i], meta) + 16 - shrink;
            lv_label_set_text(ctx->chip[i], PQ_INTERVAL_TEXT[i]);
            lv_obj_set_size(ctx->chip[i], cw, chip_h);
            lv_obj_align(ctx->chip[i], LV_ALIGN_TOP_LEFT, x, y);
            pq_chip_style(ctx->chip[i], ctx->interval == i);
            x += cw + gap;
        }
        y += chip_h + gap;
    }
    for (int i = 0; i < PQ_INTERVALS; i++) pq_set_hidden(ctx->chip[i], !chips);

    /* the choice of holdings (largest, best, worst) belongs to the Holdings view */
    const bool sorts = tabs && ctx->view == PQ_HOLDINGS;
    if (sorts) {
        const char *names[PQ_SORTS] = {ui_i18n_get("parqet.largest", "Largest"), ui_i18n_get("parqet.top", "Top 5"), ui_i18n_get("parqet.flop", "Flop 5")};
        lv_coord_t x = pad;
        for (int i = 0; i < PQ_SORTS; i++) {
            const lv_coord_t cw = pq_text_w(names[i], meta) + 16;
            lv_label_set_text(ctx->sort_chip[i], names[i]);
            lv_obj_set_size(ctx->sort_chip[i], cw, chip_h);
            lv_obj_align(ctx->sort_chip[i], LV_ALIGN_TOP_LEFT, x, y);
            pq_chip_style(ctx->sort_chip[i], (int)ctx->sort == i);
            x += cw + gap;
        }
        y += chip_h + gap;
    }
    for (int i = 0; i < PQ_SORTS; i++) pq_set_hidden(ctx->sort_chip[i], !sorts);

    /* nothing to show yet, or the other interval is still on its way */
    const bool have_sensor = ctx->has_value;
    const bool have_ws = ws && ctx->loaded_interval == ctx->interval;
    if ((!ws && !have_sensor) || (ws && !have_ws)) {
        const char *text = (ws && ctx->attrs.entry_id[0] != '\0') ? pq_status_text(ctx) : ui_i18n_get("parqet.no_data", "No data");
        lv_label_set_text(ctx->status_label, text);
        lv_obj_set_width(ctx->status_label, w - 2 * pad);
        lv_obj_align(ctx->status_label, LV_ALIGN_TOP_LEFT, pad, y);
        pq_set_hidden(ctx->status_label, false);
        return;
    }
    pq_set_hidden(ctx->status_label, true);

    if (ctx->view == PQ_HOLDINGS) {
        if (!ws && ctx->sort != PQ_LARGEST) { /* the sensors know the weights only: best and worst need an interval */
            lv_label_set_text(ctx->status_label, ui_i18n_get("parqet.pick_interval", "Pick an interval"));
            lv_obj_set_width(ctx->status_label, w - 2 * pad);
            lv_obj_align(ctx->status_label, LV_ALIGN_TOP_LEFT, pad, y);
            pq_set_hidden(ctx->status_label, false);
            return;
        }
        pq_place_rows(ctx, ws, y, limit, w, pad, gap, meta_h);
        return;
    }

    /* Performance: the hero first */
    char hero[48];
    char gain_text[96] = "";
    double hero_sign = 0.0;
    pq_kpi_item_t items[PQ_KPIS];
    int item_count = 0;
    if (ws) {
        const parqet_perf_t *p = &ctx->perf;
        if (ctx->hide_amounts) {
            pq_percent(hero, sizeof(hero), p->unrealized_return, 1, true);
            hero_sign = p->unrealized_return;
        } else {
            pq_money(ctx, hero, sizeof(hero), p->value_end, false);
            char amount[48], pct[24];
            pq_money(ctx, amount, sizeof(amount), p->unrealized_gain, true);
            pq_percent(pct, sizeof(pct), p->unrealized_return, 1, true);
            snprintf(gain_text, sizeof(gain_text), "%s  %s", amount, pct);
        }
        pq_kpi_item_t *k = &items[item_count++];
        snprintf(k->name, sizeof(k->name), "%s", ui_i18n_get("parqet.xirr", "XIRR"));
        pq_percent(k->value, sizeof(k->value), p->xirr, 1, true);
        k->color = pq_sign_color(p->xirr);
        k->colored = true;
        k = &items[item_count++];
        snprintf(k->name, sizeof(k->name), "%s", ui_i18n_get("parqet.ttwror", "TTWROR"));
        pq_percent(k->value, sizeof(k->value), p->ttwror, 1, true);
        k->color = pq_sign_color(p->ttwror);
        k->colored = true;
        if (!ctx->hide_amounts) {
            k = &items[item_count++];
            snprintf(k->name, sizeof(k->name), "%s", ui_i18n_get("parqet.dividends", "Dividends"));
            pq_money(ctx, k->value, sizeof(k->value), p->dividends, false);
            k->colored = false;
            k = &items[item_count++];
            snprintf(k->name, sizeof(k->name), "%s", ui_i18n_get("parqet.realized", "Realised"));
            pq_money(ctx, k->value, sizeof(k->value), p->realized_gain, true);
            k->color = pq_sign_color(p->realized_gain);
            k->colored = true;
        }
    } else {
        /* the sensors' own figures; a sensor in another currency or unit than expected is left out, not converted */
        const bool gain_ok = ctx->has_gain && pq_money_unit_ok(ctx, ctx->gain_unit);
        const bool div_ok = ctx->has_div && pq_money_unit_ok(ctx, ctx->div_unit);
        const bool xirr_ok = ctx->has_xirr && pq_percent_unit_ok(ctx->xirr_unit);
        const bool ret_ok = ctx->has_ret && pq_percent_unit_ok(ctx->ret_unit);
        const bool unit_mismatch = (ctx->has_gain && !gain_ok) || (ctx->has_div && !div_ok) || (ctx->has_xirr && !xirr_ok) ||
                                   (ctx->has_ret && !ret_ok);
        if (ctx->hide_amounts) {
            if (ret_ok) {
                pq_percent(hero, sizeof(hero), ctx->ret, 1, true);
                hero_sign = ctx->ret;
            } else if (xirr_ok) {
                pq_percent(hero, sizeof(hero), ctx->xirr, 1, true);
                hero_sign = ctx->xirr;
            } else {
                snprintf(hero, sizeof(hero), "%s", PQ_HIDDEN_TEXT);
            }
        } else {
            pq_money(ctx, hero, sizeof(hero), ctx->value, false);
            if (gain_ok || ret_ok) {
                char amount[48] = "";
                char pct[24] = "";
                if (gain_ok) pq_money(ctx, amount, sizeof(amount), ctx->gain, true);
                if (ret_ok) pq_percent(pct, sizeof(pct), ctx->ret, 1, true);
                snprintf(gain_text, sizeof(gain_text), "%s%s%s", amount, amount[0] != '\0' && pct[0] != '\0' ? "  " : "", pct);
            }
        }
        if (xirr_ok) {
            pq_kpi_item_t *k = &items[item_count++];
            snprintf(k->name, sizeof(k->name), "%s", ui_i18n_get("parqet.xirr", "XIRR"));
            pq_percent(k->value, sizeof(k->value), ctx->xirr, 1, true);
            k->color = pq_sign_color(ctx->xirr);
            k->colored = true;
        }
        if (div_ok && !ctx->hide_amounts) {
            pq_kpi_item_t *k = &items[item_count++];
            snprintf(k->name, sizeof(k->name), "%s", ui_i18n_get("parqet.dividends", "Dividends"));
            pq_money(ctx, k->value, sizeof(k->value), ctx->dividends, false);
            k->colored = false;
        }
        if (unit_mismatch) {
            const size_t used = strlen(gain_text);
            snprintf(gain_text + used, sizeof(gain_text) - used, "%s%s", used > 0 ? "  " : "",
                ui_i18n_get("parqet.unit_mismatch", "Unit differs"));
        }
    }
    lv_label_set_text(ctx->hero_label, hero);
    lv_obj_set_width(ctx->hero_label, w - 2 * pad);
    lv_obj_set_height(ctx->hero_label, key_h);
    lv_obj_align(ctx->hero_label, LV_ALIGN_TOP_LEFT, pad, y);
    lv_obj_set_style_text_color(ctx->hero_label,
        lv_color_hex(ctx->hide_amounts && hero_sign != 0.0 ? pq_sign_color(hero_sign) : APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    pq_set_hidden(ctx->hero_label, false);
    y += key_h + gap;

    if (gain_text[0] != '\0' && y + lab_h <= limit) {
        lv_label_set_text(ctx->gain_label, gain_text);
        lv_obj_set_width(ctx->gain_label, w - 2 * pad);
        lv_obj_set_height(ctx->gain_label, lab_h);
        lv_obj_align(ctx->gain_label, LV_ALIGN_TOP_LEFT, pad, y);
        lv_obj_set_style_text_color(ctx->gain_label,
            lv_color_hex(pq_sign_color(ws ? ctx->perf.unrealized_gain : (ctx->has_gain && pq_money_unit_ok(ctx, ctx->gain_unit) ? ctx->gain : ctx->ret))), LV_PART_MAIN);
        pq_set_hidden(ctx->gain_label, false);
        y += lab_h + gap;
    }

    y = pq_place_kpis(ctx, items, item_count, y, limit, w, pad, gap, meta_h, lab_h);

    if (ws) {
        /* the value curve in the space that is left (at least a thumbnail high) */
        const parqet_perf_t *p = &ctx->perf;
        const lv_coord_t room = limit - y;
        if (p->point_count >= 2 && room >= 28) {
            float lo = p->point[0], hi = p->point[0];
            for (int i = 1; i < p->point_count; i++) {
                if (p->point[i] < lo) lo = p->point[i];
                if (p->point[i] > hi) hi = p->point[i];
            }
            ctx->spark_min = lo;
            ctx->spark_max = hi;
            ctx->spark_on = true;
            lv_obj_set_size(ctx->spark, w - 2 * pad, room > 90 ? 90 : room);
            lv_obj_align(ctx->spark, LV_ALIGN_TOP_LEFT, pad, y);
            pq_set_hidden(ctx->spark, false);
            lv_obj_invalidate(ctx->spark);
        }
    } else if (ctx->has_attrs && ctx->attrs.count > 0) {
        pq_place_rows(ctx, false, y, limit, w, pad, gap, meta_h);
    }
}

static void pq_spark_draw_cb(lv_event_t *e)
{
    w_pq_ctx_t *ctx = (w_pq_ctx_t *)lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    if (ctx == NULL || layer == NULL || !ctx->spark_on || ctx->perf.point_count < 2) return;
    lv_area_t area;
    lv_obj_get_coords(ctx->spark, &area);
    const int32_t w = lv_area_get_width(&area);
    const int32_t h = lv_area_get_height(&area);
    if (w < 8 || h < 6) return;
    const float span = ctx->spark_max - ctx->spark_min;
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    const bool up = ctx->perf.point[ctx->perf.point_count - 1] >= ctx->perf.point[0];
    line.color = lv_color_hex(up ? APP_UI_COLOR_OK : APP_UI_COLOR_ERROR);
    line.width = 2;
    line.opa = LV_OPA_COVER;
    const int n = ctx->perf.point_count;
    for (int i = 0; i + 1 < n; i++) {
        const float a = span > 0.0f ? (ctx->perf.point[i] - ctx->spark_min) / span : 0.5f;
        const float b = span > 0.0f ? (ctx->perf.point[i + 1] - ctx->spark_min) / span : 0.5f;
        line.p1.x = area.x1 + (int32_t)((int64_t)i * (w - 1) / (n - 1));
        line.p1.y = area.y2 - 1 - (int32_t)(a * (float)(h - 3) + 0.5f);
        line.p2.x = area.x1 + (int32_t)((int64_t)(i + 1) * (w - 1) / (n - 1));
        line.p2.y = area.y2 - 1 - (int32_t)(b * (float)(h - 3) + 0.5f);
        lv_draw_line(layer, &line);
    }
}

static void pq_refresh(w_pq_ctx_t *ctx)
{
    if (ctx == NULL || ctx->card == NULL) return;
    ui_fonts_tile_begin("parqet_portfolio", (int)lv_obj_get_width(ctx->card), (int)lv_obj_get_height(ctx->card));
    pq_layout(ctx);
    ui_fonts_tile_end();
}

static void pq_tab_clicked(lv_event_t *e)
{
    w_pq_ctx_t *ctx = (w_pq_ctx_t *)lv_event_get_user_data(e);
    lv_obj_t *target = lv_event_get_target(e);
    if (ctx == NULL) return;
    ctx->view = target == ctx->tab[1] ? PQ_HOLDINGS : PQ_PERFORMANCE;
    pq_refresh(ctx);
}

static void pq_sort_clicked(lv_event_t *e)
{
    w_pq_ctx_t *ctx = (w_pq_ctx_t *)lv_event_get_user_data(e);
    lv_obj_t *target = lv_event_get_target(e);
    if (ctx == NULL) return;
    for (int i = 0; i < PQ_SORTS; i++) {
        if (target == ctx->sort_chip[i]) ctx->sort = (pq_sort_t)i;
    }
    pq_refresh(ctx);
}

static void pq_chip_clicked(lv_event_t *e)
{
    w_pq_ctx_t *ctx = (w_pq_ctx_t *)lv_event_get_user_data(e);
    lv_obj_t *target = lv_event_get_target(e);
    if (ctx == NULL) return;
    for (int i = 0; i < PQ_INTERVALS; i++) {
        if (target == ctx->chip[i] && ctx->interval != i) {
            ctx->interval = i;
            if (ctx->load == PQ_LOADING) parqet_request_cancel(ctx); /* the answer of the interval left is not needed any more */
            ctx->load = PQ_NO_REQUEST;
            pq_request(ctx);
            break;
        }
    }
    pq_refresh(ctx);
}

static void pq_event_cb(lv_event_t *e)
{
    w_pq_ctx_t *ctx = (w_pq_ctx_t *)lv_event_get_user_data(e);
    if (ctx == NULL) return;
    if (lv_event_get_code(e) == LV_EVENT_DELETE) {
        parqet_request_cancel(ctx);
        free(ctx);
    } else if (lv_event_get_code(e) == LV_EVENT_SIZE_CHANGED) {
        pq_refresh(ctx);
    }
}

static lv_obj_t *pq_chip(w_pq_ctx_t *ctx, lv_event_cb_t cb)
{
    lv_obj_t *c = lv_label_create(ctx->card);
    ui_text_set(c, UI_TEXT_META, true);
    lv_label_set_long_mode(c, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(c, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_radius(c, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_top(c, 3, LV_PART_MAIN);
    lv_obj_set_clickable(c, true);
    lv_obj_add_event_cb(c, cb, LV_EVENT_CLICKED, ctx);
    return c;
}

esp_err_t w_parqet_portfolio_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    w_pq_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    strlcpy(ctx->entity_id, def->entity_id, sizeof(ctx->entity_id));
    strlcpy(ctx->gain_entity, def->secondary_entity_id, sizeof(ctx->gain_entity));
    strlcpy(ctx->xirr_entity, def->tertiary_entity_id, sizeof(ctx->xirr_entity));
    strlcpy(ctx->div_entity, def->extra_entity_id[UI_PARQET_EXTRA_DIVIDENDS], sizeof(ctx->div_entity));
    strlcpy(ctx->ret_entity, def->extra_entity_id[UI_PARQET_EXTRA_RETURN], sizeof(ctx->ret_entity));
    ctx->hide_amounts = def->parqet_hide_amounts;
    ctx->selector = def->parqet_selector;
    ctx->view = strcmp(def->parqet_view, "holdings") == 0 ? PQ_HOLDINGS : PQ_PERFORMANCE;
    ctx->interval = PQ_MAX_INDEX;
    for (int i = 0; i < PQ_INTERVALS; i++) {
        if (strcmp(def->parqet_interval, PQ_INTERVAL_CODE[i]) == 0) ctx->interval = i;
    }
    ctx->loaded_interval = -1;
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

    ctx->interval_label = pq_label(card, UI_TEXT_META, APP_UI_COLOR_TEXT_SOFT);
    for (int i = 0; i < 2; i++) ctx->tab[i] = pq_chip(ctx, pq_tab_clicked);
    for (int i = 0; i < PQ_INTERVALS; i++) ctx->chip[i] = pq_chip(ctx, pq_chip_clicked);
    for (int i = 0; i < PQ_SORTS; i++) ctx->sort_chip[i] = pq_chip(ctx, pq_sort_clicked);
    ctx->hero_label = pq_label(card, UI_TEXT_KEY, APP_UI_COLOR_TEXT_PRIMARY);
    ctx->gain_label = pq_label(card, UI_TEXT_LABEL, APP_UI_COLOR_TEXT_SOFT);
    for (int i = 0; i < PQ_KPIS; i++) {
        ctx->kpi[i].name = pq_label(card, UI_TEXT_META, APP_UI_COLOR_TEXT_SOFT);
        ctx->kpi[i].value = pq_label(card, UI_TEXT_LABEL, APP_UI_COLOR_TEXT_PRIMARY);
    }
    ctx->spark = lv_obj_create(card);
    lv_obj_remove_style_all(ctx->spark);
    lv_obj_set_scrollable(ctx->spark, false);
    lv_obj_set_clickable(ctx->spark, false);
    lv_obj_add_event_cb(ctx->spark, pq_spark_draw_cb, LV_EVENT_DRAW_POST_END, ctx);
    ctx->status_label = pq_label(card, UI_TEXT_LABEL, APP_UI_COLOR_TEXT_SOFT);
    lv_label_set_long_mode(ctx->status_label, LV_LABEL_LONG_WRAP);
    for (int i = 0; i < PQ_ROWS; i++) {
        ctx->row[i].name = pq_label(card, UI_TEXT_META, APP_UI_COLOR_TEXT_PRIMARY);
        lv_label_set_long_mode(ctx->row[i].name, LV_LABEL_LONG_DOT);
        ctx->row[i].right = pq_label(card, UI_TEXT_META, APP_UI_COLOR_TEXT_SOFT);
        lv_obj_set_style_text_align(ctx->row[i].right, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        ctx->row[i].bar = lv_obj_create(card);
        lv_obj_remove_style_all(ctx->row[i].bar);
        lv_obj_set_scrollable(ctx->row[i].bar, false);
        lv_obj_set_clickable(ctx->row[i].bar, false);
        lv_obj_set_style_bg_opa(ctx->row[i].bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(ctx->row[i].bar, lv_color_hex(APP_UI_COLOR_TOPBAR_STATUS_ON), LV_PART_MAIN);
        lv_obj_set_style_radius(ctx->row[i].bar, 2, LV_PART_MAIN);
    }

    lv_obj_add_event_cb(card, pq_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, pq_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    pq_refresh(ctx);
    return ESP_OK;
}

void w_parqet_portfolio_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->ctx == NULL || state == NULL) return;
    w_pq_ctx_t *ctx = (w_pq_ctx_t *)instance->ctx;
    double v = 0.0;
    const bool ok = parqet_number(state->state, &v);
    parqet_attrs_t own;
    (void)parqet_attrs_parse(state->attributes_json, &own);
    if (strcmp(state->entity_id, ctx->entity_id) == 0) {
        ctx->has_value = ok;
        ctx->value = ok ? v : 0.0;
        ctx->has_attrs = parqet_attrs_parse(state->attributes_json, &ctx->attrs);
        pq_request(ctx); /* the attributes carry the ids the WebSocket command needs; every update also renews an old answer */
    }
    /* one sensor can fill several roles (the same entity as gain and dividends is the user's choice) */
    if (ctx->gain_entity[0] != '\0' && strcmp(state->entity_id, ctx->gain_entity) == 0) {
        ctx->has_gain = ok;
        snprintf(ctx->gain_unit, sizeof(ctx->gain_unit), "%s", own.unit);
        ctx->gain = ok ? v : 0.0;
    }
    if (ctx->xirr_entity[0] != '\0' && strcmp(state->entity_id, ctx->xirr_entity) == 0) {
        ctx->has_xirr = ok;
        snprintf(ctx->xirr_unit, sizeof(ctx->xirr_unit), "%s", own.unit);
        ctx->xirr = ok ? v : 0.0;
    }
    if (ctx->div_entity[0] != '\0' && strcmp(state->entity_id, ctx->div_entity) == 0) {
        ctx->has_div = ok;
        snprintf(ctx->div_unit, sizeof(ctx->div_unit), "%s", own.unit);
        ctx->dividends = ok ? v : 0.0;
    }
    if (ctx->ret_entity[0] != '\0' && strcmp(state->entity_id, ctx->ret_entity) == 0) {
        ctx->has_ret = ok;
        snprintf(ctx->ret_unit, sizeof(ctx->ret_unit), "%s", own.unit);
        ctx->ret = ok ? v : 0.0;
    }
    pq_refresh(ctx);
}

void w_parqet_portfolio_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) return;
    w_pq_ctx_t *ctx = (w_pq_ctx_t *)instance->ctx;
    ctx->has_value = false;
    ctx->has_attrs = false;
    pq_refresh(ctx);
}
