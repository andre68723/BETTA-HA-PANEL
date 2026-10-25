/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "popup_contract_regression.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "cJSON.h"
#include "host_adapters.h"
#include "ui/ui_widget_factory.h"
#include "ui/theme/ui_style.h"

esp_err_t w_bambu_printer_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_bambu_printer_apply_state(ui_widget_instance_t *, const ha_state_t *);
void w_bambu_printer_set_visible(ui_widget_instance_t *, bool);

static unsigned checks;
static uint32_t now;
#define CHECK(condition) do { checks++; if (!(condition)) { \
    fprintf(stderr, "Bambu regression failed at line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)
static uint32_t tick(void) { return now; }
static void advance(uint32_t ms) {
    for (uint32_t elapsed = 0; elapsed < ms;) {
        uint32_t step = ms - elapsed > 100 ? 100 : ms - elapsed;
        now += step; elapsed += step; lv_timer_handler();
    }
    sim_popup_contract_check();
}

/* Find public labels, then dispatch the same clicked event as the input driver. */
static lv_obj_t *label(lv_obj_t *root, const char *text)
{
    if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *found = label(lv_obj_get_child(root, i), text);
        if (found != NULL) return found;
    }
    return NULL;
}
/* Temperature text must not cover any solid part of the illustration or footer. */
static void clear_of_shapes(lv_obj_t *root, const lv_area_t *reading)
{
    if (!lv_obj_check_type(root, &lv_label_class) && lv_obj_get_child_count(root) == 0 &&
        lv_obj_get_style_bg_opa(root, LV_PART_MAIN) != LV_OPA_TRANSP) {
        lv_area_t shape;
        lv_obj_get_coords(root, &shape);
        CHECK(reading->x2 < shape.x1 || reading->x1 > shape.x2 ||
              reading->y2 < shape.y1 || reading->y1 > shape.y2);
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        clear_of_shapes(lv_obj_get_child(root, i), reading);
    }
}
static void temperature_layout(ui_widget_instance_t *widget)
{
    lv_obj_update_layout(widget->obj);
    const char *texts[] = {"Nozzle 220 °C", "Bed 65 °C"};
    lv_area_t card;
    lv_obj_get_coords(widget->obj, &card);
    for (size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); i++) {
        lv_obj_t *reading = label(widget->obj, texts[i]);
        CHECK(reading != NULL);
        CHECK(lv_obj_get_style_bg_opa(reading, LV_PART_MAIN) == LV_OPA_TRANSP);
        lv_area_t area;
        lv_obj_get_coords(reading, &area);
        CHECK(area.x1 >= card.x1 && area.x2 <= card.x2 && area.y1 >= card.y1 && area.y2 <= card.y2);
        clear_of_shapes(widget->obj, &area);
    }
}
static void click(lv_obj_t *root, const char *text)
{
    lv_obj_t *found = label(root, text);
    CHECK(found != NULL);
    lv_obj_t *button = lv_obj_get_parent(found);
    CHECK(lv_obj_check_type(button, &lv_button_class));
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
}
static void state(ui_widget_instance_t *widget, const char *entity, const char *value)
{
    ha_state_t s = {0};
    snprintf(s.entity_id, sizeof(s.entity_id), "%s", entity);
    snprintf(s.state, sizeof(s.state), "%s", value);
    sim_host_state_add(&s);
    if (widget != NULL) w_bambu_printer_apply_state(widget, &s);
}
static ui_widget_instance_t create(void)
{
    ui_widget_def_t def = {.w = 320, .h = 320};
    snprintf(def.type, sizeof(def.type), "bambu_printer");
    snprintf(def.entity_id, sizeof(def.entity_id), "sensor.test_printer");
    snprintf(def.secondary_entity_id, APP_MAX_ENTITY_ID_LEN, "sensor.test_nozzle");
    snprintf(def.tertiary_entity_id, APP_MAX_ENTITY_ID_LEN, "sensor.test_bed");
    snprintf(def.extra_entity_id[UI_BAMBU_EXTRA_PAUSE], APP_MAX_ENTITY_ID_LEN, "button.test_pause");
    snprintf(def.extra_entity_id[UI_BAMBU_EXTRA_RESUME], APP_MAX_ENTITY_ID_LEN, "button.test_resume");
    snprintf(def.extra_entity_id[UI_BAMBU_EXTRA_STOP], APP_MAX_ENTITY_ID_LEN, "button.test_stop");
    snprintf(def.extra_entity_id[UI_BAMBU_EXTRA_CAMERA], APP_MAX_ENTITY_ID_LEN, "camera.synthetic");
    sim_host_set_online(true);
    state(NULL, "sensor.test_nozzle", "220");
    state(NULL, "sensor.test_bed", "65");
    state(NULL, "sensor.test_printer", "running");
    state(NULL, "button.test_pause", "unknown");
    state(NULL, "button.test_resume", "unknown");
    state(NULL, "button.test_stop", "unknown");
    ui_widget_instance_t widget = {0};
    CHECK(ui_widget_factory_create(&def, lv_screen_active(), &widget) == ESP_OK);
    state(&widget, "sensor.test_printer", "running");
    state(&widget, "sensor.test_nozzle", "220");
    state(&widget, "sensor.test_bed", "65");
    state(&widget, "button.test_pause", "unknown");
    state(&widget, "button.test_resume", "unknown");
    state(&widget, "button.test_stop", "unknown");
    return widget;
}
static void sent(unsigned count, const char *entity)
{
    const sim_service_calls_t *calls = sim_host_service_calls();
    CHECK(calls->count == count);
    if (entity == NULL) return;
    CHECK(!strcmp(calls->domain, "button"));
    CHECK(!strcmp(calls->service, "press"));
    cJSON *data = cJSON_Parse(calls->data);
    CHECK(data != NULL);
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(data, "entity_id");
    CHECK(cJSON_IsString(id) && !strcmp(id->valuestring, entity));
    CHECK(cJSON_GetArraySize(data) == 1);
    cJSON_Delete(data);
}
static void destroy(ui_widget_instance_t *widget)
{
    lv_obj_delete(widget->obj);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
    advance(20000); /* run deferred deletes and timers after owner destruction */
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
}
int sim_bambu_regression(void)
{
    now = lv_tick_get();
    lv_tick_set_cb(tick);
    sim_host_service_reset(ESP_OK);
    ui_widget_instance_t w = create();
    lv_color_t running_bg = lv_obj_get_style_bg_color(lv_obj_get_parent(label(w.obj, "Pause")), LV_PART_MAIN);
    temperature_layout(&w);
    /* Hidden pages still receive state events; showing one needs no replay of
     * every printer binding just to recover its latest reading. */
    ui_widget_factory_set_visible(&w, false);
    lv_obj_set_hidden(w.obj, true);
    state(&w, "sensor.test_nozzle", "215");
    ui_widget_factory_set_visible(&w, true);
    lv_obj_set_hidden(w.obj, false);
    CHECK(label(w.obj, "Nozzle 215 °C") != NULL);
    state(&w, "sensor.test_nozzle", "220");
    lv_obj_set_size(w.obj, 640, 320);
    temperature_layout(&w);
    state(&w, "sensor.test_nozzle", "unknown");
    CHECK(label(w.obj, "Nozzle —") != NULL);
    state(&w, "sensor.test_nozzle", "220");
    temperature_layout(&w);
    lv_obj_set_size(w.obj, 360, 420);
    temperature_layout(&w);
    lv_obj_set_size(w.obj, 320, 320);
    temperature_layout(&w);
    click(w.obj, "Status");
    sent(0, NULL);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 1);
    click(lv_layer_top(), "Live camera"); advance(500);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 2 && sim_camera_active() == 1);
    w_bambu_printer_set_visible(&w, false);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
    CHECK(sim_camera_active() == 0);
    click(w.obj, "Abort");
    click(lv_layer_top(), "Cancel");
    advance(1);
    sent(0, NULL);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);

    click(w.obj, "Pause");
    sent(1, "button.test_pause");
    click(w.obj, "Pause");
    click(w.obj, "Abort");
    sent(1, NULL);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
    state(&w, "sensor.test_printer", "pause");
    CHECK(label(w.obj, "Resume") != NULL);
    lv_color_t paused_bg = lv_obj_get_style_bg_color(lv_obj_get_parent(label(w.obj, "Resume")), LV_PART_MAIN);
    CHECK(running_bg.red != paused_bg.red || running_bg.green != paused_bg.green || running_bg.blue != paused_bg.blue);
    click(w.obj, "Resume");
    sent(2, "button.test_resume");
    state(&w, "sensor.test_printer", "running");
    click(w.obj, "Abort");
    click(lv_layer_top(), "Abort print");
    click(lv_layer_top(), "Abort print"); /* second click before deferred deletion */
    sent(3, "button.test_stop");
    advance(1);
    state(&w, "sensor.test_printer", "idle");
    click(w.obj, "Pause");
    sent(3, NULL);

    state(&w, "sensor.test_printer", "running");
    lv_obj_set_size(w.obj, 640, 560);
    lv_obj_update_layout(w.obj);
    click(w.obj, "Pause");
    advance(15501);
    CHECK(label(w.obj, "Command not confirmed") != NULL);
    click(w.obj, "Pause");
    sent(5, "button.test_pause");
    sim_host_set_online(false);
    advance(501);
    CHECK(label(w.obj, "Command not confirmed") != NULL);
    click(w.obj, "Pause");
    sent(5, NULL);
    sim_host_set_online(true);
    advance(501);
    sim_host_service_reset(ESP_FAIL);
    click(w.obj, "Pause");
    sent(1, "button.test_pause");
    CHECK(label(w.obj, "Could not send command") != NULL);
    sim_host_service_reset(ESP_OK);

    /* Confirmation must recheck current model/connection, even without a UI update. */
    click(w.obj, "Abort");
    state(NULL, "sensor.test_printer", "idle");
    click(lv_layer_top(), "Abort print");
    advance(1);
    sent(0, NULL);
    state(&w, "sensor.test_printer", "running");
    click(w.obj, "Abort");
    sim_host_set_online(false);
    click(lv_layer_top(), "Abort print");
    advance(1);
    sent(0, NULL);
    sim_host_set_online(true);
    state(&w, "button.test_pause", "unavailable");
    click(w.obj, "Pause");
    sent(0, NULL);
    state(&w, "button.test_pause", "unknown");
    click(w.obj, "Status");
    destroy(&w);

    /* Popup ownership survives resize/rebuild, queued backdrop close, page hide. */
    for (int i = 0; i < 12; i++) {
        w = create();
        click(w.obj, i % 2 ? "Abort" : "Status");
        lv_obj_set_size(w.obj, 480, 420);
        lv_obj_update_layout(w.obj);
        CHECK(lv_obj_get_child_count(lv_layer_top()) == 1);
        if (i % 3 == 0) {
            lv_obj_send_event(lv_obj_get_child(lv_layer_top(), 0), LV_EVENT_CLICKED, NULL);
        } else if (i % 3 == 1) {
            w_bambu_printer_set_visible(&w, false);
        }
        destroy(&w);
    }
    sent(0, NULL);
    printf("Bambu LVGL regression: %u checks passed\n", checks);
    return 0;
}
