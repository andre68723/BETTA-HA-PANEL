/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/display_geometry.h"
#include "ui/ui_widget_factory.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"

#include "ui/theme/ui_typography.h"
#include "ui/theme/ui_day_grid.h"
#include "ui/ui_popup.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"

#define SOLAR_FORECAST_ROWS 6
#define SOLAR_FORECAST_DAY_ROWS 5

#define SOLAR_TITLE_FONT ui_font_role(UI_TEXT_LABEL)
#define SOLAR_ROW_FONT ui_font_role(UI_TEXT_LABEL)

/* Slightly softer than pure signal colors so the bars sit with the dark cards. */
#define SOLAR_BAR_GREEN (ui_style_has_depth() ? 0x4FCB7E : 0x22D164)
#define SOLAR_BAR_GOLD (ui_style_has_depth() ? 0xF2C14E : 0xFFD500)

typedef struct {
    lv_obj_t *plate; /* glow style: rounded surface behind the row */
    lv_obj_t *day_label;
    lv_obj_t *bar;
    lv_obj_t *rest_bar; /* today only: the part of the total still to come, drawn over the dimmed total */
    lv_obj_t *value_label;
} solar_forecast_row_t;

typedef struct {
    lv_obj_t *card;
    lv_obj_t *title_label;
    solar_forecast_row_t rows[SOLAR_FORECAST_ROWS];
    char entity_ids[SOLAR_FORECAST_ROWS][APP_MAX_ENTITY_ID_LEN];
    char units[SOLAR_FORECAST_ROWS][APP_MAX_UNIT_LEN];
    float values[SOLAR_FORECAST_ROWS];
    float bar_max_kwh;
    bool valid[SOLAR_FORECAST_ROWS];
    bool vertical_bars;
    bool aligned;
    int forecast_days;
    lv_obj_t *summary;
    lv_obj_t *popup;
    lv_timer_t *popup_timer;
} w_solar_forecast_ctx_t;

static bool solar_state_is_unavailable(const char *state)
{
    if (state == NULL || state[0] == '\0') {
        return true;
    }
    return strcmp(state, "unavailable") == 0 || strcmp(state, "unknown") == 0;
}

static int solar_entity_index(const w_solar_forecast_ctx_t *ctx, const char *entity_id)
{
    if (ctx == NULL || entity_id == NULL || entity_id[0] == '\0') {
        return -1;
    }
    for (int i = 0; i < SOLAR_FORECAST_ROWS; i++) {
        if (ctx->entity_ids[i][0] != '\0' &&
            strncmp(ctx->entity_ids[i], entity_id, APP_MAX_ENTITY_ID_LEN) == 0) {
            return i;
        }
    }
    return -1;
}

static void solar_extract_unit(const char *attrs_json, char *unit, size_t unit_size)
{
    if (unit == NULL || unit_size == 0 || attrs_json == NULL || attrs_json[0] == '\0') {
        return;
    }
    cJSON *attrs = cJSON_Parse(attrs_json);
    if (attrs == NULL) {
        return;
    }
    cJSON *unit_item = cJSON_GetObjectItemCaseSensitive(attrs, "unit_of_measurement");
    if (cJSON_IsString(unit_item) && unit_item->valuestring != NULL && unit_item->valuestring[0] != '\0') {
        snprintf(unit, unit_size, "%s", unit_item->valuestring);
    }
    cJSON_Delete(attrs);
}

static bool solar_parse_value(const ha_state_t *state, float *out_value)
{
    if (state == NULL || out_value == NULL || solar_state_is_unavailable(state->state)) {
        return false;
    }
    char *end = NULL;
    float value = strtof(state->state, &end);
    if (end == state->state || !isfinite(value)) {
        return false;
    }
    if (value < 0.0f) {
        value = 0.0f;
    }
    *out_value = value;
    return true;
}

static void solar_format_value(char *dst, size_t dst_size, float value, const char *unit)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    const char *suffix = (unit != NULL && unit[0] != '\0') ? unit : "kWh";
    snprintf(dst, dst_size, "%.0f %s", (double)value, suffix);
    for (char *p = dst; *p != '\0'; p++) {
        if (*p == '.') {
            *p = ',';
        }
    }
}

static void solar_format_number(char *dst, size_t dst_size, float value)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%.0f", (double)value);
    for (char *p = dst; *p != '\0'; p++) {
        if (*p == '.') {
            *p = ',';
        }
    }
}

static int solar_visible_rows(lv_obj_t *card)
{
    if (card == NULL) {
        return SOLAR_FORECAST_ROWS;
    }
    lv_coord_t h = lv_obj_get_height(card);
    const lv_coord_t rows_top = 54;
    const lv_coord_t row_h = 30;
    const lv_coord_t row_gap = 3;
    lv_coord_t avail = h - rows_top - 14;
    int rows = 5;
    if (avail > row_h) {
        rows = (int)((avail + row_gap) / (row_h + row_gap));
    }
    if (rows < 3) rows = 3;
    if (rows > SOLAR_FORECAST_ROWS - 1) rows = SOLAR_FORECAST_ROWS - 1; /* the remaining reading shares today's row */
    return rows;
}

static void solar_day_label(int forecast_index, char *dst, size_t dst_size)
{
    static const char *const weekday_keys[] = {
        "solar.weekday_0", "solar.weekday_1", "solar.weekday_2", "solar.weekday_3",
        "solar.weekday_4", "solar.weekday_5", "solar.weekday_6",
    };
    static const char *const weekday_fallback[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
    if (dst == NULL || dst_size == 0) {
        return;
    }
    /* An unsynced RTC starts near the 1970 epoch (still > 0), so require a
     * plausible wall-clock time before trusting the weekday. */
    const time_t plausible_epoch = 1600000000; /* 2020-09-13 */
    time_t now = time(NULL);
    struct tm tm_now = {0};
    if (now > plausible_epoch && localtime_r(&now, &tm_now) != NULL) {
        int wday = (tm_now.tm_wday + forecast_index) % 7;
        snprintf(dst, dst_size, "%s", ui_i18n_get(weekday_keys[wday], weekday_fallback[wday]));
        return;
    }

    static const char *const fallback_keys[] = {
        "solar.today", "solar.tomorrow", "solar.day_3", "solar.day_4", "solar.day_5",
    };
    static const char *const fallback_texts[] = {"Heute", "Morgen", "Tag 3", "Tag 4", "Tag 5"};
    if (forecast_index >= 0 && forecast_index < SOLAR_FORECAST_DAY_ROWS) {
        snprintf(dst, dst_size, "%s", ui_i18n_get(fallback_keys[forecast_index], fallback_texts[forecast_index]));
    } else {
        snprintf(dst, dst_size, "%s", ui_i18n_get("solar.day_generic", "Tag"));
    }
}

static int solar_bar_percent(const w_solar_forecast_ctx_t *ctx, int index, float max_value)
{
    if (ctx == NULL || index < 0 || index >= SOLAR_FORECAST_ROWS || !ctx->valid[index] || max_value <= 0.0f) {
        return 0;
    }
    int bar_value = (int)((ctx->values[index] * 100.0f / max_value) + 0.5f);
    if (bar_value < 0) bar_value = 0;
    if (bar_value > 100) bar_value = 100;
    return bar_value;
}

static void solar_configure_bar_direction(lv_obj_t *bar, bool remaining_row, bool vertical)
{
    if (bar == NULL) {
        return;
    }
    lv_bar_set_range(bar, remaining_row ? 100 : 0, remaining_row ? 0 : 100);
    if (!vertical && remaining_row) {
        lv_obj_set_style_bg_color(bar, lv_color_hex(SOLAR_BAR_GREEN), LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_color(bar, lv_color_hex(SOLAR_BAR_GOLD), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_bg_color(bar, lv_color_hex(SOLAR_BAR_GOLD), LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_color(bar, lv_color_hex(SOLAR_BAR_GREEN), LV_PART_INDICATOR);
    }
}

/* Today's column carries two readings: the daily total (bar and value, like every other day) and what is left of it. The
 * remaining part is a second bar over the dimmed total, anchored to the end of the total so the dim start reads as produced. */
static void solar_place_rest_bar(w_solar_forecast_ctx_t *ctx, int index, bool vertical, lv_coord_t x, lv_coord_t y,
                                 lv_coord_t bar_w, lv_coord_t bar_h, float max_value)
{
    solar_forecast_row_t *row = &ctx->rows[index];
    const bool show = index == 1 && ctx->valid[0] && ctx->valid[1] && ctx->values[1] > 0.0f;
    lv_obj_set_style_bg_opa(row->bar, show ? LV_OPA_40 : LV_OPA_COVER, LV_PART_INDICATOR);
    if (!show) {
        lv_obj_set_hidden(row->rest_bar, true);
        return;
    }
    const float total = ctx->values[1];
    const float rest = ctx->values[0] < total ? ctx->values[0] : total;
    const int total_pct = solar_bar_percent(ctx, 1, max_value);
    const lv_coord_t span = vertical ? bar_h : bar_w;
    const lv_coord_t len = (lv_coord_t)((span * total_pct + 50) / 100);
    const lv_coord_t thickness = vertical ? bar_w : bar_h;
    if (len < 2) {
        lv_obj_set_hidden(row->rest_bar, true);
        return;
    }
    lv_obj_set_hidden(row->rest_bar, false);
    lv_bar_set_orientation(row->rest_bar, vertical ? LV_BAR_ORIENTATION_VERTICAL : LV_BAR_ORIENTATION_HORIZONTAL);
    lv_bar_set_range(row->rest_bar, 100, 0); /* fills from the end of the total */
    lv_obj_set_style_bg_grad_dir(row->rest_bar, vertical ? LV_GRAD_DIR_VER : LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_size(row->rest_bar, vertical ? thickness : len, vertical ? len : thickness);
    lv_obj_align(row->rest_bar, LV_ALIGN_TOP_LEFT, x, vertical ? y + bar_h - len : y);
    lv_bar_set_value(row->rest_bar, (int)((rest * 100.0f / total) + 0.5f), LV_ANIM_OFF);
}

static void solar_set_row_hidden(solar_forecast_row_t *row, bool hidden)
{
    if (row == NULL) {
        return;
    }
    if (hidden || !ui_style_has_depth()) {
        lv_obj_set_hidden(row->plate, true);
    } else {
        lv_obj_set_hidden(row->plate, false);
    }
    if (hidden) {
        lv_obj_set_hidden(row->day_label, true);
        lv_obj_set_hidden(row->bar, true);
        lv_obj_set_hidden(row->rest_bar, true);
        lv_obj_set_hidden(row->value_label, true);
    } else {
        lv_obj_set_hidden(row->day_label, false);
        lv_obj_set_hidden(row->bar, false);
        lv_obj_set_hidden(row->value_label, false);
    }
}

static void solar_render(w_solar_forecast_ctx_t *ctx)
{
    if (ctx == NULL || ctx->card == NULL) {
        return;
    }
    lv_obj_update_layout(ctx->card);

    float max_value = 0.0f;
    for (int i = 0; i < SOLAR_FORECAST_ROWS; i++) {
        if (!ctx->valid[i]) {
            continue;
        }
        if (ctx->values[i] > max_value) {
            max_value = ctx->values[i];
        }
    }
    if (ctx->bar_max_kwh > 0.0f) {
        max_value = ctx->bar_max_kwh;
    } else if (max_value <= 0.0f) {
        max_value = 1.0f;
    }

    bool common_unit = true;
    for (int i = 1; i < SOLAR_FORECAST_ROWS; i++)
        if (strcmp(ctx->units[0], ctx->units[i])) common_unit = false;
    const int visible_rows = ctx->vertical_bars && ctx->aligned ? ui_day_grid_count(lv_obj_get_content_width(ctx->card) - 28, ctx->forecast_days, 5) : solar_visible_rows(ctx->card);
    lv_coord_t card_w = lv_obj_get_width(ctx->card);
    lv_coord_t content_w = card_w - 28;
#if UI_TEXT_ADAPTIVE
    content_w = lv_obj_get_content_width(ctx->card) - 28;
#endif
    if (content_w < 120) {
        content_w = 120;
    }
    lv_coord_t label_w = content_w / 5;
    if (label_w < 50) {
        label_w = 50;
    }
    lv_coord_t value_w = content_w / 4;
    if (value_w < 76) value_w = 76;
#if UI_TEXT_ADAPTIVE
    value_w = content_w / 2;
#else
    {   /* the value column is as wide as its text needs (a value with its unit must never end in dots), at least the former width */
        const lv_font_t *value_font = lv_obj_get_style_text_font(ctx->rows[0].value_label, LV_PART_MAIN);
        lv_point_t widest;
        lv_text_get_size(&widest, "88 kWh", value_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (value_w < widest.x + 8) value_w = widest.x + 8;
    }
#endif
    lv_coord_t bar_w = content_w - label_w - value_w - 18;
    if (bar_w < 24) bar_w = 24;

#if UI_TEXT_ADAPTIVE
    lv_obj_set_width(ctx->title_label, content_w - 82);
    char summary[64];
    if (ctx->aligned) {
        char number[32];
        if (ctx->valid[0]) solar_format_value(number, sizeof(number), ctx->values[0], ctx->units[0]);
        else snprintf(number, sizeof(number), "-- kWh");
        snprintf(summary, sizeof(summary), "%s %s", ui_i18n_get("solar.rest", "Rest"), number);
    } else snprintf(summary, sizeof(summary), "%s", common_unit ? (ctx->units[0][0] ? ctx->units[0] : "kWh") : "");
    lv_label_set_text(ctx->summary, summary);
    const lv_font_t *summary_font = ui_text_fit(summary, ui_font_role(UI_TEXT_LABEL), ctx->aligned ? content_w : 78, 35);
    lv_obj_set_style_text_font(ctx->summary, summary_font, LV_PART_MAIN);
    lv_obj_set_width(ctx->summary, ctx->aligned ? content_w : 78);
    lv_obj_align(ctx->summary, LV_ALIGN_TOP_RIGHT, -14, ctx->aligned && ui_widget_name_enabled(ctx->title_label) ? 42 : 10);
#else
    lv_obj_set_width(ctx->title_label, content_w);
    lv_obj_set_hidden(ctx->summary, true);
#endif
    lv_obj_align(ctx->title_label, LV_ALIGN_TOP_LEFT, 14, 10);

    if (ctx->vertical_bars) {
#if UI_TEXT_ADAPTIVE
        const lv_coord_t chart_top = ctx->aligned && ui_widget_name_enabled(ctx->title_label) ? 80 : (ui_widget_name_enabled(ctx->title_label) ? 54 : 52);
        const lv_coord_t value_h = lv_font_get_line_height(ui_font_role(UI_TEXT_VALUE));
        const lv_coord_t day_h = lv_font_get_line_height(SOLAR_ROW_FONT);
        const lv_coord_t bottom_pad = 10;
#else
        const lv_coord_t chart_top = ui_widget_name_enabled(ctx->title_label) ? 54 : 16;
        /* the value and day rows are as tall as their text (22 px at the former 16 px text), so larger text never leaves the tile */
        const lv_coord_t value_h = lv_font_get_line_height(ui_font_role(UI_TEXT_VALUE)) > 22 ? lv_font_get_line_height(ui_font_role(UI_TEXT_VALUE)) : 22;
        const lv_coord_t day_h = lv_font_get_line_height(SOLAR_ROW_FONT) > 22 ? lv_font_get_line_height(SOLAR_ROW_FONT) : 22;
        const lv_coord_t bottom_pad = 10;
#endif
        lv_coord_t card_h = lv_obj_get_height(ctx->card);
        lv_coord_t chart_h = card_h - chart_top - day_h - value_h - bottom_pad;
        if (chart_h < 20) chart_h = 20;
        lv_coord_t slot_w = ctx->aligned ? ui_day_grid_width(content_w, visible_rows) : content_w / visible_rows;
        if (slot_w < 34) {
            slot_w = 34;
        }
        lv_coord_t bar_w = slot_w / 2;
        if (bar_w < 16) {
            bar_w = 16;
        }
        if (bar_w > 42) {
            bar_w = 42;
        }

        for (int i = 0; i < SOLAR_FORECAST_ROWS; i++) {
            solar_forecast_row_t *row = &ctx->rows[i];
            const int column = i - 1; /* the remaining reading shares today's column */
            if (column < 0 || column >= visible_rows) {
                solar_set_row_hidden(row, true);
                continue;
            }
            solar_set_row_hidden(row, false);
            ui_text_record(row->day_label, UI_TEXT_LABEL, true);

            char day_label[16] = {0};
            solar_day_label(i - 1, day_label, sizeof(day_label));
            lv_label_set_text(row->day_label, day_label);
            lv_obj_set_style_text_align(row->day_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            const lv_font_t *day_font = ui_text_fit(day_label, SOLAR_ROW_FONT, slot_w - 4, day_h);
            lv_obj_set_style_text_font(row->day_label, day_font, LV_PART_MAIN);
            ui_text_compact(row->day_label, day_font == ui_font_role(UI_TEXT_META));
            lv_obj_set_size(row->day_label, slot_w, day_h);

            solar_configure_bar_direction(row->bar, false, true);
            lv_obj_set_style_bg_grad_dir(row->bar, LV_GRAD_DIR_VER, LV_PART_INDICATOR);
            lv_obj_set_size(row->bar, bar_w, chart_h);
            lv_bar_set_value(row->bar, solar_bar_percent(ctx, i, max_value), LV_ANIM_OFF);

            char value_text[32] = {0};
            if (ctx->valid[i]) {
                if (common_unit) solar_format_number(value_text, sizeof(value_text), ctx->values[i]);
                else solar_format_value(value_text, sizeof(value_text), ctx->values[i], ctx->units[i]);
            } else {
                snprintf(value_text, sizeof(value_text), "--");
            }
            lv_label_set_text(row->value_label, value_text);
            lv_obj_set_style_text_align(row->value_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            ui_text_set(row->value_label, UI_TEXT_VALUE, true);
#if UI_TEXT_ADAPTIVE
            const lv_font_t *fit = ui_text_fit(value_text, ui_font_role(UI_TEXT_VALUE), slot_w, value_h);
            lv_obj_set_style_text_font(row->value_label, fit, LV_PART_MAIN);
            ui_text_compact(row->value_label, fit == ui_font_role(UI_TEXT_META));
#endif
            lv_obj_set_size(row->value_label, slot_w, value_h);

            lv_coord_t slot_x = 14 + (ctx->aligned ? ui_day_grid_x(content_w, visible_rows, column) : column * slot_w);
            lv_obj_set_size(row->plate, slot_w - 4, chart_h + day_h + value_h + 12);
            lv_obj_align(row->plate, LV_ALIGN_TOP_LEFT, slot_x + 2, chart_top - 6);
            lv_obj_align(row->bar, LV_ALIGN_TOP_LEFT, slot_x + ((slot_w - bar_w) / 2), chart_top);
            solar_place_rest_bar(ctx, i, true, slot_x + ((slot_w - bar_w) / 2), chart_top, bar_w, chart_h, max_value);
            lv_obj_align(row->day_label, LV_ALIGN_TOP_LEFT, slot_x, chart_top + chart_h + 2);
            lv_obj_align(row->value_label, LV_ALIGN_TOP_LEFT, slot_x, chart_top + chart_h + day_h);
        }
        return;
    }

#if UI_TEXT_ADAPTIVE
    lv_coord_t row_y = 52; /* Unit header remains visible when the optional name is hidden. */
    const lv_coord_t row_gap = 3;
    const lv_coord_t bar_h = 16;
    /* The rows follow the text size but never leave the tile: with larger text the row height shrinks to what the tile has and the
     * texts are fitted into it. */
    lv_coord_t row_h = lv_font_get_line_height(SOLAR_ROW_FONT);
    if (visible_rows > 0) {
        const lv_coord_t room = lv_obj_get_height(ctx->card) - row_y - 8 - (visible_rows - 1) * row_gap;
        if (row_h * visible_rows > room) row_h = room / visible_rows;
    }
    if (row_h < 20) row_h = 20;
#else
    lv_coord_t row_y = ui_widget_name_enabled(ctx->title_label) ? 54 : 16;
    const lv_coord_t row_h = 30, row_gap = 3, bar_h = 16;
#endif

    const lv_coord_t row_y_first = row_y;
    for (int i = 0; i < SOLAR_FORECAST_ROWS; i++) {
        solar_forecast_row_t *row = &ctx->rows[i];
        const int column = i - 1; /* the remaining reading shares today's row */
        if (column < 0 || column >= visible_rows) {
            solar_set_row_hidden(row, true);
            continue;
        }
        solar_set_row_hidden(row, false);
        row_y = row_y_first + column * (row_h + row_gap);

        char day_label[16] = {0};
        solar_day_label(i - 1, day_label, sizeof(day_label));
        lv_label_set_text(row->day_label, day_label);
        lv_obj_set_style_text_align(row->day_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
#if UI_TEXT_ADAPTIVE
        const lv_font_t *row_font = ui_text_fit(day_label, SOLAR_ROW_FONT, label_w, row_h);
        lv_obj_set_style_text_font(row->day_label, row_font, LV_PART_MAIN);
#else
        const lv_font_t *row_font = SOLAR_ROW_FONT;
#endif
        lv_obj_set_size(row->day_label, label_w, row_h);
        const lv_coord_t text_dy = (row_h - lv_font_get_line_height(row_font)) / 2;
        lv_obj_align(row->day_label, LV_ALIGN_TOP_LEFT, 14, row_y + text_dy);

        solar_configure_bar_direction(row->bar, false, false);
        lv_obj_set_style_bg_grad_dir(row->bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
        lv_obj_set_size(row->plate, card_w - 16, row_h);
        lv_obj_align(row->plate, LV_ALIGN_TOP_LEFT, 8, row_y);
        lv_obj_set_size(row->bar, bar_w, bar_h);
        lv_obj_align(row->bar, LV_ALIGN_TOP_LEFT, 14 + label_w + 8, row_y + (row_h / 2) - (bar_h / 2));
        lv_bar_set_value(row->bar, solar_bar_percent(ctx, i, max_value), LV_ANIM_OFF);
        solar_place_rest_bar(ctx, i, false, 14 + label_w + 8, row_y + (row_h / 2) - (bar_h / 2), bar_w, bar_h, max_value);

        char value_text[32] = {0};
        if (ctx->valid[i]) {
            solar_format_value(value_text, sizeof(value_text), ctx->values[i], ctx->units[i]);
        } else {
            snprintf(value_text, sizeof(value_text), "--");
        }
        lv_label_set_text(row->value_label, value_text);
#if UI_TEXT_ADAPTIVE
        const lv_font_t *fit_value = ui_text_fit(value_text, ui_font_role(UI_TEXT_VALUE), value_w, row_h);
        lv_obj_set_style_text_font(row->value_label, fit_value, LV_PART_MAIN);
        ui_text_record(row->value_label, UI_TEXT_VALUE, true); ui_text_compact(row->value_label, fit_value == ui_font_role(UI_TEXT_META));
#endif
        lv_obj_set_style_text_align(row->value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_set_size(row->value_label, value_w, row_h);
        lv_obj_align(row->value_label, LV_ALIGN_TOP_RIGHT, -14, row_y + text_dy);
    }
}

/* A reduced aligned preview keeps every configured daily reading in detail. */
static void solar_popup_close(w_solar_forecast_ctx_t *ctx)
{
    if (ctx->popup_timer) { lv_timer_delete(ctx->popup_timer); ctx->popup_timer = NULL; }
    if (ctx->popup) { lv_obj_delete(ctx->popup); ctx->popup = NULL; }
}
static void solar_popup_timeout(lv_timer_t *timer) { solar_popup_close(lv_timer_get_user_data(timer)); }
static void solar_popup_close_clicked(lv_event_t *event) { solar_popup_close(lv_event_get_user_data(event)); }
static void solar_popup_open(lv_event_t *event)
{
    w_solar_forecast_ctx_t *ctx = lv_event_get_user_data(event);
    if (ctx->popup) return;
    ctx->popup = lv_obj_create(lv_layer_top());
    lv_obj_set_size(ctx->popup, APP_VIEW_WIDTH - 32, APP_VIEW_HEIGHT - 80);
    lv_obj_center(ctx->popup); theme_default_style_card(ctx->popup);
    lv_obj_set_style_bg_opa(ctx->popup, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(ctx->popup, false);
    lv_obj_t *heading = lv_label_create(ctx->popup);
    lv_label_set_text(heading, lv_label_get_text(ctx->title_label));
    ui_text_set(heading, UI_TEXT_LABEL, false);
    lv_obj_set_width(heading, APP_VIEW_WIDTH - 140);
    lv_obj_set_height(heading, ui_font_role(UI_TEXT_LABEL)->line_height);
    lv_label_set_long_mode(heading, LV_LABEL_LONG_DOT);
    lv_obj_align(heading, LV_ALIGN_TOP_LEFT, 12, 8);
    lv_obj_t *close = ui_popup_close_button(ctx->popup, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), solar_popup_close_clicked, ctx);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -8, 0);
    for (int i = 0; i < SOLAR_FORECAST_ROWS; i++) {
        char day[24], value[48];
        if (!i) snprintf(day, sizeof(day), "%s", ui_i18n_get("solar.rest", "Rest"));
        else solar_day_label(i - 1, day, sizeof(day));
        if (ctx->valid[i]) solar_format_value(value, sizeof(value), ctx->values[i], ctx->units[i]);
        else snprintf(value, sizeof(value), "-- %s", ctx->units[i]);
        lv_obj_t *label = lv_label_create(ctx->popup); lv_label_set_text(label, day);
        ui_text_set(label, UI_TEXT_LABEL, true);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 16, 72 + i * 55);
        lv_obj_t *reading = lv_label_create(ctx->popup); lv_label_set_text(reading, value);
        ui_text_set(reading, UI_TEXT_VALUE, true);
        lv_obj_align(reading, LV_ALIGN_TOP_RIGHT, -16, 70 + i * 55);
    }
    ctx->popup_timer = lv_timer_create(solar_popup_timeout, 60000, ctx);
}

static void w_solar_forecast_event_cb(lv_event_t *event)
{
    w_solar_forecast_ctx_t *ctx = (w_solar_forecast_ctx_t *)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DELETE) {
        solar_popup_close(ctx);
        free(ctx);
    } else if (code == LV_EVENT_SIZE_CHANGED) {
        solar_render(ctx);
    }
}

esp_err_t w_solar_forecast_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    w_solar_forecast_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    snprintf(ctx->entity_ids[0], sizeof(ctx->entity_ids[0]), "%s", def->entity_id);
    snprintf(ctx->entity_ids[1], sizeof(ctx->entity_ids[1]), "%s", def->forecast_today_entity_id);
    snprintf(ctx->entity_ids[2], sizeof(ctx->entity_ids[2]), "%s", def->forecast_tomorrow_entity_id);
    snprintf(ctx->entity_ids[3], sizeof(ctx->entity_ids[3]), "%s", def->forecast_day_3_entity_id);
    snprintf(ctx->entity_ids[4], sizeof(ctx->entity_ids[4]), "%s", def->forecast_day_4_entity_id);
    snprintf(ctx->entity_ids[5], sizeof(ctx->entity_ids[5]), "%s", def->forecast_day_5_entity_id);
    for (int i = 0; i < SOLAR_FORECAST_ROWS; i++) {
        snprintf(ctx->units[i], sizeof(ctx->units[i]), "%s", "kWh");
    }
    ctx->bar_max_kwh = def->solar_forecast_bar_max_kwh > 0.0f ? def->solar_forecast_bar_max_kwh : 0.0f;
    ctx->vertical_bars = strcmp(def->solar_forecast_bar_orientation, "vertical") == 0;

    lv_obj_t *card = lv_obj_create(parent);
    ctx->card = card;
    out_instance->obj = card;
    out_instance->ctx = ctx;
    lv_obj_set_pos(card, def->x, def->y);
    lv_obj_set_size(card, def->w, def->h);
    theme_default_style_card(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(card, false);

    ctx->aligned = def->align_forecast;
    ctx->forecast_days = def->forecast_days < 0 ? 0 : def->forecast_days;
    ctx->summary = lv_label_create(card);
    ui_text_record(ctx->summary, UI_TEXT_LABEL, true);
    lv_obj_set_style_text_color(ctx->summary, theme_default_color_text_primary(), LV_PART_MAIN);
    ctx->title_label = ui_widget_name_create(card, !def->hide_name);
    lv_label_set_text(ctx->title_label, def->title[0] != '\0' ? def->title : "Solar Forecast");
    ui_text_set(ctx->title_label, UI_TEXT_LABEL, false);
    lv_obj_set_height(ctx->title_label, SOLAR_TITLE_FONT->line_height);
    lv_obj_set_style_text_color(ctx->title_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->title_label, LV_LABEL_LONG_DOT);

    for (int i = 0; i < SOLAR_FORECAST_ROWS; i++) {
        solar_forecast_row_t *row = &ctx->rows[i];
        row->plate = lv_obj_create(card);
        lv_obj_remove_style_all(row->plate);
        lv_obj_set_clickable(row->plate, false);
        lv_obj_set_style_radius(row->plate, 12, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row->plate, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(row->plate,
            lv_color_mix(lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 14),
            LV_PART_MAIN);
        row->day_label = lv_label_create(card);
        lv_obj_set_style_text_font(row->day_label, SOLAR_ROW_FONT, LV_PART_MAIN);
        lv_obj_set_style_text_color(row->day_label, lv_color_hex(APP_UI_COLOR_TEXT_SOFT), LV_PART_MAIN);
        lv_label_set_long_mode(row->day_label, LV_LABEL_LONG_DOT);

        row->bar = lv_bar_create(card);
        lv_bar_set_range(row->bar, 0, 100);
        ui_style_inset_track(row->bar, lv_color_mix(lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY),
                                                    lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 26));
        if (ui_style_is_flat()) {
            lv_obj_set_style_bg_color(row->bar, lv_color_hex(0x3A3F47), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(row->bar, LV_OPA_COVER, LV_PART_MAIN);
        }
        lv_obj_set_style_bg_color(row->bar, lv_color_hex(SOLAR_BAR_GOLD), LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_color(row->bar, lv_color_hex(SOLAR_BAR_GREEN), LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_dir(row->bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(row->bar, LV_OPA_COVER, LV_PART_INDICATOR);

        row->rest_bar = lv_bar_create(card);
        lv_obj_remove_style_all(row->rest_bar);
        lv_obj_set_clickable(row->rest_bar, false);
        lv_obj_set_style_bg_opa(row->rest_bar, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_radius(row->rest_bar, lv_obj_get_style_radius(row->bar, LV_PART_MAIN), LV_PART_MAIN);
        lv_obj_set_style_radius(row->rest_bar, lv_obj_get_style_radius(row->bar, LV_PART_INDICATOR), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(row->rest_bar, lv_color_hex(SOLAR_BAR_GOLD), LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_color(row->rest_bar, lv_color_hex(SOLAR_BAR_GREEN), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(row->rest_bar, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_hidden(row->rest_bar, true);

        row->value_label = lv_label_create(card);
        lv_obj_set_style_text_font(row->value_label, SOLAR_ROW_FONT, LV_PART_MAIN);
        lv_obj_set_style_text_color(row->value_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
        lv_obj_set_style_text_align(row->value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_label_set_long_mode(row->value_label, LV_LABEL_LONG_DOT);
    }

    lv_obj_add_event_cb(card, solar_popup_open, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(card, w_solar_forecast_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, w_solar_forecast_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    solar_render(ctx);
    return ESP_OK;
}

void w_solar_forecast_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->ctx == NULL || state == NULL) {
        return;
    }
    w_solar_forecast_ctx_t *ctx = (w_solar_forecast_ctx_t *)instance->ctx;
    int index = solar_entity_index(ctx, state->entity_id);
    if (index < 0 || index >= SOLAR_FORECAST_ROWS) {
        return;
    }

    float value = 0.0f;
    ctx->valid[index] = solar_parse_value(state, &value);
    ctx->values[index] = ctx->valid[index] ? value : 0.0f;
    solar_extract_unit(state->attributes_json, ctx->units[index], sizeof(ctx->units[index]));
    solar_render(ctx);
}

void w_solar_forecast_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_solar_forecast_ctx_t *ctx = (w_solar_forecast_ctx_t *)instance->ctx;
    for (int i = 0; i < SOLAR_FORECAST_ROWS; i++) {
        ctx->valid[i] = false;
        ctx->values[i] = 0.0f;
    }
    solar_render(ctx);
}
