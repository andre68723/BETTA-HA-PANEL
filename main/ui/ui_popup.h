/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include "lvgl.h"

/* Visual size of a popup's close button and the invisible margin that widens its touch target. */
#define UI_POPUP_CLOSE_SIZE 52
#define UI_POPUP_CLOSE_EXTRA_TOUCH 10

/* The round close button in a popup's top-right corner, the same in every popup: an MDI close
 * symbol on a style-aware button of colour bg, with a touch area of UI_POPUP_CLOSE_SIZE plus
 * UI_POPUP_CLOSE_EXTRA_TOUCH on each side. The caller aligns it and passes its click handler. */
lv_obj_t *ui_popup_close_button(lv_obj_t *parent, lv_color_t bg, lv_event_cb_t cb, void *user_data);

/* The frame every popup card shares: 2 px in a clearly lighter colour than the theme's 1 px content border, so the card
 * stands out from a dark page behind the dimmed overlay. Call it after the card's other border settings. */
void ui_popup_style_frame(lv_obj_t *card);
