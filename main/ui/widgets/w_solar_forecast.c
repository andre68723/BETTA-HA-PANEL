/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#include "ui/ui_widget_factory.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"

#include "ui/fonts/app_text_fonts.h"
#include "ui/theme/theme_default.h"
#include "ui/ui_memory.h"

#define SOLAR_FORECAST_ROWS 6
#define SOLAR_FORECAST_DAY_ROWS 5

#if LV_FONT_MONTSERRAT_22
#define SOLAR_TITLE_FONT (&lv_font_montserrat_22)
#elif LV_FONT_MONTSERRAT_20
#define SOLAR_TITLE_FONT APP_FONT_TEXT_20
#else
#define SOLAR_TITLE_FONT APP_FONT_TEXT_18
#endif

#if LV_FONT_MONTSERRAT_18
#define SOLAR_ROW_FONT (&lv_font_montserrat_18)
#elif LV_FONT_MONTSERRAT_16
#define SOLAR_ROW_FONT (&lv_font_montserrat_16)
#else
#define SOLAR_ROW_FONT APP_FONT_TEXT_14
#endif

typedef struct {
    lv_obj_t *day_label;
    lv_obj_t *bar;
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
    if (value >= 100.0f) {
        snprintf(dst, dst_size, "%.0f %s", (double)value, suffix);
    } else {
        snprintf(dst, dst_size, "%.1f %s", (double)value, suffix);
    }
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
    if (value >= 100.0f) {
        snprintf(dst, dst_size, "%.0f", (double)value);
    } else {
        snprintf(dst, dst_size, "%.1f", (double)value);
    }
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
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480)
    const lv_coord_t rows_top = 46;
    const lv_coord_t row_h = 26;
    const lv_coord_t row_gap = 3;
#else
    const lv_coord_t rows_top = 54;
    const lv_coord_t row_h = 30;
    const lv_coord_t row_gap = 3;
#endif
    lv_coord_t avail = h - rows_top - 14;
    int rows = 5;
    if (avail > row_h) {
        rows = (int)((avail + row_gap) / (row_h + row_gap));
    }
    if (rows < 3) rows = 3;
    if (rows > SOLAR_FORECAST_ROWS) rows = SOLAR_FORECAST_ROWS;
    return rows;
}

static void solar_day_label(int forecast_index, char *dst, size_t dst_size)
{
    static const char *const days_de[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
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
        snprintf(dst, dst_size, "%s", days_de[wday]);
        return;
    }

    static const char *const fallback[] = {"Heute", "Morgen", "Tag 3", "Tag 4", "Tag 5"};
    if (forecast_index >= 0 && forecast_index < SOLAR_FORECAST_DAY_ROWS) {
        snprintf(dst, dst_size, "%s", fallback[forecast_index]);
    } else {
        snprintf(dst, dst_size, "Tag");
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

static void solar_set_row_hidden(solar_forecast_row_t *row, bool hidden)
{
    if (row == NULL) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(row->day_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(row->bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(row->value_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(row->day_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(row->bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(row->value_label, LV_OBJ_FLAG_HIDDEN);
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

    const int visible_rows = solar_visible_rows(ctx->card);
    lv_coord_t card_w = lv_obj_get_width(ctx->card);
    lv_coord_t content_w = card_w - 28;
    if (content_w < 120) {
        content_w = 120;
    }
    lv_coord_t label_w = content_w / 5;
    if (label_w < 50) {
        label_w = 50;
    }
    lv_coord_t value_w = content_w / 4;
    if (value_w < 76) {
        value_w = 76;
    }
    lv_coord_t bar_w = content_w - label_w - value_w - 18;
    if (bar_w < 64) {
        bar_w = 64;
    }

    lv_obj_set_width(ctx->title_label, content_w);
    lv_obj_align(ctx->title_label, LV_ALIGN_TOP_LEFT, 14, 10);

    if (ctx->vertical_bars) {
#if defined(CONFIG_APP_PANEL_VARIANT_S3_480)
        const lv_coord_t chart_top = 48;
        const lv_coord_t value_h = 22;
        const lv_coord_t day_h = 22;
        const lv_coord_t bottom_pad = 8;
#else
        const lv_coord_t chart_top = 54;
        const lv_coord_t value_h = 24;
        const lv_coord_t day_h = 24;
        const lv_coord_t bottom_pad = 10;
#endif
        lv_coord_t card_h = lv_obj_get_height(ctx->card);
        lv_coord_t chart_h = card_h - chart_top - day_h - value_h - bottom_pad;
        if (chart_h < 72) {
            chart_h = 72;
        }
        lv_coord_t slot_w = content_w / visible_rows;
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
            if (i >= visible_rows) {
                solar_set_row_hidden(row, true);
                continue;
            }
            solar_set_row_hidden(row, false);

            char day_label[16] = {0};
            if (i == 0) {
                snprintf(day_label, sizeof(day_label), "Rest");
            } else {
                solar_day_label(i - 1, day_label, sizeof(day_label));
            }
            lv_label_set_text(row->day_label, day_label);
            lv_obj_set_style_text_align(row->day_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_size(row->day_label, slot_w, day_h);

            lv_obj_set_style_bg_grad_dir(row->bar, LV_GRAD_DIR_VER, LV_PART_INDICATOR);
            lv_obj_set_size(row->bar, bar_w, chart_h);
            lv_bar_set_value(row->bar, solar_bar_percent(ctx, i, max_value), LV_ANIM_OFF);

            char value_text[32] = {0};
            if (ctx->valid[i]) {
                solar_format_number(value_text, sizeof(value_text), ctx->values[i]);
            } else {
                snprintf(value_text, sizeof(value_text), "--");
            }
            lv_label_set_text(row->value_label, value_text);
            lv_obj_set_style_text_align(row->value_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_size(row->value_label, slot_w, value_h);

            lv_coord_t slot_x = 14 + (i * slot_w);
            lv_obj_align(row->bar, LV_ALIGN_TOP_LEFT, slot_x + ((slot_w - bar_w) / 2), chart_top);
            lv_obj_align(row->day_label, LV_ALIGN_TOP_LEFT, slot_x, chart_top + chart_h + 2);
            lv_obj_align(row->value_label, LV_ALIGN_TOP_LEFT, slot_x, chart_top + chart_h + day_h);
        }
        return;
    }

#if defined(CONFIG_APP_PANEL_VARIANT_S3_480)
    lv_coord_t row_y = 44;
    const lv_coord_t row_h = 26;
    const lv_coord_t row_gap = 3;
    const lv_coord_t bar_h = 14;
#else
    lv_coord_t row_y = 52;
    const lv_coord_t row_h = 30;
    const lv_coord_t row_gap = 3;
    const lv_coord_t bar_h = 16;
#endif

    for (int i = 0; i < SOLAR_FORECAST_ROWS; i++) {
        solar_forecast_row_t *row = &ctx->rows[i];
        if (i >= visible_rows) {
            solar_set_row_hidden(row, true);
            continue;
        }
        solar_set_row_hidden(row, false);

        char day_label[16] = {0};
        if (i == 0) {
            snprintf(day_label, sizeof(day_label), "Rest");
        } else {
            solar_day_label(i - 1, day_label, sizeof(day_label));
        }
        lv_label_set_text(row->day_label, day_label);
        lv_obj_set_style_text_align(row->day_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_size(row->day_label, label_w, row_h);
        lv_obj_align(row->day_label, LV_ALIGN_TOP_LEFT, 14, row_y);

        lv_obj_set_style_bg_grad_dir(row->bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
        lv_obj_set_size(row->bar, bar_w, bar_h);
        lv_obj_align(row->bar, LV_ALIGN_TOP_LEFT, 14 + label_w + 8, row_y + (row_h / 2) - (bar_h / 2));
        lv_bar_set_value(row->bar, solar_bar_percent(ctx, i, max_value), LV_ANIM_OFF);

        char value_text[32] = {0};
        if (ctx->valid[i]) {
            solar_format_value(value_text, sizeof(value_text), ctx->values[i], ctx->units[i]);
        } else {
            snprintf(value_text, sizeof(value_text), "--");
        }
        lv_label_set_text(row->value_label, value_text);
        lv_obj_set_style_text_align(row->value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_set_size(row->value_label, value_w, row_h);
        lv_obj_align(row->value_label, LV_ALIGN_TOP_RIGHT, -14, row_y);

        row_y += row_h + row_gap;
    }
}

static void w_solar_forecast_event_cb(lv_event_t *event)
{
    w_solar_forecast_ctx_t *ctx = (w_solar_forecast_ctx_t *)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DELETE) {
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
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    ctx->title_label = lv_label_create(card);
    lv_label_set_text(ctx->title_label, def->title[0] != '\0' ? def->title : "Solar Forecast");
    lv_obj_set_style_text_font(ctx->title_label, SOLAR_TITLE_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->title_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->title_label, LV_LABEL_LONG_DOT);

    for (int i = 0; i < SOLAR_FORECAST_ROWS; i++) {
        solar_forecast_row_t *row = &ctx->rows[i];
        row->day_label = lv_label_create(card);
        lv_obj_set_style_text_font(row->day_label, SOLAR_ROW_FONT, LV_PART_MAIN);
        lv_obj_set_style_text_color(row->day_label, lv_color_hex(APP_UI_COLOR_TEXT_SOFT), LV_PART_MAIN);
        lv_label_set_long_mode(row->day_label, LV_LABEL_LONG_DOT);

        row->bar = lv_bar_create(card);
        lv_bar_set_range(row->bar, 0, 100);
        lv_obj_set_style_radius(row->bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_radius(row->bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(row->bar, lv_color_hex(0xDADADA), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row->bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(row->bar, lv_color_hex(0xFFD500), LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_color(row->bar, lv_color_hex(0x22D164), LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_dir(row->bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(row->bar, LV_OPA_COVER, LV_PART_INDICATOR);

        row->value_label = lv_label_create(card);
        lv_obj_set_style_text_font(row->value_label, SOLAR_ROW_FONT, LV_PART_MAIN);
        lv_obj_set_style_text_color(row->value_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
        lv_obj_set_style_text_align(row->value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_label_set_long_mode(row->value_label, LV_LABEL_LONG_DOT);
    }

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
