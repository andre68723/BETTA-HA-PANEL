/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/ui_widget_factory.h"
#include "ui/ui_memory.h"
#include "ui/ui_popup.h"
#include "ui/ui_i18n.h"
#include "ui/theme/ui_style.h"
#include "ui/theme/theme_default.h"
#include "ha/ha_sensor_value.h"
#include "esp_timer.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char id[AIR_ROLE_COUNT][APP_MAX_ENTITY_ID_LEN];
    char text[AIR_ROLE_COUNT][96], unit[AIR_ROLE_COUNT][32];
    bool available[AIR_ROLE_COUNT], weather, popup_dirty;
    air_input_t input[AIR_ROLE_COUNT];
    air_config_t config;
    air_memory_t memory;
    air_result_t result;
    lv_obj_t *card, *surface, *title, *status, *value, *pollutants, *advice, *extra;
    lv_obj_t *overlay, *body;
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
    if (!ha_sv_number_text(number, number, !strcmp(ctx->unit[role], "μg/m³") ? "µg/m³" : ctx->unit[role], "", HA_SV_UNIT_DEFAULT,
                           ui_i18n_get_language(), out, size)) snprintf(out, size, "%s", number);
}

static void popup_refresh(room_air_t *ctx)
{
    if (!ctx->body || !ctx->popup_dirty) return;
    ctx->popup_dirty = false;
    int scroll_y = lv_obj_get_scroll_y(ctx->body);
    lv_obj_clean(ctx->body);
    char value[160], line[512];
    snprintf(line, sizeof(line), "BETTA: %s · %s", band_name(ctx->result.overall),
        ui_i18n_get(ctx->result.partial ? "air.partial" : "air.measured_scope",
                    ctx->result.partial ? "Partial data" : "Only bound measurements evaluated"));
    lv_obj_t *label = ui_popup_label_create(ctx->body, line, UI_POPUP_LABEL_BODY);
    lv_obj_set_width(label, LV_PCT(100));
    for (int role = 0; role < AIR_ROLE_COUNT; ++role) {
        if (!ctx->input[role].bound) {
            if (role == AIR_HUMIDITY || role == AIR_CO2 || role == AIR_PM25 || role == AIR_VOC) {
                snprintf(line, sizeof(line), "%s: %s", role_name(role), ui_i18n_get("air.unbound", "Not configured"));
                label = ui_popup_label_create(ctx->body, line, UI_POPUP_LABEL_HINT);
                lv_obj_set_width(label, LV_PCT(100));
            }
            continue;
        }
        value_text(ctx, role, value, sizeof(value));
        snprintf(line, sizeof(line), "%s: %s%s%s\n%s", role_name(role), value,
            role == AIR_HUMIDITY || role == AIR_CO2 || role == AIR_PM25 || role == AIR_VOC ? " · " : "",
            role == AIR_HUMIDITY || role == AIR_CO2 || role == AIR_PM25 || role == AIR_VOC ? band_name(ctx->result.band[role]) : "",
            ctx->id[role]);
        label = ui_popup_label_create(ctx->body, line, UI_POPUP_LABEL_BODY);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    }
    for (unsigned bit = 1; bit <= AIR_CONFLICT; bit <<= 1) {
        if (!(ctx->result.reasons & bit)) continue;
        label = ui_popup_label_create(ctx->body, reason_text(bit), UI_POPUP_LABEL_BODY);
        lv_obj_set_width(label, LV_PCT(100));
    }
    if (!ctx->result.reasons) {
        label = ui_popup_label_create(ctx->body, ui_i18n_get(ctx->result.overall == AIR_GOOD ? "air.in_range" : "air.no_advice",
            ctx->result.overall == AIR_GOOD ? "Measured values in range" : "Insufficient evaluated data for advice"), UI_POPUP_LABEL_BODY);
        lv_obj_set_width(label, LV_PCT(100));
    }
    if (ctx->result.comparison) {
        snprintf(line, sizeof(line), "%s: %.1f / %.1f g/m³ · Diff %.1f g/m³", ui_i18n_get("air.absolute", "Absolute humidity: indoor / outdoor"),
            ctx->result.indoor_ah, ctx->result.outdoor_ah, ctx->result.difference);
        label = ui_popup_label_create(ctx->body, line, UI_POPUP_LABEL_BODY);
        lv_obj_set_width(label, LV_PCT(100));
    }
    label = ui_popup_label_create(ctx->body, ui_i18n_get(ctx->weather ? "air.weather_reference" : "air.age_limit",
        ctx->weather ? "Weather provider reference; not a local outdoor measurement. Sample age unknown." :
        "HA availability is not sample freshness. Unbound pollutants are not evaluated. An open window does not prove sufficient air exchange."), UI_POPUP_LABEL_HINT);
    lv_obj_set_width(label, LV_PCT(100));
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

static void line_place(lv_obj_t *label, int width, int *y, ui_text_role_t role)
{
    ui_text_set(label, role, true);
    int h = lv_font_get_line_height(ui_font_role(role));
    lv_obj_set_size(label, width, h);
    lv_obj_set_pos(label, 0, *y);
    *y += h + 4;
}

static void render(room_air_t *ctx)
{
    air_result_t result = air_evaluate(&ctx->config, ctx->input, &ctx->memory, now_ms());
    if (result.overall != ctx->result.overall || result.reasons != ctx->result.reasons) ctx->popup_dirty = true;
    ctx->result = result;
    int w = lv_obj_get_width(ctx->card) - 24, h = lv_obj_get_height(ctx->card) - 24, y = 0;
    if (w <= 0 || h <= 0) return;
    uint32_t color = band_color(ctx->result.overall);
    lv_obj_set_pos(ctx->surface, -6, -6);
    lv_obj_set_size(ctx->surface, w + 12, h + 12);
    lv_obj_set_style_bg_color(ctx->surface, lv_color_hex(color), 0);
    lv_obj_set_style_border_color(ctx->surface, lv_color_hex(color), 0);
    if (ui_widget_name_enabled(ctx->title)) {
        line_place(ctx->title, w, &y, UI_TEXT_META);
        ui_text_record(ctx->title, UI_TEXT_META, false);
    }
    char text[384], a[128], b[128];
    snprintf(text, sizeof(text), "%s%s", band_name(ctx->result.overall), ctx->result.partial ? " *" : "");
    lv_label_set_text(ctx->status, text);
    line_place(ctx->status, w, &y, UI_TEXT_META);
    lv_obj_set_style_text_color(ctx->status, lv_color_hex(color), 0);
    value_text(ctx, AIR_HUMIDITY, text, sizeof(text));
    lv_label_set_text(ctx->value, text);
    line_place(ctx->value, w, &y, UI_TEXT_KEY);
    bool pollutant_row = (ctx->input[AIR_CO2].bound || ctx->input[AIR_PM25].bound) && w >= 330 && h - y >= 95;
    lv_obj_set_hidden(ctx->pollutants, !pollutant_row);
    if (pollutant_row) {
        value_text(ctx, AIR_CO2, a, sizeof(a)); value_text(ctx, AIR_PM25, b, sizeof(b));
        if (ctx->input[AIR_CO2].bound && ctx->input[AIR_PM25].bound)
            snprintf(text, sizeof(text), "CO₂ %s · PM %s", a, b);
        else snprintf(text, sizeof(text), "%s %s", ctx->input[AIR_CO2].bound ? "CO₂" : "PM", ctx->input[AIR_CO2].bound ? a : b);
        lv_label_set_text(ctx->pollutants, text);
        line_place(ctx->pollutants, w, &y, UI_TEXT_META);
        lv_obj_set_style_text_font(ctx->pollutants, ui_text_fit(text, ui_font_role(UI_TEXT_META), w, lv_obj_get_height(ctx->pollutants)), 0);
    }
    unsigned reasons = ctx->result.reasons;
    const char *advice = reasons & AIR_CONFLICT ? ui_i18n_get("air.conflict_short", "Air exchange · moisture conflict") :
        reasons & AIR_FRESH_AIR ? ui_i18n_get("air.co2_short", "Air exchange · CO₂") :
        reasons & AIR_HUMID ? ui_i18n_get("air.humid_short", "Humidity high") :
        reasons & AIR_DRY ? reason_text(AIR_DRY) : reasons & AIR_PARTICLES ? ui_i18n_get("air.pm_short", "Particles elevated") :
        ctx->result.overall == AIR_GOOD ? (ctx->result.measured == 1 && ctx->input[AIR_HUMIDITY].valid ?
            ui_i18n_get("air.rh_range", "Humidity in range") : ui_i18n_get("air.in_range_short", "Measured values in range")) :
        ui_i18n_get("air.partial", "Partial data");
    bool more = ctx->result.partial || (reasons && (reasons & (reasons - 1))) ||
        (!pollutant_row && (ctx->input[AIR_CO2].bound || ctx->input[AIR_PM25].bound));
    snprintf(text, sizeof(text), "%s%s", advice, more ? " +" : "");
    lv_label_set_text(ctx->advice, text);
    line_place(ctx->advice, w, &y, UI_TEXT_META);
    lv_obj_set_style_text_font(ctx->advice, ui_text_fit(text, ui_font_role(UI_TEXT_META), w, lv_obj_get_height(ctx->advice)), 0);
    bool extra = h - y >= lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    lv_obj_set_hidden(ctx->extra, !extra);
    if (extra) {
        if (ctx->input[AIR_RATING].bound) {
            value_text(ctx, AIR_RATING, a, sizeof(a));
            snprintf(text, sizeof(text), "%s: %s", role_name(AIR_RATING), a);
        } else if (ctx->input[AIR_TEMPERATURE].bound || ctx->input[AIR_WINDOW].bound) {
            value_text(ctx, AIR_TEMPERATURE, a, sizeof(a)); value_text(ctx, AIR_WINDOW, b, sizeof(b));
            snprintf(text, sizeof(text), "%s%s%s", ctx->input[AIR_TEMPERATURE].bound ? a : "",
                ctx->input[AIR_TEMPERATURE].bound && ctx->input[AIR_WINDOW].bound ? " · " : "", ctx->input[AIR_WINDOW].bound ? b : "");
        } else snprintf(text, sizeof(text), "%s", ui_i18n_get("air.humidity_scope", "Humidity only"));
        lv_label_set_text(ctx->extra, text);
        line_place(ctx->extra, w, &y, UI_TEXT_META);
        ui_text_record(ctx->extra, UI_TEXT_META, false);
    }
    popup_refresh(ctx);
}

static void timer_cb(lv_timer_t *timer)
{
    room_air_t *ctx = lv_timer_get_user_data(timer);
    ui_fonts_tile_begin("room_air_tile", lv_obj_get_width(ctx->card), lv_obj_get_height(ctx->card));
    render(ctx);
    ui_fonts_tile_end();
}

static void event_cb(lv_event_t *e)
{
    room_air_t *ctx = lv_event_get_user_data(e);
    if (lv_event_get_code(e) == LV_EVENT_DELETE) {
        if (ctx->overlay) lv_obj_delete(ctx->overlay);
        lv_timer_delete(ctx->timer); free(ctx);
    } else if (lv_event_get_code(e) == LV_EVENT_CLICKED) popup_open(ctx);
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
    ctx->surface = lv_obj_create(ctx->card);
    lv_obj_remove_style_all(ctx->surface);
    lv_obj_set_clickable(ctx->surface, false);
    lv_obj_set_style_bg_opa(ctx->surface, LV_OPA_10, 0);
    lv_obj_set_style_border_width(ctx->surface, 2, 0);
    lv_obj_set_style_border_opa(ctx->surface, LV_OPA_60, 0);
    lv_obj_set_style_radius(ctx->surface, ui_style_radius(10), 0);
    ctx->title = ui_widget_name_create(ctx->card, !def->hide_name);
    lv_label_set_text(ctx->title, def->title[0] ? def->title : ui_i18n_get("air.title", "Room air"));
    ctx->status = lv_label_create(ctx->card); ctx->value = lv_label_create(ctx->card);
    ctx->pollutants = lv_label_create(ctx->card); ctx->advice = lv_label_create(ctx->card); ctx->extra = lv_label_create(ctx->card);
    lv_obj_t *labels[] = {ctx->title, ctx->status, ctx->value, ctx->pollutants, ctx->advice, ctx->extra};
    for (unsigned i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) {
        lv_label_set_long_mode(labels[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(labels[i], lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), 0);
        lv_obj_set_clickable(labels[i], false);
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
