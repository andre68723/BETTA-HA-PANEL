/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Door / window contact tile: a small door or window pictogram (closed or
 * ajar), the name and the state in words. Doors read "Opened", windows and
 * other contacts read "Open".
 */
#include "ui/ui_widget_factory.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"
#include "ui/ui_history_popup.h"

/* Theme colors: open uses the warm "on" accent, closed the "ok" green. */
#define CONTACT_OPEN_COLOR APP_UI_COLOR_LIGHT_ICON_ON
#define CONTACT_CLOSED_COLOR APP_UI_COLOR_OK

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    lv_obj_t *card;
    lv_obj_t *frame;
    lv_obj_t *leaf;
    lv_obj_t *bar_h; /* window cross */
    lv_obj_t *bar_v;
    lv_obj_t *handle;
    lv_obj_t *title;
    lv_obj_t *state;
    bool open;
    bool door;   /* door wording and pictogram */
    bool unavailable;
    bool history_popup;
} w_contact_ctx_t;

static lv_obj_t *contact_rect(lv_obj_t *parent, lv_coord_t radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    lv_obj_set_style_radius(o, radius, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    return o;
}

static const char *contact_state_text(const w_contact_ctx_t *ctx);

static void contact_layout(w_contact_ctx_t *ctx)
{
    lv_obj_update_layout(ctx->card);
    const lv_coord_t w = lv_obj_get_width(ctx->card);
    const lv_coord_t h = lv_obj_get_height(ctx->card);
    lv_coord_t side = h - 24;
    if (side > 56) side = 56;
    if (side > w / 5) side = w / 5; /* a narrow tile keeps its width for the state word ("Geschlossen") */
    if (side < 28) side = 28;
    const lv_coord_t fw = ctx->door ? (side * 62) / 100 : (side * 85) / 100;
    const lv_coord_t fh = side;
    const lv_coord_t pad = 14;
    const lv_coord_t fx = pad + (side - fw) / 2;
    const lv_coord_t fy = (h - fh) / 2;
    const lv_coord_t bw = 3; /* frame border */

    lv_obj_set_size(ctx->frame, fw, fh);
    lv_obj_set_pos(ctx->frame, fx, fy);
    lv_obj_set_style_border_width(ctx->frame, bw, LV_PART_MAIN);

    const lv_coord_t iw = fw - 2 * bw;
    const lv_coord_t ih = fh - 2 * bw;
    /* Ajar: the leaf is seen in perspective, a narrow slab on the hinge side. */
    const lv_coord_t leaf_w = ctx->open ? iw * 38 / 100 : iw;
    lv_obj_set_size(ctx->leaf, leaf_w, ih);
    lv_obj_set_pos(ctx->leaf, bw, bw);
    lv_obj_set_size(ctx->handle, 4, 4);
    lv_obj_set_pos(ctx->handle, bw + leaf_w - 8, bw + ih / 2);

    const bool cross = !ctx->door && !ctx->open;
    if (cross) {
        lv_obj_set_hidden(ctx->bar_h, false);
        lv_obj_set_hidden(ctx->bar_v, false);
        lv_obj_set_size(ctx->bar_h, iw, 2);
        lv_obj_set_pos(ctx->bar_h, bw, bw + ih / 2 - 1);
        lv_obj_set_size(ctx->bar_v, 2, ih);
        lv_obj_set_pos(ctx->bar_v, bw + iw / 2 - 1, bw);
    } else {
        lv_obj_set_hidden(ctx->bar_h, true);
        lv_obj_set_hidden(ctx->bar_v, true);
    }
    if (!ctx->door) {
        lv_obj_set_hidden(ctx->handle, true);
    }
    if (ctx->door) {
        lv_obj_set_hidden(ctx->handle, false);
    }

    /* The text starts after the drawn frame (a door is narrower than the symbol's square), and the state shrinks to the
     * board's smallest text before it ends in dots: "Geschlossen" did not fit a 180 px tile. */
    const lv_coord_t text_x = fx + fw + 12;
    lv_coord_t text_w = w - text_x - pad;
    if (text_w < 40) text_w = 40;
    const lv_coord_t title_h = ui_widget_name_height(ctx->title, ui_font_role(UI_TEXT_META));
    const lv_coord_t state_h = lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    ui_text_record(ctx->state, UI_TEXT_META, true);
    lv_obj_set_style_text_font(ctx->state, ui_text_fit(contact_state_text(ctx), ui_font_role(UI_TEXT_META), text_w, state_h),
                               LV_PART_MAIN);
    const lv_coord_t top = (h - (title_h + ui_widget_name_gap(ctx->title, 2) + state_h)) / 2;
    lv_obj_set_size(ctx->title, text_w, title_h);
    lv_obj_set_size(ctx->state, text_w, state_h);
    lv_obj_set_pos(ctx->title, text_x, top);
    lv_obj_set_pos(ctx->state, text_x, top + title_h + ui_widget_name_gap(ctx->title, 2));
}

static const char *contact_state_text(const w_contact_ctx_t *ctx)
{
    if (ctx->unavailable) {
        return ui_i18n_get("common.unavailable", "unavailable");
    }
    if (ctx->open) {
        return ctx->door ? ui_i18n_get("contact.opened", "Opened") : ui_i18n_get("contact.open", "Open");
    }
    return ui_i18n_get("contact.closed", "Closed");
}

static void contact_render(w_contact_ctx_t *ctx)
{
    const lv_color_t accent = lv_color_hex(ctx->unavailable ? APP_UI_COLOR_TEXT_MUTED
                                           : (ctx->open ? CONTACT_OPEN_COLOR : CONTACT_CLOSED_COLOR));
    lv_obj_set_style_border_color(ctx->frame, accent, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->frame, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ctx->leaf, lv_color_mix(accent, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), ctx->open ? 150 : 110), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ctx->bar_h, accent, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ctx->bar_v, accent, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ctx->handle, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->state, accent, LV_PART_MAIN);
    lv_label_set_text(ctx->state, contact_state_text(ctx));
    lv_obj_set_style_border_color(ctx->card,
        ctx->open && !ctx->unavailable ? lv_color_mix(accent, lv_color_hex(APP_UI_COLOR_CARD_BORDER), 110)
                                       : lv_color_hex(APP_UI_COLOR_CARD_BORDER), LV_PART_MAIN);
    contact_layout(ctx);
}

static void contact_event_cb(lv_event_t *event)
{
    w_contact_ctx_t *ctx = (w_contact_ctx_t *)lv_event_get_user_data(event);
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        free(ctx);
    } else if (lv_event_get_code(event) == LV_EVENT_SIZE_CHANGED && ctx != NULL) {
        contact_layout(ctx);
    } else if (lv_event_get_code(event) == LV_EVENT_CLICKED && ctx != NULL && ctx->history_popup) {
        ui_history_popup_open(ctx->entity_id, lv_label_get_text(ctx->title),
                              lv_label_get_text(ctx->state), false, ctx->door ? "door" : "window");
    }
}

esp_err_t w_contact_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    w_contact_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    snprintf(ctx->entity_id, sizeof(ctx->entity_id), "%s", def->entity_id);
    ctx->history_popup = def->history_popup;
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
    lv_obj_set_clickable(card, ctx->history_popup);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_radius(card, APP_UI_CARD_RADIUS, LV_PART_MAIN);

    ctx->frame = contact_rect(card, 4);
    lv_obj_set_style_bg_opa(ctx->frame, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(ctx->frame, LV_OPA_COVER, LV_PART_MAIN);
    ctx->leaf = contact_rect(ctx->frame, 2);
    ctx->bar_h = contact_rect(ctx->frame, 0);
    ctx->bar_v = contact_rect(ctx->frame, 0);
    ctx->handle = contact_rect(ctx->frame, LV_RADIUS_CIRCLE);

    ctx->title = ui_widget_name_create(card, !def->hide_name);
    lv_label_set_text(ctx->title, def->title[0] ? def->title : def->entity_id);
    lv_obj_set_style_text_font(ctx->title, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->title, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->title, LV_LABEL_LONG_DOT);
    ctx->state = lv_label_create(card);
    lv_obj_set_style_text_font(ctx->state, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->state, LV_LABEL_LONG_DOT);

    lv_obj_add_event_cb(card, contact_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, contact_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    if (ctx->history_popup) lv_obj_add_event_cb(card, contact_event_cb, LV_EVENT_CLICKED, ctx);
    contact_render(ctx);
    return ESP_OK;
}

void w_contact_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->ctx == NULL || state == NULL) {
        return;
    }
    w_contact_ctx_t *ctx = (w_contact_ctx_t *)instance->ctx;
    if (strncmp(state->entity_id, ctx->entity_id, APP_MAX_ENTITY_ID_LEN) != 0) {
        return;
    }
    ctx->unavailable = strcmp(state->state, "unavailable") == 0 || strcmp(state->state, "unknown") == 0;
    ctx->open = strcmp(state->state, "on") == 0 || strcmp(state->state, "open") == 0;
    cJSON *attrs = state->attributes_json[0] != '\0' ? cJSON_Parse(state->attributes_json) : NULL;
    if (attrs != NULL) {
        cJSON *dc = cJSON_GetObjectItemCaseSensitive(attrs, "device_class");
        if (cJSON_IsString(dc) && dc->valuestring != NULL) {
            ctx->door = strcmp(dc->valuestring, "door") == 0 || strcmp(dc->valuestring, "garage_door") == 0;
        }
        cJSON_Delete(attrs);
    }
    if (ctx->door == false && (strstr(ctx->entity_id, "door") != NULL || strstr(ctx->entity_id, "tuer") != NULL) && strstr(ctx->entity_id, "window") == NULL) {
        ctx->door = true; /* no device class: guess from the name (tuer/door) */
    }
    contact_render(ctx);
}

void w_contact_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_contact_ctx_t *ctx = (w_contact_ctx_t *)instance->ctx;
    ctx->unavailable = true;
    contact_render(ctx);
}
