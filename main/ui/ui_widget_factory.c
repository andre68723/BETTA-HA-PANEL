/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/text_font.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_widget_factory.h"

#include <stdio.h>
#include <string.h>

esp_err_t w_sensor_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_sensor_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_sensor_mark_unavailable(ui_widget_instance_t *instance);

esp_err_t w_button_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_button_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_button_mark_unavailable(ui_widget_instance_t *instance);

esp_err_t w_slider_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_slider_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_slider_mark_unavailable(ui_widget_instance_t *instance);

esp_err_t w_gauge_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_gauge_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_gauge_mark_unavailable(ui_widget_instance_t *instance);

esp_err_t w_graph_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_graph_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_graph_mark_unavailable(ui_widget_instance_t *instance);

esp_err_t w_empty_tile_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_empty_tile_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_empty_tile_mark_unavailable(ui_widget_instance_t *instance);

esp_err_t w_light_tile_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_light_tile_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_light_tile_set_visible(ui_widget_instance_t *instance, bool visible);
void w_light_tile_mark_unavailable(ui_widget_instance_t *instance);

esp_err_t w_heating_tile_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_heating_tile_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_heating_tile_mark_unavailable(ui_widget_instance_t *instance);

esp_err_t w_weather_tile_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_weather_tile_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_weather_tile_mark_unavailable(ui_widget_instance_t *instance);

esp_err_t w_solar_forecast_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_solar_forecast_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_solar_forecast_mark_unavailable(ui_widget_instance_t *instance);
esp_err_t w_parqet_portfolio_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
esp_err_t w_stock_quotes_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_stock_quotes_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_stock_quotes_mark_unavailable(ui_widget_instance_t *instance);
void w_parqet_portfolio_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_parqet_portfolio_mark_unavailable(ui_widget_instance_t *instance);
esp_err_t w_energy_price_forecast_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_energy_price_forecast_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_energy_price_forecast_mark_unavailable(ui_widget_instance_t *instance);

esp_err_t w_cover_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_cover_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_cover_mark_unavailable(ui_widget_instance_t *instance);

esp_err_t w_fan_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_fan_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_fan_mark_unavailable(ui_widget_instance_t *instance);

esp_err_t w_camera_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_camera_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_camera_mark_unavailable(ui_widget_instance_t *instance);
void w_camera_set_visible(ui_widget_instance_t *instance, bool visible);

esp_err_t w_contact_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_contact_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_contact_mark_unavailable(ui_widget_instance_t *instance);

esp_err_t w_scene_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_scene_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_scene_mark_unavailable(ui_widget_instance_t *instance);
esp_err_t w_lock_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_lock_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_lock_mark_unavailable(ui_widget_instance_t *instance);
esp_err_t w_alarm_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_alarm_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_alarm_mark_unavailable(ui_widget_instance_t *instance);
esp_err_t w_energy_timeline_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_energy_timeline_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_energy_timeline_mark_unavailable(ui_widget_instance_t *instance);
esp_err_t w_select_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_select_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_select_mark_unavailable(ui_widget_instance_t *instance);
esp_err_t w_datetime_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_datetime_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_datetime_mark_unavailable(ui_widget_instance_t *instance);
esp_err_t w_binary_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_binary_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_binary_mark_unavailable(ui_widget_instance_t *instance);
esp_err_t w_room_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_room_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_room_mark_unavailable(ui_widget_instance_t *instance);
esp_err_t w_climate_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_climate_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_climate_mark_unavailable(ui_widget_instance_t *instance);

esp_err_t w_wallbox_evcc_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_wallbox_evcc_apply_state(ui_widget_instance_t *, const ha_state_t *);
void w_wallbox_evcc_mark_unavailable(ui_widget_instance_t *);
void w_wallbox_evcc_set_visible(ui_widget_instance_t *, bool);

esp_err_t w_bambu_printer_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_bambu_printer_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_bambu_printer_mark_unavailable(ui_widget_instance_t *instance);
void w_bambu_printer_set_visible(ui_widget_instance_t *instance, bool visible);

esp_err_t w_car_widget_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_car_widget_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_car_widget_mark_unavailable(ui_widget_instance_t *instance);

esp_err_t w_todo_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_todo_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_todo_mark_unavailable(ui_widget_instance_t *instance);

esp_err_t w_media_player_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_media_player_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_media_player_mark_unavailable(ui_widget_instance_t *instance);
void w_media_player_set_visible(ui_widget_instance_t *instance, bool visible);

esp_err_t w_sonos_favorites_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_sonos_favorites_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_sonos_favorites_mark_unavailable(ui_widget_instance_t *instance);

esp_err_t w_vacuum_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void w_vacuum_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void w_vacuum_mark_unavailable(ui_widget_instance_t *instance);
void w_vacuum_set_visible(ui_widget_instance_t *instance, bool visible);

static esp_err_t ui_widget_factory_create_inner(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_instance, 0, sizeof(*out_instance));
    snprintf(out_instance->id, sizeof(out_instance->id), "%s", def->id);
    snprintf(out_instance->type, sizeof(out_instance->type), "%s", def->type);
    snprintf(out_instance->title, sizeof(out_instance->title), "%s", def->title);
    snprintf(out_instance->entity_id, sizeof(out_instance->entity_id), "%s", def->entity_id);
    snprintf(out_instance->secondary_entity_id, sizeof(out_instance->secondary_entity_id), "%s", def->secondary_entity_id);
    snprintf(out_instance->tertiary_entity_id, sizeof(out_instance->tertiary_entity_id), "%s", def->tertiary_entity_id);
    for (int i = 0; i < UI_WIDGET_EXTRA_ENTITIES; i++) {
        snprintf(out_instance->extra_entity_id[i], sizeof(out_instance->extra_entity_id[i]), "%s", def->extra_entity_id[i]);
    }
    snprintf(out_instance->forecast_today_entity_id, sizeof(out_instance->forecast_today_entity_id), "%s", def->forecast_today_entity_id);
    snprintf(out_instance->forecast_tomorrow_entity_id, sizeof(out_instance->forecast_tomorrow_entity_id), "%s", def->forecast_tomorrow_entity_id);
    snprintf(out_instance->forecast_day_3_entity_id, sizeof(out_instance->forecast_day_3_entity_id), "%s", def->forecast_day_3_entity_id);
    snprintf(out_instance->forecast_day_4_entity_id, sizeof(out_instance->forecast_day_4_entity_id), "%s", def->forecast_day_4_entity_id);
    snprintf(out_instance->forecast_day_5_entity_id, sizeof(out_instance->forecast_day_5_entity_id), "%s", def->forecast_day_5_entity_id);
    snprintf(out_instance->forecast_day_6_entity_id, sizeof(out_instance->forecast_day_6_entity_id), "%s", def->forecast_day_6_entity_id);
    snprintf(out_instance->forecast_day_7_entity_id, sizeof(out_instance->forecast_day_7_entity_id), "%s", def->forecast_day_7_entity_id);
    out_instance->solar_forecast_bar_max_kwh = def->solar_forecast_bar_max_kwh;
    snprintf(out_instance->solar_forecast_bar_orientation, sizeof(out_instance->solar_forecast_bar_orientation), "%s", def->solar_forecast_bar_orientation);
    snprintf(out_instance->slider_direction, sizeof(out_instance->slider_direction), "%s", def->slider_direction);
    snprintf(out_instance->slider_accent_color, sizeof(out_instance->slider_accent_color), "%s", def->slider_accent_color);
    snprintf(out_instance->button_accent_color, sizeof(out_instance->button_accent_color), "%s", def->button_accent_color);
    snprintf(out_instance->button_mode, sizeof(out_instance->button_mode), "%s", def->button_mode);
    snprintf(out_instance->graph_line_color, sizeof(out_instance->graph_line_color), "%s", def->graph_line_color);
    out_instance->energy_palette = def->energy_palette;
    out_instance->graph_point_count = def->graph_point_count;
    out_instance->graph_time_window_min = def->graph_time_window_min;
    snprintf(out_instance->graph_display_mode, sizeof(out_instance->graph_display_mode), "%s", def->graph_display_mode);
    out_instance->graph_bar_bucket_min = def->graph_bar_bucket_min;
    out_instance->graph_ha_history = def->graph_ha_history;
    snprintf(out_instance->style_variant, sizeof(out_instance->style_variant), "%s", def->style_variant);
    snprintf(out_instance->arc_opening, sizeof(out_instance->arc_opening), "%s", def->arc_opening);
    snprintf(out_instance->display_mode, sizeof(out_instance->display_mode), "%s", def->display_mode);
    out_instance->gauge_min = def->gauge_min;
    out_instance->gauge_max = def->gauge_max;
    snprintf(out_instance->gauge_bar_style, sizeof(out_instance->gauge_bar_style), "%s", def->gauge_bar_style);
    out_instance->gauge_segment_count = def->gauge_segment_count;
    out_instance->hide_name = def->hide_name;
    out_instance->sensor_show_age = def->sensor_show_age;
    out_instance->lock_confirm = def->lock_confirm;
    out_instance->alarm_actions = def->alarm_actions;
    out_instance->history_popup = def->history_popup;
    out_instance->ctx = NULL;

    if (strcmp(def->type, "sensor") == 0) {
        return w_sensor_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "button") == 0) {
        return w_button_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "slider") == 0) {
        return w_slider_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "gauge") == 0) {
        return w_gauge_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "graph") == 0) {
        return w_graph_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "empty_tile") == 0) {
        return w_empty_tile_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "light_tile") == 0) {
        return w_light_tile_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "heating_tile") == 0) {
        return w_heating_tile_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "weather_tile") == 0 || strcmp(def->type, "weather_3day") == 0) {
        return w_weather_tile_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "solar_forecast") == 0) {
        return w_solar_forecast_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "stock_quotes") == 0) {
        return w_stock_quotes_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "parqet_portfolio") == 0) {
        return w_parqet_portfolio_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "energy_price_forecast") == 0) {
        return w_energy_price_forecast_create(def, parent, out_instance);
    }
    if (!strcmp(def->type, "wallbox_evcc")) return w_wallbox_evcc_create(def, parent, out_instance);
    if (strcmp(def->type, "bambu_printer") == 0) {
        return w_bambu_printer_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "car_widget") == 0) {
        return w_car_widget_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "cover_tile") == 0) {
        return w_cover_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "fan_tile") == 0) {
        return w_fan_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "camera_tile") == 0) {
        return w_camera_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "contact_tile") == 0) {
        return w_contact_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "climate_tile") == 0) {
        return w_climate_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "room_tile") == 0) {
        return w_room_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "lock_tile") == 0) {
        return w_lock_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "alarm_tile") == 0) {
        return w_alarm_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "select_tile") == 0) {
        return w_select_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "energy_timeline") == 0) {
        return w_energy_timeline_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "datetime_tile") == 0) {
        return w_datetime_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "scene_tile") == 0) {
        return w_scene_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "binary_tile") == 0) {
        return w_binary_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "todo_list") == 0) {
        return w_todo_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "media_player") == 0) {
        return w_media_player_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "sonos_favorites") == 0) {
        return w_sonos_favorites_create(def, parent, out_instance);
    }
    if (strcmp(def->type, "vacuum_tile") == 0) {
        return w_vacuum_create(def, parent, out_instance);
    }
    return ESP_ERR_NOT_SUPPORTED;
}

static void ui_widget_factory_apply_state_inner(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->obj == NULL || state == NULL) {
        return;
    }
    if (strcmp(instance->type, "sensor") == 0) {
        w_sensor_apply_state(instance, state);
    } else if (strcmp(instance->type, "button") == 0) {
        w_button_apply_state(instance, state);
    } else if (strcmp(instance->type, "slider") == 0) {
        w_slider_apply_state(instance, state);
    } else if (strcmp(instance->type, "gauge") == 0) {
        w_gauge_apply_state(instance, state);
    } else if (strcmp(instance->type, "graph") == 0) {
        w_graph_apply_state(instance, state);
    } else if (strcmp(instance->type, "empty_tile") == 0) {
        w_empty_tile_apply_state(instance, state);
    } else if (strcmp(instance->type, "light_tile") == 0) {
        w_light_tile_apply_state(instance, state);
    } else if (strcmp(instance->type, "heating_tile") == 0) {
        w_heating_tile_apply_state(instance, state);
    } else if (strcmp(instance->type, "weather_tile") == 0 || strcmp(instance->type, "weather_3day") == 0) {
        w_weather_tile_apply_state(instance, state);
    } else if (strcmp(instance->type, "solar_forecast") == 0) {
        w_solar_forecast_apply_state(instance, state);
    } else if (strcmp(instance->type, "stock_quotes") == 0) {
        w_stock_quotes_apply_state(instance, state);
    } else if (strcmp(instance->type, "parqet_portfolio") == 0) {
        w_parqet_portfolio_apply_state(instance, state);
    } else if (strcmp(instance->type, "energy_price_forecast") == 0) {
        w_energy_price_forecast_apply_state(instance, state);
    } else if (!strcmp(instance->type, "wallbox_evcc")) {
        w_wallbox_evcc_apply_state(instance, state);
    } else if (strcmp(instance->type, "bambu_printer") == 0) {
        w_bambu_printer_apply_state(instance, state);
    } else if (strcmp(instance->type, "car_widget") == 0) {
        w_car_widget_apply_state(instance, state);
    } else if (strcmp(instance->type, "cover_tile") == 0) {
        w_cover_apply_state(instance, state);
    } else if (strcmp(instance->type, "fan_tile") == 0) {
        w_fan_apply_state(instance, state);
    } else if (strcmp(instance->type, "camera_tile") == 0) {
        w_camera_apply_state(instance, state);
    } else if (strcmp(instance->type, "contact_tile") == 0) {
        w_contact_apply_state(instance, state);
    } else if (strcmp(instance->type, "climate_tile") == 0) {
        w_climate_apply_state(instance, state);
    } else if (strcmp(instance->type, "room_tile") == 0) {
        w_room_apply_state(instance, state);
    } else if (strcmp(instance->type, "lock_tile") == 0) {
        w_lock_apply_state(instance, state);
    } else if (strcmp(instance->type, "alarm_tile") == 0) {
        w_alarm_apply_state(instance, state);
    } else if (strcmp(instance->type, "select_tile") == 0) {
        w_select_apply_state(instance, state);
    } else if (strcmp(instance->type, "energy_timeline") == 0) {
        w_energy_timeline_apply_state(instance, state);
    } else if (strcmp(instance->type, "datetime_tile") == 0) {
        w_datetime_apply_state(instance, state);
    } else if (strcmp(instance->type, "scene_tile") == 0) {
        w_scene_apply_state(instance, state);
    } else if (strcmp(instance->type, "binary_tile") == 0) {
        w_binary_apply_state(instance, state);
    } else if (strcmp(instance->type, "todo_list") == 0) {
        w_todo_apply_state(instance, state);
    } else if (strcmp(instance->type, "media_player") == 0) {
        w_media_player_apply_state(instance, state);
    } else if (strcmp(instance->type, "sonos_favorites") == 0) {
        w_sonos_favorites_apply_state(instance, state);
    } else if (strcmp(instance->type, "vacuum_tile") == 0) {
        w_vacuum_apply_state(instance, state);
    }
}

/* The style's card surface goes on after creation and after every state update, because the
 * widgets restyle their own background (ui_style_apply_card()). */
esp_err_t ui_widget_factory_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    ui_fonts_tile_begin(def != NULL ? def->type : NULL, def != NULL ? def->w : 0, def != NULL ? def->h : 0);
    esp_err_t err = ui_widget_factory_create_inner(def, parent, out_instance);
    ui_fonts_tile_end();
    if (err == ESP_OK && out_instance != NULL) {
        ui_style_apply_card(out_instance->obj);
    }
    return err;
}

void ui_widget_factory_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    const bool tile = instance != NULL && instance->obj != NULL;
    if (tile) ui_fonts_tile_begin(instance->type, (int)lv_obj_get_width(instance->obj), (int)lv_obj_get_height(instance->obj));
    ui_widget_factory_apply_state_inner(instance, state);
    if (tile) ui_fonts_tile_end();
    if (instance != NULL) {
        ui_style_apply_card(instance->obj);
    }
}

static void ui_widget_factory_mark_unavailable_inner(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->obj == NULL) {
        return;
    }
    if (strcmp(instance->type, "sensor") == 0) {
        w_sensor_mark_unavailable(instance);
    } else if (strcmp(instance->type, "button") == 0) {
        w_button_mark_unavailable(instance);
    } else if (strcmp(instance->type, "slider") == 0) {
        w_slider_mark_unavailable(instance);
    } else if (strcmp(instance->type, "gauge") == 0) {
        w_gauge_mark_unavailable(instance);
    } else if (strcmp(instance->type, "graph") == 0) {
        w_graph_mark_unavailable(instance);
    } else if (strcmp(instance->type, "empty_tile") == 0) {
        w_empty_tile_mark_unavailable(instance);
    } else if (strcmp(instance->type, "light_tile") == 0) {
        w_light_tile_mark_unavailable(instance);
    } else if (strcmp(instance->type, "heating_tile") == 0) {
        w_heating_tile_mark_unavailable(instance);
    } else if (strcmp(instance->type, "weather_tile") == 0 || strcmp(instance->type, "weather_3day") == 0) {
        w_weather_tile_mark_unavailable(instance);
    } else if (strcmp(instance->type, "solar_forecast") == 0) {
        w_solar_forecast_mark_unavailable(instance);
    } else if (strcmp(instance->type, "stock_quotes") == 0) {
        w_stock_quotes_mark_unavailable(instance);
    } else if (strcmp(instance->type, "parqet_portfolio") == 0) {
        w_parqet_portfolio_mark_unavailable(instance);
    } else if (strcmp(instance->type, "energy_price_forecast") == 0) {
        w_energy_price_forecast_mark_unavailable(instance);
    } else if (!strcmp(instance->type, "wallbox_evcc")) {
        w_wallbox_evcc_mark_unavailable(instance);
    } else if (strcmp(instance->type, "bambu_printer") == 0) {
        w_bambu_printer_mark_unavailable(instance);
    } else if (strcmp(instance->type, "car_widget") == 0) {
        w_car_widget_mark_unavailable(instance);
    } else if (strcmp(instance->type, "cover_tile") == 0) {
        w_cover_mark_unavailable(instance);
    } else if (strcmp(instance->type, "fan_tile") == 0) {
        w_fan_mark_unavailable(instance);
    } else if (strcmp(instance->type, "camera_tile") == 0) {
        w_camera_mark_unavailable(instance);
    } else if (strcmp(instance->type, "contact_tile") == 0) {
        w_contact_mark_unavailable(instance);
    } else if (strcmp(instance->type, "climate_tile") == 0) {
        w_climate_mark_unavailable(instance);
    } else if (strcmp(instance->type, "room_tile") == 0) {
        w_room_mark_unavailable(instance);
    } else if (strcmp(instance->type, "lock_tile") == 0) {
        w_lock_mark_unavailable(instance);
    } else if (strcmp(instance->type, "alarm_tile") == 0) {
        w_alarm_mark_unavailable(instance);
    } else if (strcmp(instance->type, "select_tile") == 0) {
        w_select_mark_unavailable(instance);
    } else if (strcmp(instance->type, "energy_timeline") == 0) {
        w_energy_timeline_mark_unavailable(instance);
    } else if (strcmp(instance->type, "datetime_tile") == 0) {
        w_datetime_mark_unavailable(instance);
    } else if (strcmp(instance->type, "scene_tile") == 0) {
        w_scene_mark_unavailable(instance);
    } else if (strcmp(instance->type, "binary_tile") == 0) {
        w_binary_mark_unavailable(instance);
    } else if (strcmp(instance->type, "todo_list") == 0) {
        w_todo_mark_unavailable(instance);
    } else if (strcmp(instance->type, "media_player") == 0) {
        w_media_player_mark_unavailable(instance);
    } else if (strcmp(instance->type, "sonos_favorites") == 0) {
        w_sonos_favorites_mark_unavailable(instance);
    } else if (strcmp(instance->type, "vacuum_tile") == 0) {
        w_vacuum_mark_unavailable(instance);
    }
    ui_style_apply_card(instance->obj);
}

void ui_widget_factory_mark_unavailable(ui_widget_instance_t *instance)
{
    const bool tile = instance != NULL && instance->obj != NULL;
    if (tile) ui_fonts_tile_begin(instance->type, (int)lv_obj_get_width(instance->obj), (int)lv_obj_get_height(instance->obj));
    ui_widget_factory_mark_unavailable_inner(instance);
    if (tile) ui_fonts_tile_end();
}

bool ui_widget_factory_needs_refresh_on_show(const char *type)
{
    /* Printer/EVCC bindings and Sonos favorites also stay current on hidden
     * pages. Replaying them rebuilds their visuals before the first frame. */
    static const char *const kStateOnly[] = {
        "button", "car_widget", "climate_tile", "contact_tile", "cover_tile", "fan_tile", "heating_tile",
        "light_tile", "room_tile", "lock_tile", "alarm_tile", "scene_tile", "select_tile", "datetime_tile", "sensor", "slider", "gauge", "solar_forecast", "energy_price_forecast", "parqet_portfolio", "stock_quotes", "energy_timeline",
        "bambu_printer", "wallbox_evcc", "sonos_favorites",
    };
    if (type == NULL) {
        return true;
    }
    for (size_t i = 0; i < sizeof(kStateOnly) / sizeof(kStateOnly[0]); i++) {
        if (strcmp(type, kStateOnly[i]) == 0) {
            return false;
        }
    }
    return true;
}

static void ui_widget_factory_set_visible_inner(ui_widget_instance_t *instance, bool visible)
{
    if (instance == NULL || instance->obj == NULL) {
        return;
    }
    if (instance->visible == visible) {
        return;
    }
    instance->visible = visible;

    if (strcmp(instance->type, "media_player") == 0) {
        w_media_player_set_visible(instance, visible);
    } else if (strcmp(instance->type, "light_tile") == 0) {
        w_light_tile_set_visible(instance, visible);
    } else if (strcmp(instance->type, "vacuum_tile") == 0) {
        w_vacuum_set_visible(instance, visible);
    } else if (!strcmp(instance->type, "wallbox_evcc")) {
        w_wallbox_evcc_set_visible(instance, visible);
    } else if (strcmp(instance->type, "bambu_printer") == 0) {
        w_bambu_printer_set_visible(instance, visible);
    } else if (strcmp(instance->type, "camera_tile") == 0) {
        w_camera_set_visible(instance, visible);
    }
}

void ui_widget_factory_set_visible(ui_widget_instance_t *instance, bool visible)
{
    const bool tile = instance != NULL && instance->obj != NULL;
    if (tile) ui_fonts_tile_begin(instance->type, (int)lv_obj_get_width(instance->obj), (int)lv_obj_get_height(instance->obj));
    ui_widget_factory_set_visible_inner(instance, visible);
    if (tile) ui_fonts_tile_end();
}
