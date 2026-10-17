/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Coordinates use exclusive ends, like esp_lcd_panel_draw_bitmap(). */
typedef struct {
    int32_t x1, y1, x2, y2;
} display_rect_t;

bool display_rotation_rect(display_rect_t logical, uint16_t native_width,
                           uint16_t native_height, unsigned quarter_turns,
                           display_rect_t *physical);
