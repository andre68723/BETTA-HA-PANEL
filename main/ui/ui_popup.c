/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/ui_popup.h"

#include "ui/fonts/app_symbols.h"
#include "ui/theme/theme_default.h"
#include "ui/theme/ui_style.h"

lv_obj_t *ui_popup_card_create(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
#ifdef BETTA_SIMULATOR
    lv_obj_set_user_flag(card, UI_POPUP_CARD_FLAG_INDEX, true);
#endif
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_CONTENT_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_font(card, ui_font_role(UI_TEXT_LABEL), LV_PART_MAIN);
    lv_obj_set_scrollable(card, false);
    lv_obj_set_event_bubble(card, false);
    ui_popup_style_frame(card);
    return card;
}

lv_obj_t *ui_popup_label_create(lv_obj_t *parent, const char *text, ui_popup_label_role_t role)
{
    ui_text_role_t font_role = UI_TEXT_LABEL;
    bool muted = false;
    bool essential = true;
    switch (role) {
    case UI_POPUP_LABEL_TITLE: essential = false; break;
    case UI_POPUP_LABEL_BODY: break;
    case UI_POPUP_LABEL_CAPTION: muted = true; break;
    case UI_POPUP_LABEL_HINT: font_role = UI_TEXT_META; muted = true; break;
    case UI_POPUP_LABEL_VALUE: font_role = UI_TEXT_VALUE; break;
    }
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text != NULL ? text : "");
    ui_text_set(label, font_role, essential);
    lv_obj_set_style_text_color(label, muted ? theme_default_color_text_muted() : theme_default_color_text_primary(),
                               LV_PART_MAIN);
    if (role == UI_POPUP_LABEL_TITLE || role == UI_POPUP_LABEL_CAPTION || role == UI_POPUP_LABEL_VALUE) {
        lv_label_set_long_mode(label, role == UI_POPUP_LABEL_TITLE ? LV_LABEL_LONG_DOT : LV_LABEL_LONG_CLIP);
        lv_obj_set_height(label, lv_font_get_line_height(ui_font_role(font_role)));
    }
    return label;
}

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
    lv_obj_set_style_text_color(card, theme_default_color_text_primary(), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(card,
        lv_color_mix(lv_color_hex(APP_UI_COLOR_TEXT_PRIMARY), lv_color_hex(APP_UI_COLOR_CONTENT_BG), 110), LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_COVER, LV_PART_MAIN);
}
