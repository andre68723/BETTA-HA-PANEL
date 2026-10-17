/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 * Host configuration for the LVGL SDL widget simulator. Keep visual options
 * aligned with config/sdkconfig.defaults.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_USE_SDL 1
#define LV_SDL_DIRECT_EXIT 1
#define LV_USE_LODEPNG 1
#define LV_USE_IMGFONT 1 /* the notification popup draws a few color emoji (ui/notification_emoji.c) */
#define LV_USE_SNAPSHOT 1

#define LV_FONT_MONTSERRAT_14 0
/* Every text, symbol, icon and emoji is an lv_tiny_ttf font made by ui/text_font.c; LVGL's own default is the tiny built-in font. */
#define LV_USE_TINY_TTF 1
#define LV_USE_FONT_PLACEHOLDER 1 /* a character no font has is a box, as on the panel (config/sdkconfig.defaults) */
#define LV_TINY_TTF_FILE_SUPPORT 0
#define LV_FONT_UNSCII_8 1
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(lv_font_unscii_8)
#define LV_FONT_DEFAULT (&lv_font_unscii_8)
#define LV_FONT_FMT_TXT_LARGE 1
#define LV_TXT_BREAK_CHARS " ,.;:-_"

#endif
