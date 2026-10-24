/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include "lvgl.h"

/* USER_1 is reserved on widget name labels for the persistent display preference.
 * Keep text for popup headings, while layout/state updates may independently hide labels. */
static inline bool ui_widget_name_enabled(lv_obj_t *label)
{
    return !lv_obj_get_user_flag(label, 0);
}

static inline void ui_widget_name_set_hidden(lv_obj_t *label, bool hidden)
{
    lv_obj_set_hidden(label, hidden || !ui_widget_name_enabled(label));
}

static inline void ui_widget_name_configure(lv_obj_t *label, bool visible)
{
    lv_obj_set_user_flag(label, 0, !visible);
    ui_widget_name_set_hidden(label, !visible);
}

static inline lv_obj_t *ui_widget_name_create(lv_obj_t *parent, bool visible)
{
    lv_obj_t *label = lv_label_create(parent);
    ui_widget_name_configure(label, visible);
    return label;
}

/* One line, ellipsized: a long name must not wrap into the content below it. Call after setting the font. */
static inline void ui_widget_name_one_line(lv_obj_t *label)
{
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_height(label, lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN)));
}

static inline lv_coord_t ui_widget_name_height(lv_obj_t *label, const lv_font_t *font)
{
    return ui_widget_name_enabled(label) ? lv_font_get_line_height(font) : 0;
}

static inline lv_coord_t ui_widget_name_gap(lv_obj_t *label, lv_coord_t gap)
{
    return ui_widget_name_enabled(label) ? gap : 0;
}
