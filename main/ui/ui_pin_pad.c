/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/ui_pin_pad.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/theme/ui_typography.h"
#include "ui/ui_i18n.h"
#include "ui/ui_memory.h"
#include "ui/ui_popup.h"

#define PAD_HEAD_H 60
#define PAD_DISPLAY_H 52
#define PAD_GAP 10
#define PAD_CARD_PAD 14
#define PAD_ROW_H_MAX 64
#define PAD_ROOMY_HEAD_H 76
#define PAD_ROOMY_DISPLAY_H 64
#define PAD_ROOMY_GAP 14
#define PAD_ROOMY_CARD_PAD 22
#define PAD_ROOMY_ROW_H_MAX 88
#define PAD_ROOMY_CARD_W 440

#define KEY_BACK 10
#define KEY_OK 11

typedef struct {
    ui_pin_pad_cfg_t cfg;
    lv_obj_t *overlay;
    lv_obj_t *dots;
    char code[UI_PIN_PAD_MAX_LEN + 1];
    size_t len;
} pad_t;

static void pad_show_code(pad_t *pad)
{
    char dots[UI_PIN_PAD_MAX_LEN * 4 + 1];
    size_t n = 0;
    for (size_t i = 0; i < pad->len && n + 4 < sizeof(dots); i++) {
        dots[n++] = '*';
        dots[n++] = ' ';
    }
    dots[n] = '\0';
    lv_label_set_text(pad->dots, n > 0 ? dots : "");
}

static void pad_deleted_cb(lv_event_t *event)
{
    pad_t *pad = (pad_t *)lv_event_get_user_data(event);
    if (pad == NULL) {
        return;
    }
    if (pad->cfg.closed != NULL) {
        pad->cfg.closed(pad->cfg.user);
    }
    memset(pad->code, 0, sizeof(pad->code)); /* the code does not outlive the popup */
    free(pad);
}

static void pad_close_cb(lv_event_t *event)
{
    pad_t *pad = (pad_t *)lv_event_get_user_data(event);
    lv_obj_delete_async(pad->overlay);
}

static void pad_backdrop_cb(lv_event_t *event)
{
    pad_t *pad = (pad_t *)lv_event_get_user_data(event);
    if (lv_event_get_target(event) == lv_event_get_current_target(event)) {
        lv_obj_delete_async(pad->overlay);
    }
}

static void pad_key_cb(lv_event_t *event)
{
    pad_t *pad = (pad_t *)lv_event_get_user_data(event);
    const int key = (int)(uintptr_t)lv_obj_get_user_data(lv_event_get_target(event));
    if (key == KEY_OK) {
        if (pad->len == 0) {
            return;
        }
        if (pad->cfg.submit != NULL) {
            pad->cfg.submit(pad->cfg.user, pad->code);
        }
        lv_obj_delete_async(pad->overlay);
    } else if (key == KEY_BACK) {
        if (pad->len > 0) {
            pad->code[--pad->len] = '\0';
        }
    } else if (pad->len < UI_PIN_PAD_MAX_LEN) {
        pad->code[pad->len++] = (char)('0' + key);
        pad->code[pad->len] = '\0';
    }
    pad_show_code(pad);
}

static lv_color_t pad_pressed(lv_color_t color)
{
    return lv_color_brightness(color) > 127 ? lv_color_darken(color, 26) : lv_color_lighten(color, 26);
}

lv_obj_t *ui_pin_pad_open(const ui_pin_pad_cfg_t *cfg)
{
    if (cfg == NULL) {
        return NULL;
    }
    pad_t *pad = ui_calloc_prefer_psram(1, sizeof(*pad));
    if (pad == NULL) {
        return NULL;
    }
    pad->cfg = *cfg;

    lv_obj_t *screen = lv_scr_act();
    lv_obj_update_layout(screen);
    const int screen_w = lv_obj_get_width(screen) > 0 ? lv_obj_get_width(screen) : 720;
    const int screen_h = lv_obj_get_height(screen) > 0 ? lv_obj_get_height(screen) : 720;

    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    pad->overlay = overlay;
    lv_obj_set_size(overlay, screen_w, screen_h);
    lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_scrollable(overlay, false);
    lv_obj_set_clickable(overlay, true);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(overlay, pad_backdrop_cb, LV_EVENT_CLICKED, pad);
    lv_obj_add_event_cb(overlay, pad_deleted_cb, LV_EVENT_DELETE, pad);

    const bool roomy = cfg->roomy;
    const int head_h = roomy ? PAD_ROOMY_HEAD_H : PAD_HEAD_H;
    const int display_h = roomy ? PAD_ROOMY_DISPLAY_H : PAD_DISPLAY_H;
    const int gap = roomy ? PAD_ROOMY_GAP : PAD_GAP;
    const int card_pad = roomy ? PAD_ROOMY_CARD_PAD : PAD_CARD_PAD;
    const int row_h_max = roomy ? PAD_ROOMY_ROW_H_MAX : PAD_ROW_H_MAX;
    const int card_w_max = roomy ? PAD_ROOMY_CARD_W : 360;
    int card_w = screen_w - 48;
    card_w = card_w > card_w_max ? card_w_max : (card_w < 260 ? 260 : card_w);
    /* A short screen shrinks the key rows rather than the card running off it. */
    int row_h = (screen_h - 48 - 2 * card_pad - head_h - display_h - 4 * gap) / 4;
    row_h = row_h > row_h_max ? row_h_max : (row_h < 40 ? 40 : row_h);
    const int card_h = 2 * card_pad + head_h + display_h + 4 * row_h + 4 * gap;
    const int inner_w = card_w - 2 * card_pad;

    lv_obj_t *card = ui_popup_card_create(overlay);
    lv_obj_set_size(card, card_w, card_h);
    lv_obj_center(card);
    lv_obj_set_style_radius(card, 12, LV_PART_MAIN);

    lv_obj_set_style_pad_all(card, card_pad, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, cfg->title != NULL ? cfg->title : "");
    lv_obj_set_style_text_font(title, ui_font_role(UI_TEXT_VALUE), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, inner_w - 64);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 4);

    lv_obj_t *close = ui_popup_close_button(card, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), pad_close_cb, pad);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *display = lv_obj_create(card);
    lv_obj_set_size(display, inner_w, display_h);
    lv_obj_align(display, LV_ALIGN_TOP_LEFT, 0, head_h - 6);
    lv_obj_set_scrollable(display, false);
    lv_obj_set_style_radius(display, ui_style_radius(14), LV_PART_MAIN);
    lv_obj_set_style_bg_color(display, lv_color_hex(APP_UI_COLOR_CARD_BG_OFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(display, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(display, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(display, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER), LV_PART_MAIN);
    lv_obj_set_style_pad_all(display, 0, LV_PART_MAIN);
    pad->dots = lv_label_create(display);
    lv_obj_set_style_text_font(pad->dots, ui_font_role(UI_TEXT_VALUE), LV_PART_MAIN);
    lv_obj_set_style_text_color(pad->dots, lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_label_set_text(pad->dots, "");
    lv_obj_center(pad->dots);

    const lv_coord_t key_w = (inner_w - 2 * gap) / 3;
    const lv_coord_t grid_top = head_h + display_h + gap - 6;
    /* 1 2 3 / 4 5 6 / 7 8 9 / backspace 0 OK */
    static const int keys[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, KEY_BACK, 0, KEY_OK};
    for (int i = 0; i < 12; i++) {
        const int key = keys[i];
        const bool ok = key == KEY_OK;
        const lv_color_t bg = ui_style_control_color(UI_CONTROL_ACTION, true);
        lv_obj_t *btn = lv_btn_create(card);
        lv_obj_set_size(btn, key_w, row_h);
        lv_obj_set_pos(btn, (i % 3) * (key_w + gap), grid_top + (i / 3) * (row_h + gap));
        lv_obj_set_user_data(btn, (void *)(uintptr_t)key);
        lv_obj_add_event_cb(btn, pad_key_cb, LV_EVENT_CLICKED, pad);
        lv_obj_set_style_radius(btn, ui_style_radius(18), LV_PART_MAIN);
        ui_style_button(btn, bg, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER), pad_pressed(bg));
        lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
        lv_obj_t *label = lv_label_create(btn);
        char text[16];
        if (key == KEY_BACK) {
            snprintf(text, sizeof(text), "<");
        } else if (ok) {
            snprintf(text, sizeof(text), "%s", ui_i18n_get("alarm.ok", "OK"));
        } else {
            snprintf(text, sizeof(text), "%d", key);
        }
        lv_label_set_text(label, text);
        lv_obj_set_style_text_font(label, ui_font_role(UI_TEXT_VALUE), LV_PART_MAIN);
        lv_obj_set_style_text_color(label, ui_style_button_icon_color(bg), LV_PART_MAIN);
        lv_obj_center(label);
    }
    return overlay;
}

void ui_pin_pad_close(lv_obj_t *pad)
{
    if (pad != NULL) {
        lv_obj_delete(pad);
    }
}
