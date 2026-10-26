/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_adapters.h"
#include "popup_contract_regression.h"
#include "ui/widgets/weather_tile_internal.h"
#include "ui/text_font.h"

static unsigned checks;
static void (*advance_clock)(uint32_t);
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "weather now line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static void advance(void) { advance_clock(50); lv_obj_update_layout(lv_screen_active()); }
static void close_popup(w_weather_tile_ctx_t *ctx) { CHECK(ctx->popup); lv_obj_delete(ctx->popup); advance(); CHECK(!ctx->popup); }
static cJSON *hourly_result(void)
{
    cJSON *root = cJSON_CreateObject(), *response = cJSON_AddObjectToObject(root, "response");
    cJSON *weather = cJSON_AddObjectToObject(response, "weather.now_test");
    cJSON *forecast = cJSON_AddArrayToObject(weather, "forecast");
    for (int i = 1; i <= 24; i++) {
        cJSON *hour = cJSON_CreateObject();
        time_t at = time(NULL) + i * 3600;
        struct tm utc; char date[32];
        gmtime_r(&at, &utc); strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%SZ", &utc);
        cJSON_AddStringToObject(hour, "datetime", date);
        cJSON_AddNumberToObject(hour, "temperature", 12 + i % 4);
        cJSON_AddStringToObject(hour, "condition", "cloudy"); cJSON_AddItemToArray(forecast, hour);
    }
    return root;
}
void sim_weather_now_regression(void (*delay)(uint32_t))
{
    advance_clock = delay;
    ui_widget_def_t def = {.w = 650, .h = 380, .hide_name = true, .weather_hourly = true};
    strcpy(def.type, "weather_tile"); strcpy(def.entity_id, "weather.now_test");
    strcpy(def.secondary_entity_id, "sensor.now_report");
    ui_widget_instance_t widget = {0};
    CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
    ha_state_t state = {0}; strcpy(state.entity_id, def.entity_id); strcpy(state.state, "cloudy");
    strcpy(state.attributes_json, "{\"temperature\":11,\"temperature_unit\":\"°C\",\"humidity\":70,\"pressure\":1010,\"pressure_unit\":\"hPa\"}");
    ui_widget_factory_apply_state(&widget, &state);
    strcpy(state.entity_id, def.secondary_entity_id);
    strcpy(state.attributes_json, "{\"s\":\"Public synthetic DWD report.\",\"d\":{\"0\":\"Cloudy\",\"1\":\"Cloudy\",\"2\":\"Cloudy\",\"3\":\"Cloudy\",\"4\":\"Cloudy\",\"5\":\"Cloudy\",\"6\":\"Cloudy\"}}");
    ui_widget_factory_apply_state(&widget, &state);
    w_weather_tile_ctx_t *ctx = widget.ctx;
    CHECK(ctx->available && ctx->report_summary[0]);
    CHECK(ctx->hourly.count == 0 && !lv_obj_is_hidden(ctx->hourly_status));
    const uint32_t token = ctx->hourly_token;
    cJSON *reply = hourly_result(); weather_hourly_simulator_reply(token, reply); advance();
    CHECK(ctx->hourly.count == 24 && lv_obj_is_hidden(ctx->hourly_status));
    CHECK(!lv_obj_is_hidden(ctx->hours[0].box));
    lv_obj_scroll_to_x(ctx->hourly_strip, 200, LV_ANIM_OFF); advance();
    CHECK(lv_obj_get_scroll_x(ctx->hourly_strip) > 0 && ctx->popup == NULL);
    /* Synchronous callback plus an error return must free a detail request once. */
    sim_host_response_mode(false, true);
    lv_obj_send_event(widget.obj, LV_EVENT_CLICKED, NULL); advance();
    CHECK(ctx->popup != NULL); sim_popup_contract_check(); close_popup(ctx);
    sim_host_response_mode(true, false);
    lv_obj_send_event(widget.obj, LV_EVENT_CLICKED, NULL); advance();
    CHECK(ctx->popup != NULL); close_popup(ctx);
    /* Reopening before the old response arrives must not update a recycled list object. */
    lv_obj_send_event(widget.obj, LV_EVENT_CLICKED, NULL); advance();
    lv_obj_t *popup_card = lv_obj_get_child(ctx->popup, 0);
    lv_obj_t *popup_list = lv_obj_get_child(popup_card, lv_obj_get_child_count(popup_card) - 1);
    const uint32_t rows_before = lv_obj_get_child_count(popup_list);
    cJSON *details = cJSON_Parse("{\"response\":{\"weather.now_test\":{\"forecast\":[{\"datetime\":\"\",\"uv_index\":99}]}}}");
    sim_host_response_deliver(true, details); advance(); cJSON_Delete(details);
    CHECK(lv_obj_get_child_count(popup_list) == rows_before);
    sim_popup_contract_check(); close_popup(ctx);
    sim_host_response_mode(false, false);
    /* A destroyed tile's late hourly answer cannot reach its replacement. */
    lv_obj_delete(widget.obj); advance();
    CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
    ctx = widget.ctx;
    weather_hourly_simulator_reply(token, reply); advance();
    CHECK(ctx->hourly.count == 0);
    cJSON_Delete(reply); lv_obj_delete(widget.obj); advance();
    def.weather_hourly = false;
    CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
    ctx = widget.ctx;
    strcpy(state.entity_id, def.entity_id); strcpy(state.state, "cloudy");
    strcpy(state.attributes_json, "{\"temperature\":11,\"temperature_unit\":\"°C\"}");
    ui_widget_factory_apply_state(&widget, &state);
    CHECK(ctx->hourly_timer == NULL && ctx->hourly_strip == NULL);
    lv_obj_send_event(widget.obj, LV_EVENT_CLICKED, NULL); advance();
    CHECK(ctx->popup != NULL); sim_popup_contract_check(); close_popup(ctx);
    lv_obj_send_event(widget.obj, LV_EVENT_CLICKED, NULL); advance();
    CHECK(ctx->popup != NULL);
    advance_clock(60100);
    CHECK(ctx->popup == NULL);
    lv_obj_delete(widget.obj); advance();
    printf("weather now: %u checks passed (hours, report, scroll, popup errors and late replies)\n", checks);
}
