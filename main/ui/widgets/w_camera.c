/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Camera tile: snapshots by default, with an optional MJPEG feed and full-screen
 * view. Both transports yield to the Home Assistant connection.
 */
#include "ui/ui_widget_factory.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "ha/ha_cover_fetcher.h"
#include "ui/fonts/app_symbols.h"
#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include "ui/ui_i18n.h"
#include "ui/ui_camera_view.h"
#include "ui/ui_memory.h"
#include "util/log_tags.h"

#define CAMERA_REFRESH_MS 10000
/* The shared fetcher may drop a queued request without calling back (its queue is short, other tiles fetch pictures too).
 * A request unanswered for this long is forgotten, otherwise the tile would wait for it forever. */
#define CAMERA_INFLIGHT_TIMEOUT_MS 30000
#define CAMERA_URL_LEN 256

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    char url[CAMERA_URL_LEN];
    lv_obj_t *card;
    lv_obj_t *img;
    lv_obj_t *strip;
    lv_obj_t *title;
    lv_obj_t *placeholder;
    lv_image_dsc_t dsc;
    lv_timer_t *timer;
    lv_obj_t *feed, *view;
    bool live;
    bool inflight;
    bool have_image;
    bool unavailable;
    int64_t last_request_ms;
} w_camera_ctx_t;

static int64_t camera_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void camera_release(w_camera_ctx_t *ctx)
{
    if (ctx->dsc.data != NULL) {
        heap_caps_free((void *)ctx->dsc.data);
        ctx->dsc.data = NULL;
        ctx->dsc.data_size = 0;
    }
}

static void camera_layout(w_camera_ctx_t *ctx)
{
    lv_obj_update_layout(ctx->card);
    const lv_coord_t w = lv_obj_get_width(ctx->card);
    const lv_coord_t h = lv_obj_get_height(ctx->card);
    if (ctx->feed) lv_obj_set_size(ctx->feed, w, h);
    const lv_coord_t strip_h = lv_font_get_line_height(ui_font_role(UI_TEXT_META)) + 12;
    lv_obj_set_hidden(ctx->strip, !ui_widget_name_enabled(ctx->title));
    lv_obj_set_size(ctx->strip, w, strip_h);
    lv_obj_set_pos(ctx->strip, 0, h - strip_h);
    lv_obj_set_width(ctx->title, w - 24);
    lv_obj_align(ctx->title, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_center(ctx->placeholder);
    if (ctx->have_image && ctx->dsc.header.w > 0 && ctx->dsc.header.h > 0) {
        /* Fill the tile (crop the overhang) around the center. */
        int32_t zw = (int32_t)((int64_t)w * 256 / ctx->dsc.header.w);
        int32_t zh = (int32_t)((int64_t)h * 256 / ctx->dsc.header.h);
        int32_t zoom = zw > zh ? zw : zh;
        if (zoom < 16) zoom = 16;
        lv_image_set_scale(ctx->img, (uint16_t)zoom);
        lv_obj_align(ctx->img, LV_ALIGN_CENTER, 0, 0);
    }
}

static void camera_result_cb(void *user, const ha_cover_result_t *result)
{
    w_camera_ctx_t *ctx = (w_camera_ctx_t *)user;
    if (ctx == NULL) {
        return;
    }
    ctx->inflight = false;
    if (result == NULL || !result->valid) {
        ESP_LOGW(TAG_UI, "camera %s: no picture (download or decoding failed)", ctx->entity_id);
        return; /* keep the last picture */
    }
    camera_release(ctx);
    ctx->dsc = result->image;
    ctx->have_image = true;
    lv_image_set_src(ctx->img, &ctx->dsc);
    lv_obj_set_hidden(ctx->img, false);
    lv_obj_set_hidden(ctx->placeholder, true);
    camera_layout(ctx);
}

static void camera_request(w_camera_ctx_t *ctx)
{
    if (ctx == NULL || ctx->live || ctx->url[0] == '\0' || ctx->unavailable) {
        return;
    }
    if (ctx->inflight) {
        if (camera_now_ms() - ctx->last_request_ms < CAMERA_INFLIGHT_TIMEOUT_MS) {
            return;
        }
        ESP_LOGW(TAG_UI, "camera %s: no answer for %d s, asking again", ctx->entity_id, CAMERA_INFLIGHT_TIMEOUT_MS / 1000);
        ctx->inflight = false;
    }
    lv_coord_t w = lv_obj_get_width(ctx->card);
    lv_coord_t h = lv_obj_get_height(ctx->card);
    if (w > 480) w = 480;
    if (h > 320) h = 320;
    ctx->inflight = true;
    ctx->last_request_ms = camera_now_ms();
    if (ha_cover_fetcher_request(ctx->url, w, h, camera_result_cb, ctx) != ESP_OK) {
        ctx->inflight = false;
    }
}

static void camera_timer_cb(lv_timer_t *timer)
{
    w_camera_ctx_t *ctx = (w_camera_ctx_t *)lv_timer_get_user_data(timer);
    if (ctx == NULL || ctx->card == NULL || lv_obj_is_hidden(ctx->card)) {
        return;
    }
    /* Skip while another page is shown. */
    if (!lv_obj_is_visible(ctx->card)) {
        return;
    }
    camera_request(ctx);
}

static void camera_view_closed(void *user)
{
    ((w_camera_ctx_t *)user)->view = NULL;
}

static void camera_clicked_cb(lv_event_t *e)
{
    w_camera_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx->live) {
        if (!ctx->view) ctx->view = ui_camera_view_open(ctx->entity_id, lv_label_get_text(ctx->title),
                                                      camera_view_closed, ctx);
    } else camera_request(ctx);
}

static void camera_event_cb(lv_event_t *event)
{
    w_camera_ctx_t *ctx = (w_camera_ctx_t *)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DELETE && ctx != NULL) {
        if (ctx->view) lv_obj_delete(ctx->view);
        ha_cover_fetcher_cancel(ctx);
        if (ctx->timer != NULL) {
            lv_timer_del(ctx->timer);
        }
        camera_release(ctx);
        free(ctx);
    } else if (code == LV_EVENT_SIZE_CHANGED && ctx != NULL) {
        camera_layout(ctx);
    }
}

esp_err_t w_camera_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    w_camera_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    snprintf(ctx->entity_id, sizeof(ctx->entity_id), "%s", def->entity_id);
    ctx->live = def->camera_live;
    ctx->unavailable = true;

    lv_obj_t *card = lv_obj_create(parent);
    ctx->card = card;
    out_instance->obj = card;
    out_instance->ctx = ctx;
    out_instance->visible = true;
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, def->w, def->h);
    lv_obj_set_pos(card, def->x, def->y);
    lv_obj_set_scrollable(card, false);
    lv_obj_set_clickable(card, true);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(card, APP_UI_CARD_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(card, true, LV_PART_MAIN);

    ctx->img = lv_image_create(card);
    lv_obj_set_hidden(ctx->img, true);
    lv_obj_set_clickable(ctx->img, false);

    ctx->placeholder = lv_label_create(card);
    lv_label_set_text(ctx->placeholder, APP_SYMBOL_IMAGE);
    lv_obj_set_style_text_font(ctx->placeholder, APP_FONT_SYMBOL_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->placeholder, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);

    if (ctx->live) {
        ctx->feed = ui_camera_feed_create(card, ctx->entity_id, def->w, def->h);
        lv_obj_set_hidden(ctx->placeholder, ctx->feed != NULL);
    }

    ctx->strip = lv_obj_create(card);
    lv_obj_remove_style_all(ctx->strip);
    lv_obj_set_clickable(ctx->strip, false);
    lv_obj_set_scrollable(ctx->strip, false);
    lv_obj_set_style_bg_color(ctx->strip, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->strip, LV_OPA_50, LV_PART_MAIN);
    ctx->title = ui_widget_name_create(ctx->strip, !def->hide_name);
    lv_label_set_text(ctx->title, def->title[0] ? def->title : def->entity_id);
    lv_obj_set_style_text_font(ctx->title, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    ui_widget_name_one_line(ctx->title);

    lv_obj_add_event_cb(card, camera_clicked_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(card, camera_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, camera_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    ctx->timer = lv_timer_create(camera_timer_cb, CAMERA_REFRESH_MS, ctx);
    camera_layout(ctx);
    return ESP_OK;
}

void w_camera_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->ctx == NULL || state == NULL) {
        return;
    }
    w_camera_ctx_t *ctx = (w_camera_ctx_t *)instance->ctx;
    if (strncmp(state->entity_id, ctx->entity_id, APP_MAX_ENTITY_ID_LEN) != 0) {
        return;
    }
    ctx->unavailable = strcmp(state->state, "unavailable") == 0;
    cJSON *attrs = state->attributes_json[0] != '\0' ? cJSON_Parse(state->attributes_json) : NULL;
    bool first = ctx->url[0] == '\0';
    if (attrs != NULL) {
        cJSON *pic = cJSON_GetObjectItemCaseSensitive(attrs, "entity_picture");
        if (cJSON_IsString(pic) && pic->valuestring != NULL) {
            strlcpy(ctx->url, pic->valuestring, sizeof(ctx->url));
        }
        cJSON_Delete(attrs);
    }
    if (first && ctx->url[0] != '\0') {
        camera_request(ctx);
    }
}

void w_camera_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    ((w_camera_ctx_t *)instance->ctx)->unavailable = true;
}

void w_camera_set_visible(ui_widget_instance_t *instance, bool visible)
{
    w_camera_ctx_t *ctx = instance->ctx;
    if (!visible && ctx->view) lv_obj_delete(ctx->view);
}
