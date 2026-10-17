/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * The icon fonts by their historical names. All of them are lv_tiny_ttf instances of the Material Design Icons subset made by
 * the font manager (ui/text_font.c); the _available() functions are false only when a font could not be created.
 */
#include "ui/fonts/mdi_font_registry.h"

#include "ui/text_font.h"

const lv_font_t *mdi_font_icon_42(void) { return ui_font_icon(ICON_TILE_42); }
bool mdi_font_icon_42_available(void) { return mdi_font_icon_42() != NULL; }

const lv_font_t *mdi_font_icon_56(void) { return ui_font_icon(ICON_TILE_56); }
bool mdi_font_icon_56_available(void) { return mdi_font_icon_56() != NULL; }

const lv_font_t *mdi_font_icon_72(void) { return ui_font_icon(ICON_TILE_72); }
bool mdi_font_icon_72_available(void) { return mdi_font_icon_72() != NULL; }

const lv_font_t *mdi_font_large(void) { return mdi_font_icon_56(); }
bool mdi_font_large_available(void) { return mdi_font_large() != NULL; }

const lv_font_t *mdi_font_weather_20(void) { return ui_font_icon(ICON_WEATHER_20); }
bool mdi_font_weather_20_available(void) { return mdi_font_weather_20() != NULL; }

const lv_font_t *mdi_font_weather_small(void) { return mdi_font_weather_20(); }
bool mdi_font_weather_small_available(void) { return mdi_font_weather_small() != NULL; }

const lv_font_t *mdi_font_weather(void) { return mdi_font_icon_72(); }
bool mdi_font_weather_available(void) { return mdi_font_weather() != NULL; }

const lv_font_t *mdi_font_status_20(void) { return ui_font_icon(ICON_STATUS_20); }
const lv_font_t *mdi_font_status_24(void) { return ui_font_icon(ICON_STATUS_24); }

const lv_font_t *mdi_font_energy_42(void) { return ui_font_icon(ICON_ENERGY_42); }
const lv_font_t *mdi_font_arrows_20(void) { return ui_font_icon(ICON_ARROWS_20); }
