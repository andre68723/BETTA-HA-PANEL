/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/ui_days_view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_config.h"
#include "ha/ha_history_stats.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/theme/ui_typography.h"
#include "ui/ui_i18n.h"
#include "util/chart_scale.h"

static lv_obj_t *new_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color, int x, int y,
                           int w)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    lv_obj_set_height(label, font->line_height);
    ui_text_record(label, UI_TEXT_LABEL, y > 0);
    return label;
}

/* The first two characters (not bytes) of a weekday name: "Mo", "Tu". */
void ui_days_weekday_short(int wday, char *out, size_t out_len)
{
    char key[24];
    static const char *const names[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    if (wday < 0 || wday > 6) wday = 0;
    snprintf(key, sizeof(key), "weather.day_full_%d", wday);
    const char *name = ui_i18n_get(key, names[wday]);
    size_t n = 0;
    int chars = 0;
    while (name[n] != '\0' && chars < 2) {
        n++;
        while ((name[n] & 0xC0) == 0x80) n++; /* the rest of a multi-byte character */
        chars++;
    }
    if (n >= out_len) n = out_len - 1;
    memcpy(out, name, n);
    out[n] = '\0';
}


static void render_days(const ui_days_view_t *view, const ha_history_days_t *result)
{
    const ha_stats_days_t *days = &result->days;
    float total = 0.0f, highest = 0.0f;
    bool partial = false;
    for (size_t i = 0; i < days->count; i++) {
        const ha_stats_day_t *day = &days->day[i];
        if (day->valid > 0) {
            total += day->total;
            if (day->total > highest) highest = day->total;
        }
        if (day->hours > 0 && day->complete < day->hours) partial = true;
    }
    const lv_font_t *meta = ui_font_role(UI_TEXT_META);
    const int bw = lv_obj_get_width(view->body), bh = lv_obj_get_height(view->body);
    const int decimals = highest >= 100.0f ? 0 : (highest >= 10.0f ? 1 : 2);
    char total_text[32], summary[160];
    chart_scale_format(total_text, sizeof(total_text), total, decimals, "");
    snprintf(summary, sizeof(summary), "%s  ·  %s %s", ui_i18n_get("history.days_total", "Last 7 days"), total_text,
             result->unit);
    new_label(view->body, summary, ui_font_role(UI_TEXT_LABEL), APP_UI_COLOR_TEXT_PRIMARY, 8, 6, bw - 16);

    const int summary_h = (int)lv_font_get_line_height(ui_font_role(UI_TEXT_LABEL));
    const int label_h = (int)meta->line_height;
    const int top = 6 + summary_h + label_h + 10;            /* room for the value above the tallest bar */
    int plot_h = bh - top - label_h - 14;
    if (plot_h > 235) plot_h = 235;
    if (plot_h < 60) plot_h = 60;
    const int col_w = (bw - 16) / (int)(days->count > 0 ? days->count : 1);
    const int bar_w = col_w * 6 / 10;

    lv_obj_t *base = lv_obj_create(view->body);
    lv_obj_remove_style_all(base);
    lv_obj_set_pos(base, 8, top + plot_h);
    lv_obj_set_size(base, bw - 16, 2);
    lv_obj_set_style_bg_color(base, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(base, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_clickable(base, false);

    for (size_t i = 0; i < days->count; i++) {
        const ha_stats_day_t *day = &days->day[i];
        const int x = 8 + (int)i * col_w;
        char value[24] = "-";
        int bar_h = 0;
        if (day->valid > 0) {
            chart_scale_format(value, sizeof(value), day->total, decimals, "");
            bar_h = highest > 0.0f ? (int)lroundf(day->total / highest * (float)plot_h) : 0;
            if (day->total > 0.0f && bar_h < 3) bar_h = 3;
        }
        if (bar_h > 0) {
            lv_obj_t *bar = lv_obj_create(view->body);
            lv_obj_remove_style_all(bar);
            lv_obj_set_pos(bar, x + (col_w - bar_w) / 2, top + plot_h - bar_h);
            lv_obj_set_size(bar, bar_w, bar_h);
            lv_obj_set_style_bg_color(bar, lv_color_hex(view->color), LV_PART_MAIN);
            /* an incomplete day (today, or an hour or a source missing) is lighter: its sum is a lower bound */
            const bool incomplete = day->today || day->complete < day->hours;
            lv_obj_set_style_bg_opa(bar, incomplete ? LV_OPA_50 : LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_radius(bar, ui_style_radius(6), LV_PART_MAIN);
            lv_obj_set_clickable(bar, false);
        }
        lv_obj_t *v = new_label(view->body, value, meta, APP_UI_COLOR_TEXT_PRIMARY, x, top + plot_h - bar_h - label_h - 2, col_w);
        lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

        char name[24];
        if (day->today) {
            snprintf(name, sizeof(name), "%s", ui_i18n_get("weather.today", "Today"));
        } else {
            time_t at = (time_t)day->start;
            struct tm local;
            char wd[8] = "";
            int mday = 0;
            if (localtime_r(&at, &local) != NULL) {
                ui_days_weekday_short(local.tm_wday, wd, sizeof(wd));
                mday = local.tm_mday;
            }
            snprintf(name, sizeof(name), "%s %d", wd, mday);
        }
        /* a little wider than the column, centred: "Heute" at a larger text size would otherwise be cut off */
        lv_obj_t *l = new_label(view->body, name, meta, APP_UI_COLOR_TEXT_MUTED, x - 7, top + plot_h + 8, col_w + 14);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    const bool missing_sources = result->sources_ok < result->sources_total;
    if (partial || missing_sources) {
        lv_label_set_text(view->status, missing_sources ? ui_i18n_get("history.days_sources", "Some sources are missing")
                                                           : ui_i18n_get("history.days_partial", "Lighter bars: incomplete day"));
    } else {
        lv_label_set_text(view->status, ui_i18n_get("history.days_title", "Daily consumption"));
    }
}


void ui_days_view_show(const ui_days_view_t *view, const ha_history_days_t *result)
{
    if (view == NULL || view->body == NULL || view->status == NULL || result == NULL) return;
    lv_obj_clean(view->body);
    if (result->status != ESP_OK) {
        lv_label_set_text(view->status, ui_i18n_get("history.week_error", "Could not load statistics"));
    } else if (result->sources_eligible == 0) {
        lv_label_set_text(view->status, ui_i18n_get("history.days_unsupported", "No statistics for this source"));
    } else if (result->outcome != HA_STATS_OUTCOME_OK) {
        lv_label_set_text(view->status, ui_i18n_get("history.week_empty", "No statistics for the last 7 days"));
    } else {
        render_days(view, result);
    }
}

#ifdef BETTA_SIMULATOR
ha_history_days_t *ui_days_view_simulated(bool cubic_metres)
{
    ha_history_days_t *result = calloc(1, sizeof(*result));
    if (result == NULL) return NULL;
    const time_t now = time(NULL);
    result->status = ESP_OK;
    result->outcome = HA_STATS_OUTCOME_OK;
    result->sources_total = result->sources_eligible = result->sources_ok = 1;
    strlcpy(result->unit, cubic_metres ? "m\xC2\xB3" : "L", sizeof(result->unit));
    static const float litres[HA_DAYS_COUNT] = {212.0f, 168.0f, 0.0f, 301.0f, 244.0f, 189.0f, 96.0f};
    const float scale = cubic_metres ? 0.01f : 1.0f;
    result->days.count = HA_DAYS_COUNT;
    for (int d = 0; d < HA_DAYS_COUNT; d++) {
        ha_stats_day_t *day = &result->days.day[d];
        day->start = ha_stats_local_day_start((int64_t)now, d - (HA_DAYS_COUNT - 1));
        day->hours = d == HA_DAYS_COUNT - 1 ? 14 : 24;
        day->today = d == HA_DAYS_COUNT - 1;
        if (d == 2) continue;                       /* no statistics that day */
        day->valid = day->complete = day->hours;
        day->total = litres[d] * scale;
    }
    return result;
}
#endif
