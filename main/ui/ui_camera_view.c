/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#include "ui/ui_camera_view.h"
#include "ha/ha_camera_stream.h"
#include "ui/ui_display_protection.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"
#include "ui/ui_popup.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_typography.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    lv_obj_t *root, *image, *message;
    lv_timer_t *timer;
    ha_camera_stream_t *stream;
    ha_cover_result_t frame;
    char entity[APP_MAX_ENTITY_ID_LEN];
} camera_feed_t;

static bool covered(lv_obj_t *root)
{
    if (ui_display_protection_is_active() || !lv_obj_is_visible(root)) return true;
    lv_obj_t *top = lv_layer_top();
    lv_obj_t *owner = root;
    while (owner && lv_obj_get_parent(owner) != top) owner = lv_obj_get_parent(owner);
    bool above = owner == NULL;
    for (uint32_t i = 0; i < lv_obj_get_child_count(top); i++) {
        lv_obj_t *child = lv_obj_get_child(top, (int32_t)i);
        if (child == owner) { above = true; continue; }
        if (above && lv_obj_is_visible(child)) return true;
    }
    return false;
}

static void fit(camera_feed_t *f)
{
    if (!f->frame.valid) return;
    int w = lv_obj_get_width(f->root), h = lv_obj_get_height(f->root);
    uint32_t x = (uint32_t)w * 256U / f->frame.image.header.w;
    uint32_t y = (uint32_t)h * 256U / f->frame.image.header.h;
    uint32_t scale = x < y ? x : y;
    lv_image_set_scale(f->image, (uint16_t)(scale < 16 ? 16 : scale > 4096 ? 4096 : scale));
    lv_obj_center(f->image);
}

static void poll(lv_timer_t *timer)
{
    camera_feed_t *f = lv_timer_get_user_data(timer);
    if (covered(f->root)) {
        ha_camera_stream_stop(f->stream);
        f->stream = NULL;
        lv_label_set_text(f->message, ui_i18n_get("camera.paused", "Paused"));
        lv_obj_set_hidden(f->message, false);
        return;
    }
    if (!f->stream) {
        f->stream = ha_camera_stream_start(f->entity, lv_obj_get_width(f->root), lv_obj_get_height(f->root));
    }
    ha_camera_stream_status_t status = HA_CAMERA_WAITING;
    ha_cover_result_t next = {0};
    if (f->stream && ha_camera_stream_take(f->stream, &next, &status)) {
        lv_image_cache_drop(&f->frame.image);
        ha_cover_result_t old = f->frame;
        f->frame = next;
        lv_image_set_src(f->image, &f->frame.image);
        lv_obj_set_hidden(f->image, false);
        fit(f);
        ha_cover_result_release(&old);
    }
    const char *text = status == HA_CAMERA_UNSUPPORTED ? ui_i18n_get("camera.unsupported", "MJPEG unavailable") :
        status == HA_CAMERA_FAILED ? ui_i18n_get("camera.failed", "Camera connection lost") :
        status == HA_CAMERA_WAITING ? ui_i18n_get("camera.waiting", "Waiting for camera") :
        ui_i18n_get("camera.connecting", "Connecting...");
    lv_label_set_text(f->message, text);
    lv_obj_set_hidden(f->message, status == HA_CAMERA_LIVE && f->frame.valid);
}

static void feed_event(lv_event_t *event)
{
    camera_feed_t *f = lv_event_get_user_data(event);
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        lv_timer_delete(f->timer);
        ha_camera_stream_stop(f->stream);
        lv_image_cache_drop(&f->frame.image);
        ha_cover_result_release(&f->frame);
        free(f);
    } else if (lv_event_get_code(event) == LV_EVENT_SIZE_CHANGED) fit(f);
}

lv_obj_t *ui_camera_feed_create(lv_obj_t *parent, const char *entity, int width, int height)
{
    camera_feed_t *f = ui_calloc_prefer_psram(1, sizeof(*f));
    if (!f) return NULL;
    snprintf(f->entity, sizeof(f->entity), "%s", entity);
    f->root = lv_obj_create(parent);
    lv_obj_remove_style_all(f->root);
    lv_obj_set_size(f->root, width, height);
    lv_obj_set_style_bg_color(f->root, lv_color_hex(0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(f->root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(f->root, false);
    lv_obj_set_clickable(f->root, false);
    f->image = lv_image_create(f->root);
    lv_obj_set_hidden(f->image, true);
    f->message = lv_label_create(f->root);
    ui_text_set(f->message, UI_TEXT_META, false);
    lv_label_set_text(f->message, ui_i18n_get("camera.connecting", "Connecting..."));
    lv_label_set_long_mode(f->message, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(f->message, LV_PCT(90));
    lv_obj_set_style_text_align(f->message, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(f->message, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_bg_color(f->message, lv_color_hex(0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(f->message, LV_OPA_80, LV_PART_MAIN);
    lv_obj_center(f->message);
    f->timer = lv_timer_create(poll, 200, f);
    lv_obj_add_event_cb(f->root, feed_event, LV_EVENT_ALL, f);
    return f->root;
}

typedef struct {
    void (*closed)(void *);
    void *user;
} camera_view_t;

static void view_deleted(lv_event_t *event)
{
    camera_view_t *v = lv_event_get_user_data(event);
    if (v->closed) v->closed(v->user);
    free(v);
}

static void view_close(lv_event_t *event)
{
    lv_obj_delete_async(lv_event_get_user_data(event));
}

lv_obj_t *ui_camera_view_open(const char *entity, const char *title, void (*closed)(void *), void *user)
{
    camera_view_t *v = ui_calloc_prefer_psram(1, sizeof(*v));
    if (!v) return NULL;
    v->closed = closed; v->user = user;
    int w = lv_display_get_horizontal_resolution(NULL), h = lv_display_get_vertical_resolution(NULL);
    lv_obj_t *root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, w, h);
    lv_obj_set_style_bg_color(root, lv_color_hex(0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(root, false);
    lv_obj_add_event_cb(root, view_deleted, LV_EVENT_DELETE, v);
    int head = lv_font_get_line_height(ui_font_role(UI_TEXT_VALUE)) + 28;
    if (head < UI_POPUP_CLOSE_SIZE + 16) head = UI_POPUP_CLOSE_SIZE + 16;
    lv_obj_t *label = lv_label_create(root);
    ui_text_set(label, UI_TEXT_VALUE, false);
    lv_label_set_text(label, title && *title ? title : ui_i18n_get("camera.live", "Live camera"));
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, w - UI_POPUP_CLOSE_SIZE - 48);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 16, (head - lv_font_get_line_height(ui_font_role(UI_TEXT_VALUE))) / 2);
    lv_obj_t *close = ui_popup_close_button(root, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), view_close, root);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -12, (head - UI_POPUP_CLOSE_SIZE) / 2);
    lv_obj_t *feed = ui_camera_feed_create(root, entity, w, h - head);
    if (feed) lv_obj_set_pos(feed, 0, head);
    else { lv_obj_delete(root); return NULL; }
    return root;
}
