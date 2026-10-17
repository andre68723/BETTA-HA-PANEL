/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Color emoji for the notification popup. A small set of motifs (ui/fonts/notification_emoji.txt) is stored as raw RGB565A8 images in the
 * popup's text sizes and drawn through lv_imgfont; every other character, emoji included, falls through to the font it wraps, so it stays
 * as monochrome as before. Nothing is decoded and nothing is cached: the images are read in place from flash.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

/* Index of the color motif for a codepoint in the generated table, or -1. Pure. */
int notification_emoji_index(uint32_t codepoint);

/* Index of the image size for a text height: the nearest of 20, 24, 28 and 32 px. Pure. */
int notification_emoji_size_index(int text_px);

/* A font that draws the color motifs and otherwise falls back to `base` (the popup's text font). It has the line height and base line of
 * `base`, so labels lay out as before; the fonts are created on first use and kept for the session. NULL: use `base` as it is. */
const lv_font_t *notification_emoji_font(const lv_font_t *base, int text_px);
