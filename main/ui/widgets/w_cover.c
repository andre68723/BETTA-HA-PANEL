/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Cover tile (blinds, shutters, shades): name and state on the left, up / stop /
 * down buttons on the right, and a thin bar with the current position.
 */
#include "ui/ui_widget_factory.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "ha/ha_client.h"
#include "ui/fonts/app_symbols.h"
#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"

#define W_COVER_FEAT_STOP 8U

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    lv_obj_t *card;
    lv_obj_t *title;
    lv_obj_t *state;
    lv_obj_t *bar;
    lv_obj_t *btn_up;
    lv_obj_t *btn_stop;
    lv_obj_t *btn_down;
    char state_raw[24];
    int position; /* 0 = closed .. 100 = open, -1 = unknown */
    uint32_t features;
    bool features_known;
    bool unavailable;
} w_cover_ctx_t;

static void cover_call(w_cover_ctx_t *ctx, const char *service)
{
    if (ctx == NULL || ctx->entity_id[0] == '\0' || ctx->unavailable) {
        return;
    }
    char payload[APP_MAX_ENTITY_ID_LEN + 24];
    snprintf(payload, sizeof(payload), "{\"entity_id\":\"%s\"}", ctx->entity_id);
    (void)ha_client_call_service("cover", service, payload);
}

static void cover_up_cb(lv_event_t *e)
{
    cover_call((w_cover_ctx_t *)lv_event_get_user_data(e), "open_cover");
}

static void cover_stop_cb(lv_event_t *e)
{
    cover_call((w_cover_ctx_t *)lv_event_get_user_data(e), "stop_cover");
}

static void cover_down_cb(lv_event_t *e)
{
    cover_call((w_cover_ctx_t *)lv_event_get_user_data(e), "close_cover");
}

static lv_obj_t *cover_make_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t cb, w_cover_ctx_t *ctx)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_scrollable(btn, false);
    lv_obj_set_event_bubble(btn, false);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_font(label, APP_FONT_SYMBOL_SMALL, LV_PART_MAIN);
    lv_obj_center(label);
    return btn;
}

static const char *cover_state_text(const w_cover_ctx_t *ctx)
{
    if (ctx->unavailable) {
        return ui_i18n_get("common.unavailable", "unavailable");
    }
    if (strcmp(ctx->state_raw, "open") == 0) {
        return ui_i18n_get("cover.open", "Open");
    }
    if (strcmp(ctx->state_raw, "closed") == 0) {
        return ui_i18n_get("cover.closed", "Closed");
    }
    if (strcmp(ctx->state_raw, "opening") == 0) {
        return ui_i18n_get("cover.opening", "Opening");
    }
    if (strcmp(ctx->state_raw, "closing") == 0) {
        return ui_i18n_get("cover.closing", "Closing");
    }
    return ui_i18n_get("common.unknown", "unknown");
}

static void cover_style_button(lv_obj_t *btn, bool active, bool enabled)
{
    ui_style_action_button(btn, lv_obj_get_child(btn, 0), active ? UI_CONTROL_ACTIVE : UI_CONTROL_ACTION, enabled);
}

static void cover_layout(w_cover_ctx_t *ctx)
{
    lv_obj_update_layout(ctx->card);
    const lv_coord_t w = lv_obj_get_width(ctx->card);
    const lv_coord_t h = lv_obj_get_height(ctx->card);
    const bool show_stop = !ctx->features_known || (ctx->features & W_COVER_FEAT_STOP) != 0U;
    /* Button size used to prefer a comfortable 44 px purely from height, ignoring how much
     * width was actually left over for the title/state text next to it -- at the widget's
     * documented minimum width (240 px) that squeezed "Cover" itself down to "Co...". Cap it
     * by width too on a narrow tile. */
    lv_coord_t btn = h - 24;
    if (btn > 44) {
        btn = 44;
    }
    if (w < 300 && btn > 40) {
        btn = 40;
    }
    if (btn < 32) {
        btn = 32;
    }
    const lv_coord_t gap = 10;
    const int shown = show_stop ? 3 : 2;
    const lv_coord_t buttons_w = shown * btn + (shown - 1) * gap;
    const lv_coord_t pad = 14;
    const lv_coord_t text_gap = 8; /* space between the text column and the buttons; narrower
                                       than pad on purpose, this is not a card edge */
    lv_coord_t x = w - pad - buttons_w;
    const lv_coord_t y = (h - btn) / 2;

    lv_obj_t *btns[3] = {ctx->btn_up, ctx->btn_stop, ctx->btn_down};
    for (int i = 0; i < 3; i++) {
        if (i == 1 && !show_stop) {
            lv_obj_set_hidden(btns[i], true);
            continue;
        }
        lv_obj_set_hidden(btns[i], false);
        lv_obj_set_size(btns[i], btn, btn);
        lv_obj_set_pos(btns[i], x, y);
        x += btn + gap;
    }

    const lv_coord_t text_w = w - pad - buttons_w - pad - text_gap;
    lv_obj_set_width(ctx->title, text_w > 40 ? text_w : 40);
    lv_obj_set_width(ctx->state, text_w > 40 ? text_w : 40);
    const lv_coord_t title_h = ui_widget_name_height(ctx->title, ui_font_role(UI_TEXT_META));
    const lv_coord_t state_h = lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    const lv_coord_t bar_h = ui_style_traits()->touch_tracks ? 12 : 6;
    const lv_coord_t block_h = title_h + ui_widget_name_gap(ctx->title, 2) + state_h + 6 + bar_h;
    const lv_coord_t top = (h - block_h) / 2;
    lv_obj_set_height(ctx->title, title_h);
    lv_obj_set_height(ctx->state, state_h);
    lv_obj_set_pos(ctx->title, pad, top);
    lv_obj_set_pos(ctx->state, pad, top + title_h + ui_widget_name_gap(ctx->title, 2));
    lv_obj_set_size(ctx->bar, text_w > 40 ? text_w : 40, bar_h);
    lv_obj_set_pos(ctx->bar, pad, top + title_h + ui_widget_name_gap(ctx->title, 2) + state_h + 6);
}

static void cover_render(w_cover_ctx_t *ctx)
{
    ui_style_card_set_active(ctx->card, !ctx->unavailable && (strcmp(ctx->state_raw, "open") == 0 ||
                                                               strcmp(ctx->state_raw, "opening") == 0));
    cover_layout(ctx); /* sets the state label's width, which the text choice below needs */
    char text[48];
    if (!ctx->unavailable && ctx->position >= 0) {
        snprintf(text, sizeof(text), "%s  %d %%", cover_state_text(ctx), ctx->position);
        /* At the minimum width (240 px) the three buttons leave about 64 px of text, and
         * "Offen  34 %" came out as "Offen ..." -- the position, the one number worth reading from
         * across the room, was the part cut off. Show just the position then; the bar and the lit
         * up/down button still tell whether the cover is open or moving. */
        lv_point_t size;
        lv_text_get_size(&size, text, ui_font_role(UI_TEXT_META), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x > lv_obj_get_width(ctx->state)) {
            snprintf(text, sizeof(text), "%d %%", ctx->position);
        }
    } else {
        snprintf(text, sizeof(text), "%s", cover_state_text(ctx));
    }
    lv_label_set_text(ctx->state, text);
    lv_bar_set_value(ctx->bar, ctx->position >= 0 ? ctx->position : 0, LV_ANIM_OFF);
    lv_obj_set_style_opa(ctx->bar, ctx->position >= 0 && !ctx->unavailable ? LV_OPA_COVER : LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_opa(ctx->bar, ctx->position >= 0 && !ctx->unavailable ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_INDICATOR);
    cover_style_button(ctx->btn_up, strcmp(ctx->state_raw, "opening") == 0, !ctx->unavailable);
    cover_style_button(ctx->btn_stop, false, !ctx->unavailable);
    cover_style_button(ctx->btn_down, strcmp(ctx->state_raw, "closing") == 0, !ctx->unavailable);
    const lv_opa_t opa = ctx->unavailable ? LV_OPA_40 : LV_OPA_COVER;
    lv_obj_set_style_opa(ctx->btn_up, opa, LV_PART_MAIN);
    lv_obj_set_style_opa(ctx->btn_stop, opa, LV_PART_MAIN);
    lv_obj_set_style_opa(ctx->btn_down, opa, LV_PART_MAIN);
}

static void cover_event_cb(lv_event_t *event)
{
    w_cover_ctx_t *ctx = (w_cover_ctx_t *)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DELETE) {
        free(ctx);
    } else if (code == LV_EVENT_SIZE_CHANGED && ctx != NULL) {
        cover_layout(ctx);
    }
}

esp_err_t w_cover_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    w_cover_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    snprintf(ctx->entity_id, sizeof(ctx->entity_id), "%s", def->entity_id);
    ctx->position = -1;
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
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(card, APP_UI_CARD_RADIUS, LV_PART_MAIN);

    ctx->title = ui_widget_name_create(card, !def->hide_name);
    const char *title_text = def->title[0] ? def->title : def->entity_id;
    ui_style_title_fit(ctx->title, title_text);
    lv_obj_set_style_text_font(ctx->title, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->title, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->title, LV_LABEL_LONG_DOT);

    ctx->state = lv_label_create(card);
    lv_obj_set_style_text_font(ctx->state, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->state, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->state, LV_LABEL_LONG_DOT);

    ctx->bar = lv_bar_create(card);
    lv_bar_set_range(ctx->bar, 0, 100);
    ui_style_inset_track(ctx->bar, lv_color_mix(lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 26));
    lv_obj_set_style_bg_color(ctx->bar, ui_style_control_on(lv_color_hex(APP_UI_COLOR_STATE_ON)), LV_PART_INDICATOR);
    if (ui_style_is_flat()) {
        lv_obj_set_style_bg_color(ctx->bar, lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ctx->bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ctx->bar, LV_OPA_COVER, LV_PART_INDICATOR);
    }

    ctx->btn_up = cover_make_button(card, APP_SYMBOL_ARROW_UP, cover_up_cb, ctx);
    ctx->btn_stop = cover_make_button(card, APP_SYMBOL_STOP, cover_stop_cb, ctx);
    ctx->btn_down = cover_make_button(card, APP_SYMBOL_ARROW_DOWN, cover_down_cb, ctx);

    lv_obj_add_event_cb(card, cover_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, cover_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    cover_render(ctx);
    return ESP_OK;
}

void w_cover_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->ctx == NULL || state == NULL) {
        return;
    }
    w_cover_ctx_t *ctx = (w_cover_ctx_t *)instance->ctx;
    if (strncmp(state->entity_id, ctx->entity_id, APP_MAX_ENTITY_ID_LEN) != 0) {
        return;
    }
    ctx->unavailable = strcmp(state->state, "unavailable") == 0 || strcmp(state->state, "unknown") == 0;
    strlcpy(ctx->state_raw, state->state, sizeof(ctx->state_raw));
    ctx->position = -1;
    cJSON *attrs = state->attributes_json[0] != '\0' ? cJSON_Parse(state->attributes_json) : NULL;
    if (attrs != NULL) {
        cJSON *pos = cJSON_GetObjectItemCaseSensitive(attrs, "current_position");
        if (cJSON_IsNumber(pos)) {
            int p = (int)(pos->valuedouble + 0.5);
            ctx->position = p < 0 ? 0 : (p > 100 ? 100 : p);
        }
        cJSON *feat = cJSON_GetObjectItemCaseSensitive(attrs, "supported_features");
        if (cJSON_IsNumber(feat)) {
            ctx->features = (uint32_t)feat->valuedouble;
            ctx->features_known = true;
        }
        cJSON_Delete(attrs);
    }
    cover_render(ctx);
}

void w_cover_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_cover_ctx_t *ctx = (w_cover_ctx_t *)instance->ctx;
    ctx->unavailable = true;
    cover_render(ctx);
}
