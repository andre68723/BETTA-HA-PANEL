/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * The persisted appearance style IDs and their compatibility rules, without LVGL, so settings, the
 * HTTP API, the UI runtime, the simulator and the host tests share one list.
 *
 * Contract (see docs/appearance.md):
 *  - A new device starts with UI_STYLE_DEFAULT (Glow).
 *  - Any unknown stored ID, including one of a removed style, falls back to UI_STYLE_FALLBACK
 *    (Flat); there is no migration, the user picks a style again in the editor.
 *  - The HTTP API rejects an unknown ID.
 *
 * The editor (components/webui/www/js/config.js UI_STYLES) and the tools (tools/ui_styles.py)
 * repeat these lists; tools/check_web.py compares them.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Values are internal only; settings store the string ID. */
typedef enum {
    UI_STYLE_FLAT = 0,
    UI_STYLE_GLOW = 1,
    UI_STYLE_GLASS = 2,
    UI_STYLE_MATTE = 3,
} ui_style_kind_t;

#define UI_STYLE_DEFAULT UI_STYLE_GLOW
#define UI_STYLE_FALLBACK UI_STYLE_FLAT

/* True for a supported ID; `out` (optional) receives it, or UI_STYLE_FALLBACK for an unknown or
 * NULL ID. */
bool ui_style_lookup(const char *name, ui_style_kind_t *out);
/* ui_style_lookup() without the result. */
ui_style_kind_t ui_style_from_name(const char *name);
/* The persisted ID of a supported style. */
const char *ui_style_name(ui_style_kind_t kind);
/* Supported styles in display order, for iteration; false past the end. */
size_t ui_style_count(void);
bool ui_style_at(size_t index, ui_style_kind_t *out);
