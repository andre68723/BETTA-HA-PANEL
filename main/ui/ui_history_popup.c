/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Shared Recorder history popup for sensors, binary sensors and locks.
 */
#include "ui/ui_history_popup.h"

#include "ha/ha_alarm_state.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"

#include "app_config.h"
#include "ha/ha_history.h"
#include "ha/ha_history_stats.h"
#include "ha/ha_history_week.h"
#include "ha/ha_model.h"
#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_days_view.h"
#include "ui/ui_evcc_sessions_view.h"
#include "ui/ui_memory.h"
#include "ui/ui_i18n.h"
#include "ui/ui_popup.h"
#include "util/chart_scale.h"

#ifdef BETTA_SIMULATOR
#include <math.h>
#define ha_history_snapshot_free free
#define ha_history_week_free free
#define ha_history_days_free free
#define ha_history_range_free free
#endif

typedef struct {
    lv_obj_t *overlay;
    lv_obj_t *card;
    lv_obj_t *status;
    lv_obj_t *body;
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    char current[96];
    char device_class[24];
    bool numeric;
    uint32_t generation;       /* the 24 h request */
    /* 7-day view: a metadata check decides whether the selector exists; every operation has its own identity so a
     * check or a week answer can never be taken for the 24 h chart. */
    uint32_t check_generation;
    uint32_t week_generation;
    bool week_possible;        /* a numeric sensor with a measurement state class: a check was sent */
    bool week_offered;         /* the check confirmed the week view: the selector is shown */
    bool week_active;
    bool week_convert;
    char week_convert_class[HA_STATS_CLASS_MAX];
    char week_unit[HA_STATS_UNIT_MAX];
    lv_obj_t *btn_day;
    lv_obj_t *btn_week;
    int row_y;                 /* top of the selector/status row, below the title at any text size */
    /* daily consumption view (Energy page nodes): no selector, seven bars from hourly statistics */
    bool days_mode;
    uint32_t days_generation;
    /* range view (a quote): 30 days of daily or 12 months of weekly means; the two selector buttons switch between them */
    bool range_mode;
    uint32_t range_generation;
    ha_range_kind_t range_kind;
    uint32_t bar_color;
    char ids[HA_DAYS_MAX_SOURCES][APP_MAX_ENTITY_ID_LEN];
    uint8_t id_count;
    lv_timer_t *sessions_timer;
    evcc_sessions_t *sessions;
    uint32_t sessions_version;
} history_popup_t;

static history_popup_t *s_popup;
static uint32_t s_generation;

static void popup_deleted(lv_event_t *event)
{
    history_popup_t *popup = (history_popup_t *)lv_event_get_user_data(event);
    if (s_popup == popup) s_popup = NULL;
    if (popup->sessions_timer) lv_timer_delete(popup->sessions_timer);
    free(popup->sessions);
    free(popup);
}

static void popup_close(lv_event_t *event)
{
    history_popup_t *popup = (history_popup_t *)lv_event_get_user_data(event);
    if (popup && popup->overlay) lv_obj_delete_async(popup->overlay);
}

static void backdrop_clicked(lv_event_t *event)
{
    if (lv_event_get_target(event) == lv_event_get_current_target(event)) popup_close(event);
}

static lv_obj_t *new_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                           uint32_t color, int x, int y, int w)
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

typedef struct {
    float frac;            /* position along the plot, 0 .. 1 */
    char text[20];
} axis_label_t;

#define AXIS_LABELS_MAX 12

/* A value of point i, or false for a gap. */
typedef bool (*plot_value_fn)(const void *data, size_t i, float *out);

/* The shared line chart: summary line, scaled axis, grid, the series with gaps, value ticks and time labels. */
static void render_plot(history_popup_t *popup, float lowest, float highest, size_t points, uint8_t vertical_divs,
                        int label_w, plot_value_fn value_at, const void *data, const axis_label_t *labels,
                        size_t label_count)
{
    /* round ticks that enclose the data, with their unit: 0 .. 10000 W instead of -201.6 .. 10085.6 */
    chart_scale_t scale;
    chart_scale_nice(lowest, highest, 4, &scale);
    char unit[12];
    chart_scale_unit_of(popup->current, unit, sizeof(unit));
    char low_text[24], high_text[24], summary[256];
    const int extreme_decimals = scale.decimals > 0 || scale.step >= 10.0f ? scale.decimals : 1; /* Min/Max show at least one digit below a step of 10 */
    chart_scale_format(low_text, sizeof(low_text), lowest, extreme_decimals > 3 ? 3 : extreme_decimals, "");
    chart_scale_format(high_text, sizeof(high_text), highest, extreme_decimals > 3 ? 3 : extreme_decimals, unit);
    snprintf(summary, sizeof(summary), "%s  ·  %s %s  ·  %s %s", popup->current,
             ui_i18n_get("history.min", "Min"), low_text, ui_i18n_get("history.max", "Max"), high_text);
    new_label(popup->body, summary, ui_font_role(UI_TEXT_LABEL), APP_UI_COLOR_TEXT_PRIMARY, 8, 6,
              lv_obj_get_width(popup->body) - 16);

    const int axis_w = 96;          /* the value labels left of the plot */
    /* the plot starts below the summary line and the top axis label, which grow with the text size (50 px at the reference size) */
    const int summary_h = (int)lv_font_get_line_height(ui_font_role(UI_TEXT_LABEL));
    const int axis_h = (int)lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    const int chart_top = 6 + summary_h + axis_h / 2 > 50 ? 6 + summary_h + axis_h / 2 : 50, chart_h = 235;
    const int chart_w = lv_obj_get_width(popup->body) - axis_w - 8;
    lv_obj_t *chart = lv_chart_create(popup->body);
    lv_obj_set_size(chart, chart_w, chart_h);
    lv_obj_set_pos(chart, axis_w, chart_top);
    lv_obj_set_style_bg_color(chart, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(chart, 4, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR); /* a line, not a dot per bucket */
    lv_obj_set_style_line_color(chart, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_obj_set_style_line_opa(chart, LV_OPA_30, LV_PART_MAIN);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, (uint32_t)points);
    lv_chart_set_div_line_count(chart, (uint8_t)(scale.intervals + 1), vertical_divs); /* grid lines on the ticks */

    /* integer chart values: tenths of the step's last digit, kept far from int32 overflow */
    float mult = powf(10.0f, (float)scale.decimals + 1.0f);
    while (mult > 1.0f && fmaxf(fabsf(scale.min), fabsf(scale.max)) * mult > 1.0e9f) mult /= 10.0f;
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, (int32_t)lroundf(scale.min * mult),
                       (int32_t)lroundf(scale.max * mult));
    lv_chart_series_t *series = lv_chart_add_series(chart, lv_color_hex(APP_UI_COLOR_STATE_ON),
                                                    LV_CHART_AXIS_PRIMARY_Y);
    for (size_t i = 0; i < points; i++) {
        float value;
        lv_chart_set_value_by_id(chart, series, i, value_at(data, i, &value) ? (int32_t)lroundf(value * mult)
                                                                            : LV_CHART_POINT_NONE);
    }
    lv_chart_refresh(chart);

    const lv_font_t *meta = ui_font_role(UI_TEXT_META);
    const int plot_top = chart_top + 4, plot_h = chart_h - 8; /* inside the chart's padding */
    for (int i = 0; i <= scale.intervals; i++) {
        char value[24];
        chart_scale_format(value, sizeof(value), scale.max - scale.step * (float)i, scale.decimals, unit);
        int y = plot_top + plot_h * i / scale.intervals - meta->line_height / 2;
        lv_obj_t *label = new_label(popup->body, value, meta, APP_UI_COLOR_TEXT_MUTED, 0, y, axis_w - 8);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    }
    const int time_w = label_w > (int)(2.4f * (float)meta->line_height) ? label_w : (int)(2.4f * (float)meta->line_height); /* "00:00" must fit at larger text sizes */
    int last_x = -10000;
    for (size_t i = 0; i < label_count; i++) {
        int x = axis_w + 4 + (int)((float)(chart_w - 8) * labels[i].frac) - time_w / 2;
        if (x < axis_w - 10) x = axis_w - 10;
        if (x > axis_w + chart_w - time_w + 10) x = axis_w + chart_w - time_w + 10;
        if (x - last_x < time_w) continue; /* fewer labels instead of overlapping ones */
        last_x = x;
        lv_obj_t *label = new_label(popup->body, labels[i].text, meta, APP_UI_COLOR_TEXT_MUTED, x, chart_top + chart_h + 10, time_w);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
}

static bool day_value_at(const void *data, size_t i, float *out)
{
    const ha_history_bucket_t *bucket = &((const ha_history_snapshot_t *)data)->buckets[i];
    if (bucket->count == 0) return false;
    *out = bucket->sum / bucket->count;
    return true;
}

static void render_numeric(history_popup_t *popup, const ha_history_snapshot_t *snapshot)
{
    float lowest = INFINITY, highest = -INFINITY;
    for (size_t i = 0; i < HA_HISTORY_BUCKETS; i++) {
        const ha_history_bucket_t *bucket = &snapshot->buckets[i];
        if (bucket->count == 0) continue;
        if (bucket->min < lowest) lowest = bucket->min;
        if (bucket->max > highest) highest = bucket->max;
    }
    if (!isfinite(lowest) || !isfinite(highest)) return;

    axis_label_t labels[5];
    for (int i = 0; i <= 4; i++) {
        time_t at = snapshot->start_epoch + (snapshot->end_epoch - snapshot->start_epoch) * i / 4;
        struct tm local;
        snprintf(labels[i].text, sizeof(labels[i].text), "--:--");
        if (localtime_r(&at, &local)) strftime(labels[i].text, sizeof(labels[i].text), "%H:%M", &local);
        labels[i].frac = (float)i / 4.0f;
    }
    render_plot(popup, lowest, highest, HA_HISTORY_BUCKETS, 5, 80, day_value_at, snapshot, labels, 5); /* grid every 6 h */
}

static bool week_value_at(const void *data, size_t i, float *out)
{
    const ha_stats_series_t *series = (const ha_stats_series_t *)data;
    if (i >= series->slots || !(series->flags[i] & HA_STATS_FLAG_VALUE)) return false;
    *out = series->value[i];
    return true;
}

static void render_week(history_popup_t *popup, const ha_history_week_t *week)
{
    const ha_stats_series_t *series = &week->series;
    float lowest = INFINITY, highest = -INFINITY;
    for (size_t i = 0; i < series->slots; i++) {
        if (!(series->flags[i] & HA_STATS_FLAG_VALUE)) continue;
        const bool range = (series->flags[i] & HA_STATS_FLAG_RANGE) != 0;
        const float lo = range ? series->min[i] : series->value[i];
        const float hi = range ? series->max[i] : series->value[i];
        if (lo < lowest) lowest = lo;
        if (hi > highest) highest = hi;
    }
    if (!isfinite(lowest) || !isfinite(highest) || series->slots < 2) return;

    /* one label per local midnight, placed by its real time: DST days are 23 or 25 hours long */
    axis_label_t labels[AXIS_LABELS_MAX];
    size_t count = 0;
    const int64_t last_slot = series->first_slot + (int64_t)(series->slots - 1) * HA_STATS_HOUR_S;
    for (int d = 0; d < 10 && count < AXIS_LABELS_MAX; d++) {
        const int64_t at = ha_stats_local_day_start(series->first_slot, d);
        if (at < 0 || at > last_slot) break;
        if (at < series->first_slot) continue;
        time_t t = (time_t)at;
        struct tm local;
        if (localtime_r(&t, &local) == NULL) continue;
        char day[8];
        ui_days_weekday_short(local.tm_wday, day, sizeof(day));
        snprintf(labels[count].text, sizeof(labels[count].text), "%s %d", day, local.tm_mday);
        labels[count].frac = (float)(at - series->first_slot) / (float)(last_slot - series->first_slot);
        count++;
    }
    /* no vertical grid: its lines would not meet the day labels, which sit at local midnights */
    render_plot(popup, lowest, highest, series->slots, 0, 60, week_value_at, series, labels, count);
}

/* ---- Range view: 30 days of daily means or 12 months of weekly means (a quote) ---- */

static bool range_value_at(const void *data, size_t i, float *out)
{
    const ha_range_series_t *series = (const ha_range_series_t *)data;
    if (i >= series->count) return false;
    *out = series->point[i].mean;
    return true;
}

static void render_range(history_popup_t *popup, const ha_range_series_t *series)
{
    float lowest = INFINITY, highest = -INFINITY;
    for (size_t i = 0; i < series->count; i++) {
        if (series->point[i].min < lowest) lowest = series->point[i].min;
        if (series->point[i].max > highest) highest = series->point[i].max;
    }
    if (!isfinite(lowest) || !isfinite(highest)) return;
    axis_label_t labels[5];
    size_t count = 0;
    const bool months = series->kind == HA_RANGE_12_MONTHS;
    for (size_t k = 0; k < 5 && series->count > 1; k++) {
        const size_t index = k * (series->count - 1) / 4;
        time_t at = (time_t)series->point[index].start;
        struct tm local;
        if (localtime_r(&at, &local) == NULL) continue;
        if (months) snprintf(labels[count].text, sizeof(labels[count].text), "%02d/%02d", local.tm_mon + 1, local.tm_year % 100);
        else snprintf(labels[count].text, sizeof(labels[count].text), "%02d.%02d.", local.tm_mday, local.tm_mon + 1);
        labels[count].frac = (float)index / (float)(series->count - 1);
        count++;
    }
    render_plot(popup, lowest, highest, series->count, 0, 72, range_value_at, series, labels, count);
}

static void range_received(uint32_t generation, ha_history_range_t *range)
{
    history_popup_t *popup = s_popup;
    if (popup == NULL || popup->range_generation != generation || !popup->range_mode || range == NULL) {
        ha_history_range_free(range);
        return;
    }
    lv_obj_clean(popup->body);
    if (range->status != ESP_OK) {
        lv_label_set_text(popup->status, ui_i18n_get("history.week_error", "Could not load statistics"));
    } else if (range->outcome != HA_STATS_OUTCOME_OK || range->series.count < 2) {
        lv_label_set_text(popup->status, ui_i18n_get(range->series.kind == HA_RANGE_12_MONTHS ? "history.range_empty_12m"
                                                                                          : "history.range_empty_30d",
                                                     range->series.kind == HA_RANGE_12_MONTHS ? "No statistics for 12 months yet"
                                                                                          : "No statistics for 30 days yet"));
    } else {
        /* the change from the first to the last mean of the range */
        const float first = range->series.point[0].mean, last = range->series.point[range->series.count - 1].mean;
        char text[64];
        const char *what = ui_i18n_get(range->series.kind == HA_RANGE_12_MONTHS ? "history.weekly_mean" : "history.daily_mean",
                                       range->series.kind == HA_RANGE_12_MONTHS ? "Weekly means" : "Daily means");
        if (first != 0.0f) snprintf(text, sizeof(text), "%s  ·  %+.1f %%", what, (double)((last - first) / fabsf(first) * 100.0f));
        else snprintf(text, sizeof(text), "%s", what);
        lv_label_set_text(popup->status, text);
        render_range(popup, &range->series);
    }
    ha_history_range_free(range);
}

#ifdef BETTA_SIMULATOR
/* A synthetic quote for the layout screenshots: a drifting wave over the range, ending at the value the chip shows. */
static void simulator_show_range(history_popup_t *popup)
{
    ha_history_range_t *range = calloc(1, sizeof(*range));
    if (range == NULL) return;
    const time_t now = time(NULL);
    range->status = ESP_OK;
    const char *period = "day";
    if (!ha_stats_range_window((int64_t)now, popup->range_kind, &range->window, &period)) { free(range); return; }
    const bool year = popup->range_kind == HA_RANGE_12_MONTHS;
    range->series.kind = popup->range_kind;
    range->series.count = year ? 53 : 30;
    for (uint16_t i = 0; i < range->series.count; i++) {
        const float t = (float)i / (float)(range->series.count - 1);
        const float mean = 190.0f + 14.0f * sinf(t * (year ? 9.0f : 5.5f)) + 22.0f * t;
        range->series.point[i] = (ha_range_point_t){.start = range->window.start_epoch + (int64_t)i * (year ? 7 : 1) * 86400,
                                                    .mean = mean, .min = mean - 3.0f, .max = mean + 3.5f, .has_range = true};
    }
    range->outcome = HA_STATS_OUTCOME_OK;
    range_received(popup->range_generation, range);
}
#endif

static void request_range(history_popup_t *popup)
{
    popup->range_generation = ++s_generation;
    popup->week_active = popup->range_kind == HA_RANGE_12_MONTHS;
    lv_obj_clean(popup->body);
    lv_label_set_text(popup->status, ui_i18n_get("history.loading", "Loading history..."));
#ifndef BETTA_SIMULATOR
    if (ha_history_request_range(popup->entity_id, popup->range_kind, popup->range_generation, range_received) != ESP_OK)
        lv_label_set_text(popup->status, ui_i18n_get("history.week_error", "Could not load statistics"));
#else
    simulator_show_range(popup);
#endif
}

static uint32_t state_color(const char *state)
{
    if (strcmp(state, "unavailable") == 0 || strcmp(state, "unknown") == 0) return APP_UI_COLOR_TEXT_MUTED;
    if (strcmp(state, "on") == 0 || strcmp(state, "open") == 0 || strcmp(state, "unlocked") == 0)
        return APP_UI_COLOR_STATE_ON;
    if (strcmp(state, "jammed") == 0 || strcmp(state, "error") == 0 || strcmp(state, "triggered") == 0) return APP_UI_COLOR_ERROR;
    if (strcmp(state, "cleaning") == 0) return APP_UI_COLOR_STATE_ON;
    return 0x5AB0F6;
}

static const char *state_text(const history_popup_t *popup, const char *state)
{
    if (strncmp(popup->entity_id, "lock.", 5) == 0) {
        if (strcmp(state, "locked") == 0) return ui_i18n_get("lock.state.locked", "Locked");
        if (strcmp(state, "unlocked") == 0) return ui_i18n_get("lock.state.unlocked", "Unlocked");
        if (strcmp(state, "open") == 0) return ui_i18n_get("lock.state.open", "Open");
        if (strcmp(state, "jammed") == 0) return ui_i18n_get("lock.state.jammed", "Jammed");
    }
    if (strncmp(popup->entity_id, "alarm_control_panel.", 20) == 0) {
        const ha_alarm_state_t alarm = ha_alarm_state_parse(state);
        if (alarm != HA_ALARM_UNKNOWN) return ui_i18n_get(ha_alarm_state_key(alarm), state);
    }
    if (strncmp(popup->entity_id, "vacuum.", 7) == 0) {
        if (strcmp(state, "cleaning") == 0) return ui_i18n_get("roborock.cleaning", "Cleaning");
        if (strcmp(state, "returning") == 0) return ui_i18n_get("roborock.returning", "Returning");
        if (strcmp(state, "docked") == 0) return ui_i18n_get("roborock.dock", "Dock");
        if (strcmp(state, "idle") == 0) return ui_i18n_get("roborock.ready", "Ready");
    }
    bool on = strcmp(state, "on") == 0;
    bool off = strcmp(state, "off") == 0;
    if (on || off) {
        const char *dc = popup->device_class;
        if (strcmp(dc, "door") == 0 || strcmp(dc, "window") == 0 || strcmp(dc, "opening") == 0)
            return on ? ui_i18n_get("contact.open", "Open") : ui_i18n_get("contact.closed", "Closed");
        if (strcmp(dc, "motion") == 0 || strcmp(dc, "moving") == 0)
            return on ? ui_i18n_get("binary.motion", "Motion") : ui_i18n_get("binary.clear", "Clear");
        if (strcmp(dc, "presence") == 0 || strcmp(dc, "occupancy") == 0)
            return on ? ui_i18n_get("binary.home", "Home") : ui_i18n_get("binary.away", "Away");
        if (strcmp(dc, "moisture") == 0)
            return on ? ui_i18n_get("binary.wet", "Wet") : ui_i18n_get("binary.dry", "Dry");
        return on ? ui_i18n_get("binary.on", "On") : ui_i18n_get("binary.off", "Off");
    }
    return state;
}

static void render_states(history_popup_t *popup, const ha_history_snapshot_t *snapshot)
{
    const int width = lv_obj_get_width(popup->body) - 16;
    new_label(popup->body, popup->current, ui_font_role(UI_TEXT_VALUE), APP_UI_COLOR_TEXT_PRIMARY, 8, 6, width);
    lv_obj_t *bar = lv_obj_create(popup->body);
    lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar, 8, 46);
    lv_obj_set_size(bar, width, 30);
    lv_obj_set_scrollable(bar, false);
    const ha_history_event_t *last = NULL;
    size_t event_index = 0;
    for (int i = 0; i < 48; i++) {
        int64_t at = snapshot->start_epoch +
                     (snapshot->end_epoch - snapshot->start_epoch) * i / 48;
        while (event_index < snapshot->event_count) {
            const ha_history_event_t *next = ha_history_event_at(snapshot, event_index);
            if (next == NULL || next->epoch > at) break;
            last = next;
            event_index++;
        }
        lv_obj_t *segment = lv_obj_create(bar);
        lv_obj_remove_style_all(segment);
        lv_obj_set_pos(segment, i * width / 48, 0);
        lv_obj_set_size(segment, (i + 1) * width / 48 - i * width / 48, 30);
        lv_obj_set_style_bg_color(segment, lv_color_hex(last ? state_color(last->state) : APP_UI_COLOR_TEXT_MUTED),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_clickable(segment, false);
    }
    lv_obj_t *list = lv_obj_create(popup->body);
    lv_obj_set_pos(list, 8, 96);
    lv_obj_set_size(list, width, lv_obj_get_height(popup->body) - 105);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 4, LV_PART_MAIN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    int y = 0;
    for (size_t n = 0; n < snapshot->event_count && n < 20; n++) {
        const ha_history_event_t *entry = ha_history_event_at(snapshot, snapshot->event_count - 1 - n);
        time_t epoch = (time_t)entry->epoch;
        struct tm local;
        if (localtime_r(&epoch, &local) == NULL) continue;
        char row[100];
        snprintf(row, sizeof(row), "%02d:%02d  %s", local.tm_hour, local.tm_min,
                 state_text(popup, entry->state));
        new_label(list, row, ui_font_role(UI_TEXT_LABEL), APP_UI_COLOR_TEXT_PRIMARY, 4, y, width - 16);
        y += (int)lv_font_get_line_height(ui_font_role(UI_TEXT_LABEL)) + 2 > 34 ? (int)lv_font_get_line_height(ui_font_role(UI_TEXT_LABEL)) + 2 : 34;
    }
}

static void history_received(uint32_t generation, ha_history_snapshot_t *snapshot, esp_err_t status)
{
    history_popup_t *popup = s_popup;
    if (popup == NULL || popup->generation != generation || popup->week_active) {
        ha_history_snapshot_free(snapshot);
        return;
    }
    if (status != ESP_OK || snapshot == NULL) {
        lv_label_set_text(popup->status, ui_i18n_get("history.error", "Could not load history"));
    } else if (snapshot->valid_count == 0) {
        lv_label_set_text(popup->status, ui_i18n_get("history.empty", "No history recorded"));
    } else {
        lv_label_set_text(popup->status, snapshot->shortened ?
            ui_i18n_get("history.shortened", "History shortened to fit") : "");
        if (popup->numeric) render_numeric(popup, snapshot);
        else render_states(popup, snapshot);
    }
    ha_history_snapshot_free(snapshot);
}

static void week_received(uint32_t generation, ha_history_week_t *week);

#ifdef BETTA_SIMULATOR
/* Device-independent fixtures for popup layout screenshots: a day of readings, or a synthetic week of hourly means. */
static void simulator_show_day(history_popup_t *popup)
{
    /* Device-independent fixture for popup layout screenshots. */
    ha_history_snapshot_t *sample = calloc(1, sizeof(*sample));
    if (sample != NULL) {
        time_t end = time(NULL);
        sample->numeric = popup->numeric;
        sample->start_epoch = end - 86400;
        sample->end_epoch = end;
        if (popup->numeric) {
            /* A smooth day of room temperature: one slow wave plus a small second harmonic, ending at the
             * 21.8 degrees the gallery example shows as the current value. */
            for (size_t i = 0; i < HA_HISTORY_BUCKETS; i++) {
                const float x = 0.72f - 6.2832f * (float)(HA_HISTORY_BUCKETS - 1U - i) / (float)(HA_HISTORY_BUCKETS - 1U);
                const float mean = 19.2f + 3.5f * sinf(x) + 0.4f * sinf(2.0f * x + 1.0f);
                sample->buckets[i] = (ha_history_bucket_t){.min = mean - 0.25f, .max = mean + 0.25f, .sum = mean, .count = 1};
            }
            sample->valid_count = HA_HISTORY_BUCKETS;
        } else {
            sample->event_count = 4;
            for (size_t i = 0; i < 4; i++) {
                sample->events[i].epoch = end - (int64_t)(4 - i) * 3600;
                const bool is_lock = strncmp(popup->entity_id, "lock.", 5) == 0;
                static const char *const vacuum_states[4] = {"docked", "cleaning", "returning", "docked"};
                const bool is_vacuum = strncmp(popup->entity_id, "vacuum.", 7) == 0;
                const char *state = is_vacuum ? vacuum_states[i] : is_lock ? (i % 2 ? "locked" : "unlocked") : (i % 2 ? "off" : "on");
                static const int vacuum_ago_min[4] = {540, 190, 105, 95}; /* one run of about 85 minutes */
                if (is_vacuum) sample->events[i].epoch = end - (int64_t)vacuum_ago_min[i] * 60;
                strlcpy(sample->events[i].state, state, sizeof(sample->events[i].state));
            }
            sample->valid_count = 4;
        }
        history_received(popup->generation, sample, ESP_OK);
    }
}

static void simulator_show_week(history_popup_t *popup)
{
    ha_history_week_t *week = calloc(1, sizeof(*week));
    if (week == NULL) return;
    const time_t now = time(NULL);
    week->status = ESP_OK;
    if (!ha_stats_week_window((int64_t)now, &week->window)) { free(week); return; }
    week->series.first_slot = week->window.first_slot;
    week->series.slots = (uint16_t)week->window.slots;
    for (size_t i = 0; i + 1 < week->window.slots; i++) {  /* the current hour has no statistic yet */
        const float t = (float)i;
        const float mean = 20.0f + 2.2f * sinf(6.2832f * t / 24.0f - 1.9f) + 0.9f * sinf(6.2832f * t / 168.0f * 3.0f);
        if (i >= 61 && i < 68) continue;    /* a gap: the sensor was offline for a few hours */
        week->series.value[i] = mean;
        week->series.min[i] = mean - 0.4f;
        week->series.max[i] = mean + 0.4f;
        week->series.flags[i] = HA_STATS_FLAG_VALUE | HA_STATS_FLAG_RANGE;
        week->series.valid++;
    }
    week->outcome = HA_STATS_OUTCOME_OK;
    week_received(popup->week_generation, week);
}
#endif

static void request_day(history_popup_t *popup)
{
    popup->week_active = false;
    popup->generation = ++s_generation;
    lv_obj_clean(popup->body);
    lv_label_set_text(popup->status, ui_i18n_get("history.loading", "Loading history..."));
#ifndef BETTA_SIMULATOR
    if (ha_history_request(popup->entity_id, popup->numeric, popup->generation, history_received) != ESP_OK)
        lv_label_set_text(popup->status, ui_i18n_get("history.error", "Could not load history"));
#else
    simulator_show_day(popup);
#endif
}

static void week_received(uint32_t generation, ha_history_week_t *week)
{
    history_popup_t *popup = s_popup;
    if (popup == NULL || popup->week_generation != generation || !popup->week_active || week == NULL) {
        ha_history_week_free(week);
        return;
    }
    lv_obj_clean(popup->body);
    if (week->status != ESP_OK) {
        lv_label_set_text(popup->status, ui_i18n_get("history.week_error", "Could not load statistics"));
    } else if (week->outcome != HA_STATS_OUTCOME_OK) {
        lv_label_set_text(popup->status, ui_i18n_get("history.week_empty", "No statistics for the last 7 days"));
    } else {
        lv_label_set_text(popup->status, ui_i18n_get("history.hourly_mean", "Hourly means"));
        render_week(popup, week);
    }
    ha_history_week_free(week);
}

static void request_week(history_popup_t *popup)
{
    popup->week_active = true;
    popup->generation++; /* a 24 h answer still on its way is for the other view */
    popup->week_generation = ++s_generation;
    lv_obj_clean(popup->body);
    lv_label_set_text(popup->status, ui_i18n_get("history.loading", "Loading history..."));
#ifndef BETTA_SIMULATOR
    if (ha_history_request_week(popup->entity_id, popup->week_unit, popup->week_convert, popup->week_convert_class,
                                popup->week_generation, week_received) != ESP_OK)
        lv_label_set_text(popup->status, ui_i18n_get("history.week_error", "Could not load statistics"));
#else
    simulator_show_week(popup);
#endif
}

static void style_range_button(history_popup_t *popup)
{
    lv_obj_t *buttons[2] = {popup->btn_day, popup->btn_week};
    for (int i = 0; i < 2; i++) {
        if (buttons[i] == NULL) continue;
        const bool active = (i == 1) == popup->week_active;
        lv_obj_set_style_bg_color(buttons[i], lv_color_hex(active ? APP_UI_COLOR_STATE_ON : APP_UI_COLOR_NAV_BTN_BG_IDLE),
                                  LV_PART_MAIN);
        lv_obj_set_style_text_color(buttons[i], lv_color_hex(active ? APP_UI_COLOR_CONTENT_BG : APP_UI_COLOR_TEXT_PRIMARY),
                                    LV_PART_MAIN);
    }
}

static void range_clicked(lv_event_t *event)
{
    history_popup_t *popup = (history_popup_t *)lv_event_get_user_data(event);
    if (popup == NULL || popup != s_popup || !popup->week_offered) return;
    const bool want_week = lv_event_get_target(event) == popup->btn_week;
    if (popup->range_mode) {
        const ha_range_kind_t kind = want_week ? HA_RANGE_12_MONTHS : HA_RANGE_30_DAYS;
        if (kind == popup->range_kind) return;
        popup->range_kind = kind;
        request_range(popup);
        style_range_button(popup);
        return;
    }
    if (want_week == popup->week_active) return;
    if (want_week) request_week(popup);
    else request_day(popup);
    style_range_button(popup);
}

static lv_obj_t *range_button(history_popup_t *popup, const char *text, int x, int w, int h, int y)
{
    lv_obj_t *button = lv_btn_create(popup->card);
    lv_obj_remove_style_all(button);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(button, ui_style_radius(10), LV_PART_MAIN);
    lv_obj_add_event_cb(button, range_clicked, LV_EVENT_CLICKED, popup);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_center(label);
    return button;
}

/* Header geometry that follows the text size: the selector row sits below the title and keeps the body clear. */
static int range_button_height(void)
{
    const int h = (int)lv_font_get_line_height(ui_font_role(UI_TEXT_META)) + 8;
    return h > 28 ? h : 28;
}

static int range_button_width(void)
{
    return 4 * (int)lv_font_get_line_height(ui_font_role(UI_TEXT_META));
}

static void check_received(uint32_t generation, const ha_history_check_t *check)
{
    history_popup_t *popup = s_popup;
    if (popup == NULL || popup->check_generation != generation || check == NULL) return;
    /* no answer, an unsupported entity or a failed lookup: no selector and no message, the 24 h view stays as it is */
    if (check->status != ESP_OK || check->result.kind != HA_STATS_KIND_MEASUREMENT) return;
    popup->week_offered = true;
    popup->week_convert = check->result.convert;
    strlcpy(popup->week_convert_class, check->result.convert_class, sizeof(popup->week_convert_class));
    const int w = range_button_width(), h = range_button_height();
    popup->btn_day = range_button(popup, ui_i18n_get("history.range_day", "24 h"), 0, w, h, popup->row_y);
    popup->btn_week = range_button(popup, ui_i18n_get("history.range_week", "7 days"), w + 8, w, h, popup->row_y);
    style_range_button(popup);
}

/* The unit and state class Home Assistant reported for the entity; empty when the panel does not know the entity. */
static void read_entity_attributes(const char *entity_id, char *unit, size_t unit_len, ha_stats_state_class_t *state_class)
{
    unit[0] = '\0';
    *state_class = HA_STATS_SC_UNKNOWN;
    ha_state_t *state = calloc(1, sizeof(*state));
    if (state == NULL) return;
    if (ha_model_get_state(entity_id, state)) {
        cJSON *attrs = cJSON_Parse(state->attributes_json);
        if (cJSON_IsObject(attrs)) {
            const cJSON *u = cJSON_GetObjectItemCaseSensitive(attrs, "unit_of_measurement");
            if (cJSON_IsString(u) && u->valuestring != NULL && strlen(u->valuestring) < unit_len) strcpy(unit, u->valuestring);
            const cJSON *c = cJSON_GetObjectItemCaseSensitive(attrs, "state_class");
            if (cJSON_IsString(c)) *state_class = ha_stats_state_class_from_string(c->valuestring);
        }
        cJSON_Delete(attrs);
    }
    free(state);
}

/* ---- Daily consumption: the bars come from the shared days view ---- */

static void days_received(uint32_t generation, ha_history_days_t *result)
{
    history_popup_t *popup = s_popup;
    if (popup == NULL || popup->days_generation != generation || !popup->days_mode || result == NULL) {
        ha_history_days_free(result);
        return;
    }
    const ui_days_view_t view = {.body = popup->body, .status = popup->status, .color = popup->bar_color};
    ui_days_view_show(&view, result);
    ha_history_days_free(result);
}

/* The overlay, card, title and close button every history popup shares. */
static void create_frame(history_popup_t *popup, const char *title, const char *fallback_title, int *out_cw, int *out_ch)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_update_layout(screen);
    int sw = lv_obj_get_width(screen), sh = lv_obj_get_height(screen);
    if (sw < 100 || sh < 100) { sw = 720; sh = 720; }
    popup->overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(popup->overlay, sw, sh);
    lv_obj_center(popup->overlay);
    lv_obj_set_scrollable(popup->overlay, false);
    lv_obj_set_style_bg_color(popup->overlay, lv_color_hex(0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(popup->overlay, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(popup->overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(popup->overlay, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(popup->overlay, popup_deleted, LV_EVENT_DELETE, popup);
    lv_obj_add_event_cb(popup->overlay, backdrop_clicked, LV_EVENT_CLICKED, popup);

    int cw = sw - 24;
    if (cw > 680) cw = 680;
    int ch = sh - 48;
    if (ch > 560) ch = 560;
    popup->card = lv_obj_create(popup->overlay);
    lv_obj_set_size(popup->card, cw, ch);
    lv_obj_center(popup->card);
    lv_obj_set_scrollable(popup->card, false);
    lv_obj_set_event_bubble(popup->card, false);
    lv_obj_set_style_bg_color(popup->card, lv_color_hex(APP_UI_COLOR_CONTENT_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(popup->card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(popup->card, ui_style_radius(18), LV_PART_MAIN);
    ui_popup_style_frame(popup->card);
    lv_obj_set_style_pad_all(popup->card, 14, LV_PART_MAIN);
    char title_buf[APP_MAX_NAME_LEN * 2];
    const char *shown_title = ui_style_title(title_buf, sizeof(title_buf), title ? title : fallback_title);
    new_label(popup->card, shown_title, ui_font_role(UI_TEXT_LABEL), APP_UI_COLOR_TEXT_PRIMARY, 0, 0, cw - 100);
    lv_obj_t *close = ui_popup_close_button(popup->card, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE),
                                             popup_close, popup);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, 0);
    *out_cw = cw;
    *out_ch = ch;
}

void ui_history_popup_open(const char *entity_id, const char *title, const char *current,
                           bool numeric, const char *device_class)
{
    if (entity_id == NULL || entity_id[0] == '\0') return;
    if (s_popup != NULL) lv_obj_delete(s_popup->overlay);
    history_popup_t *popup = calloc(1, sizeof(*popup));
    if (popup == NULL) return;
    popup->generation = ++s_generation;
    popup->numeric = numeric;
    strlcpy(popup->entity_id, entity_id, sizeof(popup->entity_id));
    strlcpy(popup->current, current ? current : "", sizeof(popup->current));
    strlcpy(popup->device_class, device_class ? device_class : "", sizeof(popup->device_class));
    s_popup = popup;

    int cw, ch;
    create_frame(popup, title, entity_id, &cw, &ch);
    /* a numeric sensor with a measurement state class may offer the 7-day view: its selector gets a reserved spot right
     * of the status text, so confirming it later moves nothing */
    char unit[HA_STATS_UNIT_MAX];
    ha_stats_state_class_t state_class;
    read_entity_attributes(entity_id, unit, sizeof(unit), &state_class);
    if (unit[0] == '\0') chart_scale_unit_of(popup->current, unit, sizeof(unit));
#ifdef BETTA_SIMULATOR
    state_class = HA_STATS_SC_MEASUREMENT;
#endif
    popup->week_possible = numeric && strncmp(entity_id, "sensor.", 7) == 0 && state_class == HA_STATS_SC_MEASUREMENT;
    strlcpy(popup->week_unit, unit, sizeof(popup->week_unit));
    /* the selector sits left of the status text, clear of the close button on the right */
    const int selector_w = popup->week_possible ? 2 * range_button_width() + 8 + 12 : 0;
    const int row_h = popup->week_possible ? range_button_height() : 28;
    const int status_h = (int)lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    const int title_h = (int)lv_font_get_line_height(ui_font_role(UI_TEXT_LABEL));
    popup->row_y = title_h + 2 > 32 ? title_h + 2 : 32;
    const int status_y = popup->week_possible && row_h > status_h ? popup->row_y + (row_h - status_h) / 2
                                                                  : (popup->row_y + 4 > 36 ? popup->row_y + 4 : 36);
    popup->status = new_label(popup->card, ui_i18n_get("history.loading", "Loading history..."),
                              ui_font_role(UI_TEXT_META), APP_UI_COLOR_TEXT_MUTED, selector_w, status_y, cw - 70 - selector_w);
    popup->body = lv_obj_create(popup->card);
    lv_obj_remove_style_all(popup->body);
    const int body_y = popup->row_y + row_h + 4;  /* 64 px at the reference text size */
    lv_obj_set_pos(popup->body, 0, body_y);
    lv_obj_set_size(popup->body, cw - 28, ch - body_y - 28);
    lv_obj_set_scrollable(popup->body, false);
    lv_obj_update_layout(popup->card);

#ifdef BETTA_SIMULATOR
    simulator_show_day(popup);
    if (popup->week_possible) {
        /* the simulator has no Home Assistant: the week view is always offered and drawn from a synthetic week */
        ha_history_check_t check = {.status = ESP_OK, .result = {.kind = HA_STATS_KIND_MEASUREMENT, .why = HA_STATS_WHY_OK}};
        popup->check_generation = ++s_generation;
        check_received(popup->check_generation, &check);
    }
#else
    if (ha_history_request(entity_id, numeric, popup->generation, history_received) != ESP_OK)
        lv_label_set_text(popup->status, ui_i18n_get("history.error", "Could not load history"));
    if (popup->week_possible) {
        popup->check_generation = ++s_generation;
        (void)ha_history_check_week(entity_id, unit, state_class, popup->check_generation, check_received);
    }
#endif
}

void ui_history_popup_open_range(const char *entity_id, const char *title, const char *current)
{
    if (entity_id == NULL || entity_id[0] == '\0') return;
    if (s_popup != NULL) lv_obj_delete(s_popup->overlay);
    history_popup_t *popup = calloc(1, sizeof(*popup));
    if (popup == NULL) return;
    popup->numeric = true;
    popup->range_mode = true;
    popup->week_offered = true;
    popup->range_kind = HA_RANGE_30_DAYS;
    strlcpy(popup->entity_id, entity_id, sizeof(popup->entity_id));
    strlcpy(popup->current, current ? current : "", sizeof(popup->current));
    s_popup = popup;
    int cw, ch;
    create_frame(popup, title, entity_id, &cw, &ch);
    const int selector_w = 2 * range_button_width() + 8 + 12;
    const int row_h = range_button_height();
    const int status_h = (int)lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    const int title_h = (int)lv_font_get_line_height(ui_font_role(UI_TEXT_LABEL));
    popup->row_y = title_h + 2 > 32 ? title_h + 2 : 32;
    popup->status = new_label(popup->card, ui_i18n_get("history.loading", "Loading history..."), ui_font_role(UI_TEXT_META),
                              APP_UI_COLOR_TEXT_MUTED, selector_w, popup->row_y + (row_h > status_h ? (row_h - status_h) / 2 : 0),
                              cw - 70 - selector_w);
    popup->body = lv_obj_create(popup->card);
    lv_obj_remove_style_all(popup->body);
    const int body_y = popup->row_y + row_h + 4;
    lv_obj_set_pos(popup->body, 0, body_y);
    lv_obj_set_size(popup->body, cw - 28, ch - body_y - 28);
    lv_obj_set_scrollable(popup->body, false);
    lv_obj_update_layout(popup->card);
    const int w = range_button_width(), h = range_button_height();
    popup->btn_day = range_button(popup, ui_i18n_get("history.range_30d", "30 d"), 0, w, h, popup->row_y);
    popup->btn_week = range_button(popup, ui_i18n_get("history.range_12m", "12 m"), w + 8, w, h, popup->row_y);
    request_range(popup);
    style_range_button(popup);
}

void ui_history_popup_range_select(bool year)
{
    history_popup_t *popup = s_popup;
    if (popup == NULL || !popup->range_mode) return;
    const ha_range_kind_t kind = year ? HA_RANGE_12_MONTHS : HA_RANGE_30_DAYS;
    if (kind == popup->range_kind) return;
    popup->range_kind = kind;
    request_range(popup);
    style_range_button(popup);
}

#ifdef BETTA_SIMULATOR
static void simulator_show_days(history_popup_t *popup)
{
    ha_history_days_t *result = ui_days_view_simulated(strstr(popup->ids[0], "gas") != NULL);
    if (result != NULL) days_received(popup->days_generation, result);
}
#endif

void ui_history_popup_open_days(const char *title, uint32_t bar_color, const char *const *ids, size_t count)
{
    if (ids == NULL || count == 0 || count > HA_DAYS_MAX_SOURCES || ids[0] == NULL || ids[0][0] == '\0') return;
    if (s_popup != NULL) lv_obj_delete(s_popup->overlay);
    history_popup_t *popup = calloc(1, sizeof(*popup));
    if (popup == NULL) return;
    popup->days_mode = true;
    popup->numeric = true;
    popup->bar_color = bar_color;
    popup->id_count = (uint8_t)count;
    for (size_t i = 0; i < count; i++) {
        if (ids[i] != NULL) strlcpy(popup->ids[i], ids[i], sizeof(popup->ids[i]));
    }
    strlcpy(popup->entity_id, popup->ids[0], sizeof(popup->entity_id));
    s_popup = popup;
    int cw, ch;
    create_frame(popup, title, popup->ids[0], &cw, &ch);
    popup->row_y = (int)lv_font_get_line_height(ui_font_role(UI_TEXT_LABEL)) + 2 > 32
                       ? (int)lv_font_get_line_height(ui_font_role(UI_TEXT_LABEL)) + 2 : 32;
    popup->status = new_label(popup->card, ui_i18n_get("history.loading", "Loading history..."),
                              ui_font_role(UI_TEXT_META), APP_UI_COLOR_TEXT_MUTED, 0, popup->row_y + 4, cw - 70);
    popup->body = lv_obj_create(popup->card);
    lv_obj_remove_style_all(popup->body);
    const int body_y = popup->row_y + 32;
    lv_obj_set_pos(popup->body, 0, body_y);
    lv_obj_set_size(popup->body, cw - 28, ch - body_y - 28);
    lv_obj_set_scrollable(popup->body, false);
    lv_obj_update_layout(popup->card);
    popup->days_generation = ++s_generation;
#ifdef BETTA_SIMULATOR
    simulator_show_days(popup);
#else
    const char *list[HA_DAYS_MAX_SOURCES];
    for (size_t i = 0; i < count; i++) list[i] = popup->ids[i];
    if (ha_history_request_days(list, count, popup->days_generation, days_received) != ESP_OK)
        lv_label_set_text(popup->status, ui_i18n_get("history.week_error", "Could not load statistics"));
#endif
}

static void sessions_tick(lv_timer_t *timer)
{
    history_popup_t *popup = lv_timer_get_user_data(timer);
    ha_evcc_sessions_snapshot(popup->entity_id, popup->sessions);
    if (popup->sessions_version == popup->sessions->version) return;
    popup->sessions_version = popup->sessions->version;
    ui_evcc_sessions_view(popup->body, popup->status, popup->sessions);
}
void ui_history_popup_close_evcc(const char *entity)
{
    if (s_popup && s_popup->sessions && entity && !strcmp(s_popup->entity_id, entity)) lv_obj_delete(s_popup->overlay);
}
void ui_history_popup_open_evcc(const char *entity)
{
    if (!entity || !*entity) return;
    if (s_popup) lv_obj_delete(s_popup->overlay);
    history_popup_t *popup = ui_calloc_prefer_psram(1, sizeof(*popup));
    if (!popup) return;
    popup->sessions = ui_calloc_prefer_psram(1, sizeof(*popup->sessions));
    if (!popup->sessions) { free(popup); return; }
    strlcpy(popup->entity_id, entity, sizeof(popup->entity_id));
    popup->sessions_version = UINT32_MAX; s_popup = popup;
    int cw, ch; create_frame(popup, ui_i18n_get("evcc.sessions", "Sessions"), entity, &cw, &ch);
    int top = UI_POPUP_CLOSE_SIZE + 8;
    int meta = lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    popup->status = new_label(popup->card, "", ui_font_role(UI_TEXT_META), APP_UI_COLOR_TEXT_MUTED, 0, top, cw-28);
    popup->body = lv_obj_create(popup->card); lv_obj_remove_style_all(popup->body);
    lv_obj_set_pos(popup->body, 0, top+meta+8); lv_obj_set_size(popup->body, cw-28, ch-top-meta-36);
    lv_obj_set_scroll_dir(popup->body, LV_DIR_VER); lv_obj_set_scrollable(popup->body, true);
    ha_evcc_sessions_request(entity, false);
    popup->sessions_timer = lv_timer_create(sessions_tick, 250, popup);
    if (!popup->sessions_timer) { lv_obj_delete(popup->overlay); return; }
    lv_obj_update_layout(popup->card); sessions_tick(popup->sessions_timer);
}
