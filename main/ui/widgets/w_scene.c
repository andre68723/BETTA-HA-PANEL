/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Scene / script / button trigger tile: a momentary tile with a play glyph and
 * the name. A tap runs scene.turn_on, script.turn_on or button.press and the
 * tile flashes its accent colour, then fades back over 400 ms. Asked for in
 * cptkirki/BETTA-HA-PANEL issue 8; the flash follows the trigger card idea of
 * jtenniswood/espcontrol (nothing copied).
 */
#include "ui/ui_widget_factory.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ha/ha_client.h"
#include "ui/fonts/app_symbols.h"
#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    lv_obj_t *card;
    lv_obj_t *circle;
    lv_obj_t *glyph;
    lv_obj_t *title;
    lv_obj_t *kind;
    bool unavailable;
} w_scene_ctx_t;

static void scene_flash_exec(void *var, int32_t v)
{
    lv_obj_t *card = (lv_obj_t *)var;
    lv_obj_set_style_bg_color(card, lv_color_mix(lv_color_lighten(lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), 24), lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), (uint8_t)v), LV_PART_MAIN);
}

static void scene_clicked_cb(lv_event_t *event)
{
    w_scene_ctx_t *ctx = (w_scene_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL || ctx->entity_id[0] == '\0' || ctx->unavailable) {
        return;
    }
    const char *dot = strchr(ctx->entity_id, '.');
    if (dot == NULL) {
        return;
    }
    char domain[16];
    snprintf(domain, sizeof(domain), "%.*s", (int)(dot - ctx->entity_id) < 15 ? (int)(dot - ctx->entity_id) : 15, ctx->entity_id);
    const char *service = strcmp(domain, "button") == 0 || strcmp(domain, "input_button") == 0 ? "press"
                          : (strcmp(domain, "lock") == 0 ? "unlock" : "turn_on");
    char payload[APP_MAX_ENTITY_ID_LEN + 24];
    snprintf(payload, sizeof(payload), "{\"entity_id\":\"%s\"}", ctx->entity_id);
    (void)ha_client_call_service(domain, service, payload);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ctx->card);
    lv_anim_set_exec_cb(&a, scene_flash_exec);
    lv_anim_set_values(&a, 255, 0);
    lv_anim_set_duration(&a, 400);
    lv_anim_start(&a);
}

static void scene_layout(w_scene_ctx_t *ctx)
{
    lv_obj_update_layout(ctx->card);
    const lv_coord_t w = lv_obj_get_width(ctx->card);
    const lv_coord_t h = lv_obj_get_height(ctx->card);
    lv_coord_t side = h - 28;
    if (side > 48) side = 48;
    if (side < 26) side = 26;
    const lv_coord_t pad = 14;
    lv_obj_set_size(ctx->circle, side, side);
    lv_obj_set_pos(ctx->circle, pad, (h - side) / 2);
    lv_obj_center(ctx->glyph);
    const lv_coord_t text_x = pad + side + 14;
    lv_coord_t text_w = w - text_x - pad;
    if (text_w < 40) text_w = 40;
    const lv_coord_t title_h = ui_widget_name_height(ctx->title, ui_font_role(UI_TEXT_META));
    const lv_coord_t kind_h = lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    const lv_coord_t top = (h - (title_h + ui_widget_name_gap(ctx->title, 2) + kind_h)) / 2;
    lv_obj_set_size(ctx->title, text_w, title_h);
    lv_obj_set_size(ctx->kind, text_w, kind_h);
    lv_obj_set_pos(ctx->title, text_x, top);
    lv_obj_set_pos(ctx->kind, text_x, top + title_h + ui_widget_name_gap(ctx->title, 2));
}

static void scene_event_cb(lv_event_t *event)
{
    w_scene_ctx_t *ctx = (w_scene_ctx_t *)lv_event_get_user_data(event);
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        lv_anim_delete(lv_event_get_target(event), scene_flash_exec);
        free(ctx);
    } else if (lv_event_get_code(event) == LV_EVENT_SIZE_CHANGED && ctx != NULL) {
        scene_layout(ctx);
    }
}

static const char *scene_kind_text(const char *entity_id)
{
    if (strncmp(entity_id, "script.", 7) == 0) return ui_i18n_get("scene.script", "Script");
    if (strncmp(entity_id, "button.", 7) == 0 || strncmp(entity_id, "input_button.", 12) == 0) {
        return ui_i18n_get("scene.button", "Button");
    }
    if (strncmp(entity_id, "lock.", 5) == 0) return ui_i18n_get("scene.lock", "Unlock");
    return ui_i18n_get("scene.scene", "Scene");
}

esp_err_t w_scene_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    w_scene_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    strlcpy(ctx->entity_id, def->entity_id, sizeof(ctx->entity_id));

    lv_obj_t *card = lv_obj_create(parent);
    ctx->card = card;
    ctx->unavailable = true;
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
    lv_obj_set_style_border_opa(card, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_radius(card, APP_UI_CARD_RADIUS, LV_PART_MAIN);

    /* Same glow-button surface every other icon button in the app uses (raised on glow,
     * translucent on glass), rather than a plain flat fill fixed regardless of style -- a
     * sonos_favorites row is, structurally, one of these plus a label, so the two read as the
     * same kind of thing now instead of the row looking like a different, plainer control. */
    ctx->circle = lv_obj_create(card);
    lv_obj_remove_style_all(ctx->circle);
    lv_obj_set_scrollable(ctx->circle, false);
    lv_obj_set_clickable(ctx->circle, false);
    lv_obj_set_style_radius(ctx->circle, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    const lv_color_t circle_bg = ui_style_control_color(UI_CONTROL_ACTION, !ctx->unavailable);
    ui_style_button_auto(ctx->circle, circle_bg, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER));
    ctx->glyph = lv_label_create(ctx->circle);
    lv_label_set_text(ctx->glyph, APP_SYMBOL_PLAY);
    lv_obj_set_style_text_font(ctx->glyph, APP_FONT_SYMBOL_MEDIUM, LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->glyph, ui_style_button_icon_color(circle_bg), LV_PART_MAIN);

    ctx->title = ui_widget_name_create(card, !def->hide_name);
    lv_label_set_text(ctx->title, def->title[0] ? def->title : def->entity_id);
    lv_obj_set_style_text_font(ctx->title, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->title, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->title, LV_LABEL_LONG_DOT);
    ctx->kind = lv_label_create(card);
    lv_label_set_text(ctx->kind, scene_kind_text(def->entity_id));
    lv_obj_set_style_text_font(ctx->kind, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->kind, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);

    lv_obj_add_event_cb(card, scene_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, scene_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    lv_obj_add_event_cb(card, scene_clicked_cb, LV_EVENT_CLICKED, ctx);
    scene_layout(ctx);
    return ESP_OK;
}

void w_scene_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->ctx == NULL || state == NULL) {
        return;
    }
    w_scene_ctx_t *ctx = (w_scene_ctx_t *)instance->ctx;
    if (strncmp(state->entity_id, ctx->entity_id, APP_MAX_ENTITY_ID_LEN) != 0) {
        return;
    }
    ctx->unavailable = state->state[0] == '\0' || strcmp(state->state, "unavailable") == 0;
    ui_style_action_button(ctx->circle, ctx->glyph, UI_CONTROL_ACTION, !ctx->unavailable);
}

void w_scene_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_scene_ctx_t *ctx = (w_scene_ctx_t *)instance->ctx;
    ctx->unavailable = true;
    ui_style_action_button(ctx->circle, ctx->glyph, UI_CONTROL_ACTION, false);
}
