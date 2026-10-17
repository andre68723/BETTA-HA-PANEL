/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/ui_popup.h"

#include "ui/fonts/app_symbols.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"

lv_obj_t *ui_popup_close_button(lv_obj_t *parent, lv_color_t bg, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, UI_POPUP_CLOSE_SIZE, UI_POPUP_CLOSE_SIZE);
    lv_obj_set_ext_click_area(button, UI_POPUP_CLOSE_EXTRA_TOUCH);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
    ui_style_button_auto(button, bg, lv_color_hex(APP_UI_COLOR_TOPBAR_CHIP_BORDER));
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, APP_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(label, APP_FONT_SYMBOL_LARGE, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, ui_style_button_icon_color(bg), LV_PART_MAIN);
    lv_obj_center(label);
    return button;
}

void ui_popup_style_frame(lv_obj_t *card)
{
    if (card == NULL) {
        return;
    }
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(card,
        lv_color_mix(lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), lv_color_hex(APP_UI_COLOR_CONTENT_BG), 110), LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_COVER, LV_PART_MAIN);
}
