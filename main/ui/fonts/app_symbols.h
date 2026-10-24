/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include "lvgl.h"

#include "ui/text_font.h"

/* UI symbols from Material Design Icons, the same icon family as the page, status and
 * widget icons. They replace LVGL's LV_SYMBOL_* (FontAwesome glyphs inside the built-in
 * Montserrat fonts), which are no longer compiled in. An MDI glyph fills less of its em
 * square than FontAwesome did, so SMALL/MEDIUM are 18/26 px to keep the former 14/20 px
 * icons' visual size. The media transport glyphs (play, pause, stop, next, previous) are
 * drawn smaller still: use them one step up (MEDIUM where a 14 px symbol used to be), and
 * LARGE, which only the button widget's transport icons use, is 48 px for the former 28 px.
 * Each falls back to the Poppins text font of its size at run time (text_font.c), so a label may mix a
 * symbol with text ("AP " + APP_SYMBOL_WIFI). The codepoints rendered by the firmware are listed in
 * mdi_codepoints.txt: add a symbol there and below, then regenerate the subset (tools/generate_widget_fonts.py --mdi). */
#define APP_FONT_SYMBOL_SMALL  (ui_font_icon(ICON_SYMBOL_SMALL))  /* 18 px */
#define APP_FONT_SYMBOL_MEDIUM (ui_font_icon(ICON_SYMBOL_MEDIUM)) /* 26 px */
#define APP_FONT_SYMBOL_LARGE  (ui_font_icon(ICON_SYMBOL_LARGE))  /* 48 px */

#define APP_SYMBOL_PLAY         "\xF3\xB0\x90\x8A" /* U+F040A play */
#define APP_SYMBOL_PAUSE        "\xF3\xB0\x8F\xA4" /* U+F03E4 pause */
#define APP_SYMBOL_STOP         "\xF3\xB0\x93\x9B" /* U+F04DB stop */
#define APP_SYMBOL_NEXT         "\xF3\xB0\x92\xAD" /* U+F04AD skip-next */
#define APP_SYMBOL_PREV         "\xF3\xB0\x92\xAE" /* U+F04AE skip-previous */
#define APP_SYMBOL_SHUFFLE      "\xF3\xB0\x92\x9D" /* U+F049D shuffle */
#define APP_SYMBOL_REPEAT       "\xF3\xB0\x91\x96" /* U+F0456 repeat */
#define APP_SYMBOL_REPEAT_ONCE  "\xF3\xB0\x91\x98" /* U+F0458 repeat-once */
#define APP_SYMBOL_VOLUME       "\xF3\xB0\x96\x80" /* U+F0580 volume-medium */
#define APP_SYMBOL_MUTE         "\xF3\xB0\x96\x81" /* U+F0581 volume-off */
#define APP_SYMBOL_POWER        "\xF3\xB0\x90\xA5" /* U+F0425 power */
#define APP_SYMBOL_PLUS         "\xF3\xB0\x90\x95" /* U+F0415 plus */
#define APP_SYMBOL_MINUS        "\xF3\xB0\x8D\xB4" /* U+F0374 minus */
#define APP_SYMBOL_ARROW_UP     "\xF3\xB0\x81\x9D" /* U+F005D arrow-up */
#define APP_SYMBOL_ARROW_DOWN   "\xF3\xB0\x81\x85" /* U+F0045 arrow-down */
#define APP_SYMBOL_CHEVRON_DOWN "\xF3\xB0\x85\x80" /* U+F0140 chevron-down */
#define APP_SYMBOL_CHECK        "\xF3\xB0\x84\xAC" /* U+F012C check */
#define APP_SYMBOL_WIFI         "\xF3\xB0\x96\xA9" /* U+F05A9 wifi */
#define APP_SYMBOL_WIFI_OFF     "\xF3\xB0\x96\xAA" /* U+F05AA wifi-off */
#define APP_SYMBOL_WIFI_WEAK    "\xF3\xB0\xA4\x9F" /* U+F091F wifi-strength-1 */
#define APP_SYMBOL_WIFI_MEDIUM  "\xF3\xB0\xA4\xA2" /* U+F0922 wifi-strength-2 */
#define APP_SYMBOL_WIFI_STRONG  "\xF3\xB0\xA4\xA8" /* U+F0928 wifi-strength-4 */
#define APP_SYMBOL_HOME         "\xF3\xB0\x8B\x9C" /* U+F02DC home */
#define APP_SYMBOL_SOLAR        "\xF3\xB0\xA9\xB2" /* U+F0A72 solar-power */
#define APP_SYMBOL_GRID         "\xF3\xB0\xB4\xBE" /* U+F0D3E transmission-tower */
#define APP_SYMBOL_BATTERY      "\xF3\xB1\x8A\xA3" /* U+F12A3 battery-high */
#define APP_SYMBOL_CAR          "\xF3\xB0\x84\x8B" /* U+F010B car */
#define APP_SYMBOL_LIST         "\xF3\xB0\x89\xB9" /* U+F0279 format-list-bulleted */
#define APP_SYMBOL_LOCK        "\xF3\xB0\x8C\xBE" /* U+F033E lock */
#define APP_SYMBOL_LOCK_OPEN   "\xF3\xB0\x8C\xBF" /* U+F033F lock-open */
#define APP_SYMBOL_LOCK_ALERT  "\xF3\xB0\xA3\xAE" /* U+F08EE lock-alert */
#define APP_SYMBOL_DOOR_OPEN   "\xF3\xB0\xA0\x9C" /* U+F081C door-open */
#define APP_SYMBOL_IMAGE        "\xF3\xB0\x8B\xA9" /* U+F02E9 image */
#define APP_SYMBOL_LOCATE       "\xF3\xB0\x86\xA4" /* U+F01A4 crosshairs-gps */
#define APP_SYMBOL_CLOSE        "\xF3\xB0\x85\x96" /* U+F0156 close */
#define APP_SYMBOL_ALERT        "\xF3\xB0\x80\xA6" /* U+F0026 alert */
