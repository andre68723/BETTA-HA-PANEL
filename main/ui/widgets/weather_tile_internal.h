/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include "ui/ui_widget_factory.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
#include "esp_memory_utils.h"
#endif

#include "ha/ha_client.h"
#include "ha/ha_weather_hourly.h"
#include "ui/assets/weather/weather_icons.h"
#include "ui/theme/ui_typography.h"
#include "ui/theme/ui_day_grid.h"
#include "ui/ui_popup.h"
#include "ui/fonts/mdi_font_registry.h"
#include "ui/ui_i18n.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/widgets/weather_value_parse.h"
#include "ui/widgets/weather_condition_icon.h"
#include "ui/widgets/weather_now_view.h"

#ifndef APP_UI_WEATHER_ICON_DEBUG
#define APP_UI_WEATHER_ICON_DEBUG 0
#endif

#ifndef APP_UI_WEATHER_LOTTIE_ASSETS
#define APP_UI_WEATHER_LOTTIE_ASSETS 0
#endif

#if LV_USE_LOTTIE && APP_UI_WEATHER_LOTTIE_ASSETS
#define APP_UI_WEATHER_LOTTIE_ENABLED 1
#else
#define APP_UI_WEATHER_LOTTIE_ENABLED 0
#endif

#define WEATHER_TEMP_FONT ui_font_role(UI_TEXT_KEY)
#define WEATHER_TEMP_FONT_LARGE ui_font_role(UI_TEXT_KEY)

/* Translated weather labels and readings use the shared accented text subset. */
#define WEATHER_CONDITION_FONT ui_font_role(UI_TEXT_META)
#define WEATHER_META_FONT_LARGE ui_font_role(UI_TEXT_META)
#define WEATHER_3DAY_TEMP_FONT ui_font_role(UI_TEXT_VALUE)
#define WEATHER_3DAY_META_FONT ui_font_role(UI_TEXT_META)

/* Forecast row labels (day / low / high).  A bit larger than the muted
 * secondary meta text so the forecast values stay readable on the tile. */
#define WEATHER_3DAY_ROW_FONT ui_font_role(UI_TEXT_META)

/* Meta line below the big current temperature (condition + humidity).
 * One step up from the forecast-row font to keep the visual hierarchy
 * big temp -> meta -> forecast rows. */
#define WEATHER_3DAY_SUBMETA_FONT ui_font_role(UI_TEXT_META)

#define WEATHER_3DAY_MAX_FORECAST 6
#define WEATHER_3DAY_ROWS (1 + WEATHER_3DAY_MAX_FORECAST)
#define WEATHER_3DAY_ROW_HEIGHT 44
#define WEATHER_3DAY_ROW_GAP 4
#define WEATHER_3DAY_ROWS_TOP 150
#define WEATHER_3DAY_ROWS_BOTTOM_PAD 12
#define WEATHER_3DAY_TRACK_BG 0x4A5D6D
#define WEATHER_3DAY_FILL_COLD 0x4DA6FF
#define WEATHER_3DAY_FILL_MIXED 0x9FCF73
#define WEATHER_3DAY_FILL_WARM 0xF2D34D
#define WEATHER_3DAY_FILL_HOT 0xE8783A
#define WEATHER_3DAY_MARKER_RING 0x2E3C49

#define WEATHER_3DAY_SCALE_MIN_C 0.0f
#define WEATHER_3DAY_SCALE_MIX_C 16.0f
#define WEATHER_3DAY_SCALE_WARM_C 22.0f
#define WEATHER_3DAY_SCALE_MAX_C 30.0f

#define WEATHER_LOTTIE_MAX_SIZE 160

typedef struct {
    bool valid;
    char day[12];
    bool has_high;
    bool has_low;
    float high_temp;
    float low_temp;
    char condition_key[32];
    char condition[24];
    char date[11];     /* YYYY-MM-DD, for the detail popup */
    float precip_mm;   /* -1 = unknown */
    int precip_prob;   /* -1 = unknown */
} weather_forecast_t;

typedef struct {
    bool has_temp;
    float temp;
    int humidity;
    char unit[12];
    char condition_key[32];
    char condition[32];
    bool today_has_high;
    bool today_has_low;
    float today_high_temp;
    float today_low_temp;
    char today_condition_key[32];
    char today_date[11];
    /* Current conditions, shown in the detail popup (P4 boards only). */
    bool has_pressure;
    float pressure;
    char pressure_unit[8];
    bool has_wind;
    float wind_speed;
    int wind_bearing; /* degrees, -1 = unknown */
    char wind_unit[8];
    bool has_visibility;
    float visibility;
    char visibility_unit[6];
    float today_precip_mm;
    int today_precip_prob;
    char precip_unit[8];
    weather_forecast_t forecast[WEATHER_3DAY_MAX_FORECAST];
} weather_values_t;

typedef struct {
    lv_obj_t *container;
    lv_obj_t *day_label;
    lv_obj_t *icon_label;
    lv_obj_t *low_label;
    lv_obj_t *bar_track;
    lv_obj_t *bar_fill;
    lv_obj_t *bar_marker;
    lv_obj_t *high_label;
} weather_3day_row_widgets_t;

typedef struct {
    bool valid;
    bool has_low;
    bool has_high;
    bool has_point;
    float low_temp;
    float high_temp;
    float point_temp;
    char day[12];
    char condition_key[32];
    char condition_text[32];
    char date[11];
    float precip_mm;
    int precip_prob;
} weather_3day_row_t;

#define WEATHER_COLS_MAX 7
#define WEATHER_COLS_LEGACY_MAX 6 /* layouts saved before "Visible days" keep their six columns */

/* One column of the horizontal forecast view. */
typedef struct {
    lv_obj_t *box;
    lv_obj_t *day;
    lv_obj_t *icon;
    lv_obj_t *hi;
    lv_obj_t *lo;
    lv_obj_t *rain;
} weather_col_widgets_t;

typedef struct weather_tile_ctx {
    weather_now_view_t now_view;
    lv_obj_t *card;
    bool available;
    bool show_hourly;
    lv_timer_t *hourly_timer;
    uint32_t hourly_token;
    int64_t hourly_attempt_ms, hourly_ok_ms, hourly_retry_after_ms, hourly_view_hour;
    bool hourly_inflight;
    ha_weather_hourly_t hourly;
    struct weather_tile_ctx *hourly_next;
    lv_obj_t *hourly_strip, *hourly_status, *hourly_icon;
    struct { lv_obj_t *box, *time, *icon, *temp; } hours[HA_WEATHER_HOURS_MAX];
    bool show_forecast;
    bool hide_name;
    bool show_precip;
    int forecast_days;
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    lv_obj_t *popup;               /* day detail popup on the top layer */
    lv_timer_t *popup_close_timer; /* auto-closes the popup after WEATHER_POPUP_AUTO_CLOSE_MS */
    /* Optional DWD weather report (secondary entity): summary and one text per weekday. */
    char report_entity_id[APP_MAX_ENTITY_ID_LEN];
    char report_summary[200];
    char report_day[7][224];       /* index = weekday, 0 = Sunday */
    lv_obj_t *report_label;
    lv_coord_t report_x;
    lv_coord_t report_y;
    lv_coord_t report_w;
    lv_coord_t report_h;
    weather_values_t last_values;  /* what the columns were rendered from */
    /* Horizontal view (colored icons, one column per day); built on first use. */
    bool cols_built;
    lv_obj_t *col_main_icon;
    weather_col_widgets_t cols[WEATHER_COLS_MAX];
    lv_obj_t *condition_label;
    lv_obj_t *temp_label;
    lv_obj_t *meta_label;
    weather_3day_row_widgets_t rows[WEATHER_3DAY_ROWS];
    weather_3day_row_t render_rows[WEATHER_3DAY_ROWS];
    lv_obj_t *lottie_icon;
    void *lottie_buf;
    size_t lottie_buf_size;
    lv_coord_t lottie_size;
    const void *last_lottie_src;
    size_t last_lottie_src_size;
    lv_coord_t configured_min_dim;
    uint32_t last_icon_cp;
    const lv_font_t *last_icon_font;
    char last_condition_key[32];
    char last_condition_text[32];
} w_weather_tile_ctx_t;

#ifndef APP_UI_WEATHER_ICON_ALLOW_72
#define APP_UI_WEATHER_ICON_ALLOW_72 1
#endif


/* ---- Shared between the weather tile modules (see w_weather_tile.c) ---- */

/* main */
lv_coord_t weather_condition_text_width(lv_obj_t *card);
lv_coord_t weather_card_min_dim(lv_obj_t *card);
lv_coord_t weather_effective_min_dim(lv_obj_t *card, const w_weather_tile_ctx_t *ctx);
const lv_font_t *weather_pick_temp_font(lv_obj_t *card);
const lv_font_t *weather_pick_meta_font(lv_obj_t *card);

/* lottie */
lv_coord_t weather_cap_lottie_size(lv_coord_t size);
bool weather_has_lottie_for_key(const char *key);
lv_coord_t weather_pick_lottie_size(lv_obj_t *card, const w_weather_tile_ctx_t *ctx);
void weather_free_lottie(w_weather_tile_ctx_t *ctx);
void weather_hide_lottie(w_weather_tile_ctx_t *ctx);
bool weather_show_lottie(lv_obj_t *card, w_weather_tile_ctx_t *ctx, const char *condition_key, lv_coord_t icon_x,
    lv_coord_t icon_y, lv_coord_t requested_size);

/* text */
void weather_copy_text(char *dst, size_t dst_size, const char *src);
void weather_normalize_condition_key(const char *src, char *dst, size_t dst_size);
bool weather_icon_utf8_from_codepoint(uint32_t codepoint, char out[5]);
bool weather_has_token(const char *key, const char *token);
bool weather_has_alpha(const char *text);
const char *weather_effective_condition_key(const w_weather_tile_ctx_t *ctx, const weather_values_t *values);
void weather_humanize_condition(const char *src, char *dst, size_t dst_size);
void weather_update_icon_cache_from_state(w_weather_tile_ctx_t *ctx, const char *state_text);
void weather_day_from_datetime(const char *datetime, char *out, size_t out_size);
bool weather_datetime_is_today(const char *datetime);
bool weather_datetime_is_before_today(const char *datetime);

/* icon */
const lv_font_t *weather_find_icon_font_for_cp(uint32_t codepoint);
bool weather_font_has_codepoint(const lv_font_t *font, uint32_t codepoint);
const lv_font_t *weather_pick_render_icon_font(
    lv_obj_t *card, const w_weather_tile_ctx_t *ctx, uint32_t codepoint, const lv_font_t *preferred);

/* values */
void weather_extract_values(const ha_state_t *state, bool want_forecast, weather_values_t *out);

/* forecast */
lv_color_t weather_3day_temp_color(float value, const char *unit);
float weather_normalize_temp(float value, float range_min, float range_max);
void weather_format_temp(char *dst, size_t dst_size, float temp, const char *unit);
void weather_set_row_icon(lv_obj_t *label, const char *condition_key);
void weather_build_3day_rows(const weather_values_t *values, weather_3day_row_t rows[WEATHER_3DAY_ROWS]);
void weather_compute_3day_range(
    const weather_3day_row_t rows[WEATHER_3DAY_ROWS], float *out_min, float *out_max);

/* layout */
void weather_set_3day_rows_layout(lv_obj_t *card, w_weather_tile_ctx_t *ctx, lv_coord_t requested_rows_top);
void weather_set_3day_row_values(weather_3day_row_widgets_t *widgets, const weather_3day_row_t *row,
    const char *unit, float range_min, float range_max);
lv_coord_t weather_content_top_y(const w_weather_tile_ctx_t *ctx);
lv_coord_t weather_content_bottom_y(lv_obj_t *card, const w_weather_tile_ctx_t *ctx);
void weather_compute_main_layout(lv_obj_t *card, w_weather_tile_ctx_t *ctx, bool icon_mode,
    const lv_font_t *icon_font, const lv_font_t *temp_font, const lv_font_t *meta_font,
    lv_coord_t condition_height_override, lv_coord_t *out_condition_y, lv_coord_t *out_temp_y, lv_coord_t *out_meta_y);
lv_coord_t weather_pick_lottie_size_main_adaptive(
    lv_obj_t *card, w_weather_tile_ctx_t *ctx, const lv_font_t *temp_font, const lv_font_t *meta_font);
bool weather_is_night_now(void);
bool weather_columns_wanted(lv_obj_t *card);
void weather_columns_build(lv_obj_t *card, w_weather_tile_ctx_t *ctx);
void weather_columns_hide(w_weather_tile_ctx_t *ctx);
void weather_set_image_if_changed(lv_obj_t *img, const lv_image_dsc_t *dsc);
int weather_set_col_rain(lv_obj_t *label, const weather_3day_row_t *row, const char *unit, int max_lines, lv_coord_t width);
void weather_set_col_temp(lv_obj_t *label, bool has, float value);

/* popup */
void weather_report_update_label(w_weather_tile_ctx_t *ctx);
void weather_report_parse_state(w_weather_tile_ctx_t *ctx, const ha_state_t *state);
void weather_report_clicked_cb(lv_event_t *event);
void weather_col_clicked_cb(lv_event_t *event);

/* render */
void weather_render(lv_obj_t *card, w_weather_tile_ctx_t *ctx, const weather_values_t *values, bool available);

/* Current-weather interactions and optional hourly strip. */
void weather_now_clicked_cb(lv_event_t *event);
void weather_hourly_start(w_weather_tile_ctx_t *ctx);
void weather_hourly_stop(w_weather_tile_ctx_t *ctx);
bool weather_hourly_render(lv_obj_t *card, w_weather_tile_ctx_t *ctx, const weather_values_t *values);
#ifdef BETTA_SIMULATOR
void weather_hourly_simulator_reply(uint32_t token, const cJSON *result);
#endif
