/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723
 *
 * Text by role: every text asks for a semantic role (ui/text_scale.h) and gets the lv_tiny_ttf font of the board profile and the user
 * step (ui/text_font.h). There is no legacy font argument and no pixel macro.
 */
#pragma once

#include <stdbool.h>

#include "sdkconfig.h"
#include "ui/text_font.h"
#include "ui/text_scale.h"

/* Semantic diagnostics live only in the simulator; firmware needs no label
 * allocations and leaves LVGL's four user flags to widget behavior. */
#ifdef BETTA_SIMULATOR
void ui_text_record(lv_obj_t *label, ui_text_role_t role, bool essential);
ui_text_role_t ui_text_role(lv_obj_t *label);
bool ui_text_is_essential(lv_obj_t *label);
void ui_text_compact(lv_obj_t *label, bool compact);
bool ui_text_is_compact(lv_obj_t *label);
#else
static inline void ui_text_record(lv_obj_t *label, ui_text_role_t role, bool essential)
{ (void)label; (void)role; (void)essential; }
static inline void ui_text_compact(lv_obj_t *label, bool compact) { (void)label; (void)compact; }
#endif

static inline const lv_font_t *ui_text_font(ui_text_role_t role) { return ui_font_role(role); }

static inline void ui_text_set(lv_obj_t *label, ui_text_role_t role, bool essential)
{
    ui_text_record(label, role, essential);
    lv_obj_set_style_text_font(label, ui_font_role(role), LV_PART_MAIN);
}

/* The largest size not above the preferred font that fits the text into width x height, searched in 2 px steps down to the board's
 * minimum text size; the candidates are measured with the manager's cache-less probe font and only the chosen size becomes a render
 * font. When nothing fits the minimum size is returned (units and values stay present; a tile that still cannot fit is a layout
 * failure the simulator checks report). */
static inline const lv_font_t *ui_text_fit(const char *text, const lv_font_t *preferred, lv_coord_t width, lv_coord_t height)
{
    const int preferred_px = ui_font_text_px_of(preferred);
    const int floor_px = text_scale_floor_px(ui_fonts_board());
    if (preferred_px <= floor_px) return preferred != NULL ? preferred : ui_font_text_px(floor_px);
    for (int px = preferred_px; px >= floor_px; px -= 2) {
        const lv_font_t *probe = ui_font_probe(px);
        if (probe->line_height > height) continue;
        lv_point_t size;
        lv_text_get_size(&size, text, probe, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x <= width) return ui_font_text_px(px);
    }
    return ui_font_text_px(floor_px);
}
