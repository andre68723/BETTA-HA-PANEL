/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Control row of the room card (layout option "room_controls"): one control per device type for all entities of that type on the
 * card -- lights and switches as a tap-to-toggle pill, covers as a status pill with an up and a down button. The row sits at the
 * bottom of the card, only when the card has the room for it; decisions are in util/room_extra.c (host tested).
 */
#pragma once

#include <stdbool.h>

#include "lvgl.h"

#include "app_config.h"

typedef struct room_controls room_controls_t;

/* Creates the controls under `card` for the lights, switches and covers among `ids`; NULL when there are none. */
room_controls_t *room_controls_create(lv_obj_t *card, const char ids[][APP_MAX_ENTITY_ID_LEN], int count);
void room_controls_destroy(room_controls_t *controls);
/* Re-reads the states of the member entities and updates texts, colors and enabled looks. */
void room_controls_refresh(room_controls_t *controls);
/* Places the row with its bottom edge at `bottom_y` and returns the height it takes; 0 when it does not fit in `max_h` or in
 * the card's width (it is then hidden). */
lv_coord_t room_controls_layout(room_controls_t *controls, lv_coord_t card_w, lv_coord_t pad, lv_coord_t bottom_y,
                                lv_coord_t max_h);
bool room_controls_has_lights(const room_controls_t *controls);
bool room_controls_has_covers(const room_controls_t *controls);
bool room_controls_shown(const room_controls_t *controls);
