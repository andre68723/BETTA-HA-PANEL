/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/ui_widget_factory.h"
#include "ui/ui_i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "room air line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static bool text(lv_obj_t *root, const char *needle)
{
    if (lv_obj_check_type(root, &lv_label_class) && strstr(lv_label_get_text(root), needle)) return true;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) if (text(lv_obj_get_child(root, i), needle)) return true;
    return false;
}
static void apply(ui_widget_instance_t *w, const char *id, const char *value, const char *attrs)
{
    static ha_state_t state;
    memset(&state, 0, sizeof(state));
    snprintf(state.entity_id, sizeof(state.entity_id), "%s", id);
    snprintf(state.state, sizeof(state.state), "%s", value);
    snprintf(state.attributes_json, sizeof(state.attributes_json), "%s", attrs);
    ui_widget_factory_apply_state(w, &state);
    lv_obj_update_layout(lv_screen_active());
}

void sim_room_air_regression(void)
{
    char language[APP_UI_LANGUAGE_MAX_LEN]; snprintf(language, sizeof(language), "%s", ui_i18n_get_language());
    CHECK(ui_i18n_init("en") == ESP_OK);
    ui_widget_def_t def = {.w=480, .h=320};
    strcpy(def.type, "air_quality_tile"); strcpy(def.title, "Room air test");
    strcpy(def.entity_id, "sensor.air_rh"); strcpy(def.secondary_entity_id, "sensor.air_temp");
    strcpy(def.tertiary_entity_id, "sensor.air_co2"); strcpy(def.extra_entity_id[0], "sensor.air_pm");
    strcpy(def.extra_entity_id[2], "weather.air"); strcpy(def.extra_entity_id[3], "weather.air");
    strcpy(def.extra_entity_id[5], "sensor.air_rating");
    ui_widget_instance_t w = {0};
    CHECK(ui_widget_factory_create(&def, lv_screen_active(), &w) == ESP_OK);
    apply(&w, def.entity_id, "75", "{\"unit_of_measurement\":\"%\"}");
    apply(&w, def.secondary_entity_id, "22", "{\"unit_of_measurement\":\"°C\"}");
    apply(&w, def.tertiary_entity_id, "2200", "{\"unit_of_measurement\":\"ppm\"}");
    apply(&w, def.extra_entity_id[0], "8", "{\"unit_of_measurement\":\"μg/m³\"}");
    CHECK(text(w.obj, "Poor") && text(w.obj, "µg/m³"));
    apply(&w, "weather.air", "cloudy", "{\"temperature\":30,\"temperature_unit\":\"°C\",\"humidity\":90}");
    CHECK(text(w.obj, "conflict"));
    unsigned before = lv_obj_get_child_count(lv_layer_top());
    lv_obj_send_event(w.obj, LV_EVENT_CLICKED, NULL);
    lv_obj_update_layout(lv_layer_top());
    CHECK(lv_obj_get_child_count(lv_layer_top()) == before + 1);
    CHECK(text(lv_layer_top(), "Absolute humidity") && text(lv_layer_top(), "reference"));
    apply(&w, "weather.air", "cloudy", "{\"temperature\":30,\"temperature_unit\":\"°C\"}");
    CHECK(!text(lv_layer_top(), "Absolute humidity")); /* missing RH clears the comparison */
    apply(&w, def.entity_id, "unavailable", "{}");
    CHECK(text(w.obj, "Poor")); /* available red CO2 survives missing primary */
    apply(&w, def.tertiary_entity_id, "unavailable", "{}");
    CHECK(text(w.obj, "Not evaluated"));
    CHECK(!text(lv_layer_top(), "2200")); /* no cached optional pollutant survives clearing */
    apply(&w, "sensor.air_rating", "100", "{\"unit_of_measurement\":\"%\"}");
    CHECK(text(w.obj, "Not evaluated"));
    ui_widget_factory_mark_unavailable(&w);
    CHECK(!text(lv_layer_top(), "8 µg/m³"));
    lv_obj_delete(w.obj);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == before);
    CHECK(ui_i18n_init(language) == ESP_OK);
    printf("room air LVGL: %u lifecycle, duplicate weather roles, missing values and popup checks passed\n", checks);
}
