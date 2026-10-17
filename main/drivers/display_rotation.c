/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#include "drivers/display_rotation.h"

bool display_rotation_rect(display_rect_t r, uint16_t w, uint16_t h,
                           unsigned turns, display_rect_t *out)
{
    if (!out || !w || !h || turns > 3 || r.x1 < 0 || r.y1 < 0 ||
        r.x2 <= r.x1 || r.y2 <= r.y1) return false;
    const int32_t logical_w = (turns & 1) ? h : w;
    const int32_t logical_h = (turns & 1) ? w : h;
    if (r.x2 > logical_w || r.y2 > logical_h) return false;
    /* LVGL's 90-degree orientation maps the logical top-left to the native
     * bottom-left. Keep pixel rotation and native touch transformation paired. */
    switch (turns) {
    case 0: *out = r; break;
    case 1: *out = (display_rect_t){r.y1, h - r.x2, r.y2, h - r.x1}; break;
    case 2: *out = (display_rect_t){w - r.x2, h - r.y2, w - r.x1, h - r.y1}; break;
    case 3: *out = (display_rect_t){w - r.y2, r.x1, w - r.y1, r.x2}; break;
    }
    return true;
}
