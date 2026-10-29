/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
/* Duplicate chart updates must not redraw; real data and history changes must. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "cJSON.h"
#include "host_adapters.h"
#include "ha/ha_display_precision.h"
#include "ha/ha_quote.h"
#include "ui/ui_widget_factory.h"
#include "ui/ui_history_popup.h"
#include "ui/text_font.h"
#include "ui/theme/ui_style.h"
#include "ui/theme/theme_default.h"
#include "popup_contract_regression.h"

static unsigned checks, draws;
void sim_car_refresh_regression(void);
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "chart refresh line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static void drawn(lv_event_t *event) { (void)event; draws++; }
static void advance(void)
{
    sim_virtual_delay(50);
    lv_obj_update_layout(lv_screen_active());
    lv_timer_handler();
    lv_timer_handler();
    sim_popup_contract_check();
}
static void apply(ui_widget_instance_t *widget, ha_state_t *state)
{
    sim_host_state_add(state);
    ui_widget_factory_apply_state(widget, state);
    advance();
}
static void probe(ui_widget_instance_t *widget)
{
    lv_obj_add_event_cb(widget->obj, drawn, LV_EVENT_DRAW_MAIN, NULL);
    draws = 0;
    lv_obj_invalidate(widget->obj);
    advance();
    CHECK(draws > 0);
    advance();
    draws = 0;
}
static lv_obj_t *find_chart(lv_obj_t *root)
{
    if (lv_obj_check_type(root, &lv_chart_class)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *chart = find_chart(lv_obj_get_child(root, i));
        if (chart != NULL) return chart;
    }
    return NULL;
}
static lv_obj_t *find_text(lv_obj_t *root, const char *text)
{
    if (lv_obj_check_type(root, &lv_label_class) && strcmp(lv_label_get_text(root), text) == 0) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *found = find_text(lv_obj_get_child(root, i), text);
        if (found != NULL) return found;
    }
    return NULL;
}
static void stock_cases(void)
{
    char ids[2][APP_MAX_ENTITY_ID_LEN] = {"sensor.refresh_quote", "sensor.refresh_easy"};
    CHECK(ha_display_precision_set_wanted((const char *)ids, 2));
    cJSON *request = ha_display_precision_next_request(871, 0);
    CHECK(request != NULL);
    cJSON_Delete(request);
    cJSON *reply = cJSON_Parse("{\"sensor.refresh_quote\":{\"platform\":\"easy_stock\"},\"sensor.refresh_easy\":{\"platform\":\"easy_stock\"}}");
    CHECK(ha_display_precision_handle_result(871, true, reply, NULL));
    cJSON_Delete(reply);
    static const char *const views[] = {"", "chart", "grid"};
    for (int view = 0; view < 3; view++) {
        ui_widget_def_t def = {.w = 650, .h = 400, .stock_selector = true};
        strcpy(def.type, "stock_quotes");
        strcpy(def.entity_id, ids[0]);
        strcpy(def.display_mode, views[view]);
        ui_widget_instance_t widget = {0};
        CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
        ha_state_t state = {0};
        strcpy(state.entity_id, ids[0]);
        strcpy(state.state, "100.00");
        strcpy(state.attributes_json, "{\"symbol\":\"TEST\",\"friendly_name\":\"Test quote\",\"unit_of_measurement\":\"EUR\"}");
        apply(&widget, &state);
        probe(&widget);
        apply(&widget, &state);
        CHECK(draws == 0);
        strcpy(state.state, "101.00");
        apply(&widget, &state);
        CHECK(draws > 0);
        advance();
        draws = 0;
        strcpy(state.attributes_json, "{\"symbol\":\"TEST\",\"friendly_name\":\"Test quote\",\"unit_of_measurement\":\"USD\"}");
        apply(&widget, &state);
        CHECK(draws > 0);
        advance();
        draws = 0;
        strcpy(state.state, "unavailable");
        apply(&widget, &state);
        CHECK(draws > 0);
        advance();
        draws = 0;
        apply(&widget, &state);
        CHECK(draws == 0);
        lv_obj_delete(widget.obj);
        advance();
    }
    /* Easy Stock's trade flag changes the day chart, even when the price stays put. */
    ui_widget_def_t def = {.w = 650, .h = 400, .stock_selector = true};
    strcpy(def.type, "stock_quotes");
    strcpy(def.entity_id, ids[1]);
    strcpy(def.display_mode, "chart");
    strcpy(def.stock_period, "1d");
    ui_widget_instance_t widget = {0};
    CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
    ha_state_t state = {0};
    strcpy(state.entity_id, ids[1]);
    strcpy(state.state, "110");
    strcpy(state.attributes_json, "{\"symbol\":\"TEST\",\"currency\":\"USD\",\"previous_close\":100,\"change\":10,\"change_pct\":10,\"traded_today\":false}");
    apply(&widget, &state);
    CHECK(find_text(widget.obj, "0.00 %") != NULL);
    probe(&widget);
    apply(&widget, &state);
    CHECK(draws == 0);
    strcpy(state.attributes_json, "{\"symbol\":\"TEST\",\"currency\":\"USD\",\"previous_close\":100,\"change\":10,\"change_pct\":10,\"traded_today\":true}");
    apply(&widget, &state);
    CHECK(find_text(widget.obj, "+10.00 %") != NULL);
    strcpy(state.attributes_json, "{\"symbol\":\"TEST\",\"currency\":\"USD\",\"previous_close\":100,\"change\":10,\"change_pct\":10,\"traded_today\":false}");
    apply(&widget, &state);
    CHECK(find_text(widget.obj, "0.00 %") != NULL);
    ui_widget_def_t list_def = def;
    list_def.display_mode[0] = '\0';
    ui_widget_instance_t list = {0};
    CHECK(ui_widget_factory_create(&list_def, lv_screen_active(), &list) == ESP_OK);
    apply(&list, &state);
    CHECK(find_text(list.obj, "+10.00 %") != NULL);
    lv_obj_delete(list.obj);
    ha_quote_t quote;
    CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, state.state, state.attributes_json, &quote));
    ui_history_popup_open_quote(state.entity_id, HA_QUOTE_PROVIDER_EASY_STOCK, &quote, HA_QUOTE_PERIOD_1D);
    advance();
    CHECK(find_text(lv_layer_top(), "Recorded  \xC2\xB7  0.00 $  0.00 %") != NULL);
    lv_obj_send_event(lv_obj_get_child(lv_layer_top(), 0), LV_EVENT_CLICKED, NULL);
    advance();
    strcpy(state.state, "4.35e-06");
    strcpy(state.attributes_json, "{\"symbol\":\"TEST\",\"currency\":\"USD\",\"previous_close\":0.000004,\"traded_today\":true}");
    apply(&widget, &state);
    CHECK(find_text(widget.obj, "0.00000435 $") != NULL);
    CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, state.state, state.attributes_json, &quote));
    ui_history_popup_open_quote(state.entity_id, HA_QUOTE_PROVIDER_EASY_STOCK, &quote, HA_QUOTE_PERIOD_1D);
    advance();
    lv_obj_send_event(lv_obj_get_child(lv_layer_top(), 0), LV_EVENT_CLICKED, NULL);
    advance();
    lv_obj_delete(widget.obj);
    advance();
}
static void graph_cases(void)
{
    const time_t saved = time(NULL);
    ui_widget_def_t def = {.w = 650, .h = 400, .graph_point_count = 60, .graph_time_window_min = 60};
    strcpy(def.type, "graph");
    strcpy(def.entity_id, "sensor.refresh_graph");
    strcpy(def.id, "refresh_graph");
    ui_widget_instance_t widget = {0};
    CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
    ha_state_t state = {0};
    strcpy(state.entity_id, def.entity_id);
    strcpy(state.state, "10");
    strcpy(state.attributes_json, "{\"unit_of_measurement\":\"W\"}");
    apply(&widget, &state);
    probe(&widget);
    apply(&widget, &state);
    CHECK(draws == 0);
    strcpy(state.state, "20");
    apply(&widget, &state);
    CHECK(draws > 0);
    advance();
    draws = 0;
    sim_clock_set(saved + 600);
    apply(&widget, &state); /* same value, new live sample: it must remain in history */
    CHECK(draws > 0);
    lv_obj_t *chart = find_chart(widget.obj);
    CHECK(chart != NULL);
    lv_chart_series_t *series = lv_chart_get_series_next(chart, NULL);
    CHECK(series != NULL);
    int points = 0;
    int32_t *values = lv_chart_get_y_array(chart, series);
    for (uint32_t i = 0; i < lv_chart_get_point_count(chart); i++) if (values[i] != LV_CHART_POINT_NONE) points++;
    CHECK(points >= 2);
    advance();
    draws = 0;
    strcpy(state.attributes_json, "{\"unit_of_measurement\":\"kW\"}");
    apply(&widget, &state);
    CHECK(draws > 0);
    strcpy(state.state, "unavailable");
    apply(&widget, &state);
    advance();
    draws = 0;
    apply(&widget, &state);
    CHECK(draws == 0);
    strcpy(state.state, "20");
    apply(&widget, &state);
    CHECK(draws > 0);
    lv_obj_delete(widget.obj);
    advance();
    sim_clock_set(saved);
}
static void solar_cases(bool vertical)
{
    const time_t saved = time(NULL);
    ui_widget_def_t def = {.w = 650, .h = 400};
    strcpy(def.type, "solar_forecast");
    strcpy(def.title, "Solar detail");
    strcpy(def.solar_forecast_bar_orientation, vertical ? "vertical" : "horizontal");
    strcpy(def.entity_id, "sensor.refresh_solar");
    strcpy(def.forecast_today_entity_id, "sensor.refresh_solar_total");
    strcpy(def.forecast_tomorrow_entity_id, "sensor.refresh_solar_tomorrow");
    ui_widget_instance_t widget = {0};
    CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
    ha_state_t state = {0};
    strcpy(state.entity_id, def.entity_id);
    strcpy(state.state, "20");
    strcpy(state.attributes_json, "{\"unit_of_measurement\":\"kWh\"}");
    apply(&widget, &state);
    probe(&widget);
    apply(&widget, &state);
    CHECK(draws == 0);
    strcpy(state.attributes_json, "{}");
    apply(&widget, &state);
    CHECK(draws == 0); /* missing metadata preserves the previously known unit */
    strcpy(state.state, "21");
    apply(&widget, &state);
    CHECK(draws > 0);
    advance();
    draws = 0;
    sim_clock_set(saved + 86400);
    apply(&widget, &state);
    CHECK(draws > 0); /* identical reading on another local day still changes weekday labels */
    advance();
    draws = 0;
    strcpy(state.attributes_json, "{\"unit_of_measurement\":\"Wh\"}");
    apply(&widget, &state);
    CHECK(draws > 0);
    strcpy(state.state, "unavailable");
    apply(&widget, &state);
    advance();
    draws = 0;
    apply(&widget, &state);
    CHECK(draws == 0);
    strcpy(state.state, "22");
    apply(&widget, &state);
    CHECK(draws > 0);
    const int original_step = ui_fonts_user_step();
    ui_fonts_set_user_step(2);
    lv_obj_t *hit = lv_obj_get_child(widget.obj, -1);
    lv_area_t bounds; lv_obj_get_coords(hit, &bounds);
    lv_point_t point = {.x = (bounds.x1 + bounds.x2) / 2, .y = bounds.y2 + 5};
    if (vertical) { point.x = bounds.x2 + 8; point.y = (bounds.y1 + bounds.y2) / 2; }
    lv_obj_t *future = lv_indev_search_obj(lv_screen_active(), &point);
    if (future != NULL) lv_obj_send_event(future, LV_EVENT_CLICKED, NULL);
    CHECK(find_text(lv_layer_top(), "Solar forecast today") == NULL);
    point.x = (bounds.x1 + bounds.x2) / 2;
    point.y = (bounds.y1 + bounds.y2) / 2;
    CHECK(lv_indev_search_obj(lv_screen_active(), &point) == hit);
    lv_obj_send_event(hit, LV_EVENT_CLICKED, NULL);
    advance();
    lv_obj_t *heading = find_text(lv_layer_top(), "Solar forecast today");
    CHECK(heading != NULL);
    CHECK(lv_obj_get_width(lv_obj_get_parent(heading)) <= 560);
    CHECK(lv_obj_get_height(lv_obj_get_parent(heading)) < 400);
    lv_obj_t *popup = lv_obj_get_parent(heading);
    lv_obj_t *remaining = find_text(popup, "Remaining");
    lv_obj_t *reading = find_text(popup, "22 Wh");
    CHECK(remaining != NULL && reading != NULL);
    CHECK(lv_color_eq(lv_obj_get_style_text_color(heading, LV_PART_MAIN), theme_default_color_text_primary()));
    CHECK(lv_color_eq(lv_obj_get_style_text_color(remaining, LV_PART_MAIN), theme_default_color_text_muted()));
    CHECK(lv_color_eq(lv_obj_get_style_text_color(reading, LV_PART_MAIN), theme_default_color_text_primary()));
    CHECK(find_text(popup, "Total") != NULL);
    CHECK(find_text(popup, "-- kWh") != NULL); /* missing today's total remains explicit */
    strcpy(state.entity_id, def.forecast_today_entity_id);
    strcpy(state.state, "30");
    strcpy(state.attributes_json, "{\"unit_of_measurement\":\"kWh\"}");
    apply(&widget, &state);
    CHECK(find_text(popup, "30 kWh") != NULL);
    strcpy(state.entity_id, def.forecast_tomorrow_entity_id);
    strcpy(state.state, "99");
    apply(&widget, &state);
    CHECK(find_text(popup, "99 kWh") == NULL);
    strcpy(state.entity_id, def.entity_id);
    strcpy(state.state, "unavailable");
    strcpy(state.attributes_json, "{\"unit_of_measurement\":\"Wh\"}");
    apply(&widget, &state);
    CHECK(find_text(popup, "22 Wh") == NULL);
    CHECK(find_text(popup, "-- Wh") != NULL);
    apply(&widget, &state);
    CHECK(find_text(lv_layer_top(), "Solar forecast today") == heading);
    sim_virtual_delay(60001);
    advance();
    CHECK(find_text(lv_layer_top(), "Solar forecast today") == NULL);
    ui_fonts_set_user_step(original_step);
    lv_obj_delete(widget.obj);
    advance();
    sim_clock_set(saved);
}

void sim_chart_refresh_regression(void)
{
    sim_host_set_online(true);
    const ui_style_kind_t original = ui_style_get();
    const ui_style_kind_t styles[] = {UI_STYLE_FLAT, UI_STYLE_GLOW, UI_STYLE_GLASS, UI_STYLE_MATTE};
    for (size_t i = 0; i < sizeof(styles) / sizeof(styles[0]); i++) {
        ui_style_set(styles[i]);
        stock_cases();
        graph_cases();
        solar_cases(false);
        solar_cases(true);
        sim_car_refresh_regression();
    }
    ui_style_set(original);
    printf("chart refresh: %u checks passed\n", checks);
}
