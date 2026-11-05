/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/ui_widget_factory.h"
#include "ui/ui_memory.h"
#include "ui/ui_popup.h"
#include "ui/ui_history_popup.h"
#include "ui/ui_i18n.h"
#include "ui/fonts/app_symbols.h"
#include "ui/theme/ui_style.h"
#include "ui/theme/theme_default.h"
#include "ha/ha_sensor_value.h"
#include "esp_timer.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AIR_MAX_COLUMNS 4

typedef struct {
    char id[AIR_ROLE_COUNT][APP_MAX_ENTITY_ID_LEN];
    char text[AIR_ROLE_COUNT][96], unit[AIR_ROLE_COUNT][32];
    bool available[AIR_ROLE_COUNT], weather, popup_dirty;
    air_input_t input[AIR_ROLE_COUNT];
    air_config_t config;
    air_memory_t memory;
    air_result_t result;
    lv_obj_t *card, *title, *title_icon, *chip_status, *status, *chip_advice, *advice, *advice_more, *extra;
    lv_obj_t *col_label[AIR_MAX_COLUMNS], *col_value[AIR_MAX_COLUMNS];
    lv_obj_t *overlay, *body;
    int col_role[AIR_MAX_COLUMNS]; /* the reading in each tile column, -1 when the column is empty */
    lv_timer_t *timer;
} room_air_t;

static const char *const role_keys[AIR_ROLE_COUNT] = {
    "air.humidity", "air.temperature", "air.co2", "air.pm25", "air.voc",
    "air.out_temperature", "air.out_humidity", "air.window", "air.rating"
};
static const char *const role_names[AIR_ROLE_COUNT] = {
    "Humidity", "Temperature", "CO₂", "PM2.5", "VOC", "Outdoor temperature", "Outdoor humidity", "Window", "Device rating"
};
static const char *role_name(int role) { return ui_i18n_get(role_keys[role], role_names[role]); }
static const char *const short_keys[AIR_ROLE_COUNT] = {
    "air.humidity_short", "air.temperature_short", NULL, NULL, NULL, "air.out_temperature_short", NULL, NULL, NULL
};
static const char *const short_names[AIR_ROLE_COUNT] = {"Humidity", "Temp.", NULL, NULL, NULL, "Out. temp.", NULL, NULL, NULL};

/* The caption under a reading: the long name while it fits the column, else its short form. */
static const char *column_name(int role, int width, int height)
{
    const char *full = role_name(role);
    if (short_keys[role] == NULL) return full;
    const lv_font_t *font = ui_text_fit(full, ui_font_role(UI_TEXT_META), width, height);
    lv_point_t size;
    lv_text_get_size(&size, full, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x <= width ? full : ui_i18n_get(short_keys[role], short_names[role]);
}
static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static const char *band_name(air_band_t band)
{
    switch (band) {
    case AIR_GOOD: return ui_i18n_get("air.good", "Good");
    case AIR_FAIR: return ui_i18n_get("air.fair", "Fair");
    case AIR_POOR: return ui_i18n_get("air.poor", "Poor");
    default: return ui_i18n_get("air.not_evaluated", "Not evaluated");
    }
}

static uint32_t band_color(air_band_t band)
{
    return band == AIR_GOOD ? APP_UI_COLOR_OK : band == AIR_FAIR ? 0xD49A28 :
        band == AIR_POOR ? APP_UI_COLOR_ERROR : APP_UI_COLOR_TEXT_MUTED;
}

static const char *reason_text(unsigned reason)
{
    switch (reason) {
    case AIR_DRY: return ui_i18n_get("air.dry", "Humidity low");
    case AIR_HUMID: return ui_i18n_get("air.humid", "Humidity high · check airing");
    case AIR_FRESH_AIR: return ui_i18n_get("air.fresh_air", "Air exchange · CO₂ elevated");
    case AIR_PARTICLES: return ui_i18n_get("air.particles", "Particles elevated · reduce sources / filter; outdoor air quality unknown");
    case AIR_DRYING: return ui_i18n_get("air.drying", "Reference air can help dry the room");
    case AIR_NO_DRYING: return ui_i18n_get("air.no_drying", "Reference air offers insufficient drying benefit");
    case AIR_CONFLICT: return ui_i18n_get("air.conflict", "Air exchange needed; reference air would add moisture");
    default: return "";
    }
}

static void value_text(room_air_t *ctx, int role, char *out, size_t size)
{
    if (!ctx->available[role]) { snprintf(out, size, "—"); return; }
    if (role == AIR_WINDOW) {
        snprintf(out, size, "%s", ui_i18n_get(ctx->input[role].value ? "air.open" : "air.closed",
            ctx->input[role].value ? "Open" : "Closed"));
        return;
    }
    if (role == AIR_RATING && !ctx->unit[role][0]) {
        const char *known[] = {"extremely_poor", "very_poor", "poor", "fair", "good", "moderate"};
        for (unsigned i = 0; i < sizeof(known) / sizeof(known[0]); ++i) {
            if (!strcmp(ctx->text[role], known[i])) {
                char key[64]; snprintf(key, sizeof(key), "air.enum_%s", known[i]);
                snprintf(out, size, "%s", ui_i18n_get(key, known[i])); return;
            }
        }
    }
    char number[96]; snprintf(number, sizeof(number), "%s", ctx->text[role]);
    /* Numbers use a decimal point on every tile, whatever the UI language. Tile display precision: whole ppm, % and ppb; one decimal for temperature and PM2.5, without a trailing ".0". */
    if (ctx->input[role].valid && role != AIR_WINDOW && role != AIR_RATING) {
        char *end = NULL;
        double raw = strtod(ctx->text[role], &end);
        if (end != ctx->text[role] && *end == 0) {
            int decimals = role == AIR_TEMPERATURE || role == AIR_OUT_TEMPERATURE || (role == AIR_PM25 && fabs(raw) < 10) ? 1 : 0;
            snprintf(number, sizeof(number), "%.*f", decimals, raw);
            size_t len = strlen(number);
            if (decimals && len > 2 && !strcmp(number + len - 2, ".0")) number[len - 2] = 0;
        }
    }
    if (!ha_sv_number_text(number, number, !strcmp(ctx->unit[role], "μg/m³") ? "µg/m³" : ctx->unit[role], "", HA_SV_UNIT_DEFAULT,
                           "en", out, size)) snprintf(out, size, "%s", number);
}

/* The Recorder history of one reading; windows and weather references have none worth showing. */
static bool role_has_history(const room_air_t *ctx, int role)
{
    return ctx->input[role].bound && ctx->id[role][0] && strncmp(ctx->id[role], "weather.", 8) != 0;
}

static void open_history(room_air_t *ctx, int role)
{
    static const char *const classes[AIR_ROLE_COUNT] = {"humidity", "temperature", "carbon_dioxide", "pm25",
        "volatile_organic_compounds_parts", "temperature", "humidity", "window", ""};
    char value[128], title[160];
    value_text(ctx, role, value, sizeof(value));
    snprintf(title, sizeof(title), "%s · %s", role_name(role), lv_label_get_text(ctx->title));
    const bool numeric = role != AIR_WINDOW && (role != AIR_RATING || ctx->unit[role][0]);
    ui_history_popup_open(ctx->id[role], title, value, numeric, classes[role]);
}

static void popup_row_cb(lv_event_t *e)
{
    room_air_t *ctx = lv_event_get_user_data(e);
    open_history(ctx, (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)));
}

/* One reading as a row: name on the left, value and level on the right, a chevron where a graph follows. */
static void popup_row(room_air_t *ctx, int role, int width)
{
    char value[160];
    value_text(ctx, role, value, sizeof(value));
    const bool rated = role == AIR_HUMIDITY || role == AIR_CO2 || role == AIR_PM25;
    if (rated && ctx->input[role].valid) {
        snprintf(value + strlen(value), sizeof(value) - strlen(value), " · %s", band_name(ctx->result.band[role]));
    }
    const lv_font_t *font = ui_font_role(UI_TEXT_META);
    const int line_h = lv_font_get_line_height(font);
    const bool graph = role_has_history(ctx, role);
    lv_obj_t *row = lv_obj_create(ctx->body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, width, LV_MAX(line_h + 16, 48));
    lv_obj_set_scrollable(row, false);
    lv_obj_set_style_radius(row, ui_style_radius(10), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, role_name(role));
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_size(name, width * 36 / 100, line_h);
    lv_obj_set_style_text_font(name, font, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), 0);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 12, 0);
    const int chevron_w = graph ? lv_font_get_line_height(APP_FONT_SYMBOL_SMALL) + 8 : 0;
    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, value);
    lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
    lv_obj_set_size(val, width - width * 36 / 100 - 36 - chevron_w, line_h);
    lv_obj_set_style_text_font(val, font, 0);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
    const air_band_t band = rated && ctx->input[role].valid ? ctx->result.band[role] : AIR_NONE;
    lv_obj_set_style_text_color(val, lv_color_hex(band == AIR_FAIR || band == AIR_POOR ? band_color(band) : APP_UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, -12 - chevron_w, 0);
    if (graph) {
        lv_obj_t *arrow = lv_label_create(row);
        lv_label_set_text(arrow, APP_SYMBOL_CHEVRON_RIGHT);
        lv_obj_set_style_text_font(arrow, APP_FONT_SYMBOL_SMALL, 0);
        lv_obj_set_style_text_color(arrow, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), 0);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -10, 0);
        lv_obj_set_clickable(row, true);
        lv_obj_set_user_data(row, (void *)(intptr_t)role);
        lv_obj_add_event_cb(row, popup_row_cb, LV_EVENT_CLICKED, ctx);
    }
}

static void popup_refresh(room_air_t *ctx)
{
    if (!ctx->body || !ctx->popup_dirty) return;
    ctx->popup_dirty = false;
    int scroll_y = lv_obj_get_scroll_y(ctx->body);
    lv_obj_clean(ctx->body);
    lv_obj_update_layout(ctx->body);
    const int width = lv_obj_get_width(ctx->body) - 4;
    char line[256];
    /* Verdict and advice first, then every bound reading; a tap on a reading opens its graph. */
    snprintf(line, sizeof(line), "%s%s%s", band_name(ctx->result.overall), ctx->result.partial ? " · " : "",
             ctx->result.partial ? ui_i18n_get("air.partial", "Partial data") : "");
    lv_obj_t *label = ui_popup_label_create(ctx->body, line, UI_POPUP_LABEL_VALUE);
    lv_obj_set_style_text_color(label, lv_color_hex(band_color(ctx->result.overall)), 0);
    lv_obj_set_width(label, LV_PCT(100));
    for (unsigned bit = 1; bit <= AIR_CONFLICT; bit <<= 1) {
        if (!(ctx->result.reasons & bit)) continue;
        label = ui_popup_label_create(ctx->body, reason_text(bit), UI_POPUP_LABEL_BODY);
        lv_obj_set_width(label, LV_PCT(100));
    }
    if (!ctx->result.reasons && ctx->result.overall != AIR_GOOD) {
        label = ui_popup_label_create(ctx->body, ui_i18n_get("air.no_advice", "Insufficient evaluated data for advice"), UI_POPUP_LABEL_BODY);
        lv_obj_set_width(label, LV_PCT(100));
    }
    static const int order[] = {AIR_CO2, AIR_HUMIDITY, AIR_PM25, AIR_TEMPERATURE, AIR_VOC, AIR_OUT_TEMPERATURE,
                                AIR_OUT_HUMIDITY, AIR_WINDOW, AIR_RATING};
    for (unsigned i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
        if (ctx->input[order[i]].bound) popup_row(ctx, order[i], width);
    }
    if (ctx->result.comparison) {
        snprintf(line, sizeof(line), "%s: %.1f / %.1f g/m³ · Diff %.1f g/m³", ui_i18n_get("air.absolute", "Absolute humidity: indoor / outdoor"),
            ctx->result.indoor_ah, ctx->result.outdoor_ah, ctx->result.difference);
        label = ui_popup_label_create(ctx->body, line, UI_POPUP_LABEL_BODY);
        lv_obj_set_width(label, LV_PCT(100));
    }
    if (ctx->weather) {
        label = ui_popup_label_create(ctx->body, ui_i18n_get("air.weather_reference",
            "Weather provider reference; not a local outdoor measurement. Sample age unknown."), UI_POPUP_LABEL_HINT);
        lv_obj_set_width(label, LV_PCT(100));
    }
    lv_obj_update_layout(ctx->body);
    lv_obj_scroll_to_y(ctx->body, scroll_y, LV_ANIM_OFF);
}

static void popup_deleted(lv_event_t *e)
{
    room_air_t *ctx = lv_event_get_user_data(e);
    ctx->overlay = ctx->body = NULL;
}

static void popup_close(lv_event_t *e)
{
    room_air_t *ctx = lv_event_get_user_data(e);
    if (ctx->overlay) lv_obj_delete(ctx->overlay);
}

static void popup_open(room_air_t *ctx)
{
    if (ctx->overlay) return;
    int sw = lv_obj_get_width(lv_screen_active()), sh = lv_obj_get_height(lv_screen_active());
    ctx->overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(ctx->overlay);
    lv_obj_set_size(ctx->overlay, sw, sh);
    lv_obj_set_style_bg_color(ctx->overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ctx->overlay, LV_OPA_60, 0);
    lv_obj_add_event_cb(ctx->overlay, popup_deleted, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(ctx->overlay, popup_close, LV_EVENT_CLICKED, ctx);
    lv_obj_t *card = ui_popup_card_create(ctx->overlay);
    int w = sw > 720 ? 680 : sw - 32, h = sh > 640 ? 600 : sh - 32;
    lv_obj_set_size(card, w, h); lv_obj_center(card);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_set_scrollable(card, false);
    lv_obj_t *title = ui_popup_label_create(card, lv_label_get_text(ctx->title), UI_POPUP_LABEL_TITLE);
    lv_obj_set_width(title, w - 110);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_height(title, lv_font_get_line_height(lv_obj_get_style_text_font(title, 0)));
    lv_obj_t *close = ui_popup_close_button(card, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), popup_close, ctx);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, 0);
    ctx->body = lv_obj_create(card);
    lv_obj_remove_style_all(ctx->body);
    lv_obj_set_pos(ctx->body, 0, 62);
    lv_obj_set_size(ctx->body, w - 40, h - 100);
    lv_obj_set_flex_flow(ctx->body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->body, 14, 0);
    lv_obj_set_scroll_dir(ctx->body, LV_DIR_VER);
    ctx->popup_dirty = true;
    popup_refresh(ctx);
}

static int meta_height(void) { return lv_font_get_line_height(ui_font_role(UI_TEXT_META)); }

static void place(lv_obj_t *label, ui_text_role_t role, bool essential, int x, int y, int width, const char *text, lv_text_align_t align)
{
    lv_label_set_text(label, text);
    ui_text_set(label, role, essential);
    int h = lv_font_get_line_height(ui_font_role(role));
    lv_obj_set_size(label, width, h);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_style_text_font(label, ui_text_fit(text, ui_font_role(role), width, h), 0);
    lv_obj_set_hidden(label, false);
}

/* The status pill of the Room widget: an outline in Flat and Matte, a soft tint in Glow and Glass. */
static void style_chip(lv_obj_t *chip, uint32_t color)
{
    lv_obj_set_style_border_color(chip, lv_color_hex(color), 0);
    if (ui_style_has_depth()) {
        lv_obj_set_style_bg_color(chip, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_20, 0);
        lv_obj_set_style_border_width(chip, 0, 0);
    } else {
        lv_obj_set_style_bg_opa(chip, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(chip, 1, 0);
    }
}

static void render(room_air_t *ctx)
{
    air_result_t result = air_evaluate(&ctx->config, ctx->input, &ctx->memory, now_ms());
    if (result.overall != ctx->result.overall || result.reasons != ctx->result.reasons) ctx->popup_dirty = true;
    ctx->result = result;
    const int w = lv_obj_get_width(ctx->card) - 24, h = lv_obj_get_height(ctx->card) - 24;
    if (w <= 0 || h <= 0) return;
    const uint32_t color = band_color(ctx->result.overall);
    char text[384], value[128];
    const int meta_h = meta_height(), label_h = lv_font_get_line_height(ui_font_role(UI_TEXT_LABEL));
    const int value_h = lv_font_get_line_height(ui_font_role(UI_TEXT_VALUE));

    /* Header: title (and optional icon) on the left, the quality chip on the right. Colour lives only in the
     * chips and in the readings that cause it. */
    const int chip_h = meta_h + 12, pad = 12, header_h = LV_MAX(label_h, chip_h);
    snprintf(text, sizeof(text), "%s%s", band_name(ctx->result.overall), ctx->result.partial ? " *" : "");
    lv_point_t extent;
    lv_text_get_size(&extent, text, ui_font_role(UI_TEXT_META), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const int q_w = LV_MIN(extent.x + 2 * pad, w * 45 / 100), q_x = w - q_w;
    lv_obj_set_pos(ctx->chip_status, q_x, 0);
    lv_obj_set_size(ctx->chip_status, q_w, chip_h);
    style_chip(ctx->chip_status, color);
    place(ctx->status, UI_TEXT_META, true, q_x, (chip_h - meta_h) / 2, q_w, text, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(ctx->status, lv_color_hex(color), 0);
    const int top = header_h + 6;
    if (ui_widget_name_enabled(ctx->title)) {
        const int icon_w = ctx->title_icon ? lv_font_get_line_height(APP_FONT_SYMBOL_SMALL) + 6 : 0;
        const int title_y = (header_h - label_h) / 2;
        ui_text_set(ctx->title, UI_TEXT_LABEL, false);
        lv_obj_set_size(ctx->title, LV_MAX(q_x - 8 - icon_w, 0), label_h);
        lv_obj_set_pos(ctx->title, icon_w, title_y);
        if (ctx->title_icon) {
            lv_obj_set_pos(ctx->title_icon, 0, title_y + (label_h - lv_font_get_line_height(APP_FONT_SYMBOL_SMALL)) / 2);
            lv_obj_set_hidden(ctx->title_icon, false);
        }
    } else if (ctx->title_icon) lv_obj_set_hidden(ctx->title_icon, true);

    /* Bottom: the advice chip, only when there is something to do (a Good room shows just the status chip). */
    unsigned reasons = ctx->result.reasons;
    const char *advice = reasons & AIR_CONFLICT ? ui_i18n_get("air.conflict_short", "Air exchange · moisture conflict") :
        reasons & AIR_FRESH_AIR ? ui_i18n_get("air.co2_short", "Ventilate · CO₂ elevated") :
        reasons & AIR_HUMID ? ui_i18n_get("air.humid_short", "Humidity high") :
        reasons & AIR_DRY ? reason_text(AIR_DRY) : reasons & AIR_PARTICLES ? ui_i18n_get("air.pm_short", "Particles elevated") :
        ui_i18n_get("air.partial", "Partial data");
    const bool has_advice = reasons != 0 || ctx->result.overall != AIR_GOOD;
    const bool more = ctx->result.partial || (reasons && (reasons & (reasons - 1)));
    const int chip_y = h - chip_h;
    const bool show_advice = has_advice && w >= 70;
    lv_obj_set_hidden(ctx->chip_advice, !show_advice);
    lv_obj_set_hidden(ctx->advice, !show_advice);
    lv_obj_set_hidden(ctx->advice_more, !(show_advice && more));
    if (show_advice) {
        /* A chevron marks further reasons or partial data, shown in the details. */
        const int more_w = more ? lv_font_get_line_height(APP_FONT_SYMBOL_SMALL) + 4 : 0;
        snprintf(text, sizeof(text), "%s", advice);
        lv_obj_set_pos(ctx->chip_advice, 0, chip_y);
        lv_obj_set_size(ctx->chip_advice, w, chip_h);
        style_chip(ctx->chip_advice, color);
        place(ctx->advice, UI_TEXT_META, false, 6, chip_y + (chip_h - meta_h) / 2, w - 12 - more_w, text, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_style_text_color(ctx->advice, lv_color_hex(color), 0);
        if (more) {
            lv_obj_set_pos(ctx->advice_more, w - 6 - more_w + 2, chip_y + (chip_h - lv_font_get_line_height(APP_FONT_SYMBOL_SMALL)) / 2);
            lv_obj_set_style_text_color(ctx->advice_more, lv_color_hex(color), 0);
        }
    }

    /* Middle: up to four readings as value over label, chosen by priority and shown in a fixed order. */
    static const int priority[] = {AIR_HUMIDITY, AIR_CO2, AIR_PM25, AIR_TEMPERATURE, AIR_VOC};
    static const int order[] = {AIR_CO2, AIR_HUMIDITY, AIR_PM25, AIR_TEMPERATURE, AIR_VOC};
    const int max_cols = w >= 440 ? AIR_MAX_COLUMNS : 3;
    bool pick[AIR_ROLE_COUNT] = {0};
    int cols = 0;
    for (unsigned i = 0; i < sizeof(priority) / sizeof(priority[0]) && cols < max_cols; ++i)
        if (ctx->input[priority[i]].bound) { pick[priority[i]] = true; ++cols; }
    const int zone_top = top, zone_h = (show_advice ? chip_y - 6 : h) - zone_top;
    const int block_h = value_h + meta_h + 2;
    /* A reading is quantity and unit together: with too little width drop the lowest priority column rather than cut a value. */
    while (cols > 1) {
        bool fits = true;
        for (unsigned i = 0; i < sizeof(priority) / sizeof(priority[0]); ++i) {
            if (!pick[priority[i]]) continue;
            value_text(ctx, priority[i], value, sizeof(value));
            const lv_coord_t room = w / cols - 4;
            const lv_font_t *font = ui_text_fit(value, ui_font_role(UI_TEXT_VALUE), room, value_h);
            lv_point_t size;
            lv_text_get_size(&size, value, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            if (size.x > room) { fits = false; break; }
        }
        if (fits) break;
        for (int i = (int)(sizeof(priority) / sizeof(priority[0])) - 1; i >= 0; --i)
            if (pick[priority[i]]) { pick[priority[i]] = false; --cols; break; }
    }
    const bool rating = ctx->input[AIR_RATING].bound || ctx->input[AIR_WINDOW].bound;
    const bool with_label = zone_h >= block_h;
    const bool rating_fits = rating && with_label && zone_h >= block_h + meta_h + 6;
    for (int i = 0; i < AIR_MAX_COLUMNS; ++i) {
        lv_obj_set_hidden(ctx->col_label[i], true);
        lv_obj_set_hidden(ctx->col_value[i], true);
        ctx->col_role[i] = -1;
    }
    lv_obj_set_hidden(ctx->extra, true);
    int col = 0;
    const int used_h = (with_label ? block_h : value_h) + (rating_fits ? meta_h + 6 : 0);
    const int by = zone_top + LV_MAX((zone_h - used_h) / 2, 0);
    for (unsigned i = 0; i < sizeof(order) / sizeof(order[0]) && cols > 0 && zone_h >= value_h; ++i) {
        const int role = order[i];
        if (!pick[role]) continue;
        const int cw = w / cols, x = col * cw;
        value_text(ctx, role, value, sizeof(value));
        ctx->col_role[col] = role;
        place(ctx->col_value[col], UI_TEXT_VALUE, true, x, by, cw - 4, value, LV_TEXT_ALIGN_CENTER);
        const air_band_t band = ctx->input[role].valid ? ctx->result.band[role] : AIR_NONE;
        lv_obj_set_style_text_color(ctx->col_value[col], lv_color_hex(band == AIR_FAIR || band == AIR_POOR ? band_color(band) :
            ctx->input[role].valid ? APP_UI_COLOR_TEXT_PRIMARY : APP_UI_COLOR_TEXT_MUTED), 0);
        if (with_label) {
            place(ctx->col_label[col], UI_TEXT_META, false, x, by + value_h + 2, cw - 4, column_name(role, cw - 4, meta_h), LV_TEXT_ALIGN_CENTER);
            lv_obj_set_style_text_color(ctx->col_label[col], lv_color_hex(APP_UI_COLOR_TEXT_MUTED), 0);
        }
        ++col;
    }
    /* One size for all values: the smallest that any column needed. */
    const lv_font_t *common = NULL;
    for (int i = 0; i < col; ++i) {
        const lv_font_t *font = lv_obj_get_style_text_font(ctx->col_value[i], LV_PART_MAIN);
        if (!common || font->line_height < common->line_height) common = font;
    }
    for (int i = 0; common && i < col; ++i) lv_obj_set_style_text_font(ctx->col_value[i], common, 0);
    if (rating_fits) {
        char win[64];
        if (ctx->input[AIR_RATING].bound) {
            value_text(ctx, AIR_RATING, value, sizeof(value));
            snprintf(text, sizeof(text), "%s: %s", role_name(AIR_RATING), value);
            if (ctx->input[AIR_WINDOW].bound) {
                value_text(ctx, AIR_WINDOW, win, sizeof(win));
                snprintf(text + strlen(text), sizeof(text) - strlen(text), " · %s", win);
            }
        } else {
            value_text(ctx, AIR_WINDOW, value, sizeof(value));
            snprintf(text, sizeof(text), "%s: %s", role_name(AIR_WINDOW), value);
        }
        place(ctx->extra, UI_TEXT_META, false, 0, by + block_h + 6, w, text, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_style_text_color(ctx->extra, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), 0);
    }
    popup_refresh(ctx);
}

static void timer_cb(lv_timer_t *timer)
{
    room_air_t *ctx = lv_timer_get_user_data(timer);
    ui_fonts_tile_begin("air_quality_tile", lv_obj_get_width(ctx->card), lv_obj_get_height(ctx->card));
    render(ctx);
    ui_fonts_tile_end();
}

static void event_cb(lv_event_t *e)
{
    room_air_t *ctx = lv_event_get_user_data(e);
    if (lv_event_get_code(e) == LV_EVENT_DELETE) {
        if (ctx->overlay) lv_obj_delete(ctx->overlay);
        lv_timer_delete(ctx->timer); free(ctx);
    } else if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        /* A reading opens its graph; the rest of the tile (status, advice, gaps) opens the details. */
        lv_indev_t *indev = lv_indev_active();
        lv_point_t point;
        if (indev) {
            lv_indev_get_point(indev, &point);
            for (int i = 0; i < AIR_MAX_COLUMNS; ++i) {
                const int role = ctx->col_role[i];
                if (role < 0 || lv_obj_is_hidden(ctx->col_value[i]) || !role_has_history(ctx, role)) continue;
                lv_area_t value_area, label_area;
                lv_obj_get_coords(ctx->col_value[i], &value_area);
                lv_obj_get_coords(ctx->col_label[i], &label_area);
                lv_area_t cell = {value_area.x1, value_area.y1, value_area.x2, lv_obj_is_hidden(ctx->col_label[i]) ? value_area.y2 : label_area.y2};
                lv_area_increase(&cell, 4, 4);
                if (point.x >= cell.x1 && point.x <= cell.x2 && point.y >= cell.y1 && point.y <= cell.y2) { open_history(ctx, role); return; }
            }
        }
        popup_open(ctx);
    }
}

esp_err_t w_room_air_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out)
{
    room_air_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx));
    if (!ctx) return ESP_ERR_NO_MEM;
    ctx->config = air_config_valid(&def->air_config) ? def->air_config : air_config_default();
    air_memory_reset(&ctx->memory);
    for (int i = 0; i < AIR_ROLE_COUNT; ++i) {
        int slot = air_bindings[i].slot;
        const char *id = slot == -1 ? def->entity_id : slot == -2 ? def->secondary_entity_id : slot == -3 ? def->tertiary_entity_id : def->extra_entity_id[slot];
        snprintf(ctx->id[i], sizeof(ctx->id[i]), "%s", id);
        ctx->input[i].bound = *id != 0;
        ctx->weather |= !strncmp(id, "weather.", 8);
    }
    ctx->card = lv_obj_create(parent);
    theme_default_style_card(ctx->card);
    lv_obj_set_pos(ctx->card, def->x, def->y); lv_obj_set_size(ctx->card, def->w, def->h);
    lv_obj_set_style_pad_all(ctx->card, 10, 0);
    lv_obj_set_scrollable(ctx->card, false);
    lv_obj_set_clickable(ctx->card, true);
    ctx->title = ui_widget_name_create(ctx->card, !def->hide_name);
    lv_label_set_text(ctx->title, def->title[0] ? def->title : ui_i18n_get("air.title", "Air quality"));
    if (def->show_icon) {
        ctx->title_icon = lv_label_create(ctx->card);
        lv_label_set_text(ctx->title_icon, APP_SYMBOL_AIR_QUALITY);
        lv_obj_set_style_text_font(ctx->title_icon, APP_FONT_SYMBOL_SMALL, 0);
        lv_obj_set_style_text_color(ctx->title_icon, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), 0);
        lv_obj_set_clickable(ctx->title_icon, false);
    }
    ctx->chip_status = lv_obj_create(ctx->card);
    ctx->chip_advice = lv_obj_create(ctx->card);
    lv_obj_t *chips[] = {ctx->chip_status, ctx->chip_advice};
    for (unsigned i = 0; i < 2; ++i) {
        lv_obj_remove_style_all(chips[i]);
        lv_obj_set_clickable(chips[i], false);
        lv_obj_set_style_radius(chips[i], ui_style_radius(LV_RADIUS_CIRCLE), 0);
        lv_obj_set_scrollbar_mode(chips[i], LV_SCROLLBAR_MODE_OFF);
    }
    ctx->status = lv_label_create(ctx->card);
    ctx->advice = lv_label_create(ctx->card);
    ctx->advice_more = lv_label_create(ctx->card);
    lv_label_set_text(ctx->advice_more, APP_SYMBOL_CHEVRON_RIGHT);
    lv_obj_set_style_text_font(ctx->advice_more, APP_FONT_SYMBOL_SMALL, 0);
    lv_obj_set_clickable(ctx->advice_more, false);
    ctx->extra = lv_label_create(ctx->card);
    lv_obj_t *labels[] = {ctx->title, ctx->status, ctx->advice, ctx->extra};
    for (unsigned i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) {
        lv_label_set_long_mode(labels[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(labels[i], lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), 0);
        lv_obj_set_clickable(labels[i], false);
    }
    for (int i = 0; i < AIR_MAX_COLUMNS; ++i) {
        ctx->col_label[i] = lv_label_create(ctx->card);
        ctx->col_value[i] = lv_label_create(ctx->card);
        lv_obj_t *cells[] = {ctx->col_label[i], ctx->col_value[i]};
        for (unsigned j = 0; j < 2; ++j) {
            lv_label_set_long_mode(cells[j], LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_color(cells[j], lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), 0);
            lv_obj_set_clickable(cells[j], false);
        }
    }
    ctx->timer = lv_timer_create(timer_cb, 1000, ctx);
    lv_obj_add_event_cb(ctx->card, event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(ctx->card, event_cb, LV_EVENT_CLICKED, ctx);
    out->obj = ctx->card; out->ctx = ctx;
    lv_obj_update_layout(ctx->card);
    render(ctx);
    return ESP_OK;
}

void w_room_air_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    room_air_t *ctx = instance->ctx;
    if (!ctx || !state) return;
    cJSON *attrs = cJSON_Parse(state->attributes_json);
    bool available = state->state[0] && strcmp(state->state, "unknown") && strcmp(state->state, "unavailable");
    for (int role = 0; role < AIR_ROLE_COUNT; ++role) {
        if (!ctx->input[role].bound || strcmp(ctx->id[role], state->entity_id)) continue;
        ctx->input[role].valid = false;
        ctx->available[role] = available;
        const char *unit = "", *raw = state->state;
        const cJSON *u = cJSON_GetObjectItemCaseSensitive(attrs, "unit_of_measurement");
        if (cJSON_IsString(u)) unit = u->valuestring;
        char weather_value[48];
        if (!strncmp(state->entity_id, "weather.", 8)) {
            const cJSON *v = cJSON_GetObjectItemCaseSensitive(attrs, role == AIR_OUT_TEMPERATURE ? "temperature" : "humidity");
            const cJSON *wu = cJSON_GetObjectItemCaseSensitive(attrs, "temperature_unit");
            unit = role == AIR_OUT_HUMIDITY ? "%" : cJSON_IsString(wu) ? wu->valuestring : "";
            if (cJSON_IsNumber(v)) { snprintf(weather_value, sizeof(weather_value), "%.12g", v->valuedouble); raw = weather_value; }
            else { raw = ""; ctx->available[role] = false; }
        }
        const cJSON *dc = cJSON_GetObjectItemCaseSensitive(attrs, "device_class");
        const char *expected = role == AIR_HUMIDITY || role == AIR_OUT_HUMIDITY ? "humidity" :
            role == AIR_TEMPERATURE || role == AIR_OUT_TEMPERATURE ? "temperature" :
            role == AIR_CO2 ? "carbon_dioxide" : role == AIR_PM25 ? "pm25" : NULL;
        bool class_ok = !expected || !cJSON_IsString(dc) || !dc->valuestring[0] || !strcmp(dc->valuestring, expected);
        snprintf(ctx->text[role], sizeof(ctx->text[role]), "%s", raw);
        snprintf(ctx->unit[role], sizeof(ctx->unit[role]), "%s", unit);
        if (available) {
            if (role == AIR_WINDOW) {
                ctx->input[role].valid = !strcmp(raw, "on") || !strcmp(raw, "off");
                ctx->input[role].value = !strcmp(raw, "on");
                ctx->available[role] = ctx->input[role].valid;
            } else if (role != AIR_RATING) ctx->input[role].valid = class_ok && air_normalize(role, raw, unit, &ctx->input[role].value);
        }
    }
    cJSON_Delete(attrs);
    ctx->popup_dirty = true;
    render(ctx);
}

void w_room_air_mark_unavailable(ui_widget_instance_t *instance)
{
    room_air_t *ctx = instance->ctx;
    if (!ctx) return;
    for (int i = 0; i < AIR_ROLE_COUNT; ++i) { ctx->input[i].valid = false; ctx->available[i] = false; }
    air_memory_reset(&ctx->memory);
    ctx->popup_dirty = true;
    render(ctx);
}
