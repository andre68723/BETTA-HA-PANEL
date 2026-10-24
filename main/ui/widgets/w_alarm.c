/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Alarm tile (alarm_tile) for alarm_control_panel.* entities: a lock symbol, the title and the
 * state (disarmed, armed home/away/night/vacation/custom bypass, arming, pending, disarming,
 * triggered). A tap opens the option picker with what the panel can do now: the arm modes
 * its supported_features announce while disarmed, disarm otherwise, limited to the layout's
 * alarm_actions when set (leave out "disarm" and the panel can only arm). When the entity has a
 * code_format the chosen action asks for the code on a numeric pad first and sends it as `code`.
 * The code is never stored. The state shown is always Home Assistant's, so the tile shows whether
 * the panel really armed (a wrong code leaves it unchanged). Pending and triggered are drawn in
 * the alert colour. Modelled on w_lock.c.
 */
#include "ui/ui_widget_factory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "ha/ha_alarm_state.h"
#include "ha/ha_client.h"
#include "ui/fonts/app_symbols.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/theme/ui_typography.h"
#include "ui/ui_history_popup.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"
#include "ui/ui_option_picker.h"
#include "ui/ui_pin_pad.h"

static const char *TAG = "w_alarm";

#define ALARM_ATTRS_LEN 192

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    char title[APP_MAX_NAME_LEN];
    ha_alarm_state_t state;
    char raw_state[20]; /* fallback text for a state without a translation */
    char attributes[ALARM_ATTRS_LEN];
    bool unavailable;
    bool history_popup;
    unsigned action_mask; /* alarm_actions of the layout, 0 = every action */
    bool suppress_click;
    ha_alarm_action_t actions[HA_ALARM_ACTION_COUNT]; /* the rows of the open picker */
    size_t action_count;
    ha_alarm_action_t pending_action; /* waiting for the code */
    char pad_title[APP_MAX_NAME_LEN + 32];
    lv_obj_t *card;
    lv_obj_t *circle;
    lv_obj_t *glyph;
    lv_obj_t *title_label;
    lv_obj_t *state_label;
    lv_obj_t *picker;
    lv_obj_t *pad;
} w_alarm_ctx_t;

static void alarm_call(w_alarm_ctx_t *ctx, ha_alarm_action_t action, const char *code)
{
    if (ctx == NULL || ctx->unavailable || !ha_client_is_connected()) return;
    char payload[APP_MAX_ENTITY_ID_LEN + 48];
    if (code != NULL) {
        snprintf(payload, sizeof(payload), "{\"entity_id\":\"%s\",\"code\":\"%s\"}", ctx->entity_id, code);
    } else {
        snprintf(payload, sizeof(payload), "{\"entity_id\":\"%s\"}", ctx->entity_id);
    }
    const char *service = ha_alarm_action_service(action);
    const esp_err_t err = ha_client_call_service("alarm_control_panel", service, payload);
    memset(payload, 0, sizeof(payload));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "alarm_control_panel.%s failed (%s): %s", service, ctx->entity_id, esp_err_to_name(err));
    }
}

static void alarm_pad_submit(void *user, const char *code)
{
    w_alarm_ctx_t *ctx = (w_alarm_ctx_t *)user;
    alarm_call(ctx, ctx->pending_action, code);
}

static void alarm_pad_closed(void *user)
{
    ((w_alarm_ctx_t *)user)->pad = NULL;
}

static void alarm_action_label(void *user, size_t index, char *out, size_t out_len)
{
    w_alarm_ctx_t *ctx = (w_alarm_ctx_t *)user;
    snprintf(out, out_len, "%s", ui_i18n_get(ha_alarm_action_key(ctx->actions[index]), "?"));
}

static void alarm_action_chosen(void *user, size_t index)
{
    w_alarm_ctx_t *ctx = (w_alarm_ctx_t *)user;
    if (index >= ctx->action_count) {
        return;
    }
    const ha_alarm_action_t action = ctx->actions[index];
    if (!ha_alarm_action_needs_code(action, ctx->attributes)) {
        alarm_call(ctx, action, NULL);
        return;
    }
    ctx->pending_action = action;
    snprintf(ctx->pad_title, sizeof(ctx->pad_title), "%s", ui_i18n_get(ha_alarm_action_key(action), "?"));
    const ui_pin_pad_cfg_t cfg = {
        .title = ctx->pad_title,
        .roomy = true,
        .submit = alarm_pad_submit,
        .closed = alarm_pad_closed,
        .user = ctx,
    };
    ctx->pad = ui_pin_pad_open(&cfg);
}

static void alarm_picker_closed(void *user)
{
    ((w_alarm_ctx_t *)user)->picker = NULL;
}

static void alarm_open_picker(w_alarm_ctx_t *ctx)
{
    if (ctx->unavailable || ctx->picker != NULL || ctx->pad != NULL) {
        return;
    }
    ctx->action_count = ha_alarm_actions(ctx->state, ctx->attributes, ctx->actions, HA_ALARM_ACTION_COUNT);
    ctx->action_count = ha_alarm_actions_filter(ctx->actions, ctx->action_count, ctx->action_mask);
    if (ctx->action_count == 0) {
        return;
    }
    const ui_option_picker_cfg_t cfg = {
        .title = ctx->title,
        .count = ctx->action_count,
        .current = UI_OPTION_PICKER_NONE,
        .roomy = true,
        .label = alarm_action_label,
        .chosen = alarm_action_chosen,
        .closed = alarm_picker_closed,
        .user = ctx,
    };
    ctx->picker = ui_option_picker_open(&cfg);
}

static void alarm_layout(w_alarm_ctx_t *ctx)
{
    lv_obj_update_layout(ctx->card);
    const lv_coord_t w = lv_obj_get_width(ctx->card);
    const lv_coord_t h = lv_obj_get_height(ctx->card);
    const lv_coord_t pad = 14;
    const bool roomy = h >= 100;
    const lv_font_t *title_font = roomy ? (w >= 240 ? ui_font_role(UI_TEXT_VALUE) : ui_font_role(UI_TEXT_LABEL)) : ui_font_role(UI_TEXT_META);
    const lv_font_t *state_font = roomy ? ui_font_role(UI_TEXT_LABEL) : ui_font_role(UI_TEXT_META);
    lv_obj_set_style_text_font(ctx->title_label, title_font, LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->state_label, state_font, LV_PART_MAIN);
    const lv_coord_t side_max = roomy ? (w >= 260 ? 72 : 56) : 52;
    lv_coord_t side = h - 28;
    side = side > side_max ? side_max : (side < 30 ? 30 : side);
    lv_obj_set_size(ctx->circle, side, side);
    lv_obj_set_pos(ctx->circle, pad, (h - side) / 2);
    lv_obj_center(ctx->glyph);

    const lv_coord_t text_x = pad + side + 14;
    lv_coord_t text_w = w - pad - text_x;
    if (text_w < 40) text_w = 40;
    const lv_coord_t title_h = ui_widget_name_height(ctx->title_label, title_font);
    const lv_coord_t state_h = lv_font_get_line_height(state_font);
    const lv_coord_t top = (h - (title_h + ui_widget_name_gap(ctx->title_label, 2) + state_h)) / 2;
    lv_obj_set_size(ctx->title_label, text_w, title_h);
    lv_obj_set_pos(ctx->title_label, text_x, top);
    lv_obj_set_size(ctx->state_label, text_w, state_h);
    lv_obj_set_pos(ctx->state_label, text_x, top + title_h + ui_widget_name_gap(ctx->title_label, 2));
}

static void alarm_render(w_alarm_ctx_t *ctx)
{
    if (ctx->unavailable) {
        if (ctx->picker != NULL) ui_option_picker_close(ctx->picker);
        if (ctx->pad != NULL) ui_pin_pad_close(ctx->pad);
    }
    const ha_alarm_state_t state = ctx->unavailable ? HA_ALARM_UNKNOWN : ctx->state;
    const bool armed = ha_alarm_state_armed(state);
    const bool alert = ha_alarm_state_alert(state);
    lv_label_set_text(ctx->glyph, alert ? APP_SYMBOL_LOCK_ALERT
                                  : (armed || state == HA_ALARM_ARMING) ? APP_SYMBOL_LOCK : APP_SYMBOL_LOCK_OPEN);
    const lv_color_t circle_bg = alert && !ctx->unavailable ? lv_color_hex(APP_UI_COLOR_ERROR)
        : ui_style_control_color(armed ? UI_CONTROL_ACTIVE : UI_CONTROL_ACTION, !ctx->unavailable);
    ui_style_action_button(ctx->circle, ctx->glyph, armed ? UI_CONTROL_ACTIVE : UI_CONTROL_ACTION, !ctx->unavailable);
    if (alert && !ctx->unavailable) {
        ui_style_button_auto(ctx->circle, circle_bg, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER));
        lv_obj_set_style_text_color(ctx->glyph, ui_style_button_icon_color(circle_bg), LV_PART_MAIN);
    }

    lv_label_set_text(ctx->state_label, ctx->unavailable ? ui_i18n_get("common.unavailable", "unavailable")
                                                         : ui_i18n_get(ha_alarm_state_key(state), ctx->raw_state));
    lv_obj_set_style_text_color(ctx->state_label, lv_color_hex(alert ? APP_UI_COLOR_ERROR : APP_UI_COLOR_TEXT_MUTED),
                                LV_PART_MAIN);
    ui_style_card_set_active(ctx->card, (armed || alert) && !ctx->unavailable);
}

static void alarm_clicked_cb(lv_event_t *event)
{
    w_alarm_ctx_t *ctx = (w_alarm_ctx_t *)lv_event_get_user_data(event);
    if (ctx->suppress_click) { ctx->suppress_click = false; return; }
    alarm_open_picker(ctx);
}

static void alarm_long_pressed_cb(lv_event_t *event)
{
    w_alarm_ctx_t *ctx = (w_alarm_ctx_t *)lv_event_get_user_data(event);
    ctx->suppress_click = true;
    ui_history_popup_open(ctx->entity_id, ctx->title, lv_label_get_text(ctx->state_label), false, "alarm");
}

static void alarm_event_cb(lv_event_t *event)
{
    w_alarm_ctx_t *ctx = (w_alarm_ctx_t *)lv_event_get_user_data(event);
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        if (ctx->picker != NULL) {
            ui_option_picker_close(ctx->picker);
        }
        if (ctx->pad != NULL) {
            ui_pin_pad_close(ctx->pad);
        }
        free(ctx);
    } else if (lv_event_get_code(event) == LV_EVENT_SIZE_CHANGED) {
        alarm_layout(ctx);
    }
}

esp_err_t w_alarm_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    w_alarm_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    strlcpy(ctx->entity_id, def->entity_id, sizeof(ctx->entity_id));
    strlcpy(ctx->title, def->title[0] ? def->title : def->entity_id, sizeof(ctx->title));
    ctx->history_popup = def->history_popup;
    ctx->action_mask = def->alarm_actions;
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
    lv_obj_set_style_border_opa(card, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_radius(card, APP_UI_CARD_RADIUS, LV_PART_MAIN);

    ctx->circle = lv_obj_create(card);
    lv_obj_remove_style_all(ctx->circle);
    lv_obj_set_scrollable(ctx->circle, false);
    lv_obj_set_clickable(ctx->circle, false);
    lv_obj_set_style_radius(ctx->circle, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    ctx->glyph = lv_label_create(ctx->circle);
    lv_obj_set_style_text_font(ctx->glyph, APP_FONT_SYMBOL_MEDIUM, LV_PART_MAIN);

    ctx->title_label = ui_widget_name_create(card, !def->hide_name);
    ui_style_title_fit(ctx->title_label, ctx->title);
    lv_obj_set_style_text_font(ctx->title_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->title_label, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->title_label, LV_LABEL_LONG_DOT);
    ctx->state_label = lv_label_create(card);
    lv_obj_set_style_text_font(ctx->state_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->state_label, LV_LABEL_LONG_DOT);

    lv_obj_add_event_cb(card, alarm_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, alarm_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    lv_obj_add_event_cb(card, alarm_clicked_cb, LV_EVENT_CLICKED, ctx);
    if (ctx->history_popup) lv_obj_add_event_cb(card, alarm_long_pressed_cb, LV_EVENT_LONG_PRESSED, ctx);
    alarm_layout(ctx);
    alarm_render(ctx);
    return ESP_OK;
}

void w_alarm_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->ctx == NULL || state == NULL) {
        return;
    }
    w_alarm_ctx_t *ctx = (w_alarm_ctx_t *)instance->ctx;
    if (strncmp(state->entity_id, ctx->entity_id, APP_MAX_ENTITY_ID_LEN) != 0) {
        return;
    }
    ctx->state = ha_alarm_state_parse(state->state);
    strlcpy(ctx->raw_state, state->state, sizeof(ctx->raw_state));
    strlcpy(ctx->attributes, state->attributes_json, sizeof(ctx->attributes));
    ctx->unavailable = state->state[0] == '\0' || strcmp(state->state, "unavailable") == 0 || strcmp(state->state, "unknown") == 0;
    alarm_render(ctx);
}

void w_alarm_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_alarm_ctx_t *ctx = (w_alarm_ctx_t *)instance->ctx;
    ctx->unavailable = true;
    alarm_render(ctx);
}
