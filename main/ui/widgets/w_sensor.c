/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/ui_widget_factory.h"
#include "ui/widgets/w_sensor_list.h"
#include "ui/text_font.h"

#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_timer.h"

#include "ui/theme/ui_typography.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"
#include "ui/ui_history_popup.h"
#include "ui/theme/theme_default.h"
#include "ha/ha_datetime_value.h"
#include "ha/ha_display_precision.h"
#include "ha/ha_sensor_value.h"
#include "ha/ha_timestamp_format.h"

#define SENSOR_VALUE_FONT_SMALL ui_font_role(UI_TEXT_META)
#define SENSOR_VALUE_FONT_MEDIUM ui_font_role(UI_TEXT_VALUE)
#define SENSOR_VALUE_FONT_LARGE ui_font_role(UI_TEXT_KEY)
#define SENSOR_VALUE_FONT_XL ui_font_role(UI_TEXT_KEY)
#define SENSOR_META_FONT ui_font_role(UI_TEXT_META)
#define SENSOR_PLAUSIBLE_EPOCH 1600000000LL /* 2020-09-13: an unsynchronised clock starts near 1970 */

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    lv_obj_t *card;
    lv_obj_t *font_root;
    bool list_row;
    lv_obj_t *title_label;
    lv_obj_t *value_label;
    lv_obj_t *age_label;
    char value_text[96];
    int64_t last_update_ms;
    bool has_timestamp;
    bool unavailable;
    bool show_label;
    bool show_age;
    bool history_popup;
    bool numeric;
    char format[12];      /* sensor_format as saved; its meaning follows the device class */
    bool time_format_set; /* sensor_format names a timestamp style: format timestamps even without device_class */
    ha_sv_unit_t unit_mode;
    bool timestamp;       /* the state is a timestamp, shown in time_style */
    ha_ts_style_t time_style;
    int64_t timestamp_s;
    bool date;            /* the state is a date (device_class date) */
    int date_y, date_m, date_d;
    lv_timer_t *age_timer;
} w_sensor_ctx_t;

static bool sensor_state_is_unavailable(const char *state_text)
{
    if (state_text == NULL || state_text[0] == '\0') {
        return true;
    }
    return strcmp(state_text, "unavailable") == 0 || strcmp(state_text, "unknown") == 0;
}

static int64_t sensor_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static const lv_font_t *sensor_pick_value_font(const w_sensor_ctx_t *ctx)
{
    if (ctx == NULL || ctx->card == NULL) {
        return SENSOR_VALUE_FONT_MEDIUM;
    }

    /* Without the name and the update time the value is all the tile shows: start from the largest
     * font, sensor_fit_value_font() steps down until it fits the tile's width and height. */
    if (!ctx->show_label && !ctx->show_age) {
        return SENSOR_VALUE_FONT_XL;
    }

    lv_coord_t w = lv_obj_get_width(ctx->card);
    lv_coord_t h = lv_obj_get_height(ctx->card);
    lv_coord_t min_dim = (w < h) ? w : h;

    if (min_dim >= 260) {
        return SENSOR_VALUE_FONT_XL;
    }
    if (min_dim >= 190) {
        return SENSOR_VALUE_FONT_LARGE;
    }
    if (min_dim >= 140) {
        return SENSOR_VALUE_FONT_MEDIUM;
    }
    return SENSOR_VALUE_FONT_SMALL;
}

static void sensor_set_value_text(w_sensor_ctx_t *ctx, const char *text)
{
    if (ctx == NULL || ctx->value_label == NULL) {
        return;
    }
    snprintf(ctx->value_text, sizeof(ctx->value_text), "%s", (text != NULL && text[0] != '\0') ? text : "--");
    lv_label_set_text(ctx->value_label, ctx->value_text);
}

/* A timestamp state as the chosen style; relative needs a synchronised clock and otherwise shows date and time. */
static void sensor_timestamp_text(const w_sensor_ctx_t *ctx, char *out, size_t out_len)
{
    const int64_t now_s = (int64_t)time(NULL);
    if (ctx->time_style != HA_TS_RELATIVE || now_s < SENSOR_PLAUSIBLE_EPOCH) {
        if (!ha_timestamp_format_absolute(ctx->timestamp_s, ctx->time_style, ui_i18n_get_language(), out, out_len)) {
            snprintf(out, out_len, "--");
        }
        return;
    }
    ha_ts_rel_unit_t unit;
    int amount;
    bool future;
    ha_timestamp_relative(ctx->timestamp_s, now_s, &unit, &amount, &future);
    static const char *const in_keys[][3] = {
        {"sensor.in.now", "now", NULL},
        {"sensor.in.min_one", "in 1 min", "sensor.in.min_many"},
        {"sensor.in.hour_one", "in 1 hour", "sensor.in.hour_many"},
        {"sensor.in.day_one", "in 1 day", "sensor.in.day_many"},
    };
    static const char *const in_many[] = {NULL, "in %d min", "in %d hours", "in %d days"};
    static const char *const ago_keys[][3] = {
        {"sensor.age.just_now", "just now", NULL},
        {"sensor.age.min_one", "1 min ago", "sensor.age.min_many"},
        {"sensor.age.hour_one", "1 hour ago", "sensor.age.hour_many"},
        {"sensor.age.day_one", "1 day ago", "sensor.age.day_many"},
    };
    static const char *const ago_many[] = {NULL, "%d min ago", "%d hours ago", "%d days ago"};
    const char *const *keys = future ? in_keys[unit] : ago_keys[unit];
    if (unit == HA_TS_REL_NOW || amount == 1) {
        snprintf(out, out_len, "%s", ui_i18n_get(keys[0], keys[1]));
    } else {
        snprintf(out, out_len, ui_i18n_get(keys[2], future ? in_many[unit] : ago_many[unit]), amount);
    }
}

/* A date state as a local date, or relative to today ("tomorrow", "in 3 days") with a synchronised clock. */
static void sensor_date_text(const w_sensor_ctx_t *ctx, char *out, size_t out_len)
{
    const time_t now = time(NULL);
    struct tm local;
    if (strcmp(ctx->format, "relative") == 0 && (int64_t)now >= SENSOR_PLAUSIBLE_EPOCH && localtime_r(&now, &local) != NULL) {
        const int64_t days = ha_sv_days_between(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                                                ctx->date_y, ctx->date_m, ctx->date_d);
        if (days == 0) snprintf(out, out_len, "%s", ui_i18n_get("sensor.day.today", "today"));
        else if (days == 1) snprintf(out, out_len, "%s", ui_i18n_get("sensor.day.tomorrow", "tomorrow"));
        else if (days == -1) snprintf(out, out_len, "%s", ui_i18n_get("sensor.day.yesterday", "yesterday"));
        else if (days > 0) snprintf(out, out_len, ui_i18n_get("sensor.in.day_many", "in %d days"), (int)days);
        else snprintf(out, out_len, ui_i18n_get("sensor.age.day_many", "%d days ago"), (int)-days);
        return;
    }
    ha_datetime_value_t v = {
        .kind = HA_DATETIME_DATE, .has_date = true, .year = ctx->date_y, .month = ctx->date_m, .day = ctx->date_d,
    };
    if (!ha_datetime_format(&v, ui_i18n_get_language(), out, out_len)) snprintf(out, out_len, "--");
}

/* The state as the device class and the tile's sensor_format/sensor_unit choose (ha_sensor_value.h). */
static void sensor_value_text(w_sensor_ctx_t *ctx, const ha_state_t *state, const char *device_class,
                              const char *unit, char *out, size_t out_len)
{
    if (ctx->timestamp) {
        sensor_timestamp_text(ctx, out, out_len);
        return;
    }
    if (ctx->date) {
        sensor_date_text(ctx, out, out_len);
        return;
    }
    if (strncmp(state->entity_id, "binary_sensor.", 14) == 0 &&
        (strcmp(state->state, "on") == 0 || strcmp(state->state, "off") == 0)) {
        snprintf(out, out_len, "%s", w_binary_class_state_text(device_class, strcmp(state->state, "on") == 0));
        return;
    }
    int64_t seconds;
    const ha_sv_duration_style_t duration = ha_sv_duration_style(ctx->format);
    if (strcmp(device_class, "duration") == 0 && duration != HA_SV_DURATION_RAW &&
        ha_sv_duration_seconds(state->state, unit, &seconds)) {
        const char *const labels[4] = {
            ui_i18n_get("sensor.dur.d", "d"), ui_i18n_get("sensor.dur.h", "h"),
            ui_i18n_get("sensor.dur.min", "min"), ui_i18n_get("sensor.dur.s", "s"),
        };
        if (ha_sv_duration_text(seconds, duration, labels, out, out_len)) return;
    }
    if (strcmp(device_class, "enum") == 0 && ha_sv_enum_text(state->state, out, out_len)) {
        return;
    }
    char rounded[APP_MAX_STATE_LEN];
    const char *value = ha_display_precision_format(state->entity_id, state->state, rounded, sizeof(rounded)) ||
        (strcmp(device_class, "monetary") == 0 && ha_sv_currency_round(state->state, unit, rounded, sizeof(rounded)))
        ? rounded : state->state;
    if (!ha_sv_number_text(value, state->state, unit, device_class, ctx->unit_mode, ui_i18n_get_language(), out, out_len)) {
        snprintf(out, out_len, "%s", value);
    }
}

static const lv_font_t *sensor_fit_value_font(const w_sensor_ctx_t *ctx, lv_coord_t width, lv_coord_t height)
{
    const lv_font_t *preferred = sensor_pick_value_font(ctx);
#if UI_TEXT_ADAPTIVE
    return ui_text_fit(ctx->value_text, ui_font_role(UI_TEXT_KEY), width, height);
#endif
    const lv_font_t *fonts[] = {
        preferred, SENSOR_VALUE_FONT_LARGE, SENSOR_VALUE_FONT_MEDIUM,
        SENSOR_VALUE_FONT_SMALL, ui_font_role(UI_TEXT_META),
    };
    const lv_font_t *smallest = ui_font_role(UI_TEXT_META);
    for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        const lv_font_t *font = fonts[i];
        if (lv_font_get_line_height(font) > lv_font_get_line_height(preferred) ||
            lv_font_get_line_height(font) > height) continue;
        smallest = font;
        lv_point_t text_size;
        lv_text_get_size(&text_size, ctx->value_text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (text_size.x <= width) return font;
    }
    return smallest; /* Extremely long values get one line with an ellipsis. */
}

static void sensor_update_age_label(w_sensor_ctx_t *ctx)
{
    if (ctx == NULL || ctx->age_label == NULL) {
        return;
    }

    if (!ctx->show_age || ctx->unavailable || !ctx->has_timestamp) {
        lv_obj_set_hidden(ctx->age_label, true);
        return;
    }

    int64_t age_ms = sensor_now_ms() - ctx->last_update_ms;
    if (age_ms < 0) {
        age_ms = 0;
    }

    int32_t age_min = (int32_t)(age_ms / 60000);
    int32_t age_hour = age_min / 60;
    int32_t age_day = age_hour / 24;

    char text[32] = {0};
    if (age_min <= 0) {
        snprintf(text, sizeof(text), "%s", ui_i18n_get("sensor.age.just_now", "just now"));
    } else if (age_min == 1) {
        snprintf(text, sizeof(text), "%s", ui_i18n_get("sensor.age.min_one", "1 min ago"));
    } else if (age_min < 60) {
        snprintf(text, sizeof(text), ui_i18n_get("sensor.age.min_many", "%d min ago"), (int)age_min);
    } else if (age_hour == 1) {
        snprintf(text, sizeof(text), "%s", ui_i18n_get("sensor.age.hour_one", "1 hour ago"));
    } else if (age_hour < 48) {
        snprintf(text, sizeof(text), ui_i18n_get("sensor.age.hour_many", "%d hours ago"), (int)age_hour);
    } else if (age_day == 1) {
        snprintf(text, sizeof(text), "%s", ui_i18n_get("sensor.age.day_one", "1 day ago"));
    } else {
        snprintf(text, sizeof(text), ui_i18n_get("sensor.age.day_many", "%d days ago"), (int)age_day);
    }

    lv_label_set_text(ctx->age_label, text);
    lv_obj_set_hidden(ctx->age_label, false);
    lv_obj_set_style_text_color(
        ctx->age_label,
        lv_color_hex((age_min >= 30) ? APP_UI_COLOR_STATE_OFF : APP_UI_COLOR_TEXT_MUTED),
        LV_PART_MAIN);
}

static void sensor_apply_row_layout(w_sensor_ctx_t *ctx)
{
    lv_obj_update_layout(ctx->card);
    const int width = lv_obj_get_content_width(ctx->card);
    if (width < 1) return;
    const lv_font_t *font = ui_font_role(UI_TEXT_LABEL);
    const int line = lv_font_get_line_height(font), age = lv_font_get_line_height(SENSOR_META_FONT);
    lv_point_t size;
    lv_text_get_size(&size, ctx->value_text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const bool stacked = size.x + 8 > width * 55 / 100;
    lv_obj_set_style_text_font(ctx->title_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->value_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->title_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->value_label, stacked ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_align(ctx->title_label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_size(ctx->title_label, stacked ? width : width - size.x - 8, line);
    lv_label_set_long_mode(ctx->value_label, stacked ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_CLIP);
    int value_h = line;
    if (stacked) {
        lv_text_get_size(&size, ctx->value_text, font, 0, 0, width, LV_TEXT_FLAG_NONE);
        value_h = size.y;
    }
    lv_obj_set_size(ctx->value_label, stacked ? width : size.x + 2, value_h);
    lv_obj_align(ctx->value_label, LV_ALIGN_TOP_LEFT, stacked ? 0 : width - size.x - 2, stacked ? line : 0);
    const int bottom = stacked ? line + value_h : line;
    lv_obj_set_style_text_font(ctx->age_label, SENSOR_META_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->age_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_size(ctx->age_label, width, age);
    lv_obj_align(ctx->age_label, LV_ALIGN_TOP_LEFT, 0, bottom + 2);
    const int height = bottom + (lv_obj_is_hidden(ctx->age_label) ? 0 : age + 2) + 4;
    if (lv_obj_get_height(ctx->card) != height) lv_obj_set_height(ctx->card, height);
}

static void sensor_apply_layout(w_sensor_ctx_t *ctx)
{
    if (ctx == NULL || ctx->card == NULL || ctx->title_label == NULL || ctx->value_label == NULL || ctx->age_label == NULL) {
        return;
    }

    if (ctx->list_row) { sensor_apply_row_layout(ctx); return; }

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

    lv_obj_set_style_text_font(ctx->age_label, SENSOR_META_FONT, LV_PART_MAIN);

    lv_obj_set_width(ctx->title_label, content_w);
    lv_obj_set_width(ctx->value_label, content_w);
    lv_obj_set_width(ctx->age_label, content_w);
    lv_obj_set_style_text_align(ctx->title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->value_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->age_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_align(ctx->title_label, LV_ALIGN_TOP_MID, 0, 2);

    /* Reserve room for the title and freshness line before choosing the value
     * font. A long raw HA value may include a unit and otherwise wrap into the
     * freshness line, even when the card itself has enough height. */
    const bool show_title = !lv_obj_is_hidden(ctx->title_label);
    lv_coord_t title_bottom = 0;
    if (show_title) {
        lv_obj_update_layout(ctx->title_label);
        title_bottom = lv_obj_get_y(ctx->title_label) + lv_obj_get_height(ctx->title_label);
    }

    bool show_age = !lv_obj_is_hidden(ctx->age_label);
    lv_coord_t age_h = 0;
    if (show_age) {
        lv_obj_update_layout(ctx->age_label);
        age_h = lv_obj_get_height(ctx->age_label);
    }
    lv_coord_t min_value_y = show_title ? title_bottom + 2 : 0;
    lv_coord_t value_room_h = content_h - min_value_y - (show_age ? age_h + 6 : 2);
    if (show_age && value_room_h < lv_font_get_line_height(ui_font_role(UI_TEXT_META))) {
        lv_obj_set_hidden(ctx->age_label, true);
        show_age = false;
        value_room_h = content_h - min_value_y - 2;
    }
    const lv_font_t *value_font = sensor_fit_value_font(ctx, content_w, value_room_h);
    if (show_title && lv_font_get_line_height(value_font) > value_room_h) {
        /* Too low for the name and the smallest value text (larger text on a minimum-size tile): the value wins. */
        ui_widget_name_set_hidden(ctx->title_label, true);
        min_value_y = 0;
        value_room_h = content_h - (show_age ? age_h + 6 : 2);
        value_font = sensor_fit_value_font(ctx, content_w, value_room_h);
    }
    lv_obj_set_style_text_font(ctx->value_label, value_font, LV_PART_MAIN);
    ui_text_compact(ctx->value_label, value_font == ui_font_role(UI_TEXT_META));
    lv_label_set_long_mode(ctx->value_label, LV_LABEL_LONG_DOT);
    lv_obj_set_height(ctx->value_label, lv_font_get_line_height(value_font));

    lv_coord_t min_dim = (content_w < content_h) ? content_w : content_h;
    lv_coord_t value_y = 0;
    if (show_age) {
        if (min_dim >= 260) {
            value_y = -18;
        } else if (min_dim >= 190) {
            value_y = -14;
        } else {
            value_y = -10;
        }
    }
    lv_obj_align(ctx->value_label, LV_ALIGN_CENTER, 0, value_y);
    lv_obj_update_layout(ctx->value_label);
    lv_coord_t aligned_value_top = lv_obj_get_y(ctx->value_label);
    lv_coord_t value_top = aligned_value_top;
    lv_coord_t max_value_y = content_h - lv_obj_get_height(ctx->value_label) - (show_age ? age_h + 6 : 2);
    if (value_top < min_value_y) value_top = min_value_y;
    if (value_top > max_value_y) value_top = max_value_y;
    /* Re-anchored at the top: lv_obj_set_y() after the CENTER alignment above would offset from
     * the center and pushed the value below the tile on a small one. */
    if (value_top != aligned_value_top) lv_obj_align(ctx->value_label, LV_ALIGN_TOP_MID, 0, value_top);

    if (show_age) {
        lv_coord_t age_y = value_top + lv_obj_get_height(ctx->value_label) + 4;
        if (age_y + age_h > content_h - 2) {
            age_y = content_h - age_h - 2;
        }
        lv_obj_align(ctx->age_label, LV_ALIGN_TOP_MID, 0, age_y);
    }
}

static void sensor_apply_unavailable(w_sensor_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->unavailable = true;
    ctx->timestamp = false;
    ctx->date = false;
    ctx->has_timestamp = false;
    ctx->last_update_ms = 0;
    sensor_set_value_text(ctx, ui_i18n_get("common.unavailable", "unavailable"));
    sensor_update_age_label(ctx);
    sensor_apply_layout(ctx);
}

static void sensor_age_timer_cb(lv_timer_t *timer)
{
    if (timer == NULL) {
        return;
    }
    w_sensor_ctx_t *ctx = (w_sensor_ctx_t *)lv_timer_get_user_data(timer);
    if (ctx == NULL) {
        return;
    }

    ui_fonts_tile_begin("sensor", lv_obj_get_width(ctx->font_root), lv_obj_get_height(ctx->font_root));
    if (ctx->timestamp && ctx->time_style == HA_TS_RELATIVE) {
        char text[sizeof(ctx->value_text)];
        sensor_timestamp_text(ctx, text, sizeof(text));
        sensor_set_value_text(ctx, text);
    } else if (ctx->date && strcmp(ctx->format, "relative") == 0) {
        char text[sizeof(ctx->value_text)];
        sensor_date_text(ctx, text, sizeof(text));
        sensor_set_value_text(ctx, text);
    }
    sensor_update_age_label(ctx);
    sensor_apply_layout(ctx);
    ui_fonts_tile_end();
}

static void w_sensor_event_cb(lv_event_t *event)
{
    if (event == NULL) {
        return;
    }

    w_sensor_ctx_t *ctx = (w_sensor_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DELETE) {
        if (ctx->age_timer != NULL) {
            lv_timer_del(ctx->age_timer);
            ctx->age_timer = NULL;
        }
        free(ctx);
    } else if (code == LV_EVENT_SIZE_CHANGED) {
        sensor_apply_layout(ctx);
    } else if (code == LV_EVENT_CLICKED && ctx->history_popup) {
        ui_history_popup_open(ctx->entity_id, lv_label_get_text(ctx->title_label), ctx->value_text, ctx->numeric, "");
    }
}

static esp_err_t sensor_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance, lv_obj_t *font_root)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, def->x, def->y);
    lv_obj_set_size(card, def->w, def->h);
    lv_obj_set_scrollable(card, false);
    theme_default_style_card(card);
    lv_obj_set_style_pad_left(card, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(card, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_top(card, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(card, 10, LV_PART_MAIN);

    lv_obj_t *title = ui_widget_name_create(card, !def->hide_name);
    lv_label_set_text(title, def->title[0] ? def->title : def->id);
    lv_obj_set_style_text_color(title, theme_default_color_text_muted(), LV_PART_MAIN);
    ui_text_set(title, UI_TEXT_LABEL, false);
    /* One line with an ellipsis: a wrapped name ran into the value on a narrow tile. */
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_height(title, lv_font_get_line_height(lv_obj_get_style_text_font(title, LV_PART_MAIN)));

    lv_obj_t *value = lv_label_create(card);
    lv_label_set_text(value, "--");
    ui_text_record(value, UI_TEXT_KEY, true);
    lv_obj_set_style_text_color(value, theme_default_color_text_primary(), LV_PART_MAIN);
    lv_obj_set_style_text_font(value, SENSOR_VALUE_FONT_MEDIUM, LV_PART_MAIN);

    lv_obj_t *age = lv_label_create(card);
    ui_text_record(age, UI_TEXT_META, false);
    lv_label_set_text(age, ui_i18n_get("sensor.age.just_now", "just now"));
    lv_obj_set_style_text_color(age, theme_default_color_text_muted(), LV_PART_MAIN);
    lv_obj_set_style_text_font(age, SENSOR_META_FONT, LV_PART_MAIN);
    lv_obj_set_hidden(age, true);

    w_sensor_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(w_sensor_ctx_t));
    if (ctx == NULL) {
        lv_obj_del(card);
        return ESP_ERR_NO_MEM;
    }

    ctx->card = card;
    ctx->font_root = font_root != NULL ? font_root : card;
    ctx->list_row = font_root != NULL;
    if (ctx->list_row) {
        lv_obj_set_width(card, lv_pct(100));
        lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(card, 0, LV_PART_MAIN);
        ui_text_record(value, UI_TEXT_LABEL, true);
    }
    strlcpy(ctx->entity_id, def->entity_id, sizeof(ctx->entity_id));
    ctx->history_popup = def->history_popup;
    lv_obj_set_clickable(card, ctx->history_popup);
    ctx->title_label = title;
    ctx->value_label = value;
    ctx->age_label = age;
    ctx->last_update_ms = 0;
    ctx->has_timestamp = false;
    ctx->unavailable = false;
    ctx->show_label = !def->hide_name;
    ctx->show_age = def->sensor_show_age;
    strlcpy(ctx->format, def->sensor_format, sizeof(ctx->format));
    ctx->time_format_set = def->sensor_format[0] != '\0' && ha_ts_style_from_string(def->sensor_format, &ctx->time_style);
    if (!ctx->time_format_set) ctx->time_style = HA_TS_DATETIME;
    if (!ha_sv_unit_from_string(def->sensor_unit, &ctx->unit_mode)) ctx->unit_mode = HA_SV_UNIT_DEFAULT;
    ctx->age_timer = lv_timer_create(sensor_age_timer_cb, 30000, ctx);

    lv_obj_add_event_cb(card, w_sensor_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, w_sensor_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    if (ctx->history_popup) lv_obj_add_event_cb(card, w_sensor_event_cb, LV_EVENT_CLICKED, ctx);

    sensor_update_age_label(ctx);
    sensor_apply_layout(ctx);

    out_instance->obj = card;
    out_instance->ctx = ctx;
    return ESP_OK;
}

esp_err_t w_sensor_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out)
{
    if (def != NULL && strcmp(def->display_mode, "list") == 0) return w_sensor_list_create(def, parent, out);
    return sensor_create(def, parent, out, NULL);
}

esp_err_t w_sensor_row_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out, lv_obj_t *font_root)
{
    return sensor_create(def, parent, out, font_root);
}

void w_sensor_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->obj == NULL || state == NULL) {
        return;
    }

    if (strcmp(instance->display_mode, "list") == 0) { w_sensor_list_apply_state(instance, state); return; }

    w_sensor_ctx_t *ctx = (w_sensor_ctx_t *)instance->ctx;
    if (ctx == NULL) {
        return;
    }

    if (strcmp(ctx->entity_id, state->entity_id) != 0) return;
    if (ctx->list_row) {
        char name[APP_MAX_NAME_LEN];
        if (ha_model_get_entity_name(state->entity_id, name, sizeof(name))) lv_label_set_text(ctx->title_label, name);
    }
    if (sensor_state_is_unavailable(state->state)) {
        sensor_apply_unavailable(ctx);
        return;
    }
    char *numeric_end = NULL;
    double numeric_value = strtod(state->state, &numeric_end);
    ctx->numeric = numeric_end != state->state && *numeric_end == '\0' && isfinite(numeric_value);

    char value_text[96] = {0};
    const char *unit = "";
    const char *device_class = "";
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

    /* A timestamp sensor's state is an ISO instant in UTC: shown in local time instead of the raw string. */
    const bool no_class = device_class[0] == '\0';
    ctx->timestamp = (strcmp(device_class, "timestamp") == 0 || (no_class && ctx->time_format_set)) &&
                     ha_timestamp_parse(state->state, &ctx->timestamp_s);
    ctx->date = !ctx->timestamp && strcmp(device_class, "date") == 0 &&
                ha_sv_date_parse(state->state, &ctx->date_y, &ctx->date_m, &ctx->date_d);
    sensor_value_text(ctx, state, device_class, unit, value_text, sizeof(value_text));

    if (attrs != NULL) {
        cJSON_Delete(attrs);
    }

    ctx->unavailable = false;
    ctx->last_update_ms = state->last_changed_unix_ms;
    ctx->has_timestamp = ctx->last_update_ms > 0;

    sensor_set_value_text(ctx, value_text);
    sensor_update_age_label(ctx);
    sensor_apply_layout(ctx);
}

void w_sensor_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->obj == NULL) {
        return;
    }

    if (strcmp(instance->display_mode, "list") == 0) { w_sensor_list_mark_unavailable(instance); return; }

    w_sensor_ctx_t *ctx = (w_sensor_ctx_t *)instance->ctx;
    if (ctx == NULL) {
        return;
    }

    sensor_apply_unavailable(ctx);
}
