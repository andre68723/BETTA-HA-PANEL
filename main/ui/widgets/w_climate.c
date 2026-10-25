/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Climate tile for air conditioners and thermostats: current and target
 * temperature with minus / plus buttons, an operating mode chip and (when the
 * device has fan modes) a fan mode chip. Chips open a list to pick from.
 */
#include "ui/ui_widget_factory.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "ha/ha_client.h"
#include "ui/fonts/app_symbols.h"
#include "ui/theme/ui_typography.h"
#include "ui/ui_popup.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"

#define CLIMATE_MAX_OPTIONS 10
#define CLIMATE_OPTION_LEN 24
#define CLIMATE_STEP 0.5f
#define CLIMATE_SEND_DELAY_MS 800

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    lv_obj_t *card;
    lv_obj_t *title;
    lv_obj_t *current;
    char current_full[64]; /* "Now 20.8°C", and the same without "Now" */
    char current_bare[64];
    lv_obj_t *target_label;
    lv_obj_t *btn_minus;
    lv_obj_t *btn_plus;
    lv_obj_t *mode_btn;
    lv_obj_t *mode_label;
    lv_obj_t *fan_btn;
    lv_obj_t *fan_label;
    char fan_full[2 * CLIMATE_OPTION_LEN + 16]; /* "Fan: Auto", and the chip text without its prefix */
    char fan_bare[CLIMATE_OPTION_LEN + 16];
    lv_obj_t *swing_btn;
    lv_obj_t *swing_label;
    char swing_full[2 * CLIMATE_OPTION_LEN + 16];
    char swing_bare[CLIMATE_OPTION_LEN + 16];
    lv_obj_t *popup;
    lv_timer_t *send_timer;
    char hvac_mode[CLIMATE_OPTION_LEN];
    char hvac_action[CLIMATE_OPTION_LEN];
    char fan_mode[CLIMATE_OPTION_LEN];
    char swing_mode[CLIMATE_OPTION_LEN];
    char hvac_modes[CLIMATE_MAX_OPTIONS][CLIMATE_OPTION_LEN];
    int hvac_mode_count;
    char fan_modes[CLIMATE_MAX_OPTIONS][CLIMATE_OPTION_LEN];
    int fan_mode_count;
    char swing_modes[CLIMATE_MAX_OPTIONS][CLIMATE_OPTION_LEN];
    int swing_mode_count;
    char unit[8];
    float target;
    float current_temp;
    float min_temp;
    float max_temp;
    bool has_target;
    bool has_current;
    bool pending_send;
    bool unavailable;
} w_climate_ctx_t;

static void climate_render(w_climate_ctx_t *ctx);

/* ---- services -------------------------------------------------------- */

static void climate_send_target(w_climate_ctx_t *ctx)
{
    if (ctx == NULL || ctx->unavailable || !ctx->has_target) {
        return;
    }
    char payload[APP_MAX_ENTITY_ID_LEN + 48];
    snprintf(payload, sizeof(payload), "{\"entity_id\":\"%s\",\"temperature\":%.1f}", ctx->entity_id, (double)ctx->target);
    (void)ha_client_call_service("climate", "set_temperature", payload);
    ctx->pending_send = false;
}

static void climate_send_timer_cb(lv_timer_t *timer)
{
    w_climate_ctx_t *ctx = (w_climate_ctx_t *)lv_timer_get_user_data(timer);
    lv_timer_pause(timer);
    if (ctx != NULL && ctx->pending_send) {
        climate_send_target(ctx);
    }
}

static void climate_step(w_climate_ctx_t *ctx, float delta)
{
    if (ctx == NULL || ctx->unavailable || !ctx->has_target) {
        return;
    }
    float next = ctx->target + delta;
    if (ctx->max_temp > ctx->min_temp) {
        if (next < ctx->min_temp) next = ctx->min_temp;
        if (next > ctx->max_temp) next = ctx->max_temp;
    }
    ctx->target = next;
    ctx->pending_send = true;
    climate_render(ctx);
    lv_timer_reset(ctx->send_timer);
    lv_timer_resume(ctx->send_timer);
}

static void climate_minus_cb(lv_event_t *e)
{
    climate_step((w_climate_ctx_t *)lv_event_get_user_data(e), -CLIMATE_STEP);
}

static void climate_plus_cb(lv_event_t *e)
{
    climate_step((w_climate_ctx_t *)lv_event_get_user_data(e), CLIMATE_STEP);
}

/* ---- option popup ---------------------------------------------------- */

typedef struct {
    w_climate_ctx_t *ctx;
    bool fan;
} climate_popup_target_t;

static void climate_popup_close(w_climate_ctx_t *ctx)
{
    if (ctx != NULL && ctx->popup != NULL) {
        lv_obj_t *p = ctx->popup;
        ctx->popup = NULL;
        lv_obj_del(p);
    }
}

static void climate_popup_deleted_cb(lv_event_t *e)
{
    w_climate_ctx_t *ctx = (w_climate_ctx_t *)lv_event_get_user_data(e);
    if (ctx != NULL) {
        ctx->popup = NULL;
    }
}

static void climate_popup_backdrop_cb(lv_event_t *e)
{
    if (lv_event_get_target(e) == lv_event_get_current_target(e)) {
        climate_popup_close((w_climate_ctx_t *)lv_event_get_user_data(e));
    }
}

static void climate_popup_close_cb(lv_event_t *e)
{
    climate_popup_close((w_climate_ctx_t *)lv_event_get_user_data(e));
}

/* Which of the three option popups (operating mode, fan mode, swing mode) a button belongs to,
 * encoded into the option button's user data alongside its index so one click handler serves all
 * three (climate_option_cb). */
#define CLIMATE_POPUP_MODE 0U
#define CLIMATE_POPUP_FAN 1U
#define CLIMATE_POPUP_SWING 2U

static void climate_option_cb(lv_event_t *e)
{
    w_climate_ctx_t *ctx = (w_climate_ctx_t *)lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target(e);
    uintptr_t tag = (uintptr_t)lv_obj_get_user_data(btn);
    if (ctx == NULL) {
        return;
    }
    if (ctx->unavailable) return;
    const unsigned kind = (unsigned)((tag >> 8) & 0x3U);
    const int idx = (int)(tag & 0xFFU);
    const char *value = NULL;
    const char *service = NULL;
    const char *payload_key = NULL;
    char *dst = NULL;
    switch (kind) {
        case CLIMATE_POPUP_FAN:
            value = idx < ctx->fan_mode_count ? ctx->fan_modes[idx] : NULL;
            service = "set_fan_mode";
            payload_key = "fan_mode";
            dst = ctx->fan_mode;
            break;
        case CLIMATE_POPUP_SWING:
            value = idx < ctx->swing_mode_count ? ctx->swing_modes[idx] : NULL;
            service = "set_swing_mode";
            payload_key = "swing_mode";
            dst = ctx->swing_mode;
            break;
        case CLIMATE_POPUP_MODE:
        default:
            value = idx < ctx->hvac_mode_count ? ctx->hvac_modes[idx] : NULL;
            service = "set_hvac_mode";
            payload_key = "hvac_mode";
            dst = ctx->hvac_mode;
            break;
    }
    if (value != NULL) {
        char payload[APP_MAX_ENTITY_ID_LEN + 64];
        snprintf(payload, sizeof(payload), "{\"entity_id\":\"%s\",\"%s\":\"%s\"}", ctx->entity_id, payload_key, value);
        (void)ha_client_call_service("climate", service, payload);
        strlcpy(dst, value, CLIMATE_OPTION_LEN);
        climate_render(ctx);
    }
    climate_popup_close(ctx);
}

static const char *climate_mode_text(const char *mode, char *buf, size_t buf_size)
{
    char key[48];
    snprintf(key, sizeof(key), "climate.mode.%s", mode);
    const char *text = ui_i18n_get(key, "");
    if (text[0] != '\0') {
        return text;
    }
    /* Unknown mode: show the raw name with a capital first letter. */
    strlcpy(buf, mode, buf_size);
    if (buf[0] >= 'a' && buf[0] <= 'z') {
        buf[0] = (char)(buf[0] - 'a' + 'A');
    }
    for (char *c = buf; *c != '\0'; c++) {
        if (*c == '_') *c = ' ';
    }
    return buf;
}

static void climate_show_popup(w_climate_ctx_t *ctx, unsigned kind)
{
    const int count = kind == CLIMATE_POPUP_FAN ? ctx->fan_mode_count
                     : kind == CLIMATE_POPUP_SWING ? ctx->swing_mode_count
                                                    : ctx->hvac_mode_count;
    if (ctx == NULL || ctx->popup != NULL || count == 0 || ctx->unavailable) {
        return;
    }
    lv_obj_t *screen = lv_scr_act();
    lv_obj_update_layout(screen);
    int sw = lv_obj_get_width(screen);
    int sh = lv_obj_get_height(screen);
    if (sw <= 0) sw = 720;
    if (sh <= 0) sh = 720;

    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, sw, sh);
    lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_scrollable(overlay, false);
    lv_obj_set_clickable(overlay, true);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(overlay, climate_popup_backdrop_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(overlay, climate_popup_deleted_cb, LV_EVENT_DELETE, ctx);
    ctx->popup = overlay;

    const int row_h = 44;
    const int row_gap = 8;
    const int head_h = 52;
    int card_w = sw - 48;
    if (card_w > 380) card_w = 380;
    if (card_w < 240) card_w = 240;
    int want_h = head_h + count * (row_h + row_gap) + 12;
    int card_h = want_h < sh - 48 ? want_h : sh - 48;

    lv_obj_t *card = ui_popup_card_create(overlay);
    lv_obj_set_size(card, card_w, card_h);
    lv_obj_center(card);
    lv_obj_set_style_radius(card, 14, LV_PART_MAIN);

    lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(card);
    const char *title_text = kind == CLIMATE_POPUP_FAN ? ui_i18n_get("climate.fan_title", "Fan mode")
                            : kind == CLIMATE_POPUP_SWING ? ui_i18n_get("climate.swing_title", "Swing mode")
                                                           : ui_i18n_get("climate.mode_title", "Mode");
    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_font(title, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 4);

    lv_obj_t *close = ui_popup_close_button(card, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), climate_popup_close_cb, ctx);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *list = lv_obj_create(card);
    lv_obj_set_size(list, card_w - 28, card_h - 28 - head_h);
    lv_obj_align(list, LV_ALIGN_TOP_LEFT, 0, head_h - 6);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, row_gap, LV_PART_MAIN);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(list, true);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    const char *current = kind == CLIMATE_POPUP_FAN ? ctx->fan_mode
                        : kind == CLIMATE_POPUP_SWING ? ctx->swing_mode
                                                       : ctx->hvac_mode;
    for (int i = 0; i < count; i++) {
        const char *value = kind == CLIMATE_POPUP_FAN ? ctx->fan_modes[i]
                           : kind == CLIMATE_POPUP_SWING ? ctx->swing_modes[i]
                                                          : ctx->hvac_modes[i];
        const bool active = strcmp(value, current) == 0;
        lv_obj_t *btn = lv_btn_create(list);
        lv_obj_set_size(btn, LV_PCT(100), row_h);
        lv_obj_set_user_data(btn, (void *)(uintptr_t)((kind << 8) | (unsigned)i));
        lv_obj_add_event_cb(btn, climate_option_cb, LV_EVENT_CLICKED, ctx);
        lv_obj_set_style_radius(btn, ui_style_radius(12), LV_PART_MAIN);
        ui_style_action_button(btn, NULL, active ? UI_CONTROL_SELECTED : UI_CONTROL_ACTION, true);
        lv_obj_t *label = lv_label_create(btn);
        char raw[CLIMATE_OPTION_LEN + 4];
        lv_label_set_text(label, climate_mode_text(value, raw, sizeof(raw)));
        lv_obj_set_style_text_font(label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
        ui_style_action_button(btn, label, active ? UI_CONTROL_SELECTED : UI_CONTROL_ACTION, true);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 8, 0);
    }
}

static void climate_mode_btn_cb(lv_event_t *e)
{
    climate_show_popup((w_climate_ctx_t *)lv_event_get_user_data(e), CLIMATE_POPUP_MODE);
}

static void climate_fan_btn_cb(lv_event_t *e)
{
    climate_show_popup((w_climate_ctx_t *)lv_event_get_user_data(e), CLIMATE_POPUP_FAN);
}

static void climate_swing_btn_cb(lv_event_t *e)
{
    climate_show_popup((w_climate_ctx_t *)lv_event_get_user_data(e), CLIMATE_POPUP_SWING);
}

/* ---- layout and rendering -------------------------------------------- */

static lv_obj_t *climate_round_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t cb, w_climate_ctx_t *ctx)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_scrollable(btn, false);
    lv_obj_set_event_bubble(btn, false);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ctx);
    const lv_color_t btn_bg = ui_style_control_color(UI_CONTROL_ACTION, true);
    ui_style_button_auto(btn, btn_bg, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER));
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_font(label, APP_FONT_SYMBOL_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, ui_style_button_icon_color(btn_bg), LV_PART_MAIN);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *climate_chip(lv_obj_t *parent, lv_obj_t **out_label, lv_event_cb_t cb, w_climate_ctx_t *ctx)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_scrollable(btn, false);
    lv_obj_set_event_bubble(btn, false);
    lv_obj_set_style_radius(btn, ui_style_radius(18), LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ctx);
    const lv_color_t chip_bg = ui_style_control_color(UI_CONTROL_ACTION, true);
    ui_style_button_auto(btn, chip_bg, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER));
    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(label, ui_style_button_icon_color(chip_bg), LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    /* One line: without a fixed height a long state ("Lüfter: Auto") wrapped out of a low chip. */
    lv_obj_set_height(label, lv_font_get_line_height(ui_font_role(UI_TEXT_META)));
    lv_obj_center(label);
    *out_label = label;
    return btn;
}

/* Split anatomy (ui_style_traits()->split_wide_tiles) from this width on: above the service chips,
 * information on the left and the real temperature controls on the right. Narrower tiles keep the
 * compact layout. */
#define CLIMATE_SPLIT_MIN_W 360

static void climate_layout_split(w_climate_ctx_t *ctx, lv_coord_t w, lv_coord_t pad, lv_coord_t chips_y,
                                 lv_coord_t title_h, lv_coord_t current_h)
{
    const lv_coord_t left_w = w * 42 / 100 - pad;
    const lv_coord_t right_x = w * 42 / 100 + 6;
    const lv_coord_t right_w = w - right_x - pad;
    const lv_coord_t control_btn = right_w >= 220 ? 48 : 36;
    const lv_coord_t target_w = right_w - 2 * control_btn - 12;
    const lv_coord_t control_y = (chips_y + 10 - control_btn) / 2;
    lv_obj_set_style_text_align(ctx->current, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_size(ctx->title, left_w, title_h);
    lv_obj_set_pos(ctx->title, pad, (chips_y - title_h - current_h - ui_widget_name_gap(ctx->title, 4)) / 2);
    lv_obj_set_size(ctx->current, left_w, current_h);
    lv_obj_set_pos(ctx->current, pad, lv_obj_get_y(ctx->title) + title_h + ui_widget_name_gap(ctx->title, 4));
    lv_obj_set_size(ctx->btn_minus, control_btn, control_btn);
    lv_obj_set_size(ctx->btn_plus, control_btn, control_btn);
    lv_obj_set_pos(ctx->btn_minus, right_x, control_y);
    lv_obj_set_pos(ctx->btn_plus, right_x + right_w - control_btn, control_y);
    lv_obj_set_width(ctx->target_label, target_w);
    lv_obj_set_pos(ctx->target_label, right_x + control_btn + 6,
                   control_y + (control_btn - lv_font_get_line_height(ui_font_role(UI_TEXT_VALUE))) / 2);
}

/* A chip shows "Fan: Auto"; on a narrow tile only "Auto" fits without ending in "...". */
static void climate_chip_fit(lv_obj_t *label, const char *full, const char *bare)
{
    if (label == NULL || full[0] == '\0') {
        return;
    }
    lv_point_t size;
    lv_text_get_size(&size, full, lv_obj_get_style_text_font(label, LV_PART_MAIN), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const char *use = (lv_obj_get_width(label) > 0 && size.x > lv_obj_get_content_width(label)) ? bare : full;
    if (strcmp(lv_label_get_text(label), use) != 0) {
        lv_label_set_text(label, use);
    }
}

static void climate_layout(w_climate_ctx_t *ctx)
{
    lv_obj_update_layout(ctx->card);
    const lv_coord_t w = lv_obj_get_width(ctx->card);
    const lv_coord_t h = lv_obj_get_height(ctx->card);
    const lv_coord_t pad = 14;
    const bool fan = ctx->fan_mode_count > 0;
    const bool swing = ctx->swing_mode_count > 0;
    /* Mode is always one row. Fan and swing share a second row when both are present (a mode chip
     * plus one full-width extra chip is the existing single-extra layout, unchanged below); only a
     * device with both needs the extra vertical space for a second chip row. */
    const bool two_chip_rows = fan && swing;
    const lv_coord_t title_h = ui_widget_name_height(ctx->title, ui_font_role(UI_TEXT_META));
    const lv_coord_t current_h = lv_font_get_line_height(ui_font_role(UI_TEXT_META));
    /* A short tile (the 240x180 minimum with two chip rows) left the target and its buttons no
     * room: there the current temperature moves into the title row and the chips get lower. */
    const bool compact = ui_widget_name_enabled(ctx->title) && h - 2 * pad - (title_h + current_h + 8) - (38 + (two_chip_rows ? 8 + 38 : 0)) < 48;
    const lv_coord_t chip_h = compact ? 32 : 38;
    const lv_coord_t chip_row_gap = compact ? 6 : 8;
    const lv_coord_t chips_block_h = chip_h + (two_chip_rows ? chip_row_gap + chip_h : 0);

    if (compact) {
        lv_obj_set_style_text_align(ctx->current, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_set_size(ctx->current, (w - 2 * pad) / 2, current_h);
        lv_obj_set_pos(ctx->current, w - pad - (w - 2 * pad) / 2, 10 + (title_h - current_h) / 2);
        lv_obj_set_size(ctx->title, (w - 2 * pad) / 2 - 4, title_h);
    } else {
        lv_obj_set_style_text_align(ctx->current, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_size(ctx->current, w - 2 * pad, current_h);
        lv_obj_set_pos(ctx->current, pad, 10 + title_h + ui_widget_name_gap(ctx->title, 2));
        lv_obj_set_size(ctx->title, w - 2 * pad, title_h);
    }
    lv_obj_set_pos(ctx->title, pad, 10);

    /* Chips at the bottom, target temperature with -/+ in the middle. */
    const lv_coord_t chips_y = h - pad - chips_block_h;
    const lv_coord_t area_top = 10 + title_h + (compact ? 0 : current_h) + 8;
    const lv_coord_t area_h = chips_y - area_top - 6;
    lv_coord_t btn = area_h < 96 ? area_h - 8 : 64;
    if (btn < 34) btn = 34;
    if (btn > 64) btn = 64;
    /* The target reading ("21.0C") is ui_font_role(UI_TEXT_VALUE), six characters wide at a large size --
     * the old width floor below (40 px) only kept it from going negative, not from actually
     * fitting the text, so at the widget's minimum width the buttons crowded the reading until
     * it rendered clipped/overlapped ("21.0C" as ":1.0C"). Shrink the buttons first if that is
     * what it takes to give the reading real room, rather than the other way around. */
    const lv_coord_t min_target_w = 100;
    lv_coord_t max_btn_for_w = (w - 2 * pad - 12 - min_target_w) / 2;
    if (btn > max_btn_for_w) {
        btn = max_btn_for_w;
    }
    if (btn < 28) {
        btn = 28;
    }
    const lv_coord_t mid_y = area_top + area_h / 2;
    lv_obj_set_size(ctx->btn_minus, btn, btn);
    lv_obj_set_size(ctx->btn_plus, btn, btn);
    lv_obj_set_pos(ctx->btn_minus, pad, mid_y - btn / 2);
    lv_obj_set_pos(ctx->btn_plus, w - pad - btn, mid_y - btn / 2);
    const lv_coord_t tw = w - 2 * pad - 2 * btn - 12;
    lv_obj_set_width(ctx->target_label, tw > min_target_w ? tw : min_target_w);
    lv_obj_set_style_text_align(ctx->target_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(ctx->target_label, pad + btn + 6, mid_y - lv_font_get_line_height(ui_font_role(UI_TEXT_VALUE)) / 2);

    const lv_coord_t gap = 10;
    if (two_chip_rows) {
        /* Mode on its own full-width row, fan and swing sharing the row below it. */
        lv_obj_set_size(ctx->mode_btn, w - 2 * pad, chip_h);
        lv_obj_set_pos(ctx->mode_btn, pad, chips_y);
        lv_obj_set_width(ctx->mode_label, w - 2 * pad - 16);
        lv_obj_center(ctx->mode_label);

        const lv_coord_t half_w = (w - 2 * pad - gap) / 2;
        const lv_coord_t row2_y = chips_y + chip_h + chip_row_gap;
        lv_obj_set_hidden(ctx->fan_btn, false);
        lv_obj_set_size(ctx->fan_btn, half_w, chip_h);
        lv_obj_set_pos(ctx->fan_btn, pad, row2_y);
        lv_obj_set_width(ctx->fan_label, half_w - 16);
        lv_obj_center(ctx->fan_label);

        lv_obj_set_hidden(ctx->swing_btn, false);
        lv_obj_set_size(ctx->swing_btn, half_w, chip_h);
        lv_obj_set_pos(ctx->swing_btn, pad + half_w + gap, row2_y);
        lv_obj_set_width(ctx->swing_label, half_w - 16);
        lv_obj_center(ctx->swing_label);
    } else {
        /* Zero or one extra chip: same single-row layout this tile has always used. */
        const lv_coord_t chip_w = (fan || swing) ? (w - 2 * pad - gap) / 2 : w - 2 * pad;
        lv_obj_set_size(ctx->mode_btn, chip_w, chip_h);
        lv_obj_set_pos(ctx->mode_btn, pad, chips_y);
        lv_obj_set_width(ctx->mode_label, chip_w - 16);
        lv_obj_center(ctx->mode_label);
        if (fan || swing) {
            lv_obj_t *extra_btn = fan ? ctx->fan_btn : ctx->swing_btn;
            lv_obj_t *extra_label = fan ? ctx->fan_label : ctx->swing_label;
            lv_obj_set_hidden(extra_btn, false);
            lv_obj_set_size(extra_btn, chip_w, chip_h);
            lv_obj_set_pos(extra_btn, pad + chip_w + gap, chips_y);
            lv_obj_set_width(extra_label, chip_w - 16);
            lv_obj_center(extra_label);
            lv_obj_set_hidden(fan ? ctx->swing_btn : ctx->fan_btn, true);
        } else {
            lv_obj_set_hidden(ctx->fan_btn, true);
            lv_obj_set_hidden(ctx->swing_btn, true);
        }
    }
    if (ui_style_traits()->split_wide_tiles && w >= CLIMATE_SPLIT_MIN_W) {
        climate_layout_split(ctx, w, pad, chips_y, title_h, current_h);
    }
    lv_obj_update_layout(ctx->card);
    climate_chip_fit(ctx->current, ctx->current_full, ctx->current_bare);
    climate_chip_fit(ctx->fan_label, ctx->fan_full, ctx->fan_bare);
    climate_chip_fit(ctx->swing_label, ctx->swing_full, ctx->swing_bare);
}

static void climate_render(w_climate_ctx_t *ctx)
{
    char text[64];
    char raw[CLIMATE_OPTION_LEN + 4];
    if (ctx->has_target && !ctx->unavailable) {
        ui_format_temp_c(text, sizeof(text), true, ctx->target);
    } else {
        snprintf(text, sizeof(text), "--");
    }
    lv_label_set_text(ctx->target_label, text);

    if (ctx->unavailable) {
        snprintf(text, sizeof(text), "%s", ui_i18n_get("common.unavailable", "unavailable"));
    } else if (ctx->has_current) {
        char temp_text[16];
        ui_format_temp_c(temp_text, sizeof(temp_text), true, ctx->current_temp);
        snprintf(text, sizeof(text), "%s %s", ui_i18n_get("climate.current", "Now"), temp_text);
        snprintf(ctx->current_bare, sizeof(ctx->current_bare), "%s", temp_text);
    } else {
        text[0] = '\0';
        ctx->current_bare[0] = '\0';
    }
    snprintf(ctx->current_full, sizeof(ctx->current_full), "%s", text);
    lv_label_set_text(ctx->current, text);

    lv_label_set_text(ctx->mode_label, (!ctx->unavailable && ctx->hvac_mode[0] != '\0') ? climate_mode_text(ctx->hvac_mode, raw, sizeof(raw)) : "--");
    if (ctx->fan_mode_count > 0) {
        snprintf(ctx->fan_bare, sizeof(ctx->fan_bare), "%s",
                 (!ctx->unavailable && ctx->fan_mode[0] != '\0') ? climate_mode_text(ctx->fan_mode, raw, sizeof(raw)) : "--");
        snprintf(ctx->fan_full, sizeof(ctx->fan_full), "%s: %s", ui_i18n_get("climate.fan_short", "Fan"), ctx->fan_bare);
        lv_label_set_text(ctx->fan_label, ctx->fan_full);
    }
    if (ctx->swing_mode_count > 0) {
        snprintf(ctx->swing_bare, sizeof(ctx->swing_bare), "%s",
                 (!ctx->unavailable && ctx->swing_mode[0] != '\0') ? climate_mode_text(ctx->swing_mode, raw, sizeof(raw)) : "--");
        snprintf(ctx->swing_full, sizeof(ctx->swing_full), "%s: %s", ui_i18n_get("climate.swing_short", "Swing"), ctx->swing_bare);
        lv_label_set_text(ctx->swing_label, ctx->swing_full);
    }
    if (ctx->unavailable) {
        climate_popup_close(ctx);
        ctx->pending_send = false;
        if (ctx->send_timer != NULL) lv_timer_pause(ctx->send_timer);
    }
    ui_style_action_button(ctx->btn_minus, lv_obj_get_child(ctx->btn_minus, 0), UI_CONTROL_ACTION, !ctx->unavailable && ctx->has_target);
    ui_style_action_button(ctx->btn_plus, lv_obj_get_child(ctx->btn_plus, 0), UI_CONTROL_ACTION, !ctx->unavailable && ctx->has_target);
    ui_style_action_button(ctx->mode_btn, ctx->mode_label, UI_CONTROL_ACTION, !ctx->unavailable && ctx->hvac_mode_count > 0);
    ui_style_action_button(ctx->fan_btn, ctx->fan_label, UI_CONTROL_ACTION, !ctx->unavailable && ctx->fan_mode_count > 0);
    ui_style_action_button(ctx->swing_btn, ctx->swing_label, UI_CONTROL_ACTION, !ctx->unavailable && ctx->swing_mode_count > 0);
    const bool active = !ctx->unavailable && strcmp(ctx->hvac_mode, "off") != 0;
    ui_style_card_set_active(ctx->card, active);
    lv_obj_set_style_text_color(ctx->target_label, lv_color_hex(active ? APP_UI_COLOR_TEXT_PRIMARY : APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    climate_layout(ctx);
}

static void climate_event_cb(lv_event_t *event)
{
    w_climate_ctx_t *ctx = (w_climate_ctx_t *)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_DELETE && ctx != NULL) {
        if (ctx->popup != NULL) {
            lv_obj_t *p = ctx->popup;
            ctx->popup = NULL;
            lv_obj_del(p);
        }
        if (ctx->send_timer != NULL) {
            lv_timer_del(ctx->send_timer);
        }
        free(ctx);
    } else if (code == LV_EVENT_SIZE_CHANGED && ctx != NULL) {
        climate_layout(ctx);
    }
}

esp_err_t w_climate_create(const ui_widget_def_t *def, lv_obj_t *parent, ui_widget_instance_t *out_instance)
{
    if (def == NULL || parent == NULL || out_instance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    w_climate_ctx_t *ctx = ui_calloc_prefer_psram(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    snprintf(ctx->entity_id, sizeof(ctx->entity_id), "%s", def->entity_id);
    ctx->unavailable = true;
    ctx->min_temp = 7.0f;
    ctx->max_temp = 35.0f;
    strlcpy(ctx->unit, "C", sizeof(ctx->unit));

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

    ctx->current = lv_label_create(card);
    lv_obj_set_style_text_font(ctx->current, ui_font_role(UI_TEXT_META), LV_PART_MAIN);
    lv_obj_set_style_text_color(ctx->current, lv_color_hex(APP_UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->current, LV_LABEL_LONG_DOT);

    ctx->target_label = lv_label_create(card);
    lv_obj_set_style_text_font(ctx->target_label, ui_font_role(UI_TEXT_VALUE), LV_PART_MAIN);
    lv_label_set_long_mode(ctx->target_label, LV_LABEL_LONG_CLIP);

    ctx->btn_minus = climate_round_button(card, APP_SYMBOL_MINUS, climate_minus_cb, ctx);
    ctx->btn_plus = climate_round_button(card, APP_SYMBOL_PLUS, climate_plus_cb, ctx);
    ctx->mode_btn = climate_chip(card, &ctx->mode_label, climate_mode_btn_cb, ctx);
    ctx->fan_btn = climate_chip(card, &ctx->fan_label, climate_fan_btn_cb, ctx);
    ctx->swing_btn = climate_chip(card, &ctx->swing_label, climate_swing_btn_cb, ctx);

    ctx->send_timer = lv_timer_create(climate_send_timer_cb, CLIMATE_SEND_DELAY_MS, ctx);
    lv_timer_pause(ctx->send_timer);

    lv_obj_add_event_cb(card, climate_event_cb, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(card, climate_event_cb, LV_EVENT_SIZE_CHANGED, ctx);
    climate_render(ctx);
    return ESP_OK;
}

static int climate_read_options(cJSON *arr, char out[][CLIMATE_OPTION_LEN])
{
    int n = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr) {
        if (n >= CLIMATE_MAX_OPTIONS) {
            break;
        }
        if (cJSON_IsString(item) && item->valuestring != NULL) {
            strlcpy(out[n++], item->valuestring, CLIMATE_OPTION_LEN);
        }
    }
    return n;
}

void w_climate_apply_state(ui_widget_instance_t *instance, const ha_state_t *state)
{
    if (instance == NULL || instance->ctx == NULL || state == NULL) {
        return;
    }
    w_climate_ctx_t *ctx = (w_climate_ctx_t *)instance->ctx;
    if (strncmp(state->entity_id, ctx->entity_id, APP_MAX_ENTITY_ID_LEN) != 0) {
        return;
    }
    ctx->unavailable = strcmp(state->state, "unavailable") == 0 || strcmp(state->state, "unknown") == 0;
    strlcpy(ctx->hvac_mode, state->state, sizeof(ctx->hvac_mode)); /* the entity state is the hvac mode */
    cJSON *attrs = state->attributes_json[0] != '\0' ? cJSON_Parse(state->attributes_json) : NULL;
    if (attrs != NULL) {
        float v = 0.0f;
        cJSON *n = cJSON_GetObjectItemCaseSensitive(attrs, "temperature");
        if (cJSON_IsNumber(n) && !ctx->pending_send) {
            ctx->target = (float)n->valuedouble;
            ctx->has_target = true;
        }
        n = cJSON_GetObjectItemCaseSensitive(attrs, "current_temperature");
        ctx->has_current = cJSON_IsNumber(n);
        if (ctx->has_current) {
            ctx->current_temp = (float)n->valuedouble;
        }
        n = cJSON_GetObjectItemCaseSensitive(attrs, "min_temp");
        if (cJSON_IsNumber(n)) { v = (float)n->valuedouble; ctx->min_temp = v; }
        n = cJSON_GetObjectItemCaseSensitive(attrs, "max_temp");
        if (cJSON_IsNumber(n)) { v = (float)n->valuedouble; ctx->max_temp = v; }
        cJSON *fm = cJSON_GetObjectItemCaseSensitive(attrs, "fan_mode");
        ctx->fan_mode[0] = '\0';
        if (cJSON_IsString(fm) && fm->valuestring != NULL) {
            strlcpy(ctx->fan_mode, fm->valuestring, sizeof(ctx->fan_mode));
        }
        cJSON *hm = cJSON_GetObjectItemCaseSensitive(attrs, "hvac_modes");
        if (cJSON_IsArray(hm)) {
            ctx->hvac_mode_count = climate_read_options(hm, ctx->hvac_modes);
        }
        cJSON *fms = cJSON_GetObjectItemCaseSensitive(attrs, "fan_modes");
        if (cJSON_IsArray(fms)) {
            ctx->fan_mode_count = climate_read_options(fms, ctx->fan_modes);
        }
        cJSON *sm = cJSON_GetObjectItemCaseSensitive(attrs, "swing_mode");
        ctx->swing_mode[0] = '\0';
        if (cJSON_IsString(sm) && sm->valuestring != NULL) {
            strlcpy(ctx->swing_mode, sm->valuestring, sizeof(ctx->swing_mode));
        }
        cJSON *sms = cJSON_GetObjectItemCaseSensitive(attrs, "swing_modes");
        if (cJSON_IsArray(sms)) {
            ctx->swing_mode_count = climate_read_options(sms, ctx->swing_modes);
        }
        cJSON_Delete(attrs);
    }
    climate_render(ctx);
}

void w_climate_mark_unavailable(ui_widget_instance_t *instance)
{
    if (instance == NULL || instance->ctx == NULL) {
        return;
    }
    w_climate_ctx_t *ctx = (w_climate_ctx_t *)instance->ctx;
    ctx->unavailable = true;
    climate_render(ctx);
}
