/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include <stdbool.h>

int display_geometry_width(void);
int display_geometry_height(void);
int display_geometry_rotation(void);
bool display_geometry_set_rotation(int degrees);
bool display_geometry_portrait(void);

#define APP_VIEW_WIDTH (display_geometry_width())
#define APP_VIEW_HEIGHT (display_geometry_height())
#define APP_VIEW_CONTENT_WIDTH APP_VIEW_WIDTH
#define APP_VIEW_CONTENT_HEIGHT (APP_VIEW_HEIGHT - 120)
