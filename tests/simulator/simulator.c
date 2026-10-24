/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * A small desktop entry point for real firmware widget sources. The fixture
 * uses the firmware layout's pages/widgets shape and a separate states array.
 */
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>
#include "cJSON.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "app_config.h"
#include "ui/display_geometry.h"
#include "layout/layout_orientation.h"
#include "ha/ha_alarm_state.h"
#include "ha/ha_attr_serialize.h"
#include "ha/ha_display_precision.h"
#include "ui/text_font.h"
#include "ui/theme/ui_day_grid.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/theme_palette.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_i18n.h"
#include "ui/ui_energy_page.h"
#include "ui/ui_history_popup.h"
#include "ha/parqet_performance.h"
#include "ui/ui_boot_splash.h"
#include "ui/ui_house_page.h"
#include "ui/ui_notification_popup.h"
#include "ui/ui_widget_factory.h"
#include "layout/evcc_slots.h"
#include "ha/ha_evcc_sessions.h"
#include "layout/widget_types.h"
#include "host_adapters.h"
#include "simulator_frame.h"
#include "ui/ui_conn_popup.h"
#include "ui/ui_pages.h"
#include "typography_dump.h"

#ifndef SIM_DEFAULT_FIXTURE
#define SIM_DEFAULT_FIXTURE "demo.json"
#endif

typedef esp_err_t (*sim_create_fn)(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
typedef void (*sim_apply_fn)(ui_widget_instance_t *, const ha_state_t *);

esp_err_t w_empty_tile_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_empty_tile_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_sensor_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_sensor_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_button_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_button_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_slider_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_slider_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_gauge_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_gauge_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_graph_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_graph_apply_state(ui_widget_instance_t *, const ha_state_t *);
void w_graph_sim_seed(ui_widget_instance_t *, const float *, size_t, uint32_t);
esp_err_t w_todo_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_todo_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_camera_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_media_player_set_visible(ui_widget_instance_t *, bool);
void w_vacuum_set_visible(ui_widget_instance_t *, bool);
void w_camera_apply_state(ui_widget_instance_t *, const ha_state_t *);
void w_todo_sim_seed(ui_widget_instance_t *, const char *const *, const bool *, size_t);
esp_err_t w_light_tile_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_light_tile_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_heating_tile_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_heating_tile_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_cover_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_cover_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_fan_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_fan_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_contact_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_contact_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_scene_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_scene_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_lock_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_lock_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_alarm_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_alarm_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_parqet_portfolio_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
esp_err_t w_stock_quotes_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_stock_quotes_apply_state(ui_widget_instance_t *, const ha_state_t *);
void w_parqet_portfolio_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_energy_price_forecast_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_energy_price_forecast_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_energy_timeline_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_energy_timeline_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_select_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_select_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_datetime_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_datetime_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_binary_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_binary_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_room_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_room_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_climate_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_climate_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_sonos_favorites_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_sonos_favorites_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_media_player_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_media_player_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_vacuum_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_vacuum_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_solar_forecast_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_solar_forecast_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_wallbox_evcc_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_wallbox_evcc_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_bambu_printer_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_bambu_printer_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_car_widget_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_car_widget_apply_state(ui_widget_instance_t *, const ha_state_t *);
esp_err_t w_weather_tile_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_weather_tile_apply_state(ui_widget_instance_t *, const ha_state_t *);

typedef struct {
    const char *type;
    sim_create_fn create;
    sim_apply_fn apply;
} sim_widget_type_t;

static const sim_widget_type_t k_types[] = {
    {"empty_tile", w_empty_tile_create, w_empty_tile_apply_state},
    {"sensor", w_sensor_create, w_sensor_apply_state},
    {"button", w_button_create, w_button_apply_state},
    {"slider", w_slider_create, w_slider_apply_state},
    {"gauge", w_gauge_create, w_gauge_apply_state},
    {"graph", w_graph_create, w_graph_apply_state},
    {"light_tile", w_light_tile_create, w_light_tile_apply_state},
    {"heating_tile", w_heating_tile_create, w_heating_tile_apply_state},
    {"cover_tile", w_cover_create, w_cover_apply_state},
    {"fan_tile", w_fan_create, w_fan_apply_state},
    {"contact_tile", w_contact_create, w_contact_apply_state},
    {"scene_tile", w_scene_create, w_scene_apply_state},
    {"select_tile", w_select_create, w_select_apply_state},
    {"datetime_tile", w_datetime_create, w_datetime_apply_state},
    {"lock_tile", w_lock_create, w_lock_apply_state},
    {"alarm_tile", w_alarm_create, w_alarm_apply_state},
    {"energy_price_forecast", w_energy_price_forecast_create, w_energy_price_forecast_apply_state},
    {"parqet_portfolio", w_parqet_portfolio_create, w_parqet_portfolio_apply_state},
    {"stock_quotes", w_stock_quotes_create, w_stock_quotes_apply_state},
    {"energy_timeline", w_energy_timeline_create, w_energy_timeline_apply_state},
    {"binary_tile", w_binary_create, w_binary_apply_state},
    {"room_tile", w_room_create, w_room_apply_state},
    {"climate_tile", w_climate_create, w_climate_apply_state},
    {"sonos_favorites", w_sonos_favorites_create, w_sonos_favorites_apply_state},
    {"media_player", w_media_player_create, w_media_player_apply_state},
    {"vacuum_tile", w_vacuum_create, w_vacuum_apply_state},
    {"solar_forecast", w_solar_forecast_create, w_solar_forecast_apply_state},
    {"wallbox_evcc", w_wallbox_evcc_create, w_wallbox_evcc_apply_state},
    {"bambu_printer", w_bambu_printer_create, w_bambu_printer_apply_state},
    {"car_widget", w_car_widget_create, w_car_widget_apply_state},
    {"todo_list", w_todo_create, w_todo_apply_state},
    {"camera_tile", w_camera_create, w_camera_apply_state},
    {"weather", w_weather_tile_create, w_weather_tile_apply_state},
    {"weather_tile", w_weather_tile_create, w_weather_tile_apply_state},
    {"weather_3day", w_weather_tile_create, w_weather_tile_apply_state},
};

static bool check_text_font_coverage(void)
{
    const uint32_t required[] = {
        'A', 0x00B0, 0x00B2, 0x00B3, 0x00B5, 0x00E4, 0x00EB,
        0x00EF, 0x00F6, 0x00FC, 0x20AC, 0x2026,
        0x2080, 0x2081, 0x2082, 0x2083, 0x2084,
        0x2085, 0x2086, 0x2087, 0x2088, 0x2089,
    };
    /* every step of the scale, from the table (16 ... 64 px), made by the same manager the firmware uses */
    for (int step = 0; step < TEXT_STEP_COUNT; step++) {
        const int px = text_scale_step_px(step);
        const lv_font_t *font = ui_font_text_px(px);
        if (font == NULL) {
            fprintf(stderr, "Poppins %d could not be created\n", px);
            return false;
        }
        for (size_t j = 0; j < sizeof(required) / sizeof(required[0]); j++) {
            lv_font_glyph_dsc_t glyph;
            if (!font->get_glyph_dsc(font, &glyph, required[j], 0)) {
                fprintf(stderr, "Poppins %d is missing U+%04X\n", px, (unsigned)required[j]);
                return false;
            }
        }
    }
    return true;
}

typedef struct {
    ui_widget_instance_t instance;
    ui_widget_def_t def;
    const sim_widget_type_t *type;
    const cJSON *json;
} sim_widget_t;

static const char *json_string(const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring != NULL ? item->valuestring : "";
}

static int json_int(const cJSON *object, const char *key, int fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static float json_float(const cJSON *object, const char *key, float fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(item) ? (float)item->valuedouble : fallback;
}

static bool json_bool(const cJSON *object, const char *key, bool fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

static void copy_field(char *out, size_t capacity, const cJSON *object, const char *key)
{
    snprintf(out, capacity, "%s", json_string(object, key));
}

static void parse_widget_def(const cJSON *json, ui_widget_def_t *def)
{
    memset(def, 0, sizeof(*def));
    copy_field(def->id, sizeof(def->id), json, "id");
    copy_field(def->type, sizeof(def->type), json, "type");
    copy_field(def->title, sizeof(def->title), json, "title");
    copy_field(def->entity_id, sizeof(def->entity_id), json, "entity_id");
    copy_field(def->secondary_entity_id, sizeof(def->secondary_entity_id), json, "secondary_entity_id");
    copy_field(def->tertiary_entity_id, sizeof(def->tertiary_entity_id), json, "tertiary_entity_id");
    copy_field(def->forecast_today_entity_id, sizeof(def->forecast_today_entity_id), json, "forecast_today_entity_id");
    copy_field(def->forecast_tomorrow_entity_id, sizeof(def->forecast_tomorrow_entity_id), json, "forecast_tomorrow_entity_id");
    copy_field(def->forecast_day_3_entity_id, sizeof(def->forecast_day_3_entity_id), json, "forecast_day_3_entity_id");
    copy_field(def->forecast_day_4_entity_id, sizeof(def->forecast_day_4_entity_id), json, "forecast_day_4_entity_id");
    copy_field(def->forecast_day_5_entity_id, sizeof(def->forecast_day_5_entity_id), json, "forecast_day_5_entity_id");
    copy_field(def->slider_direction, sizeof(def->slider_direction), json, "slider_direction");
    copy_field(def->slider_accent_color, sizeof(def->slider_accent_color), json, "slider_accent_color");
    copy_field(def->button_accent_color, sizeof(def->button_accent_color), json, "button_accent_color");
    copy_field(def->button_mode, sizeof(def->button_mode), json, "button_mode");
    copy_field(def->graph_line_color, sizeof(def->graph_line_color), json, "graph_line_color");
    /* As ui_runtime_widget_from_json(): the sensor's value format and unit style (missing here, the formats rendered raw). */
    copy_field(def->sensor_format, sizeof(def->sensor_format), json, "sensor_format");
    copy_field(def->sensor_unit, sizeof(def->sensor_unit), json, "sensor_unit");
    if (strcmp(def->type, "energy_timeline") == 0) def->energy_palette = etl_palette_from_json(json);
    copy_field(def->graph_display_mode, sizeof(def->graph_display_mode), json, "graph_display_mode");
    copy_field(def->style_variant, sizeof(def->style_variant), json, "style_variant");
    copy_field(def->arc_opening, sizeof(def->arc_opening), json, "arc_opening");
    copy_field(def->display_mode, sizeof(def->display_mode), json, "display_mode");
    /* As ui_runtime_widget_from_json(): a binary tile in the "door" look is drawn by the contact renderer. */
    if (strcmp(def->type, "binary_tile") == 0 && strcmp(def->display_mode, "door") == 0) {
        snprintf(def->type, sizeof(def->type), "%s", "contact_tile");
    }
    copy_field(def->default_mode, sizeof(def->default_mode), json, "default_mode");
    copy_field(def->gauge_bar_style, sizeof(def->gauge_bar_style), json, "gauge_bar_style");
    def->gauge_segment_count = json_int(json, "gauge_segment_count", 0);
    copy_field(def->gauge_color_mode, sizeof(def->gauge_color_mode), json, "gauge_color_mode");
    copy_field(def->gauge_color_low, sizeof(def->gauge_color_low), json, "gauge_color_low");
    copy_field(def->gauge_color_high, sizeof(def->gauge_color_high), json, "gauge_color_high");
    copy_field(def->gauge_target_color, sizeof(def->gauge_target_color), json, "gauge_target_color");
    copy_field(def->solar_forecast_bar_orientation, sizeof(def->solar_forecast_bar_orientation), json,
               "solar_forecast_bar_orientation");
    def->gauge_min = json_float(json, "gauge_min", 0.0f);
    def->gauge_max = json_float(json, "gauge_max", 100.0f);
    def->gauge_target_low = json_float(json, "gauge_target_low", 0.0f);
    def->gauge_target_high = json_float(json, "gauge_target_high", 0.0f);
    def->solar_forecast_bar_max_kwh = json_float(json, "solar_forecast_bar_max_kwh", 0.0f);
    def->graph_point_count = json_int(json, "graph_point_count", 0);
    def->graph_time_window_min = json_int(json, "graph_time_window_min", 0);
    def->graph_bar_bucket_min = json_int(json, "graph_bar_bucket_min", 0);
    def->graph_ha_history = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "graph_ha_history"));
    def->hide_name = !widget_show_name(json);
    def->sensor_show_age = json_bool(json, "sensor_show_age", true);
    def->lock_confirm = json_bool(json, "lock_confirm", false);
    def->price_day_switch = json_bool(json, "price_day_switch", false);
    def->price_cheap_times = json_bool(json, "price_cheap_times", false);
    def->price_line = json_bool(json, "price_line", false);
    def->price_average = json_float(json, "price_average", 0.0f);
    def->parqet_hide_amounts = json_bool(json, "parqet_hide_amounts", false);
    def->parqet_selector = json_bool(json, "parqet_selector", false);
    copy_field(def->parqet_view, sizeof(def->parqet_view), json, "parqet_view");
    copy_field(def->parqet_interval, sizeof(def->parqet_interval), json, "parqet_interval");
    def->camera_live = json_bool(json, "camera_live", false);
    def->alarm_actions = ha_alarm_action_mask_parse(cJSON_GetObjectItemCaseSensitive(json, "alarm_actions"));
    def->history_popup = json_bool(json, "history_popup", false);
    def->forecast_days = json_int(json, "forecast_days", -1);
    def->align_forecast = json_bool(json, "align_forecast", false);
    def->show_precipitation = json_bool(json, "show_precipitation", true);
    def->room_controls = json_bool(json, "room_controls", false);
    const cJSON *room_entities = cJSON_GetObjectItemCaseSensitive(json, "room_entities");
    for (int i = 0; i < UI_WIDGET_EXTRA_ENTITIES; i++) {
        const cJSON *id = cJSON_GetArrayItem(room_entities, i);
        if (cJSON_IsString(id)) {
            snprintf(def->extra_entity_id[i], sizeof(def->extra_entity_id[i]), "%s", id->valuestring);
        }
    }
    if (!strcmp(def->type, "wallbox_evcc")) {
        evcc_slots_from_json(json, def->entity_id, def->secondary_entity_id, def->tertiary_entity_id, def->extra_entity_id, UI_WIDGET_EXTRA_ENTITIES);
        char *const site[EVCC_SITE_COUNT] = {def->forecast_today_entity_id, def->forecast_tomorrow_entity_id,
                                             def->forecast_day_3_entity_id, def->forecast_day_4_entity_id};
        def->forecast_day_5_entity_id[0] = '\0';
        evcc_header_from_json(json, def->style_variant, sizeof(def->style_variant), &def->gauge_max, site);
    }
    if (strcmp(def->type, "bambu_printer") == 0) {
        bambu_printer_slots_from_json(json, def->entity_id, def->secondary_entity_id, def->tertiary_entity_id,
                                      def->extra_entity_id, UI_WIDGET_EXTRA_ENTITIES);
    }
    if (strcmp(def->type, "car_widget") == 0) {
        copy_field(def->extra_entity_id[UI_CAR_EXTRA_PRECOND], APP_MAX_ENTITY_ID_LEN, json, "precond_entity_id");
        copy_field(def->extra_entity_id[UI_CAR_EXTRA_LOCK], APP_MAX_ENTITY_ID_LEN, json, "lock_entity_id");
        copy_field(def->extra_entity_id[UI_CAR_EXTRA_TEMP], APP_MAX_ENTITY_ID_LEN, json, "interior_temp_entity_id");
    }
    if (strcmp(def->type, "vacuum_tile") == 0 || strcmp(def->type, "roborock_tile") == 0) {
        copy_field(def->extra_entity_id[UI_VACUUM_EXTRA_BATTERY], APP_MAX_ENTITY_ID_LEN, json, "battery_entity_id");
    }
    if (strcmp(def->type, "parqet_portfolio") == 0) {
        copy_field(def->extra_entity_id[UI_PARQET_EXTRA_DIVIDENDS], APP_MAX_ENTITY_ID_LEN, json, "dividends_entity_id");
        copy_field(def->extra_entity_id[UI_PARQET_EXTRA_RETURN], APP_MAX_ENTITY_ID_LEN, json, "return_entity_id");
    }
    if (strcmp(def->type, "stock_quotes") == 0) {
        const cJSON *more = cJSON_GetObjectItemCaseSensitive(json, "stock_entities");
        for (int i = 0; i < UI_WIDGET_EXTRA_ENTITIES; i++) def->extra_entity_id[i][0] = '\0';
        for (int i = 0; i < UI_STOCK_MAX_QUOTES - 1; i++) {
            const cJSON *id = cJSON_GetArrayItem(more, i);
            if (cJSON_IsString(id)) snprintf(def->extra_entity_id[i], sizeof(def->extra_entity_id[i]), "%s", id->valuestring);
        }
        copy_field(def->display_mode, sizeof(def->display_mode), json, "stock_view");
        copy_field(def->stock_period, sizeof(def->stock_period), json, "stock_period");
        copy_field(def->stock_delta, sizeof(def->stock_delta), json, "stock_delta");
        def->stock_ticker = json_bool(json, "stock_ticker", false);
    }
    if (strcmp(def->type, "room_tile") == 0) {
        copy_field(def->display_mode, sizeof(def->display_mode), json, "room_extra_mode");
        copy_field(def->secondary_entity_id, sizeof(def->secondary_entity_id), json, "room_extra_entity_id");
    }
    if (strcmp(def->type, "energy_timeline") == 0) {
        copy_field(def->extra_entity_id[UI_ETL_EXTRA_BATTERY_CHARGE], APP_MAX_ENTITY_ID_LEN, json, "battery_charge_entity_id");
        copy_field(def->extra_entity_id[UI_ETL_EXTRA_BATTERY_DISCHARGE], APP_MAX_ENTITY_ID_LEN, json, "battery_discharge_entity_id");
        copy_field(def->extra_entity_id[UI_ETL_EXTRA_BATTERY_SOC], APP_MAX_ENTITY_ID_LEN, json, "battery_soc_entity_id");
    }
    const cJSON *rect = cJSON_GetObjectItemCaseSensitive(json, "rect");
    if (!cJSON_IsObject(rect)) rect = json;
    def->x = json_int(rect, "x", 0);
    def->y = json_int(rect, "y", 0);
    def->w = json_int(rect, "w", 200);
    def->h = json_int(rect, "h", 160);
}

static const sim_widget_type_t *find_type(const char *name)
{
    for (size_t i = 0; i < sizeof(k_types) / sizeof(k_types[0]); i++) {
        if (strcmp(name, k_types[i].type) == 0) {
            return &k_types[i];
        }
    }
    return NULL;
}

static void init_instance(const ui_widget_def_t *def, ui_widget_instance_t *out)
{
    memset(out, 0, sizeof(*out));
#define COPY(name) snprintf(out->name, sizeof(out->name), "%s", def->name)
    COPY(id);
    COPY(type);
    COPY(title);
    COPY(entity_id);
    COPY(secondary_entity_id);
    COPY(tertiary_entity_id);
    COPY(forecast_today_entity_id);
    COPY(forecast_tomorrow_entity_id);
    COPY(forecast_day_3_entity_id);
    COPY(forecast_day_4_entity_id);
    COPY(forecast_day_5_entity_id);
    COPY(solar_forecast_bar_orientation);
    COPY(slider_direction);
    COPY(slider_accent_color);
    COPY(button_accent_color);
    COPY(button_mode);
    COPY(graph_line_color);
    out->energy_palette = def->energy_palette;
    COPY(graph_display_mode);
    COPY(style_variant);
    COPY(arc_opening);
    COPY(display_mode);
    COPY(gauge_bar_style);
    out->gauge_segment_count = def->gauge_segment_count;
#undef COPY
    for (int i = 0; i < UI_WIDGET_EXTRA_ENTITIES; i++) {
        snprintf(out->extra_entity_id[i], sizeof(out->extra_entity_id[i]), "%s", def->extra_entity_id[i]);
    }
    out->solar_forecast_bar_max_kwh = def->solar_forecast_bar_max_kwh;
    out->graph_point_count = def->graph_point_count;
    out->graph_time_window_min = def->graph_time_window_min;
    out->graph_bar_bucket_min = def->graph_bar_bucket_min;
    out->graph_ha_history = def->graph_ha_history;
    out->gauge_min = def->gauge_min;
    out->gauge_max = def->gauge_max;
    out->hide_name = def->hide_name;
    out->sensor_show_age = def->sensor_show_age;
    out->lock_confirm = def->lock_confirm;
    out->alarm_actions = def->alarm_actions;
    out->visible = true;
}

static bool widget_uses_entity(const ui_widget_def_t *def, const char *entity_id)
{
    const char *ids[] = {
        def->entity_id, def->secondary_entity_id, def->tertiary_entity_id,
        def->forecast_today_entity_id, def->forecast_tomorrow_entity_id,
        def->forecast_day_3_entity_id, def->forecast_day_4_entity_id, def->forecast_day_5_entity_id,
    };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        if (ids[i][0] != '\0' && strcmp(ids[i], entity_id) == 0) {
            return true;
        }
    }
    for (size_t i = 0; i < UI_WIDGET_EXTRA_ENTITIES; i++) {
        if (def->extra_entity_id[i][0] != '\0' && strcmp(def->extra_entity_id[i], entity_id) == 0) return true;
    }
    return false;
}

static char *read_text_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Cannot open fixture %s: %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || length > 4 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Fixture is too large or unreadable: %s\n", path);
        fclose(file);
        return NULL;
    }
    char *data = malloc((size_t)length + 1);
    if (data == NULL || fread(data, 1, (size_t)length, file) != (size_t)length) {
        fprintf(stderr, "Cannot read fixture: %s\n", path);
        free(data);
        fclose(file);
        return NULL;
    }
    data[length] = '\0';
    fclose(file);
    return data;
}

/* A fixture states a price list as {"price_series": {"shape": "epex"|"dao"|"raw", "interval_min": 60, "today": [...], "tomorrow": [...]}}: the prices of
 * the local calendar days around the moment the simulator runs, expanded to the `data` attribute a price sensor has in Home Assistant. */
/* The answers the portfolio tile would get from the integration for an interval (see parqet_request_sim.c). */
extern void sim_parqet_set(const char *portfolio_id, const char *interval, const parqet_perf_t *perf);

static void load_parqet_perf(const cJSON *root)
{
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "parqet_perf")) {
        parqet_perf_t perf;
        memset(&perf, 0, sizeof(perf));
        perf.value_start = json_float(item, "value_start", 0.0f);
        perf.value_end = json_float(item, "value_end", 0.0f);
        perf.unrealized_gain = json_float(item, "unrealized_gain", 0.0f);
        perf.unrealized_return = json_float(item, "unrealized_return", 0.0f);
        perf.realized_gain = json_float(item, "realized_gain", 0.0f);
        perf.dividends = json_float(item, "dividends", 0.0f);
        perf.xirr = json_float(item, "xirr", 0.0f);
        perf.ttwror = json_float(item, "ttwror", 0.0f);
        /* the holdings of the fixture fill the three rankings like parqet_perf_parse(): by value, best return, worst return */
        parqet_perf_holding_t all[16];
        int all_count = 0;
        const cJSON *holding = NULL;
        cJSON_ArrayForEach(holding, cJSON_GetObjectItemCaseSensitive(item, "holdings")) {
            if (all_count >= 16) break;
            parqet_perf_holding_t *h = &all[all_count++];
            memset(h, 0, sizeof(*h));
            snprintf(h->name, sizeof(h->name), "%s", json_string(holding, "name"));
            h->value = json_float(holding, "value", 0.0f);
            h->weight = json_float(holding, "weight", 0.0f);
            h->gain = json_float(holding, "gain", 0.0f);
            h->ret = json_float(holding, "ret", 0.0f);
            h->has_return = cJSON_GetObjectItemCaseSensitive(holding, "ret") != NULL;
        }
        for (int mode = 0; mode < 3; mode++) {
            parqet_perf_holding_t *list = mode == 0 ? perf.largest : mode == 1 ? perf.top : perf.flop;
            int *count = mode == 0 ? &perf.largest_count : mode == 1 ? &perf.top_count : &perf.flop_count;
            bool used[16] = {false};
            while (*count < PARQET_PERF_RANK) {
                int pick = -1;
                for (int i = 0; i < all_count; i++) {
                    if (used[i] || (mode > 0 && !all[i].has_return)) continue;
                    if (mode == 2 && perf.top_count + *count >= all_count) break;
                    const double a = mode == 0 ? all[i].value : mode == 1 ? all[i].ret : -all[i].ret;
                    const double b = pick < 0 ? 0.0 : mode == 0 ? all[pick].value : mode == 1 ? all[pick].ret : -all[pick].ret;
                    if (pick < 0 || a > b) pick = i;
                }
                if (pick < 0) break;
                used[pick] = true;
                list[(*count)++] = all[pick];
            }
        }
        const cJSON *point = NULL;
        cJSON_ArrayForEach(point, cJSON_GetObjectItemCaseSensitive(item, "points")) {
            if (perf.point_count < PARQET_PERF_POINTS && cJSON_IsNumber(point)) perf.point[perf.point_count++] = (float)point->valuedouble;
        }
        sim_parqet_set(json_string(item, "portfolio_id"), json_string(item, "interval"), &perf);
    }
}

static void expand_price_series(cJSON *attrs)
{
    const cJSON *spec = cJSON_GetObjectItemCaseSensitive(attrs, "price_series");
    if (!cJSON_IsObject(spec)) return;
    const cJSON *shape = cJSON_GetObjectItemCaseSensitive(spec, "shape");
    const bool dao = cJSON_IsString(shape) && shape->valuestring != NULL && strcmp(shape->valuestring, "dao") == 0;
    /* "raw": the Nord Pool / Energy Price Forecast shape, raw_today (only the slots from now on) and raw_tomorrow with {start (UTC), value} */
    const bool raw = cJSON_IsString(shape) && shape->valuestring != NULL && strcmp(shape->valuestring, "raw") == 0;
    const int interval_min = json_int(spec, "interval_min", 60);
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    const time_t midnight = mktime(&tm);
    cJSON *data = cJSON_CreateArray();
    cJSON *raw_days[2] = {cJSON_CreateArray(), cJSON_CreateArray()};
    int slot = 0;
    const char *days[2] = {"today", "tomorrow"};
    for (int d = 0; d < 2; d++) {
        const cJSON *list = cJSON_GetObjectItemCaseSensitive(spec, days[d]);
        const cJSON *price;
        cJSON_ArrayForEach(price, list) {
            cJSON *item = cJSON_CreateObject();
            const time_t start = midnight + (time_t)slot * interval_min * 60;
            struct tm lt;
            localtime_r(&start, &lt);
            if (raw) {
                struct tm ut;
                char text[24];
                gmtime_r(&start, &ut);
                strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &ut);
                cJSON_AddStringToObject(item, "start", text);
                cJSON_AddNumberToObject(item, "value", price->valuedouble);
                if (d == 1 || start + interval_min * 60 > now) {
                    cJSON_AddItemToArray(raw_days[d], item);
                } else {
                    cJSON_Delete(item);
                }
                slot++;
                continue;
            }
            if (dao) {
                cJSON_AddNumberToObject(item, "time_ts", (double)start * 1000.0);
                char local[20];
                strftime(local, sizeof(local), "%Y-%m-%d %H:%M", &lt);
                cJSON_AddStringToObject(item, "time", local);
                cJSON_AddNumberToObject(item, "da_ex", price->valuedouble * 0.6);
                cJSON_AddNumberToObject(item, "da_cons", price->valuedouble);
                cJSON_AddNumberToObject(item, "da_prod", price->valuedouble);
                cJSON_AddStringToObject(item, "datatype", start < now ? "recorded" : "expected");
            } else {
                char text[40];
                const long off = lt.tm_gmtoff;
                size_t n = strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S", &lt);
                snprintf(text + n, sizeof(text) - n, "%c%02ld:%02ld", off < 0 ? '-' : '+', labs(off) / 3600, (labs(off) % 3600) / 60);
                cJSON_AddStringToObject(item, "start_time", text);
                cJSON_AddNumberToObject(item, "price_per_kwh", price->valuedouble);
            }
            cJSON_AddItemToArray(data, item);
            slot++;
        }
    }
    cJSON_DeleteItemFromObjectCaseSensitive(attrs, "price_series");
    cJSON_DeleteItemFromObjectCaseSensitive(attrs, "data");
    if (raw) {
        cJSON_Delete(data);
        cJSON_AddItemToObject(attrs, "raw_today", raw_days[0]);
        cJSON_AddItemToObject(attrs, "raw_tomorrow", raw_days[1]);
        return;
    }
    cJSON_Delete(raw_days[0]);
    cJSON_Delete(raw_days[1]);
    cJSON_AddItemToObject(attrs, "data", data);
}

static bool parse_state(const cJSON *json, ha_state_t *state)
{
    memset(state, 0, sizeof(*state));
    copy_field(state->entity_id, sizeof(state->entity_id), json, "entity_id");
    copy_field(state->state, sizeof(state->state), json, "state");
    if (state->entity_id[0] == '\0') {
        return false;
    }
    const cJSON *attrs = cJSON_GetObjectItemCaseSensitive(json, "attributes");
    if (cJSON_IsObject(attrs)) {
        cJSON *resolved = cJSON_Duplicate(attrs, true);
        cJSON *days = cJSON_GetObjectItemCaseSensitive(resolved, "forecast");
        cJSON *day = NULL;
        cJSON_ArrayForEach(day, days) {
            cJSON *offset = cJSON_GetObjectItemCaseSensitive(day, "day_offset");
            if (cJSON_IsNumber(offset)) {
                char date[11]; ui_day_grid_date(offset->valueint, date);
                cJSON_DeleteItemFromObjectCaseSensitive(day, "datetime");
                cJSON_AddStringToObject(day, "datetime", date);
                cJSON_DeleteItemFromObjectCaseSensitive(day, "day_offset");
            }
        }
        expand_price_series(resolved);
        if (ha_client_attrs_is_price_series(resolved)) {
            /* the firmware's own compaction, so the widget is fed exactly what it gets from Home Assistant */
            const bool ok = ha_client_serialize_price_series_compact(resolved, state->attributes_json, sizeof(state->attributes_json));
            cJSON_Delete(resolved);
            if (!ok) {
                fprintf(stderr, "Price list of %s is not representable\n", state->entity_id);
                return false;
            }
        } else {
            char *serialized = cJSON_PrintUnformatted(resolved); cJSON_Delete(resolved);
            if (serialized == NULL || strlen(serialized) >= sizeof(state->attributes_json)) {
                fprintf(stderr, "Attributes too long for %s\n", state->entity_id);
                cJSON_free(serialized);
                return false;
            }
            snprintf(state->attributes_json, sizeof(state->attributes_json), "%s", serialized);
            cJSON_free(serialized);
        }
    } else if (cJSON_IsString(attrs)) {
        snprintf(state->attributes_json, sizeof(state->attributes_json), "%s", attrs->valuestring);
    } else {
        snprintf(state->attributes_json, sizeof(state->attributes_json), "{}");
    }
    /* The firmware uses a monotonic millisecond timestamp for sensor age. */
    int age_seconds = json_int(json, "age_seconds", 0);
    if (age_seconds < 0) age_seconds = 0;
    state->last_changed_unix_ms = esp_timer_get_time() / 1000 - (int64_t)age_seconds * 1000;
    return true;
}

/* Replay the optional fixture precision through the same registry cache the
 * firmware's widgets use. A raw HA state does not contain this setting. */
static bool load_display_precisions(const cJSON *states)
{
    int count = cJSON_GetArraySize(states);
    char *ids = calloc((size_t)count + 1, APP_MAX_ENTITY_ID_LEN);
    if (ids == NULL) return false;
    size_t wanted = 0;
    const cJSON *state = NULL;
    cJSON_ArrayForEach(state, states) {
        /* a registry entry: the precision and/or the integration ("platform": "yahoofinance", "easy_stock", ...) */
        const cJSON *digits = cJSON_GetObjectItemCaseSensitive(state, "display_precision");
        const cJSON *platform = cJSON_GetObjectItemCaseSensitive(state, "platform");
        if ((cJSON_IsNumber(digits) && digits->valueint >= 0 && digits->valueint <= 6) || cJSON_IsString(platform)) {
            snprintf(ids + wanted * APP_MAX_ENTITY_ID_LEN, APP_MAX_ENTITY_ID_LEN,
                     "%s", json_string(state, "entity_id"));
            wanted++;
        }
    }
    ha_display_precision_set_wanted(ids, wanted);
    free(ids);
    uint32_t request_id = 1;
    while (ha_display_precision_is_due(0)) {
        cJSON *request = ha_display_precision_next_request(request_id, 0);
        cJSON *result = cJSON_CreateObject();
        if (request == NULL || result == NULL) {
            cJSON_Delete(request);
            cJSON_Delete(result);
            return false;
        }
        const cJSON *requested = cJSON_GetObjectItemCaseSensitive(request, "entity_ids");
        const cJSON *id = NULL;
        cJSON_ArrayForEach(id, requested) {
            cJSON_ArrayForEach(state, states) {
                if (strcmp(id->valuestring, json_string(state, "entity_id")) != 0) continue;
                cJSON *entry = cJSON_AddObjectToObject(result, id->valuestring);
                if (cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(state, "display_precision"))) {
                    cJSON *options = cJSON_AddObjectToObject(entry, "options");
                    cJSON *sensor = cJSON_AddObjectToObject(options, "sensor");
                    cJSON_AddNumberToObject(sensor, "display_precision", json_int(state, "display_precision", 0));
                }
                if (json_string(state, "platform")[0] != '\0') cJSON_AddStringToObject(entry, "platform", json_string(state, "platform"));
                break;
            }
        }
        ha_display_precision_handle_result(request_id, true, result, NULL);
        cJSON_Delete(result);
        cJSON_Delete(request);
        request_id++;
    }
    return true;
}

static void print_usage(const char *program)
{
    fprintf(stderr, "Usage: %s [--fixture path] [--page id] [--rotation 0|90|180|270 (Tab5)] [--text-step 0|1|2] [--text-cap 0..3] [--theme id] [--style ", program);
    ui_style_kind_t kind;
    for (size_t i = 0; ui_style_at(i, &kind); i++) {
        fprintf(stderr, "%s%s", i > 0 ? "|" : "", ui_style_name(kind));
    }
    fprintf(stderr,
            "] [--language de|en] [--exit-after-ms duration] [--screenshot path.bmp] "
            "[--dump-layout path.jsonl] [--tap x,y]... [--regression wallbox|bambu|history|energy|controls|fonts|notification|price]\n");
}

static bool save_screenshot(lv_display_t *display, const char *path)
{
    SDL_Renderer *renderer = lv_sdl_window_get_renderer(display);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, APP_VIEW_WIDTH, APP_VIEW_HEIGHT,
                                                          32, SDL_PIXELFORMAT_ARGB8888);
    if (renderer == NULL || surface == NULL) {
        fprintf(stderr, "Cannot prepare SDL screenshot: %s\n", SDL_GetError());
        SDL_FreeSurface(surface);
        return false;
    }
    bool ok = SDL_RenderReadPixels(renderer, NULL, surface->format->format,
                                   surface->pixels, surface->pitch) == 0 &&
              SDL_SaveBMP(surface, path) == 0;
    if (!ok) fprintf(stderr, "Cannot save %s: %s\n", path, SDL_GetError());
    SDL_FreeSurface(surface);
    return ok;
}

static bool sim_area_intersect(lv_area_t *res, const lv_area_t *a, const lv_area_t *b)
{
    lv_area_t r = {a->x1 > b->x1 ? a->x1 : b->x1, a->y1 > b->y1 ? a->y1 : b->y1,
                   a->x2 < b->x2 ? a->x2 : b->x2, a->y2 < b->y2 ? a->y2 : b->y2};
    if (r.x1 > r.x2 || r.y1 > r.y2) return false;
    *res = r;
    return true;
}

/* One JSON line per visible object inside a widget card: absolute rectangle, whether it is a label
 * and its text, so tools/check_widget_layout.py can find text that overlaps or leaves its tile.
 * Content of a scrolling list is reported as far as it is visible: a row half cut off at the bottom is what a
 * scrolling list looks like, not a layout defect, and a row scrolled out of sight is not reported at all. */
static void dump_object(FILE *out, const ui_widget_def_t *def, lv_obj_t *obj, int depth, const lv_area_t *clip)
{
    lv_area_t scroll_clip;
    if (lv_obj_is_scrollable(obj) &&
        (lv_obj_get_scroll_top(obj) > 0 || lv_obj_get_scroll_bottom(obj) > 0 ||
         lv_obj_get_scroll_left(obj) > 0 || lv_obj_get_scroll_right(obj) > 0)) {
        lv_obj_get_content_coords(obj, &scroll_clip);
        if (clip != NULL && !sim_area_intersect(&scroll_clip, &scroll_clip, clip)) return;
        clip = &scroll_clip;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        lv_obj_t *child = lv_obj_get_child(obj, (int32_t)i);
        if (lv_obj_is_hidden(child)) continue;
        lv_area_t a;
        lv_obj_get_coords(child, &a);
        if (clip != NULL && !sim_area_intersect(&a, &a, clip)) continue;
        const bool is_label = lv_obj_check_type(child, &lv_label_class);
        cJSON *line = cJSON_CreateObject();
        cJSON_AddStringToObject(line, "widget", def->id);
        cJSON_AddStringToObject(line, "type", def->type);
        cJSON_AddNumberToObject(line, "depth", depth);
        cJSON_AddStringToObject(line, "kind", is_label ? "label"
                                              : lv_obj_check_type(child, &lv_arc_class) ? "arc"
                                              : lv_obj_check_type(child, &lv_button_class) ? "button"
                                              : lv_obj_check_type(child, &lv_bar_class) ? "bar"
                                              : lv_obj_check_type(child, &lv_slider_class) ? "slider" : "obj");
        cJSON_AddNumberToObject(line, "x1", a.x1);
        cJSON_AddNumberToObject(line, "y1", a.y1);
        cJSON_AddNumberToObject(line, "x2", a.x2);
        cJSON_AddNumberToObject(line, "y2", a.y2);
        if (is_label) {
            /* The drawn text's own rectangle: a label object is often wider than its text. */
            simulator_dump_typography(line, child);
            const char *text = lv_label_get_text(child);
            const lv_font_t *font = lv_obj_get_style_text_font(child, LV_PART_MAIN);
            lv_point_t size;
            lv_text_get_size(&size, text, font, lv_obj_get_style_text_letter_space(child, LV_PART_MAIN),
                             lv_obj_get_style_text_line_space(child, LV_PART_MAIN), lv_obj_get_content_width(child),
                             LV_TEXT_FLAG_NONE);
            lv_area_t content;
            lv_obj_get_content_coords(child, &content);
            const lv_text_align_t align = lv_obj_get_style_text_align(child, LV_PART_MAIN);
            int32_t tx = content.x1;
            if (align == LV_TEXT_ALIGN_CENTER) tx = content.x1 + (lv_area_get_width(&content) - size.x) / 2;
            else if (align == LV_TEXT_ALIGN_RIGHT) tx = content.x2 + 1 - size.x;
            const int32_t th = size.y < lv_area_get_height(&content) ? size.y : lv_area_get_height(&content);
            lv_area_t drawn = {tx, content.y1, tx + size.x - 1, content.y1 + th - 1};
            if (clip != NULL && !sim_area_intersect(&drawn, &drawn, clip)) {
                cJSON_Delete(line);
                continue;
            }
            cJSON_AddStringToObject(line, "text", text);
            cJSON_AddNumberToObject(line, "tx1", drawn.x1);
            cJSON_AddNumberToObject(line, "ty1", drawn.y1);
            cJSON_AddNumberToObject(line, "tx2", drawn.x2);
            cJSON_AddNumberToObject(line, "ty2", drawn.y2);
        }
        char *text = cJSON_PrintUnformatted(line);
        if (text != NULL) fprintf(out, "%s\n", text);
        cJSON_free(text);
        cJSON_Delete(line);
        dump_object(out, def, child, depth + 1, clip);
    }
}

static bool dump_layout(const char *path, const sim_widget_t *widgets, int count, lv_obj_t *energy_page)
{
    FILE *out = fopen(path, "w");
    if (out == NULL) {
        fprintf(stderr, "Cannot write %s\n", path);
        return false;
    }
    for (int i = 0; i < count; i++) {
        lv_obj_t *card = widgets[i].instance.obj;
        lv_area_t a;
        lv_obj_get_coords(card, &a);
        fprintf(out, "{\"widget\":\"%s\",\"type\":\"%s\",\"depth\":0,\"kind\":\"card\",\"x1\":%d,\"y1\":%d,\"x2\":%d,\"y2\":%d}\n",
                widgets[i].def.id, widgets[i].def.type, (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2);
        dump_object(out, &widgets[i].def, card, 1, NULL);
    }
    if (energy_page != NULL) {
        /* The energy page is checked like one large widget. */
        ui_widget_def_t def = {0};
        snprintf(def.id, sizeof(def.id), "%s", "energy_page");
        snprintf(def.type, sizeof(def.type), "%s", "energy_dashboard");
        lv_area_t a;
        lv_obj_get_coords(energy_page, &a);
        fprintf(out, "{\"widget\":\"energy_page\",\"type\":\"energy_dashboard\",\"depth\":0,\"kind\":\"card\",\"x1\":%d,\"y1\":%d,\"x2\":%d,\"y2\":%d}\n",
                (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2);
        dump_object(out, &def, energy_page, 1, NULL);
    }
    /* Popup text is checked independently from the obscured dashboard. */
    lv_obj_t *layer = lv_layer_top();
    if (lv_obj_get_child_count(layer)) {
        ui_widget_def_t def = {0}; snprintf(def.id, sizeof(def.id), "popup"); snprintf(def.type, sizeof(def.type), "popup");
        fprintf(out, "{\"widget\":\"popup\",\"type\":\"popup\",\"depth\":0,\"kind\":\"card\",\"x1\":0,\"y1\":0,\"x2\":%d,\"y2\":%d}\n", APP_VIEW_WIDTH - 1, APP_VIEW_HEIGHT - 1);
        dump_object(out, &def, layer, 1, NULL);
    }
    fclose(out);
    return true;
}

static void sim_style_card(ui_widget_instance_t *instance);

/* Room cards of a house page (ui_house_page.c) become simulator widgets like any other. */
typedef struct {
    sim_widget_t *widgets;
    int capacity;
    int *created;
} sim_house_ctx_t;

static bool sim_house_room_cb(const ui_widget_def_t *def, lv_obj_t *parent, void *user)
{
    sim_house_ctx_t *house = user;
    if (*house->created >= house->capacity) {
        return false;
    }
    sim_widget_t *entry = &house->widgets[*house->created];
    entry->json = NULL;
    entry->def = *def;
    entry->type = find_type(entry->def.type);
    if (entry->type == NULL) {
        return false;
    }
    init_instance(&entry->def, &entry->instance);
    ui_fonts_tile_begin(entry->def.type, entry->def.w, entry->def.h);
    const esp_err_t create_err = entry->type->create(&entry->def, parent, &entry->instance);
    ui_fonts_tile_end();
    if (create_err != ESP_OK) {
        return false;
    }
    sim_style_card(&entry->instance);
    (*house->created)++;
    return true;
}

/* Optional "notification" object of the fixture: opens the real notification card on top, as
 * POST /api/notifications or a layout alert would (the gallery's doorbell popup). A camera's
 * "entity_picture" may be "file:<path>" to a local PNG, see host_adapters.c. */
static void show_fixture_notification(const cJSON *json)
{
    if (!cJSON_IsObject(json)) {
        return;
    }
    app_notification_t *n = calloc(1, sizeof(*n));
    if (n == NULL) {
        return;
    }
    copy_field(n->title, sizeof(n->title), json, "title");
    copy_field(n->message, sizeof(n->message), json, "message");
    copy_field(n->message_id, sizeof(n->message_id), json, "message_id");
    copy_field(n->action_entity_id, sizeof(n->action_entity_id), json, "action_entity_id");
    copy_field(n->action_label, sizeof(n->action_label), json, "action_label");
    copy_field(n->camera_entity_id, sizeof(n->camera_entity_id), json, "camera_entity_id");
    const cJSON *sound = cJSON_GetObjectItemCaseSensitive(json, "sound");
    if (cJSON_IsString(sound)) (void)chime_kind_parse(sound->valuestring, &n->sound);
    ui_notification_popup_show(n); /* takes ownership */
}

static void show_fixture_history(const cJSON *json)
{
    if (!cJSON_IsObject(json)) return;
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "range"))) { /* the stock chip's popup: 30 days, a tap on "12 m" shows the year */
        ui_history_popup_open_range(json_string(json, "entity_id"), json_string(json, "title"), json_string(json, "current"));
        return;
    }
    ui_history_popup_open(json_string(json, "entity_id"), json_string(json, "title"),
                          json_string(json, "current"),
                          cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "numeric")),
                          json_string(json, "device_class"));
}

/* The card surface that ui_widget_factory_create()/_apply_state() add around every widget; the
 * simulator calls the widgets directly, so it has to repeat it or every style renders the same card. */
static void sim_style_card(ui_widget_instance_t *instance)
{
    ui_style_apply_card(instance->obj);
}

int main(int argc, char **argv)
{
    extern int sim_wallbox_regression(void);
    bool wallbox_regression = false;
    extern int sim_controls_regression(void);
    bool controls_regression = false;
    extern int sim_bambu_regression(void);
    extern int sim_history_regression(void);
    extern int sim_energy_regression(void);
    extern int sim_font_budget_regression(void);
    extern int sim_notification_regression(void);
    bool notification_regression = false;
    extern int sim_price_regression(void);
    extern int sim_parqet_regression(void);
    bool parqet_regression = false;
    bool price_regression = false;
    bool font_budget_regression = false;
    bool energy_regression = false;
    bool bambu_regression = false;
    bool history_regression = false;
    const char *fixture_path = SIM_DEFAULT_FIXTURE;
    const char *page_id = NULL;
    const char *theme_id = "dark_v2";
    const char *style_name = ui_style_name(UI_STYLE_DEFAULT);
    const char *language = "en";
    const char *screenshot_path = NULL;
    const char *layout_path = NULL;
    int exit_after_ms = 0;
    int rotation = -1;
    lv_point_t taps[4];
    int tap_count = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (i + 1 >= argc) {
            print_usage(argv[0]);
            return 2;
        }
        const char *value = argv[++i];
        if (strcmp(argv[i - 1], "--regression") == 0 && strcmp(value, "wallbox") == 0) wallbox_regression = true;
            else if (strcmp(argv[i - 1], "--regression") == 0 && strcmp(value, "bambu") == 0) bambu_regression = true;
        else if (strcmp(argv[i - 1], "--regression") == 0 && strcmp(value, "history") == 0) history_regression = true;
        else if (strcmp(argv[i - 1], "--regression") == 0 && strcmp(value, "energy") == 0) energy_regression = true;
        else if (strcmp(argv[i - 1], "--regression") == 0 && strcmp(value, "controls") == 0) controls_regression = true;
        else if (strcmp(argv[i - 1], "--regression") == 0 && strcmp(value, "notification") == 0) notification_regression = true;
        else if (strcmp(argv[i - 1], "--regression") == 0 && strcmp(value, "price") == 0) price_regression = true;
        else if (strcmp(argv[i - 1], "--regression") == 0 && strcmp(value, "parqet") == 0) parqet_regression = true;
        else if (strcmp(argv[i - 1], "--regression") == 0 && strcmp(value, "fonts") == 0) font_budget_regression = true;
        else if (strcmp(argv[i - 1], "--fixture") == 0) fixture_path = value;
        else if (strcmp(argv[i - 1], "--page") == 0) page_id = value;
        else if (strcmp(argv[i - 1], "--theme") == 0) theme_id = value;
        else if (strcmp(argv[i - 1], "--rotation") == 0) {
            if (!strcmp(value,"0")) rotation = 0;
            else if (!strcmp(value,"90")) rotation = 90;
            else if (!strcmp(value,"180")) rotation = 180;
            else if (!strcmp(value,"270")) rotation = 270;
            else return 2;
        }
        else if (strcmp(argv[i - 1], "--text-step") == 0) {
            if (!strcmp(value, "0")) ui_fonts_set_user_step(0);
            else if (!strcmp(value, "1")) ui_fonts_set_user_step(1);
            else if (!strcmp(value, "2")) ui_fonts_set_user_step(2);
            else return 2;
        }
        else if (strcmp(argv[i - 1], "--text-cap") == 0) {
            if (value[0] < '0' || value[0] > '3' || value[1] != '\0') return 2;
            ui_fonts_set_cap_override(value[0] - '0');
        }
        else if (strcmp(argv[i - 1], "--tap") == 0) {
            int tx = 0, ty = 0;
            if (tap_count >= 4 || sscanf(value, "%d,%d", &tx, &ty) != 2) return 2;
            taps[tap_count].x = tx;
            taps[tap_count].y = ty;
            tap_count++;
        }
        else if (strcmp(argv[i - 1], "--style") == 0) style_name = value;
        else if (strcmp(argv[i - 1], "--language") == 0) language = value;
        else if (strcmp(argv[i - 1], "--screenshot") == 0) screenshot_path = value;
        else if (strcmp(argv[i - 1], "--dump-layout") == 0) layout_path = value;
        else if (strcmp(argv[i - 1], "--exit-after-ms") == 0) {
            char *end = NULL;
            long parsed = strtol(value, &end, 10);
            if (end == value || *end != '\0' || parsed < 1 || parsed > INT_MAX) {
                print_usage(argv[0]);
                return 2;
            }
            exit_after_ms = (int)parsed;
        }
        else {
            print_usage(argv[0]);
            return 2;
        }
    }
    ui_style_kind_t style_kind;
    if (!ui_style_lookup(style_name, &style_kind)) {
        print_usage(argv[0]);
        return 2;
    }

    if (rotation >= 0 && !display_geometry_set_rotation(rotation)) return 2;
    char *fixture_text = read_text_file(fixture_path);
    if (fixture_text == NULL) return 1;
    cJSON *root = cJSON_Parse(fixture_text);
    free(fixture_text);
    if (root == NULL) {
        fprintf(stderr, "Invalid JSON in %s\n", fixture_path);
        return 1;
    }
    sim_host_set_online(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ha_online")));
#if defined(CONFIG_APP_PANEL_VARIANT_TAB5_1280)
    if (!layout_orientation_project(root, display_geometry_portrait())) { cJSON_Delete(root); return 1; }
#endif
    const cJSON *energy_json = cJSON_GetObjectItemCaseSensitive(root, "energy_snapshot");
    ha_energy_snapshot_t snapshot = {0};
#define ENERGY_BOOL(key) snapshot.key = json_bool(energy_json, #key, false)
#define ENERGY_NUMBER(key) snapshot.key = json_float(energy_json, #key, 0)
    ENERGY_BOOL(available); ENERGY_BOOL(has_grid); ENERGY_BOOL(has_solar);
    ENERGY_BOOL(has_battery); ENERGY_BOOL(has_gas); ENERGY_BOOL(has_water);
    ENERGY_NUMBER(from_grid_kwh); ENERGY_NUMBER(to_grid_kwh); ENERGY_NUMBER(solar_kwh);
    ENERGY_NUMBER(to_battery_kwh); ENERGY_NUMBER(from_battery_kwh);
    ENERGY_NUMBER(gas_value); ENERGY_NUMBER(water_value);
    copy_field(snapshot.gas_unit, sizeof(snapshot.gas_unit), energy_json, "gas_unit");
    copy_field(snapshot.water_unit, sizeof(snapshot.water_unit), energy_json, "water_unit");
    sim_host_energy_set(&snapshot);
#undef ENERGY_BOOL
#undef ENERGY_NUMBER
    const cJSON *pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
    const cJSON *page = NULL;
    cJSON_ArrayForEach(page, pages) {
        if (page_id == NULL || strcmp(json_string(page, "id"), page_id) == 0) break;
    }
    if (!cJSON_IsObject(page)) {
        fprintf(stderr, "Page %s not found in %s\n", page_id != NULL ? page_id : "(first)", fixture_path);
        cJSON_Delete(root);
        return 1;
    }
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(page, "ha_online"))) sim_host_set_online(true); /* before any widget exists */
    const cJSON *widgets_json = cJSON_GetObjectItemCaseSensitive(page, "widgets");
    const char *page_type = json_string(page, "type");
    const bool house_page = strcmp(page_type, "house_dashboard") == 0;
    const bool energy_page = strcmp(page_type, "energy_dashboard") == 0;
    if (!cJSON_IsArray(widgets_json) && !house_page && !energy_page) {
        fprintf(stderr, "Selected page has no widgets array\n");
        cJSON_Delete(root);
        return 1;
    }

    if (theme_palette_activate_by_id(theme_id) != ESP_OK) {
        fprintf(stderr, "Unknown built-in theme: %s\n", theme_id);
        cJSON_Delete(root);
        return 2;
    }
    ui_style_set(style_kind);
    if (ui_i18n_init(language) != ESP_OK) {
        fprintf(stderr, "Cannot initialize language %s\n", language);
        cJSON_Delete(root);
        return 1;
    }
    lv_init();
    if (!check_text_font_coverage()) {
        cJSON_Delete(root);
        return 1;
    }
    lv_display_t *display = lv_sdl_window_create(APP_VIEW_WIDTH, APP_VIEW_HEIGHT);
    if (display == NULL) {
        fprintf(stderr, "SDL display creation failed\n");
        cJSON_Delete(root);
        return 1;
    }
    if (sim_clock_fixed()) {
        /* after lv_sdl_window_create(), which sets SDL's real tick: animations follow the virtual time (sim_clock.h) */
        lv_tick_set_cb(sim_virtual_tick);
        lv_delay_set_cb(sim_virtual_delay);
    }
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_sdl_window_set_title(display, "BETTA widget simulator");
    lv_sdl_mouse_create();
    theme_default_init();
    if (wallbox_regression) { cJSON_Delete(root); return sim_wallbox_regression(); }
    if (controls_regression) {
        cJSON_Delete(root);
        return sim_controls_regression();
    }
    if (bambu_regression) {
        cJSON_Delete(root);
        return sim_bambu_regression();
    }
    if (history_regression) {
        cJSON_Delete(root);
        return sim_history_regression();
    }
    if (energy_regression) {
        cJSON_Delete(root);
        return sim_energy_regression();
    }
    if (notification_regression) {
        cJSON_Delete(root);
        return sim_notification_regression();
    }
    if (price_regression) {
        cJSON_Delete(root);
        return sim_price_regression();
    }
    if (parqet_regression) {
        cJSON_Delete(root);
        return sim_parqet_regression();
    }
    if (font_budget_regression) {
        cJSON_Delete(root);
        return sim_font_budget_regression();
    }

    const cJSON *sessions_fixture;
    cJSON_ArrayForEach(sessions_fixture, cJSON_GetObjectItemCaseSensitive(root, "evcc_sessions")) {
        ha_evcc_sessions_seed(json_string(sessions_fixture, "mode_entity_id"), json_string(sessions_fixture, "loadpoint"), cJSON_GetObjectItemCaseSensitive(sessions_fixture, "sessions"));
    }
    /* The firmware's own top bar, navigation and page containers (ui_pages.c), so the frame is the real one. */
    lv_obj_t *screen = lv_screen_active();
    ui_pages_init();
    lv_obj_t *content = NULL;
    /* The navigation holds APP_MAX_PAGES pages: a fixture with more (the gallery) shows a window that ends at the selected one. */
    int selected_index = 0;
    {
        const cJSON *walk = NULL;
        int i = 0;
        cJSON_ArrayForEach(walk, pages) { if (walk == page) selected_index = i; i++; }
    }
    const int first_nav_page = selected_index >= APP_MAX_PAGES ? selected_index - APP_MAX_PAGES + 1 : 0;
    const cJSON *nav_page = NULL;
    int nav_index = -1;
    cJSON_ArrayForEach(nav_page, pages) {
        if (++nav_index < first_nav_page) continue;
        lv_obj_t *container = ui_pages_add(json_string(nav_page, "id"), json_string(nav_page, "title"));
        if (container == NULL) continue;
        const char *type = json_string(nav_page, "type");
        const char *icon = json_string(nav_page, "icon");
        if (icon[0] == '\0') icon = strcmp(type, "house_dashboard") == 0 ? "house"
                                    : strcmp(type, "energy_dashboard") == 0 ? "energy" : "";
        ui_pages_set_icon(json_string(nav_page, "id"), icon);
        if (nav_page == page) content = container;
    }
    if (content == NULL) {
        fprintf(stderr, "Cannot create the page container\n");
        cJSON_Delete(root);
        return 1;
    }

    int count = cJSON_GetArraySize(widgets_json);
    if (count < APP_MAX_WIDGETS_PER_PAGE) {
        count = APP_MAX_WIDGETS_PER_PAGE; /* a house page creates its room cards itself */
    }
    sim_widget_t *widgets = calloc((size_t)count, sizeof(*widgets));
    if (widgets == NULL) {
        fprintf(stderr, "Cannot allocate %d widgets\n", count);
        cJSON_Delete(root);
        return 1;
    }
    int created = 0;
    int skipped = 0;
    const cJSON *widget_json = NULL;
    cJSON_ArrayForEach(widget_json, widgets_json) {
        sim_widget_t *entry = &widgets[created];
        entry->json = widget_json;
        parse_widget_def(widget_json, &entry->def);
        entry->type = find_type(entry->def.type);
        if (entry->type == NULL) {
            fprintf(stderr, "Skipping unsupported widget: %s (%s)\n", entry->def.type, entry->def.id);
            skipped++;
            continue;
        }
        init_instance(&entry->def, &entry->instance);
        ui_fonts_tile_begin(entry->def.type, entry->def.w, entry->def.h);
        const esp_err_t content_err = entry->type->create(&entry->def, content, &entry->instance);
        ui_fonts_tile_end();
        if (content_err != ESP_OK) {
            fprintf(stderr, "Cannot create widget: %s (%s)\n", entry->def.type, entry->def.id);
            skipped++;
            continue;
        }
        sim_style_card(&entry->instance);
        created++;
    }
    sim_house_ctx_t house = {.widgets = widgets, .capacity = count, .created = &created};
    if (house_page) {
        ui_house_page_build(page, content, sim_house_room_cb, &house);
    }
    ui_energy_page_instance_t energy = {0};
    bool have_energy = false;
    if (energy_page) {
        ui_energy_page_config_t config;
        ui_energy_page_config_from_json(page, json_string(page, "id"), json_string(page, "title"), &config);
        have_energy = ui_energy_page_create(&config, content, &energy) == ESP_OK;
        if (!have_energy) {
            fprintf(stderr, "Cannot create the energy page\n");
            skipped++;
        }
    }

    load_parqet_perf(root);
    const cJSON *states = cJSON_GetObjectItemCaseSensitive(root, "states");
    if (!load_display_precisions(states)) {
        fprintf(stderr, "Cannot load display precision metadata\n");
        free(widgets);
        cJSON_Delete(root);
        return 1;
    }
    const cJSON *state_json = NULL;
    cJSON_ArrayForEach(state_json, states) {
        ha_state_t state;
        if (parse_state(state_json, &state)) sim_host_state_add(&state);
    }
    cJSON_ArrayForEach(state_json, states) {
        ha_state_t state;
        if (!parse_state(state_json, &state)) continue;
        if (have_energy) {
            ui_energy_page_apply_state(&energy, &state);
        }
        for (int i = 0; i < created; i++) {
            if (widget_uses_entity(&widgets[i].def, state.entity_id)) {
                ui_fonts_tile_begin(widgets[i].def.type, widgets[i].def.w, widgets[i].def.h);
                widgets[i].type->apply(&widgets[i].instance, &state);
                ui_fonts_tile_end();
                sim_style_card(&widgets[i].instance);
            }
        }
    }
    for (int i = 0; i < created; i++) {
        if (strcmp(widgets[i].def.type, "todo_list") != 0) continue;
        const char *summaries[16];
        bool completed[16];
        size_t n = 0;
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(widgets[i].json, "items")) {
            if (n >= 16) break;
            summaries[n] = json_string(item, "summary");
            completed[n] = json_bool(item, "completed", false);
            n++;
        }
        if (n > 0) w_todo_sim_seed(&widgets[i].instance, summaries, completed, n);
    }
    for (int i = 0; i < created; i++) {
        if (strcmp(widgets[i].def.type, "graph") != 0) continue;
        const cJSON *history = cJSON_GetObjectItemCaseSensitive(widgets[i].json, "history");
        float values[64];
        size_t n = 0;
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, history) {
            if (cJSON_IsNumber(item) && n < sizeof(values) / sizeof(values[0])) {
                values[n++] = (float)item->valuedouble;
            }
        }
        int step_min = json_int(widgets[i].json, "history_step_minutes", 15);
        if (step_min < 1 || step_min > 1440) step_min = 15;
        if (n > 0) w_graph_sim_seed(&widgets[i].instance, values, n, (uint32_t)step_min);
    }
    fprintf(stdout, "Page %s: %d widget(s) rendered, %d skipped; %dx%d, theme %s, style %s\n",
            json_string(page, "id"), created, skipped, APP_VIEW_WIDTH, APP_VIEW_HEIGHT,
            theme_id, style_name);
    fflush(stdout);
    ui_pages_show(json_string(page, "id"));
    /* The panel marks the shown page's widgets visible once laid out; the media player fetches its cover art
     * (deferred while hidden) only then. */
    for (int n = 0; n < 6; n++) { lv_timer_handler(); lv_delay_ms(5); }
    for (int i = 0; i < created; i++) {
        const bool vacuum = strcmp(widgets[i].def.type, "vacuum_tile") == 0;
        if (strcmp(widgets[i].def.type, "media_player") != 0 && !vacuum) continue;
        if (vacuum) w_vacuum_set_visible(&widgets[i].instance, true);
        else w_media_player_set_visible(&widgets[i].instance, true);
        cJSON_ArrayForEach(state_json, states) {
            ha_state_t state;
            if (parse_state(state_json, &state) && widget_uses_entity(&widgets[i].def, state.entity_id)) {
                widgets[i].type->apply(&widgets[i].instance, &state);
            }
        }
    }
    simulator_frame_apply(cJSON_GetObjectItemCaseSensitive(root, "frame"));
    show_fixture_notification(cJSON_GetObjectItemCaseSensitive(root, "notification"));
    show_fixture_history(cJSON_GetObjectItemCaseSensitive(root, "history_popup"));
    const char *sessions_popup = json_string(root, "evcc_sessions_popup");
    if (*sessions_popup) ui_history_popup_open_evcc(sessions_popup);
    /* "tap_widget": "<id>" clicks that widget once, as a finger would (a popup opens, ...). */
    const char *tap_id = json_string(root, "tap_widget");
    for (int i = 0; tap_id[0] != '\0' && i < created; i++) {
        if (strcmp(widgets[i].def.id, tap_id) == 0) lv_obj_send_event(widgets[i].instance.obj, LV_EVENT_CLICKED, NULL);
    }
    /* "open_popup": "wifi" | "ha" | "mqtt" opens the top bar's connectivity popup (data comes from pages_stubs.c). */
    const char *popup = json_string(root, "open_popup");
    if (strcmp(popup, "wifi") == 0) ui_conn_popup_show_wifi();
    else if (strcmp(popup, "ha") == 0) ui_conn_popup_show_ha();
    else if (strcmp(popup, "mqtt") == 0) ui_conn_popup_show_mqtt();
    /* Public demo credentials render the real boot splash, matching app_show_wifi_setup_screen(). */
    const cJSON *setup = cJSON_GetObjectItemCaseSensitive(root, "setup_screen");
    if (cJSON_IsObject(setup)) {
        ui_boot_splash_show();
        ui_boot_splash_set_title(ui_i18n_get("boot.wifi_setup_title", "Wi-Fi Setup"));
        ui_boot_splash_set_status_layout(true, 520, 0);
        ui_boot_splash_clear_status();
        ui_boot_splash_set_progress(100);
        ui_boot_splash_set_status(ui_i18n_get("boot.wifi_credentials_missing", "Wi-Fi credentials missing"));
        char line[64];
        snprintf(line, sizeof(line), "%s: %s", ui_i18n_get("topbar.ap", "AP"), json_string(setup, "ssid"));
        ui_boot_splash_set_status(line);
        snprintf(line, sizeof(line), "%s: %s", ui_i18n_get("boot.setup_ap_password", "Password"), json_string(setup, "password"));
        ui_boot_splash_set_status(line);
        ui_boot_splash_set_status(ui_i18n_get("boot.open_editor", "Open BETTA Editor:"));
        snprintf(line, sizeof(line), "http://%s", json_string(setup, "ip"));
        ui_boot_splash_set_status(line);
    }
    cJSON_Delete(root);

    /* --tap: a click on whatever is at the screen point (popups first), to render popups that open on a tap. */
    for (int t = 0; t < tap_count; t++) {
        for (int n = 0; n < 12; n++) { lv_timer_handler(); lv_delay_ms(5); }
        lv_point_t point = taps[t];
        lv_obj_t *target = lv_indev_search_obj(lv_layer_top(), &point);
        if (target == NULL) { point = taps[t]; target = lv_indev_search_obj(lv_screen_active(), &point); }
        if (target != NULL) lv_obj_send_event(target, LV_EVENT_CLICKED, NULL);
    }
    if (tap_count > 0) for (int n = 0; n < 12; n++) { lv_timer_handler(); lv_delay_ms(5); }
    if (screenshot_path != NULL && exit_after_ms == 0) exit_after_ms = 250;
    int64_t deadline_us = exit_after_ms > 0 ? esp_timer_get_time() + (int64_t)exit_after_ms * 1000 : INT64_MAX;
    while (esp_timer_get_time() < deadline_us) { /* virtual time on the fixed clock: lv_delay_ms() advances it */
        lv_timer_handler();
        lv_delay_ms(5);
    }
    if (screenshot_path != NULL && !save_screenshot(display, screenshot_path)) {
        free(widgets);
        return 1;
    }
    lv_obj_update_layout(lv_layer_top());
    if (layout_path != NULL && !dump_layout(layout_path, widgets, created, have_energy ? energy.obj : NULL)) {
        free(widgets);
        return 1;
    }
    lv_obj_clean(screen);
    free(widgets);
    return 0;
}
