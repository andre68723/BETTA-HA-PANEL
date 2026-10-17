/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "cJSON.h"
#include "host_adapters.h"
#include "ui/ui_widget_factory.h"

esp_err_t w_light_tile_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_light_tile_apply_state(ui_widget_instance_t *, const ha_state_t *);
void w_light_tile_mark_unavailable(ui_widget_instance_t *);
void w_light_tile_set_visible(ui_widget_instance_t *, bool);
esp_err_t w_vacuum_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_vacuum_apply_state(ui_widget_instance_t *, const ha_state_t *);
void w_vacuum_mark_unavailable(ui_widget_instance_t *);
void w_vacuum_set_visible(ui_widget_instance_t *, bool);

static const char *phase;
static unsigned checks;
static uint32_t now;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "Controls regression line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static uint32_t tick(void) { return now; }
static void advance(void) { now += 100; lv_timer_handler(); }
static lv_obj_t *label(lv_obj_t *root, const char *text)
{
    if (lv_obj_check_type(root, &lv_label_class)) {
        const char *shown = lv_label_get_text(root);
        const size_t n = strlen(shown);
        if (strcmp(shown, text) == 0 || (n > 3 && strcmp(shown + n - 3, "...") == 0 &&
            strncmp(shown, text, n - 3) == 0)) return root;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *found = label(lv_obj_get_child(root, i), text);
        if (found != NULL) return found;
    }
    return NULL;
}
static void dump_labels(lv_obj_t *root)
{
    if (lv_obj_check_type(root, &lv_label_class)) fprintf(stderr, "Label: [%s]\n", lv_label_get_text(root));
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) dump_labels(lv_obj_get_child(root, i));
}
static void click(lv_obj_t *root, const char *text)
{
    lv_obj_t *found = label(root, text);
    if (found == NULL) { fprintf(stderr, "Missing control label: %s (%s)\n", text, phase); dump_labels(root); }
    CHECK(found != NULL);
    lv_obj_t *button = lv_obj_get_parent(found);
    CHECK(lv_obj_check_type(button, &lv_button_class));
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    advance();
}
static void sent(const char *domain, const char *service, const char *entity, const char *key, const char *value)
{
    const sim_service_calls_t *calls = sim_host_service_calls();
    CHECK(calls->count == 1 && strcmp(calls->domain, domain) == 0 && strcmp(calls->service, service) == 0);
    cJSON *data = cJSON_Parse(calls->data);
    CHECK(data != NULL);
    CHECK(strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(data, "entity_id")), entity) == 0);
    CHECK(strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(data, key)), value) == 0);
    CHECK(cJSON_GetArraySize(data) == 2);
    cJSON_Delete(data);
}
static void apply(ui_widget_instance_t *widget, bool light, const char *entity, const char *value, const char *attrs)
{
    ha_state_t state = {0};
    snprintf(state.entity_id, sizeof(state.entity_id), "%s", entity);
    snprintf(state.state, sizeof(state.state), "%s", value);
    snprintf(state.attributes_json, sizeof(state.attributes_json), "%s", attrs);
    sim_host_state_add(&state);
    if (light) w_light_tile_apply_state(widget, &state); else w_vacuum_apply_state(widget, &state);
    lv_obj_update_layout(widget->obj);
}
static void light_test(void)
{
    ui_widget_def_t def = {.w = 240, .h = 240};
    snprintf(def.entity_id, sizeof(def.entity_id), "light.test");
    ui_widget_instance_t widget = {0};
    CHECK(w_light_tile_create(&def, lv_screen_active(), &widget) == ESP_OK);
    const char *effects = "{\"supported_color_modes\":[\"onoff\"],\"effect\":\"None\",\"effect_list\":[\"None\",\"A\\\"B\\\\C\",\"Rainbow\"]}";
    apply(&widget, true, "light.test", "off", effects);
    lv_obj_t *button = lv_obj_get_child(widget.obj, 5);
    CHECK(!lv_obj_is_hidden(button));
    sim_host_service_reset(ESP_OK);
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    click(lv_layer_top(), "A\"B\\C");
    sent("light", "turn_on", "light.test", "effect", "A\"B\\C");
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
    /* A stale picker is dismissed when capabilities change. */
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    apply(&widget, true, "light.test", "on", "{\"supported_color_modes\":[\"onoff\"]}");
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0 && lv_obj_is_hidden(button));
    /* Color lights keep their controls and offer effects below their presets. */
    const char *color = "{\"supported_color_modes\":[\"rgb\"],\"rgb_color\":[255,0,0],\"effect\":\"None\",\"effect_list\":[\"None\",\"Rainbow\"]}";
    apply(&widget, true, "light.test", "on", color);
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    click(lv_layer_top(), "Effect: None");
    sim_host_service_reset(ESP_FAIL);
    click(lv_layer_top(), "Rainbow");
    CHECK(label(lv_layer_top(), "Effect: None") != NULL);
    sim_host_service_reset(ESP_OK);
    click(lv_layer_top(), "Effect: None");
    click(lv_layer_top(), "Rainbow");
    sent("light", "turn_on", "light.test", "effect", "Rainbow");
    CHECK(label(lv_layer_top(), "Effect: Rainbow") != NULL);
    click(lv_layer_top(), "Effect: Rainbow");
    w_light_tile_set_visible(&widget, false);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    w_light_tile_mark_unavailable(&widget);
    sim_host_service_reset(ESP_OK);
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0 && sim_host_service_calls()->count == 0);
    apply(&widget, true, "light.test", "on", effects);
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    sim_host_set_online(false);
    click(lv_layer_top(), "Rainbow");
    CHECK(sim_host_service_calls()->count == 0);
    sim_host_set_online(true);
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    lv_obj_delete(widget.obj);
    advance();
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
}
static void vacuum_test(void)
{
    ui_widget_def_t def = {.w = 240, .h = 220};
    snprintf(def.entity_id, sizeof(def.entity_id), "vacuum.test");
    snprintf(def.tertiary_entity_id, sizeof(def.tertiary_entity_id), "select.test_mode");
    ui_widget_instance_t widget = {0};
    CHECK(w_vacuum_create(&def, lv_screen_active(), &widget) == ESP_OK);
    const char *attrs = "{\"supported_features\":40,\"fan_speed\":\"Normal\",\"fan_speed_list\":[\"Normal\",\"Turbo\"]}";
    apply(&widget, false, "vacuum.test", "docked", attrs);
    apply(&widget, false, "select.test_mode", "Auto", "{\"options\":[\"Auto\",\"Mop\"]}");
    sim_host_service_reset(ESP_OK);
    phase = "vacuum initial fan";
    click(widget.obj, "Normal");
    click(lv_layer_top(), "Turbo");
    sent("vacuum", "set_fan_speed", "vacuum.test", "fan_speed", "Turbo");
    CHECK(label(widget.obj, "Turbo") != NULL);
    /* The separate cleaning-mode entity still receives its own action. */
    sim_host_service_reset(ESP_OK);
    phase = "vacuum mode";
    click(widget.obj, "Auto");
    click(lv_layer_top(), "Mop");
    sent("select", "select_option", "select.test_mode", "option", "Mop");
    apply(&widget, false, "vacuum.test", "docked", attrs);
    sim_host_service_reset(ESP_FAIL);
    click(widget.obj, "Normal"); click(lv_layer_top(), "Turbo");
    CHECK(label(widget.obj, "Normal") != NULL);
    click(widget.obj, "Normal");
    w_vacuum_set_visible(&widget, false);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
    click(widget.obj, "Normal");
    w_vacuum_mark_unavailable(&widget);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
    sim_host_service_reset(ESP_OK);
    click(widget.obj, "Normal");
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0 && sim_host_service_calls()->count == 0);
    apply(&widget, false, "vacuum.test", "docked", "{\"supported_features\":8,\"fan_speed\":\"Normal\",\"fan_speed_list\":[\"Normal\"]}");
    CHECK(label(widget.obj, "Normal") != NULL && lv_obj_is_hidden(lv_obj_get_parent(label(widget.obj, "Normal"))));
    apply(&widget, false, "vacuum.test", "docked", attrs);
    click(widget.obj, "Normal");
    lv_obj_delete(widget.obj); advance();
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
}
static void cached_favorites_test(void)
{
    ui_widget_def_t def = {.w = 400, .h = 400};
    snprintf(def.type, sizeof(def.type), "sonos_favorites");
    snprintf(def.entity_id, sizeof(def.entity_id), "media_player.synthetic");
    snprintf(def.secondary_entity_id, sizeof(def.secondary_entity_id), "sensor.synthetic_favorites");
    ui_widget_instance_t widget = {0};
    CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
    ha_state_t state = {0};
    snprintf(state.entity_id, sizeof(state.entity_id), "%s", def.secondary_entity_id);
    snprintf(state.state, sizeof(state.state), "1");
    snprintf(state.attributes_json, sizeof(state.attributes_json), "{\"items\":{\"FV:1\":\"Old station\"}}");
    ui_widget_factory_apply_state(&widget, &state);
    CHECK(label(widget.obj, "Old station") != NULL);
    ui_widget_factory_set_visible(&widget, false);
    lv_obj_set_hidden(widget.obj, true);
    snprintf(state.attributes_json, sizeof(state.attributes_json), "{\"items\":{\"FV:2\":\"New station\"}}");
    ui_widget_factory_apply_state(&widget, &state);
    ui_widget_factory_set_visible(&widget, true);
    lv_obj_set_hidden(widget.obj, false);
    CHECK(label(widget.obj, "New station") != NULL && label(widget.obj, "Old station") == NULL);
    lv_obj_delete(widget.obj);
}

int sim_controls_regression(void)
{
    now = lv_tick_get(); lv_tick_set_cb(tick);
    sim_host_set_online(true);
    phase = "light"; light_test(); phase = "vacuum"; vacuum_test();
    phase = "cached favorites"; cached_favorites_test();
    printf("controls regression: %u checks passed\n", checks);
    int sim_camera_regression(void);
    sim_host_service_reset(ESP_OK);
    return sim_camera_regression();
}
