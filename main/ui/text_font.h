/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * The font manager: every text, symbol, icon and emoji font of the panel is an lv_tiny_ttf instance made on demand from the
 * four embedded subsets (ui/fonts/ttf_data.h). Sizes come from the role, the board profile and the user step
 * (ui/text_scale.h); the manager applies the line metrics of the former bitmap fonts, which lv_tiny_ttf would otherwise
 * replace by the font's own (about 20 % taller lines), and wires the fallback chains at run time.
 *
 * Threading: like all LVGL objects, call it from the UI task or with the display lock held; never from the HTTP task.
 * Lifetime: an instance is created once and kept; the number of instances is bounded (UI_FONT_MAX_INSTANCES). A font that
 * widgets or styles use is never resized or freed.
 *
 * Failure outcome (budget reached or lv_tiny_ttf out of memory): ui_font_text_px(), ui_font_role() and ui_font_probe() return the nearest
 * existing text size at or below the request, else the smallest above it, else LVGL's default font; ui_font_icon() returns LVGL's default
 * font (its glyphs show as the placeholder box). None of them returns NULL, and a key that was created before keeps its instance.
 * ui_font_emoji_for_text() may return NULL: a text font without the emoji fallback is valid. ui_fonts_stats() counts both causes.
 */
#pragma once

#include "lvgl.h"

#include "ui/text_scale.h"

#define UI_FONT_MAX_INSTANCES 64

/* User step 0 (Standard), 1 (Larger), 2 (Largest); applies to fonts requested after the call, so rebuild the UI after changing it. */
void ui_fonts_set_user_step(int step);
int ui_fonts_user_step(void);

/* The board profile of the build (text_scale_build_board()); tests and the simulator may override it. */
void ui_fonts_set_board(text_board_t board);
text_board_t ui_fonts_board(void);

/* A widget builds and updates its text inside a tile context: the size of the tile caps how far its text may grow
 * (text_scale_tile_cap), so a tile that is too small keeps a smaller text instead of overlapping. The widget factory sets the
 * context around every widget callback; popups, the bars and pages outside a tile are not capped. ui_fonts_set_cap_override(n)
 * forces the cap of every tile (0..TEXT_TILE_CAP_NONE) for calibration and tests; -1 turns it off. */
void ui_fonts_tile_begin(const char *widget_type, int w, int h);
void ui_fonts_tile_end(void);
void ui_fonts_set_cap_override(int cap);

/* Effective size of a role on this board at the current user step (and the tile cap, if a tile context is open); 0 for UI_TEXT_UNASSIGNED. */
int ui_fonts_role_px(ui_text_role_t role);

/* Text font of a role, or of an explicit pixel size (the fitting uses sizes between the board floor and the preferred size). */
const lv_font_t *ui_font_role(ui_text_role_t role);
const lv_font_t *ui_font_text_px(int px);

/* Poppins Bold at the base text font's size/metrics. On budget/OOM, retain base. */
const lv_font_t *ui_font_emphasis(const lv_font_t *base);

/* Pixel size of a text font made by the manager, 0 for any other font (the probe included). */
int ui_font_text_px_of(const lv_font_t *font);

/* True when the font is a text font at the board's minimum size: the "compact" state of a fitted text. */
bool ui_font_at_floor(const lv_font_t *font);

/* Symbol, icon and weather glyph fonts (Material Design Icons) and the emoji font that follows a text size. */
const lv_font_t *ui_font_icon(ui_icon_font_t kind);
const lv_font_t *ui_font_emoji_for_text(int text_px);

/* A shared measuring font for the fitting: no glyph cache, resized per call, valid until the next probe call. Never draw with it. */
const lv_font_t *ui_font_probe(int px);

typedef struct {
    int instances;       /* live fonts, all families */
    int budget_hits;     /* requests answered by a nearest existing size because the instance budget was reached */
    int create_failures; /* lv_tiny_ttf could not create a font (out of memory or a bad font) */
} ui_fonts_stats_t;

ui_fonts_stats_t ui_fonts_stats(void);

#ifdef UI_FONTS_TEST_HOOKS
/* The next `count` font creations fail as if lv_tiny_ttf ran out of memory (tests only). */
void ui_fonts_test_fail_creates(int count);
#endif
