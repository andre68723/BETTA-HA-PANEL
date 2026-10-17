/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Original stylised front view of an open-frame Bambu printer (frame, X gantry, print head, bed, spool ring) built
 * from about a dozen non-interactive LVGL shapes in normalised coordinates with one uniform scale. Theme colours
 * only; the nozzle and bed take a warm accent while they are hot. Not a product photograph and no copied artwork.
 */
#pragma once

#include <stdbool.h>

#include "lvgl.h"

typedef struct {
    lv_coord_t nozzle_x; /* where the nozzle badge attaches, in the illustration's own coordinates */
    lv_coord_t nozzle_y;
    lv_coord_t bed_x;    /* centre of the base under the bed: the bed badge sits on it, the bed stays visible */
    lv_coord_t bed_y;
    lv_coord_t art_x;    /* the scaled drawing box inside the illustration */
    lv_coord_t art_y;
    lv_coord_t art_w;
    lv_coord_t art_h;
} bambu_illustration_anchors_t;

typedef struct bambu_illustration bambu_illustration_t;

/* Creates the container (no click, no scroll) under `parent`; it frees itself with the container. */
bambu_illustration_t *bambu_illustration_create(lv_obj_t *parent);
lv_obj_t *bambu_illustration_obj(const bambu_illustration_t *illustration);
/* Positions every shape for a w x h box (aspect ratio kept, centred) and reports the badge anchors. */
void bambu_illustration_layout(bambu_illustration_t *illustration, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                               lv_coord_t h, bambu_illustration_anchors_t *anchors);
/* Heating accents; `active` false dims the whole drawing (offline, no data). */
void bambu_illustration_set_heat(bambu_illustration_t *illustration, bool nozzle_hot, bool bed_hot, bool active);
/* Re-applies theme colours after a style change. */
void bambu_illustration_restyle(bambu_illustration_t *illustration);
