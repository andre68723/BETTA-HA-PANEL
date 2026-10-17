/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Text size by role: the step table, the board profiles, the user step and the line metrics of the fonts, as pure
 * functions (no LVGL, no hardware), so they are host-tested. The font manager (text_font.c) turns the sizes into
 * lv_tiny_ttf fonts; docs/appearance.md describes the scale.
 *
 * effective_index = clamp(clamp(role_index + board_offset, board_min, 9) + user_step, board_min, 9)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* One semantic role for every text. UI_TEXT_UNASSIGNED is a diagnostic for a missing classification, not a size. */
typedef enum {
    UI_TEXT_UNASSIGNED,
    UI_TEXT_META,  /* age, date, incidental hints */
    UI_TEXT_LABEL, /* names, days, buttons, required units, ordinary list text */
    UI_TEXT_VALUE, /* ordinary measured values and important states */
    UI_TEXT_KEY,   /* prominent readings within a widget */
    UI_TEXT_HERO,  /* very large readings when the layout provides room */
    UI_TEXT_ROLE_COUNT
} ui_text_role_t;

typedef enum {
    TEXT_BOARD_PANEL4, /* 4-inch, about 255 ppi: the reference density */
    TEXT_BOARD_TAB5,   /* 5-inch, about 294 ppi */
    TEXT_BOARD_WIDE10, /* 10.1-inch boards, about 149 ppi: the same role sizes as the Panel4 (see text_scale.c for why not smaller) */
    TEXT_BOARD_COUNT
} text_board_t;

#define TEXT_STEP_COUNT 10
#define TEXT_USER_STEP_MAX 2

/* The preferred sizes, index 0..9: 16 20 24 28 32 36 42 48 56 64 px. */
int text_scale_step_px(int index);
int text_scale_clamp_index(int index);
int text_scale_clamp_user_step(int step);

/* Reference index of a role (1, 2, 3, 5, 6), -1 for UI_TEXT_UNASSIGNED. */
int text_scale_role_index(ui_text_role_t role);
int text_scale_board_offset(text_board_t board);
int text_scale_board_min_index(text_board_t board);

/* Effective index and size of a role on a board for a user step (0..2). UI_TEXT_UNASSIGNED gives -1 / 0. */
int text_scale_index(text_board_t board, ui_text_role_t role, int user_step);
int text_scale_px(text_board_t board, ui_text_role_t role, int user_step);

/* A tile that is too small for larger text caps the growth of its text: max_extra is the most steps above the role's
 * reference index its text may use (0 = the reference size of the role, as on the Panel4 at the standard step). The board's
 * density offset and the user step both count as growth; a negative board offset is never capped. */
int text_scale_index_capped(text_board_t board, ui_text_role_t role, int user_step, int max_extra);
int text_scale_px_capped(text_board_t board, ui_text_role_t role, int user_step, int max_extra);

#define TEXT_TILE_CAP_NONE 3 /* the largest growth the scale offers: Tab5 +1 with user +2 */

/* The growth a tile of a widget type and size can take (0..TEXT_TILE_CAP_NONE) on a board class (the Panel4 and Tab5 share the
 * adaptive layouts, the 10-inch boards have their own table), calibrated from the layout fixtures with
 * tools/calibrate_text_caps.py: the text of a tile smaller than the thresholds of its type keeps a smaller size than the scale
 * asks for instead of overlapping (-1: not even above the standard size of the board). Unknown types and sizes of 0 are not capped. */
int text_scale_tile_cap(text_board_t board, const char *widget_type, int w, int h); /* -1 .. TEXT_TILE_CAP_NONE */

/* Smallest size a role may be fitted down to on a board: the board's minimum text size. */
int text_scale_floor_px(text_board_t board);

/* Largest preferred size of the table. */
int text_scale_max_px(void);

/* Line metrics of the Poppins text font at a pixel size (the values of the former generated fonts at the table sizes,
 * linear in between and outside). The tiny_ttf base line is base_line - 1 (see text_font.c). */
void text_scale_text_metrics(int px, int *line_height, int *base_line);

/* Icon and emoji fonts. Each has its own glyph set and therefore its own metrics (taken from the former bitmap fonts). */
typedef enum {
    ICON_SYMBOL_SMALL, /* 18 px: transport and status symbols next to text */
    ICON_SYMBOL_MEDIUM,/* 26 px */
    ICON_SYMBOL_LARGE, /* 48 px */
    ICON_STATUS_20,    /* top bar status glyphs */
    ICON_STATUS_24,
    ICON_ENERGY_42,
    ICON_ARROWS_20,
    ICON_WEATHER_20,   /* small weather icons */
    ICON_TILE_42,      /* tile icons */
    ICON_TILE_56,
    ICON_TILE_72,
    ICON_FONT_COUNT
} ui_icon_font_t;

typedef struct {
    int px;
    int line_height;
    int base_line;
} icon_metrics_t;

icon_metrics_t text_scale_icon_metrics(ui_icon_font_t font);

/* Emoji fonts (monochrome outlines) used as the fallback of a text font: size and metrics for a text size. */
icon_metrics_t text_scale_emoji_metrics(int text_px);

/* Boards whose widgets lay out from the line heights of the fonts in use (the "adaptive" branches that were written for the Panel4):
 * the Panel4 and the Tab5. The 10-inch boards keep their fixed row heights; text growth there is limited per tile (text_scale_tile_cap). */
#if defined(CONFIG_APP_PANEL_VARIANT_4INCH_720) || defined(CONFIG_APP_PANEL_VARIANT_TAB5_1280)
#define UI_TEXT_ADAPTIVE 1
#else
#define UI_TEXT_ADAPTIVE 0
#endif

/* Board of the firmware or simulator build (from CONFIG_APP_PANEL_VARIANT_*). */
static inline text_board_t text_scale_build_board(void)
{
#if defined(CONFIG_APP_PANEL_VARIANT_TAB5_1280)
    return TEXT_BOARD_TAB5;
#elif defined(CONFIG_APP_PANEL_VARIANT_4INCH_720)
    return TEXT_BOARD_PANEL4;
#else
    return TEXT_BOARD_WIDE10;
#endif
}
