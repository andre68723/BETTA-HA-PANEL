/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdbool.h>

#include "lvgl.h"

const lv_font_t *mdi_font_large(void);
bool mdi_font_large_available(void);

const lv_font_t *mdi_font_icon_42(void);
bool mdi_font_icon_42_available(void);

const lv_font_t *mdi_font_icon_56(void);
bool mdi_font_icon_56_available(void);

const lv_font_t *mdi_font_icon_72(void);
bool mdi_font_icon_72_available(void);

const lv_font_t *mdi_font_weather(void);
bool mdi_font_weather_available(void);

const lv_font_t *mdi_font_weather_20(void);
bool mdi_font_weather_20_available(void);

const lv_font_t *mdi_font_weather_small(void);
bool mdi_font_weather_small_available(void);

/* Top-bar status glyphs: "home-assistant" (MDI_GLYPH_HOME_ASSISTANT) and
 * "transit-connection-variant" (MDI_GLYPH_MQTT).; NULL only if a font could not be created, then callers fall back to text. */
#define MDI_GLYPH_HOME_ASSISTANT "\xF3\xB0\x9F\x90" /* U+F07D0 */
#define MDI_GLYPH_MQTT           "\xF3\xB0\xB4\xBD" /* U+F0D3D */
#define MDI_GLYPH_ROOM           "\xF3\xB0\x92\xB9" /* U+F04B9 sofa */
#define MDI_GLYPH_HOUSE          "\xF3\xB0\x8B\x9C" /* U+F02DC home */
const lv_font_t *mdi_font_status_20(void);
const lv_font_t *mdi_font_status_24(void);

/* Energy page node icons (42 px) and the arrows of the battery/grid values (20 px). */
const lv_font_t *mdi_font_energy_42(void);
const lv_font_t *mdi_font_arrows_20(void);
