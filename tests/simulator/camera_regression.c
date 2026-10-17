/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#include "ui/ui_widget_factory.h"
#include "ui/ui_camera_view.h"
#include "host_adapters.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

esp_err_t w_camera_create(const ui_widget_def_t *, lv_obj_t *, ui_widget_instance_t *);
void w_camera_set_visible(ui_widget_instance_t *, bool);
static unsigned checks;
static uint32_t now;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "Camera regression %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static uint32_t tick(void) { return now; }
static void advance(void) { now += 250; lv_timer_handler(); }

int sim_camera_regression(void)
{
    now = lv_tick_get(); lv_tick_set_cb(tick);
    sim_host_set_online(true);
    unsigned start = sim_camera_starts();
    ui_widget_def_t def = {.w = 320, .h = 180};
    snprintf(def.entity_id, sizeof(def.entity_id), "camera.synthetic");
    ui_widget_instance_t widget = {0};
    CHECK(w_camera_create(&def, lv_screen_active(), &widget) == ESP_OK);
    advance();
    lv_obj_send_event(widget.obj, LV_EVENT_CLICKED, NULL);
    CHECK(sim_camera_starts() == start && lv_obj_get_child_count(lv_layer_top()) == 0);
    lv_obj_delete(widget.obj);
    def.camera_live = true;
    CHECK(w_camera_create(&def, lv_screen_active(), &widget) == ESP_OK);
    for (int i = 0; i < 10; i++) advance(); /* several buffer replacements */
    CHECK(sim_camera_active() == 1);
    sim_camera_status(HA_CAMERA_FAILED); advance();
    sim_camera_status(HA_CAMERA_UNSUPPORTED); advance();
    sim_camera_status(HA_CAMERA_LIVE); advance();
    lv_obj_set_hidden(widget.obj, true); advance();
    CHECK(sim_camera_active() == 0);
    lv_obj_set_hidden(widget.obj, false); advance();
    CHECK(sim_camera_active() == 1);
    sim_camera_sleep(true); advance();
    CHECK(sim_camera_active() == 0);
    sim_camera_sleep(false); advance();
    CHECK(sim_camera_active() == 1);
    lv_obj_send_event(widget.obj, LV_EVENT_CLICKED, NULL); advance(); advance();
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 1 && sim_camera_active() == 1);
    /* The fullscreen feed replaces the tile's stream; owner hide closes it. */
    w_camera_set_visible(&widget, false);
    lv_obj_set_hidden(widget.obj, true); advance();
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0 && sim_camera_active() == 0);
    lv_obj_set_hidden(widget.obj, false); advance();
    for (int i = 0; i < 12; i++) {
        lv_obj_send_event(widget.obj, LV_EVENT_CLICKED, NULL); advance();
        CHECK(sim_camera_active() <= 1);
        w_camera_set_visible(&widget, false); advance();
        CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);
    }
    lv_obj_send_event(widget.obj, LV_EVENT_CLICKED, NULL); advance();
    lv_obj_delete(widget.obj); advance();
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0 && sim_camera_active() == 0);
    CHECK(sim_host_service_calls()->count == 0);
    printf("camera lifecycle: %u checks passed\n", checks);
    return 0;
}
