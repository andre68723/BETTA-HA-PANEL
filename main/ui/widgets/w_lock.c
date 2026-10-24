/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Lock tile (lock_tile) for lock.* entities: a lock symbol, the title and the state (locked,
 * unlocked, locking, unlocking, open, opening, jammed). A tap locks or unlocks (a jammed lock is
 * locked again), and an "Open" button unlatches the door with lock.open when the lock supports it
 * (LockEntityFeature.OPEN, e.g. Nuki), where unlock only retracts the bolt. With lock_confirm set,
 * unlocking and opening ask first, in the shared option picker. The state shown is always Home
 * Assistant's, so the tile shows whether the door really unlocked or jammed. Toggle handling after
 * kruzio's w_lock.c; the transitional states, jammed and open are ours.
 */
#include "ui/ui_widget_factory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "ha/ha_client.h"
#include "ha/ha_lock_state.h"
#include "ui/fonts/app_symbols.h"
#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"
#include "ui/ui_option_picker.h"
#include "ui/ui_history_popup.h"

static const char *TAG = "w_lock";

#define LOCK_OPEN_BUTTON_MIN_W 260

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    char title[APP_MAX_NAME_LEN];
    ha_lock_state_t state;
    char raw_state[16]; /* fallback text for a state without a translation */
    bool unavailable;
    bool supports_open;
    bool confirm;
    bool history_popup;
    bool suppress_click;
    const char *pending_service; /* service waiting for the confirmation */
    char confirm_text[APP_MAX_NAME_LEN + 32];
    lv_obj_t *card;
    lv_obj_t *circle;
    lv_obj_t *glyph;
    lv_obj_t *title_label;
    lv_obj_t *state_label;
    lv_obj_t *open_btn;
    lv_obj_t *open_label;
    lv_obj_t *picker;
} w_lock_ctx_t;

static void lock_call(w_lock_ctx_t *ctx, const char *service)
{
    if (ctx == NULL || ctx->unavailable || !ha_client_is_connected()) return;
    char payload[APP_MAX_ENTITY_ID_LEN + 24];
    snprintf(payload, sizeof(payload), "{\"entity_id\":\"%s\"}", ctx->entity_id);
    esp_err_t err = ha_client_call_service("lock", service, payload);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "lock.%s failed (%s): %s", service, ctx->entity_id, esp_err_to_name(err));
    }
}

static void lock_confirm_label(void *user, size_t index, char *out, size_t out_len)
{
    (void)index;
    w_lock_ctx_t *ctx = (w_lock_ctx_t *)user;
    snprintf(out, out_len, "%s", strcmp(ctx->pending_service, "open") == 0 ? ui_i18n_get("lock.open", "Open")
                                                                            : ui_i18n_get("lock.unlock", "Unlock"));
}

static void lock_confirm_chosen(void *user, size_t index)
{
    (void)index;
    w_lock_ctx_t *ctx = (w_lock_ctx_t *)user;
    if (ctx->pending_service != NULL) {
        lock_call(ctx, ctx->pending_service);
    }
}

static void lock_confirm_closed(void *user)
{
    w_lock_ctx_t *ctx = (w_lock_ctx_t *)user;
    ctx->picker = NULL;
    ctx->pending_service = NULL;
}

/* Locking never asks; unlocking and opening ask first when lock_confirm is set (a front door). */
static void lock_request(w_lock_ctx_t *ctx, const char *service)
{
    if (service == NULL || ctx->unavailable || ctx->picker != NULL) {
        return;
    }
    if (!ctx->confirm || strcmp(service, "lock") == 0) {
        lock_call(ctx, service);
        return;
    }
    const bool open = strcmp(service, "open") == 0;
    snprintf(ctx->confirm_text, sizeof(ctx->confirm_text),
             ui_i18n_get(open ? "lock.confirm_open" : "lock.confirm_unlock", open ? "Open %s?" : "Unlock %s?"), ctx->title);
    ctx->pending_service = service;
    const ui_option_picker_cfg_t cfg = {
        .title = ctx->confirm_text,
        .count = 1,
        .current = UI_OPTION_PICKER_NONE,
        .label = lock_confirm_label,
        .chosen = lock_confirm_chosen,
        .closed = lock_confirm_closed,
        .user = ctx,
    };
    ctx->picker = ui_option_picker_open(&cfg);
    if (ctx->picker == NULL) {
        ctx->pending_service = NULL;
    }
}

static void lock_layout(w_lock_ctx_t *ctx)
{
    lv_obj_update_layout(ctx->card);
    const lv_coord_t w = lv_obj_get_width(ctx->card);
    const lv_coord_t h = lv_obj_get_height(ctx->card);
    const lv_coord_t pad = 14;
    /* A tile at least 100 px tall gets a bigger circle and bigger text: easier to hit and to read on the 4-inch panel. */
    const bool roomy = h >= 100;
    const lv_font_t *title_font = roomy ? (w >= 240 ? ui_font_role(UI_TEXT_VALUE) : ui_font_role(UI_TEXT_LABEL)) : ui_font_role(UI_TEXT_META);
    const lv_font_t *state_font = roomy ? ui_font_role(UI_TEXT_LABEL) : ui_font_role(UI_TEXT_META);
    lv_obj_set_style_text_font(ctx->title_label, title_font, LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->state_label, state_font, LV_PART_MAIN);
    const lv_coord_t side_max = roomy ? (w >= 260 ? 72 : 56) : 52; /* a narrower tile keeps room for "Entriegelt" at 24 px */
    lv_coord_t side = h - 28;
    side = side > side_max ? side_max : (side < 30 ? 30 : side);
    lv_obj_set_size(ctx->circle, side, side);
    lv_obj_set_pos(ctx->circle, pad, (h - side) / 2);
    lv_obj_center(ctx->glyph);

    const bool open_btn = ctx->supports_open && w >= LOCK_OPEN_BUTTON_MIN_W;
    lv_obj_set_hidden(ctx->open_btn, !open_btn);
    lv_coord_t right = w - pad;
    if (open_btn) {
        /* as wide as the word needs (larger text, longer translations), at most a third of the tile, the word fitted
         * to that on one line */
        const lv_font_t *font = ui_font_role(UI_TEXT_META);
        const char *word = lv_label_get_text(ctx->open_label);
        lv_point_t size;
        lv_text_get_size(&size, word, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        lv_coord_t btn_w = size.x + 20;
        btn_w = btn_w < 84 ? 84 : (btn_w > w / 3 ? w / 3 : btn_w);
        const lv_coord_t btn_h = side < 40 ? side : 40;
        lv_obj_set_size(ctx->open_btn, btn_w, btn_h);
        lv_obj_set_pos(ctx->open_btn, w - pad - btn_w, (h - btn_h) / 2);
        const lv_font_t *fitted = ui_text_fit(word, font, btn_w - 8, btn_h);
        lv_obj_set_style_text_font(ctx->open_label, fitted, LV_PART_MAIN);
        lv_obj_set_size(ctx->open_label, btn_w - 8, lv_font_get_line_height(fitted));
        lv_obj_center(ctx->open_label);
        right = w - pad - btn_w - 10;
    }
    const lv_coord_t text_x = pad + side + 14;
    lv_coord_t text_w = right - text_x;
    if (text_w < 40) text_w = 40;
    const lv_coord_t title_h = ui_widget_name_height(ctx->title_label, title_font);
    const lv_coord_t state_h = lv_font_get_line_height(state_font);
    const lv_coord_t top = (h - (title_h + ui_widget_name_gap(ctx->title_label, 2) + state_h)) / 2;
    lv_obj_set_size(ctx->title_label, text_w, title_h);
    lv_obj_set_pos(ctx->title_label, text_x, top);
    lv_obj_set_size(ctx->state_label, text_w, state_h);
    lv_obj_set_pos(ctx->state_label, text_x, top + title_h + ui_widget_name_gap(ctx->title_label, 2));
}

static void lock_render(w_lock_ctx_t *ctx)
{
    if (ctx->unavailable && ctx->picker != NULL) ui_option_picker_close(ctx->picker);
    const ha_lock_state_t state = ctx->unavailable ? HA_LOCK_UNKNOWN : ctx->state;
    const bool unsecured = ha_lock_state_unsecured(state);
    const bool jammed = state == HA_LOCK_JAMMED;
    lv_label_set_text(ctx->glyph, jammed ? APP_SYMBOL_LOCK_ALERT
                                  : (state == HA_LOCK_OPEN || state == HA_LOCK_OPENING) ? APP_SYMBOL_DOOR_OPEN
                                  : unsecured ? APP_SYMBOL_LOCK_OPEN : APP_SYMBOL_LOCK);
    const lv_color_t circle_bg = jammed && !ctx->unavailable ? lv_color_hex(APP_UI_COLOR_ERROR)
        : ui_style_control_color(unsecured ? UI_CONTROL_ACTIVE : UI_CONTROL_ACTION, !ctx->unavailable);
    ui_style_action_button(ctx->circle, ctx->glyph, unsecured ? UI_CONTROL_ACTIVE : UI_CONTROL_ACTION, !ctx->unavailable);
    if (jammed && !ctx->unavailable) {
        ui_style_button_auto(ctx->circle, circle_bg, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER));
        lv_obj_set_style_text_color(ctx->glyph, ui_style_button_icon_color(circle_bg), LV_PART_MAIN);
    }

    lv_label_set_text(ctx->state_label, ctx->unavailable ? ui_i18n_get("common.unavailable", "unavailable")
                                                         : ui_i18n_get(ha_lock_state_key(state), ctx->raw_state));
    lv_obj_set_style_text_color(ctx->state_label, lv_color_hex(jammed ? APP_UI_COLOR_ERROR
                                                               : APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    ui_style_action_button(ctx->open_btn, ctx->open_label, UI_CONTROL_ACTION, !ctx->unavailable && ctx->supports_open);
    ui_style_card_set_active(ctx->card, unsecured && !ctx->unavailable);
}

static void lock_clicked_cb(lv_event_t *event)
{
    w_lock_ctx_t *ctx = (w_lock_ctx_t *)lv_event_get_user_data(event);
    if (ctx->suppress_click) { ctx->suppress_click = false; return; }
    lock_request(ctx, ha_lock_tap_service(ctx->state));
}

static void lock_long_pressed_cb(lv_event_t *event)
{
    w_lock_ctx_t *ctx = (w_lock_ctx_t *)lv_event_get_user_data(event);
    ctx->suppress_click = true;
    ui_history_popup_open(ctx->entity_id, ctx->title, lv_label_get_text(ctx->state_label), false, "lock");
}

static void lock_open_clicked_cb(lv_event_t *event)
{
    w_lock_ctx_t *ctx = (w_lock_ctx_t *)lv_event_get_user_data(event);
    lock_request(ctx, "open");
}

static void lock_event_cb(lv_event_t *event)
{
    w_lock_ctx_t *ctx = (w_lock_ctx_t *)lv_event_get_user_data(event);
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        if (ctx->picker != NULL) {
            ui_option_picker_close(ctx->picker);
        }
        free(ctx);
    } else if (lv_event_get_code(event) == LV_EVENT_SIZE_CHANGED) {
        lock_layout(ctx);
    }
}

esp_err_t w_lock_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    w_lock_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    strlcpy(ctx->entity_id, def->entity_id, sizeof(ctx->entity_id));
    strlcpy(ctx->title, def->title[0] ? def->title : def->entity_id, sizeof(ctx->title));
    ctx->confirm = def->lock_confirm;
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

    ctx->open_btn = lv_btn_create(card);
    lv_obj_set_style_radius(ctx->open_btn, ui_style_radius(12), LV_PART_MAIN);
    ui_style_button_auto(ctx->open_btn, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE),
                         lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER));
    lv_obj_set_ext_click_area(ctx->open_btn, 8);
    ctx->open_label = lv_label_create(ctx->open_btn);
    lv_label_set_text(ctx->open_label, ui_i18n_get("lock.open", "Open"));
    lv_obj_set_style_text_font(ctx->open_label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->open_label, ui_style_button_icon_color(lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE)),
                                LV_PART_MAIN);
    lv_obj_set_style_text_align(ctx->open_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(ctx->open_label, LV_LABEL_LONG_DOT);
    lv_obj_set_hidden(ctx->open_btn, true);

    lv_obj_add_event_cb(card, lock_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, lock_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    lv_obj_add_event_cb(card, lock_clicked_cb, LV_EVENT_CLICKED, ctx);
    if (ctx->history_popup) lv_obj_add_event_cb(card, lock_long_pressed_cb, LV_EVENT_LONG_PRESSED, ctx);
    lv_obj_add_event_cb(ctx->open_btn, lock_open_clicked_cb, LV_EVENT_CLICKED, ctx);
    lock_layout(ctx);
    lock_render(ctx);
    return ESP_OK;
}

void w_lock_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->ctx == NULL || state == NULL) {
        return;
    }
    w_lock_ctx_t *ctx = (w_lock_ctx_t *)instance->ctx;
    if (strncmp(state->entity_id, ctx->entity_id, APP_MAX_ENTITY_ID_LEN) != 0) {
        return;
    }
    ctx->state = ha_lock_state_parse(state->state);
    strlcpy(ctx->raw_state, state->state, sizeof(ctx->raw_state));
    ctx->unavailable = state->state[0] == '\0' || strcmp(state->state, "unavailable") == 0 || strcmp(state->state, "unknown") == 0;
    const bool supports_open = ha_lock_supports_open(state->attributes_json);
    if (supports_open != ctx->supports_open) {
        ctx->supports_open = supports_open;
        lock_layout(ctx);
    }
    lock_render(ctx);
}

void w_lock_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_lock_ctx_t *ctx = (w_lock_ctx_t *)instance->ctx;
    ctx->unavailable = true;
    lock_render(ctx);
}
