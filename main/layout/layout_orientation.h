/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include <stdbool.h>
#include "cJSON.h"
#include "layout/widget_types.h"

#define LAYOUT_PORTRAIT_WIDTH 720
#define LAYOUT_PORTRAIT_HEIGHT 1160
#define LAYOUT_PORTRAIT_MAX_Y 30000

/* Project a canonical landscape layout for viewing/editing, retaining each
 * landscape rectangle so a portrait save cannot overwrite the landscape. */
bool layout_orientation_project(cJSON *root, bool portrait);
bool layout_orientation_canonicalize(cJSON *root);
bool layout_orientation_rect_valid(const cJSON *widget, const cJSON *rect, bool portrait);
widget_size_limits_t layout_orientation_widget_limits(const char *type, const char *style, bool portrait);
