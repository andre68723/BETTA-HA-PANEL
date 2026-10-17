/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "ui/ui_widget_name.h"

#include "app_config.h"
#include "ha/ha_model.h"
#include "layout/bambu_printer_slots.h"
#include "layout/energy_timeline_colors.h"

/* Entities beyond primary / secondary / tertiary: the room card list (7) and the car tile extras. */
#define UI_WIDGET_EXTRA_ENTITIES 8
/* extra_entity_id[] of the car tile */
#define UI_CAR_EXTRA_PRECOND 0
#define UI_CAR_EXTRA_LOCK 1
#define UI_CAR_EXTRA_TEMP 2
/* extra_entity_id[] of the vacuum tile (JSON key battery_entity_id): an optional sensor with the charge in percent */
#define UI_VACUUM_EXTRA_BATTERY 0
/* extra_entity_id[] of the energy timeline (JSON keys battery_charge_entity_id, battery_discharge_entity_id and
 * battery_soc_entity_id): the two battery energy counters and the live state of charge. The forecast sensor is
 * forecast_today_entity_id. */
#define UI_ETL_EXTRA_BATTERY_CHARGE 0
#define UI_ETL_EXTRA_BATTERY_DISCHARGE 1
#define UI_ETL_EXTRA_BATTERY_SOC 2

typedef struct {
    char id[APP_MAX_WIDGET_ID_LEN];
    char type[16];
    char title[APP_MAX_NAME_LEN];
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    char secondary_entity_id[APP_MAX_ENTITY_ID_LEN];
    char tertiary_entity_id[APP_MAX_ENTITY_ID_LEN];
    char extra_entity_id[UI_WIDGET_EXTRA_ENTITIES][APP_MAX_ENTITY_ID_LEN]; /* room card list, car tile extras */
    char forecast_today_entity_id[APP_MAX_ENTITY_ID_LEN];
    char forecast_tomorrow_entity_id[APP_MAX_ENTITY_ID_LEN];
    char forecast_day_3_entity_id[APP_MAX_ENTITY_ID_LEN];
    char forecast_day_4_entity_id[APP_MAX_ENTITY_ID_LEN];
    char forecast_day_5_entity_id[APP_MAX_ENTITY_ID_LEN];
    float solar_forecast_bar_max_kwh;
    char solar_forecast_bar_orientation[APP_MAX_UI_OPTION_LEN];
    char slider_direction[APP_MAX_UI_OPTION_LEN];
    char slider_accent_color[APP_MAX_COLOR_STR_LEN];
    char button_accent_color[APP_MAX_COLOR_STR_LEN];
    char button_mode[APP_MAX_UI_OPTION_LEN];
    char graph_line_color[APP_MAX_COLOR_STR_LEN];
    etl_palette_t energy_palette;
    int graph_point_count;
    int graph_time_window_min;
    char graph_display_mode[APP_MAX_UI_OPTION_LEN];
    int graph_bar_bucket_min;
    bool graph_ha_history; /* fill the graph from the Home Assistant Recorder (ui/graph_history_feed.h) */
    char style_variant[APP_MAX_UI_OPTION_LEN];
    char arc_opening[APP_MAX_UI_OPTION_LEN];
    char display_mode[APP_MAX_UI_OPTION_LEN]; /* binary_tile: "" (text + since), "icon" or "since" */
    char default_mode[48]; /* vacuum_tile: cleaning program (option of the mode select) set before Start */
    int forecast_days; /* weather/solar: -1 legacy, 0 measured auto, 1..6 (solar max 5) */
    bool align_forecast; /* solar: dated columns with Rest in the header */
    bool show_precipitation; /* weather_3day: rain chance and amount, default true */
    bool room_controls; /* room_tile: a control row (lights, switches, covers) when the card is large enough, default false */
    float gauge_min; /* gauge: bottom of the bar's range, default 0 */
    float gauge_max; /* gauge: top of the bar's range, default 100 */
    char gauge_bar_style[APP_MAX_UI_OPTION_LEN]; /* gauge, linear style only: "smooth" (default) or "segmented" */
    int gauge_segment_count; /* gauge, segmented look: LED blocks, 0 = GAUGE_SEGMENT_COUNT_DEFAULT */
    char gauge_color_mode[APP_MAX_UI_OPTION_LEN]; /* gauge: "" (default fill-level gold->green), "gradient", "range" */
    char gauge_color_low[APP_MAX_COLOR_STR_LEN]; /* gradient: color at gauge_min; range: color below gauge_target_low */
    char gauge_color_high[APP_MAX_COLOR_STR_LEN]; /* gradient: color at gauge_max; range: color above gauge_target_high */
    char gauge_target_color[APP_MAX_COLOR_STR_LEN]; /* range only: color within [gauge_target_low, gauge_target_high] */
    float gauge_target_low; /* range only: start of the in-range band */
    float gauge_target_high; /* range only: end of the in-range band */
    bool hide_name; /* shared show_name=false; zero-initialized widgets keep their name */
    bool sensor_show_age; /* sensor: "N min ago" freshness text below the value, default true */
    bool lock_confirm; /* lock_tile: ask before unlocking or opening, default false */
    bool camera_live; /* camera_tile: HA MJPEG while visible; opt-in, default false */
    unsigned alarm_actions; /* alarm_tile: bit (1 << ha_alarm_action_t) per action offered, 0 = all */
    bool history_popup; /* sensor/binary/contact/lock: show Recorder history on tap/long press */
    bool price_day_switch;  /* price_forecast: Today/Tomorrow buttons, default false */
    bool price_line; /* price_forecast: a stepped line instead of filled bars, default false */
    bool price_cheap_times; /* price_forecast: shade the cheap times and name the next one, default false */
    float price_average;    /* price_forecast: fixed average in EUR/kWh, 0 = none (the day's own average, or the sensor in secondary_entity_id) */
    int x;
    int y;
    int w;
    int h;
} ui_widget_def_t;

typedef struct {
    char id[APP_MAX_WIDGET_ID_LEN];
    char page_id[APP_MAX_PAGE_ID_LEN];
    char type[16];
    char title[APP_MAX_NAME_LEN];
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    char secondary_entity_id[APP_MAX_ENTITY_ID_LEN];
    char tertiary_entity_id[APP_MAX_ENTITY_ID_LEN];
    char extra_entity_id[UI_WIDGET_EXTRA_ENTITIES][APP_MAX_ENTITY_ID_LEN]; /* room card list, car tile extras */
    char forecast_today_entity_id[APP_MAX_ENTITY_ID_LEN];
    char forecast_tomorrow_entity_id[APP_MAX_ENTITY_ID_LEN];
    char forecast_day_3_entity_id[APP_MAX_ENTITY_ID_LEN];
    char forecast_day_4_entity_id[APP_MAX_ENTITY_ID_LEN];
    char forecast_day_5_entity_id[APP_MAX_ENTITY_ID_LEN];
    float solar_forecast_bar_max_kwh;
    char solar_forecast_bar_orientation[APP_MAX_UI_OPTION_LEN];
    char slider_direction[APP_MAX_UI_OPTION_LEN];
    char slider_accent_color[APP_MAX_COLOR_STR_LEN];
    char button_accent_color[APP_MAX_COLOR_STR_LEN];
    char button_mode[APP_MAX_UI_OPTION_LEN];
    char graph_line_color[APP_MAX_COLOR_STR_LEN];
    etl_palette_t energy_palette;
    int graph_point_count;
    int graph_time_window_min;
    char graph_display_mode[APP_MAX_UI_OPTION_LEN];
    int graph_bar_bucket_min;
    bool graph_ha_history; /* fill the graph from the Home Assistant Recorder (ui/graph_history_feed.h) */
    char style_variant[APP_MAX_UI_OPTION_LEN];
    char arc_opening[APP_MAX_UI_OPTION_LEN];
    char display_mode[APP_MAX_UI_OPTION_LEN];
    float gauge_min;
    float gauge_max;
    char gauge_bar_style[APP_MAX_UI_OPTION_LEN];
    int gauge_segment_count;
    bool hide_name; /* shared show_name=false; zero-initialized widgets keep their name */
    bool sensor_show_age;
    bool lock_confirm;
    unsigned alarm_actions;
    bool history_popup;
    bool visible;
    void *ctx;
    lv_obj_t *obj;
} ui_widget_instance_t;

/* False for tiles that are updated whenever their entity changes (hidden or not) and keep nothing
 * that depends on being on screen, so showing their page needs no state re-apply. */
bool ui_widget_factory_needs_refresh_on_show(const char *type);

esp_err_t ui_widget_factory_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void ui_widget_factory_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void ui_widget_factory_mark_unavailable(ui_widget_instance_t *instance);
void ui_widget_factory_set_visible(ui_widget_instance_t *instance, bool visible);
