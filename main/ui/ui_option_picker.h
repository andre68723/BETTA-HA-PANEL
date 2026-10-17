/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * A popup list to pick one option (a select entity's options, the vacuum's cleaning modes): a
 * title, a close button and one touch-sized row per option, the current one highlighted, in a
 * scrollable card on lv_layer_top(). Tapping an option reports its index and closes the popup;
 * tapping outside or the close button just closes it. The caller supplies the row texts and
 * keeps the returned object only until its `closed` callback runs.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "lvgl.h"

#define UI_OPTION_PICKER_NONE ((size_t)-1)

typedef struct {
    const char *title;
    size_t count;
    size_t current; /* highlighted row, or UI_OPTION_PICKER_NONE */
    /* Row text of option `index`. */
    void (*label)(void *user, size_t index, char *out, size_t out_len);
    /* The user tapped option `index`; the popup closes itself afterwards. */
    void (*chosen)(void *user, size_t index);
    /* The popup is gone (chosen, dismissed or deleted with the screen): drop the handle. */
    void (*closed)(void *user);
    void *user;
    /* A touch-friendlier popup (taller rows, wider card, more padding) for a short list of important actions. */
    bool roomy;
} ui_option_picker_cfg_t;

/* Opens the popup; NULL when `count` is 0 or memory is short. */
lv_obj_t *ui_option_picker_open(const ui_option_picker_cfg_t *cfg);
/* Closes a popup returned by ui_option_picker_open() (its `closed` callback runs). */
void ui_option_picker_close(lv_obj_t *picker);
