/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdbool.h>

#include "cJSON.h"
#include "lvgl.h"

#include "ui/ui_widget_factory.h"

/* Creates one room card from its definition (position and size set) inside `parent`; returns
 * false when it could not. The definition is scratch memory, valid only during the call. */
typedef bool (*ui_house_room_cb_t)(const ui_widget_def_t *def, lv_obj_t *parent, void *user);

/* Lays out a house_dashboard page: floor headings and a grid of room cards in `page_container`,
 * shrinking the cards to fit and scrolling when they cannot. */
void ui_house_page_build(const cJSON *page, lv_obj_t *page_container, ui_house_room_cb_t create_room, void *user);
