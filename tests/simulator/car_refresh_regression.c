/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
/* Car bindings: duplicate reconciliation must not redraw; real changes must. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "host_adapters.h"
#include "ui/ui_widget_factory.h"

static unsigned checks, draws;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "car refresh line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static void drawn(lv_event_t *event) { (void)event; draws++; }
static void settle(void)
{
    sim_virtual_delay(50);
    lv_obj_update_layout(lv_screen_active());
    lv_timer_handler();
    lv_timer_handler();
}
static void apply(ui_widget_instance_t *widget, const char *entity, const char *value, const char *attrs)
{
    ha_state_t state = {0};
    snprintf(state.entity_id, sizeof(state.entity_id), "%s", entity);
    snprintf(state.state, sizeof(state.state), "%s", value);
    snprintf(state.attributes_json, sizeof(state.attributes_json), "%s", attrs);
    sim_host_state_add(&state);
    ui_widget_factory_apply_state(widget, &state);
    settle();
}
static lv_obj_t *label(lv_obj_t *root, const char *text)
{
    if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *found = label(lv_obj_get_child(root, i), text);
        if (found != NULL) return found;
    }
    return NULL;
}
void sim_car_refresh_regression(void)
{
    checks = draws = 0;
    ui_widget_def_t def = {.w = 400, .h = 400};
    strcpy(def.type, "car_widget");
    strcpy(def.entity_id, "sensor.car_soc");
    strcpy(def.secondary_entity_id, "sensor.car_power");
    strcpy(def.tertiary_entity_id, "sensor.car_range");
    strcpy(def.extra_entity_id[UI_CAR_EXTRA_PRECOND], "climate.car");
    strcpy(def.extra_entity_id[UI_CAR_EXTRA_LOCK], "lock.car");
    strcpy(def.extra_entity_id[UI_CAR_EXTRA_TEMP], "sensor.car_temp");
    ui_widget_instance_t widget = {0};
    CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
    const char *ids[] = {"sensor.car_soc", "sensor.car_power", "sensor.car_range", "climate.car", "lock.car", "sensor.car_temp"};
    const char *values[] = {"67", "1100", "312", "off", "locked", "21.5"};
    const char *next[] = {"68", "2200", "315", "heat", "unlocked", "22.5"};
    const char *attrs[] = {"{}", "{\"unit_of_measurement\":\"W\"}", "{\"unit_of_measurement\":\"km\"}", "{}", "{}", "{\"unit_of_measurement\":\"°C\"}"};
    for (size_t i = 0; i < 6; i++) apply(&widget, ids[i], values[i], attrs[i]);
    lv_obj_add_event_cb(widget.obj, drawn, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_invalidate(widget.obj);
    settle();
    CHECK(draws > 0);
    settle();
    draws = 0;
    apply(&widget, ids[0], "67.0", "{}");
    CHECK(draws == 0);
    for (size_t i = 0; i < 6; i++) {
        draws = 0;
        apply(&widget, ids[i], values[i], attrs[i]);
        CHECK(draws == 0);
        apply(&widget, ids[i], next[i], attrs[i]);
        CHECK(draws > 0);
        settle();
        draws = 0;
        apply(&widget, ids[i], next[i], attrs[i]);
        CHECK(draws == 0);
    }
    CHECK(label(widget.obj, "68") != NULL && label(widget.obj, "315 km") != NULL);
    CHECK(label(widget.obj, "Unlocked") != NULL);
    draws = 0;
    apply(&widget, ids[5], next[5], "{\"unit_of_measurement\":\"°F\"}");
    CHECK(draws > 0 && label(widget.obj, "22.5°F") != NULL);
    apply(&widget, ids[5], next[5], attrs[5]);
    lv_obj_t *precond = label(widget.obj, "Precondition");
    CHECK(precond != NULL);
    sim_host_service_reset(ESP_OK);
    lv_obj_send_event(lv_obj_get_parent(precond), LV_EVENT_CLICKED, NULL);
    CHECK(sim_host_service_calls()->count == 1);
    CHECK(strcmp(sim_host_service_calls()->service, "turn_off") == 0);
    settle();
    /* Pending: the neutral word replaces the label, a second tap sends nothing, the entity's answer ends it. */
    CHECK(label(widget.obj, "Sending...") != NULL && label(widget.obj, "Precondition") == NULL);
    lv_obj_send_event(lv_obj_get_parent(label(widget.obj, "Sending...")), LV_EVENT_CLICKED, NULL);
    CHECK(sim_host_service_calls()->count == 1);
    apply(&widget, ids[3], "heat_cool", attrs[3]); /* still on: not an answer */
    CHECK(label(widget.obj, "Sending...") != NULL);
    apply(&widget, ids[3], "off", attrs[3]);
    CHECK(label(widget.obj, "Precondition") != NULL && label(widget.obj, "Sending...") == NULL);
    /* No answer at all: the cap ends it, and a refused call never starts it. */
    apply(&widget, ids[3], "heat", attrs[3]);
    precond = label(widget.obj, "Precondition");
    sim_host_service_reset(ESP_OK);
    lv_obj_send_event(lv_obj_get_parent(precond), LV_EVENT_CLICKED, NULL);
    CHECK(label(widget.obj, "Sending...") != NULL);
    sim_virtual_delay(61000);
    settle();
    CHECK(label(widget.obj, "Precondition") != NULL && label(widget.obj, "Sending...") == NULL);
    sim_host_service_reset(ESP_FAIL);
    precond = label(widget.obj, "Precondition");
    lv_obj_send_event(lv_obj_get_parent(precond), LV_EVENT_CLICKED, NULL);
    CHECK(label(widget.obj, "Sending...") == NULL);
    sim_host_service_reset(ESP_OK);
    apply(&widget, ids[3], next[3], attrs[3]);
    draws = 0;
    apply(&widget, "sensor.unrelated", "123", "{}");
    CHECK(draws == 0);
    for (size_t i = 0; i < 6; i++) {
        apply(&widget, ids[i], "unavailable", "{}");
        /* Availability transitions can animate the disabled button surface. */
        for (int n = 0; n < 10; n++) settle();
        draws = 0;
        apply(&widget, ids[i], "unavailable", "{}");
        if (draws != 0) fprintf(stderr, "duplicate unavailable binding %zu drew %u times\n", i, draws);
        CHECK(draws == 0);
        apply(&widget, ids[i], next[i], attrs[i]);
        CHECK(draws > 0);
    }
    ui_widget_factory_mark_unavailable(&widget);
    for (int n = 0; n < 10; n++) settle();
    draws = 0;
    ui_widget_factory_mark_unavailable(&widget);
    settle();
    CHECK(draws == 0);
    apply(&widget, ids[0], next[0], attrs[0]);
    CHECK(draws > 0 && label(widget.obj, "68") != NULL);
    lv_obj_delete(widget.obj);
    settle();
    printf("car refresh: %u checks passed\n", checks);
}
