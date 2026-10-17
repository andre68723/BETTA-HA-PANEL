/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#pragma once
#include "lvgl.h"

/* LVGL task/display lock only. Feeds stop on hide, overlays, sleep and deletion.
 * Owns its image buffers; network workers never retain an LVGL pointer. */
lv_obj_t *ui_camera_feed_create(lv_obj_t *parent, const char *entity_id, int width, int height);
lv_obj_t *ui_camera_view_open(const char *entity_id, const char *title,
                             void (*closed)(void *), void *user);
