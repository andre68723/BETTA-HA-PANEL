/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "lvgl.h"

#include "app_config.h"
#include "ha/ha_model.h"

typedef struct {
    char id[APP_MAX_WIDGET_ID_LEN];
    char type[16];
    char title[APP_MAX_NAME_LEN];
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    char secondary_entity_id[APP_MAX_ENTITY_ID_LEN];
    char tertiary_entity_id[APP_MAX_ENTITY_ID_LEN];
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
    int graph_point_count;
    int graph_time_window_min;
    char graph_display_mode[APP_MAX_UI_OPTION_LEN];
    int graph_bar_bucket_min;
    char style_variant[APP_MAX_UI_OPTION_LEN];
    char arc_opening[APP_MAX_UI_OPTION_LEN];
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
    int graph_point_count;
    int graph_time_window_min;
    char graph_display_mode[APP_MAX_UI_OPTION_LEN];
    int graph_bar_bucket_min;
    char style_variant[APP_MAX_UI_OPTION_LEN];
    char arc_opening[APP_MAX_UI_OPTION_LEN];
    bool visible;
    void *ctx;
    lv_obj_t *obj;
} ui_widget_instance_t;

esp_err_t ui_widget_factory_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance);
void ui_widget_factory_apply_state(ui_widget_instance_t *instance, const ha_state_t *state);
void ui_widget_factory_mark_unavailable(ui_widget_instance_t *instance);
void ui_widget_factory_set_visible(ui_widget_instance_t *instance, bool visible);
