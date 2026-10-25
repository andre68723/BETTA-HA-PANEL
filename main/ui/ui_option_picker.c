/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Moved out of the vacuum tile's cleaning-mode popup so the select tile shares it.
 */
#include "ui/ui_option_picker.h"

#include <stdlib.h>

#include "ui/theme/ui_typography.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"
#include "ui/ui_memory.h"
#include "ui/ui_popup.h"

#define PICKER_ROW_H 64   /* a comfortable finger target on the 4-inch panel, room for 28 px text */
#define PICKER_ROW_GAP 10
#define PICKER_HEAD_H 60
#define PICKER_CARD_PAD 14
/* roomy: the alarm tile's actions, which are touched deliberately and should not be a fiddly target */
#define PICKER_ROOMY_ROW_H 88
#define PICKER_ROOMY_ROW_GAP 16
#define PICKER_ROOMY_HEAD_H 84
#define PICKER_ROOMY_CARD_PAD 24
#define PICKER_ROOMY_CARD_W 560
#define PICKER_LABEL_LEN 96

typedef struct {
    ui_option_picker_cfg_t cfg;
    lv_obj_t *overlay;
} picker_t;

static void picker_deleted_cb(lv_event_t *event)
{
    picker_t *picker = (picker_t *)lv_event_get_user_data(event);
    if (picker == NULL) {
        return;
    }
    if (picker->cfg.closed != NULL) {
        picker->cfg.closed(picker->cfg.user);
    }
    free(picker);
}

static void picker_backdrop_cb(lv_event_t *event)
{
    picker_t *picker = (picker_t *)lv_event_get_user_data(event);
    if (lv_event_get_target(event) == lv_event_get_current_target(event)) {
        lv_obj_delete_async(picker->overlay);
    }
}

static void picker_close_cb(lv_event_t *event)
{
    picker_t *picker = (picker_t *)lv_event_get_user_data(event);
    lv_obj_delete_async(picker->overlay);
}

static void picker_option_cb(lv_event_t *event)
{
    picker_t *picker = (picker_t *)lv_event_get_user_data(event);
    const size_t index = (size_t)(uintptr_t)lv_obj_get_user_data(lv_event_get_target(event));
    if (index < picker->cfg.count && picker->cfg.chosen != NULL) {
        picker->cfg.chosen(picker->cfg.user, index);
    }
    /* Not deleted in place: this row belongs to the overlay. */
    lv_obj_delete_async(picker->overlay);
}

lv_obj_t *ui_option_picker_open(const ui_option_picker_cfg_t *cfg)
{
    if (cfg == NULL || cfg->count == 0 || cfg->label == NULL) {
        return NULL;
    }
    picker_t *picker = ui_calloc_prefer_psram(1, sizeof(*picker));
    if (picker == NULL) {
        return NULL;
    }
    picker->cfg = *cfg;

    lv_obj_t *screen = lv_scr_act();
    lv_obj_update_layout(screen);
    int screen_w = lv_obj_get_width(screen) > 0 ? lv_obj_get_width(screen) : 720;
    int screen_h = lv_obj_get_height(screen) > 0 ? lv_obj_get_height(screen) : 720;

    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    picker->overlay = overlay;
    lv_obj_set_size(overlay, screen_w, screen_h);
    lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_scrollable(overlay, false);
    lv_obj_set_clickable(overlay, true);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(overlay, picker_backdrop_cb, LV_EVENT_CLICKED, picker);
    lv_obj_add_event_cb(overlay, picker_deleted_cb, LV_EVENT_DELETE, picker);

    const int row_h = cfg->roomy ? PICKER_ROOMY_ROW_H : PICKER_ROW_H;
    const int row_gap = cfg->roomy ? PICKER_ROOMY_ROW_GAP : PICKER_ROW_GAP;
    const int head_h = cfg->roomy ? PICKER_ROOMY_HEAD_H : PICKER_HEAD_H;
    const int card_pad = cfg->roomy ? PICKER_ROOMY_CARD_PAD : PICKER_CARD_PAD;
    const int card_w_max = cfg->roomy ? PICKER_ROOMY_CARD_W : 480;
    int card_w = screen_w - 48;
    card_w = card_w > card_w_max ? card_w_max : (card_w < 260 ? 260 : card_w);
    /* the rows, the gaps between them, the header and the card's padding on both sides (a smaller want_h cut off the last row) */
    const int rows_h = (int)cfg->count * row_h + ((int)cfg->count > 0 ? (int)cfg->count - 1 : 0) * row_gap;
    const int want_h = head_h + rows_h + 2 * card_pad;
    const int card_h = want_h < screen_h - 48 ? want_h : screen_h - 48;

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
    lv_obj_set_width(title, card_w - 2 * card_pad - 64);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 4);

    lv_obj_t *close = ui_popup_close_button(card, lv_color_hex(APP_UI_COLOR_NAV_BTN_BG_IDLE), picker_close_cb, picker);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, 0);

    /* Scrollable: a long list (11 cleaning modes, an evcc vehicle list) does not fit a small panel. */
    lv_obj_t *list = lv_obj_create(card);
    lv_obj_set_size(list, card_w - 2 * card_pad, card_h - 2 * card_pad - head_h);
    lv_obj_align(list, LV_ALIGN_TOP_LEFT, 0, head_h - 6);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, row_gap, LV_PART_MAIN);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(list, true);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    lv_obj_t *current_btn = NULL;
    for (size_t i = 0; i < cfg->count; i++) {
        const bool current = i == cfg->current;
        const lv_color_t bg = ui_style_control_color(current ? UI_CONTROL_SELECTED : UI_CONTROL_ACTION, true);
        lv_obj_t *btn = lv_btn_create(list);
        lv_obj_set_size(btn, LV_PCT(100), row_h);
        lv_obj_set_scroll_on_focus(btn, false);
        lv_obj_set_user_data(btn, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(btn, picker_option_cb, LV_EVENT_CLICKED, picker);
        lv_obj_set_style_radius(btn, ui_style_radius(18), LV_PART_MAIN);
        ui_style_action_button(btn, NULL, current ? UI_CONTROL_SELECTED : UI_CONTROL_ACTION, true);
        lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);

        lv_obj_t *label = lv_label_create(btn);
        char text[PICKER_LABEL_LEN] = {0};
        cfg->label(cfg->user, i, text, sizeof(text));
        lv_label_set_text(label, text);
        lv_obj_set_style_text_font(label, ui_font_role(UI_TEXT_VALUE), LV_PART_MAIN);
        lv_obj_set_style_text_color(label, ui_style_button_icon_color(bg), LV_PART_MAIN);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, card_w - 2 * card_pad - 28);
        lv_obj_set_height(label, lv_font_get_line_height(ui_font_role(UI_TEXT_VALUE)));
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 8, 0);
        if (current) {
            current_btn = btn;
        }
    }
    if (current_btn != NULL) {
        lv_obj_scroll_to_view(current_btn, LV_ANIM_OFF); /* a long list opens at the current option */
    }
    return overlay;
}

void ui_option_picker_close(lv_obj_t *picker)
{
    if (picker != NULL) {
        lv_obj_delete(picker);
    }
}
