/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include "app_config.h"
#include "ui/theme/theme_palette.h"

#ifndef APP_UI_REWORK_V2
#define APP_UI_REWORK_V2 1
#endif

/*
 * Global switch for experimental tile spacing/alignment tuning.
 * Set to 0 to instantly restore legacy per-tile layout positions.
 */
#ifndef APP_UI_TILE_LAYOUT_TUNED
#define APP_UI_TILE_LAYOUT_TUNED 1
#endif

#if APP_UI_REWORK_V2
#ifndef APP_UI_CARD_RADIUS
#define APP_UI_CARD_RADIUS 22
#endif
#else
#ifndef APP_UI_CARD_RADIUS
#define APP_UI_CARD_RADIUS 20
#endif
#endif

/*
 * All APP_UI_COLOR_* macros route through the runtime theme palette so a
 * theme change at runtime instantly re-colours every widget that calls
 * lv_color_hex(APP_UI_COLOR_X) during its re-render pass.
 * Built-in presets, persistence and the web editor live in
 * theme_palette.{c,h} and theme_store.{c,h}.
 */
#define APP_UI_COLOR_SCREEN_BG           (theme_palette_active()->screen_bg)
#define APP_UI_COLOR_SCREEN_BG_GRAD      (theme_palette_active()->screen_bg_grad)
#define APP_UI_COLOR_CONTENT_BG          (theme_palette_active()->content_bg)
#define APP_UI_COLOR_CONTENT_BORDER      (theme_palette_active()->content_border)

#define APP_UI_COLOR_TOPBAR_TEXT         (theme_palette_active()->topbar_text)
#define APP_UI_COLOR_TOPBAR_MUTED        (theme_palette_active()->topbar_muted)
#define APP_UI_COLOR_TOPBAR_STATUS_ON    (theme_palette_active()->topbar_status_on)
#define APP_UI_COLOR_TOPBAR_STATUS_OFF   (theme_palette_active()->topbar_status_off)
#define APP_UI_COLOR_TOPBAR_BG           (theme_palette_active()->topbar_bg)
#define APP_UI_COLOR_TOPBAR_BORDER       (theme_palette_active()->topbar_border)
#define APP_UI_COLOR_TOPBAR_CHIP_BG      (theme_palette_active()->topbar_chip_bg)
#define APP_UI_COLOR_TOPBAR_CHIP_BORDER  (theme_palette_active()->topbar_chip_border)

#define APP_UI_COLOR_TEXT_PRIMARY        (theme_palette_active()->text_primary)
#define APP_UI_COLOR_TEXT_MUTED          (theme_palette_active()->text_muted)
#define APP_UI_COLOR_TEXT_SOFT           (theme_palette_active()->text_soft)

#define APP_UI_COLOR_NAV_BG              (theme_palette_active()->nav_bg)
#define APP_UI_COLOR_NAV_BORDER          (theme_palette_active()->nav_border)
#define APP_UI_COLOR_NAV_BTN_BG_IDLE     (theme_palette_active()->nav_btn_bg_idle)
#define APP_UI_COLOR_NAV_BTN_BG_ACTIVE   (theme_palette_active()->nav_btn_bg_active)
#define APP_UI_COLOR_NAV_TAB_IDLE        (theme_palette_active()->nav_tab_idle)
#define APP_UI_COLOR_NAV_TAB_ACTIVE      (theme_palette_active()->nav_tab_active)
#define APP_UI_COLOR_NAV_HOME_IDLE       (theme_palette_active()->nav_home_idle)
#define APP_UI_COLOR_NAV_HOME_ACTIVE     (theme_palette_active()->nav_home_active)

#define APP_UI_COLOR_OK                  (theme_palette_active()->ok)
#define APP_UI_COLOR_ERROR               (theme_palette_active()->error)
#define APP_UI_COLOR_WIFI_OFF            (theme_palette_active()->wifi_off)

#define APP_UI_COLOR_CARD_BG_OFF         (theme_palette_active()->card_bg_off)
#define APP_UI_COLOR_CARD_BG_ON          (theme_palette_active()->card_bg_on)
#define APP_UI_COLOR_CARD_BORDER         (theme_palette_active()->card_border)
#define APP_UI_COLOR_CARD_ICON_OFF       (theme_palette_active()->card_icon_off)
#define APP_UI_COLOR_CARD_ICON_ON        (theme_palette_active()->card_icon_on)
#define APP_UI_COLOR_STATE_ON            (theme_palette_active()->state_on)
#define APP_UI_COLOR_STATE_OFF           (theme_palette_active()->state_off)

#define APP_UI_COLOR_LIGHT_ICON_ON       (theme_palette_active()->light_icon_on)
#define APP_UI_COLOR_LIGHT_TRACK_ON      (theme_palette_active()->light_track_on)
#define APP_UI_COLOR_LIGHT_TRACK_OFF     (theme_palette_active()->light_track_off)
#define APP_UI_COLOR_LIGHT_IND_ON        (theme_palette_active()->light_ind_on)
#define APP_UI_COLOR_LIGHT_IND_OFF       (theme_palette_active()->light_ind_off)
#define APP_UI_COLOR_LIGHT_KNOB_ON       (theme_palette_active()->light_knob_on)
#define APP_UI_COLOR_LIGHT_KNOB_OFF      (theme_palette_active()->light_knob_off)

#define APP_UI_COLOR_HEAT_ICON_ON        (theme_palette_active()->heat_icon_on)
#define APP_UI_COLOR_HEAT_TRACK_ON       (theme_palette_active()->heat_track_on)
#define APP_UI_COLOR_HEAT_TRACK_OFF      (theme_palette_active()->heat_track_off)
#define APP_UI_COLOR_HEAT_IND_ON         (theme_palette_active()->heat_ind_on)
#define APP_UI_COLOR_HEAT_IND_OFF        (theme_palette_active()->heat_ind_off)
#define APP_UI_COLOR_HEAT_KNOB_ON        (theme_palette_active()->heat_knob_on)
#define APP_UI_COLOR_HEAT_KNOB_OFF       (theme_palette_active()->heat_knob_off)

#define APP_UI_COLOR_WEATHER_ICON        (theme_palette_active()->weather_icon)

void theme_default_init(void);
void theme_default_rebuild_styles(void);
void theme_default_style_screen(lv_obj_t *obj);
void theme_default_style_card(lv_obj_t *obj);
void theme_default_style_button(lv_obj_t *obj, bool is_on);
lv_color_t theme_default_color_text_primary(void);
lv_color_t theme_default_color_text_muted(void);
