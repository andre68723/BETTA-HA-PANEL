/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * A numeric code pad popup (the alarm tile's PIN): a title, a close button, the entered digits as
 * dots, a 3x4 keypad with backspace and OK on lv_layer_top(). The code lives only in the popup's
 * memory and is wiped when it closes; nothing is stored or logged. Tapping OK reports the code and
 * closes the popup; tapping outside or the close button just closes it.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "lvgl.h"

#define UI_PIN_PAD_MAX_LEN 12

typedef struct {
    const char *title;
    /* OK was tapped with at least one digit entered; the popup closes itself afterwards. */
    void (*submit)(void *user, const char *code);
    /* The popup is gone (submitted, dismissed or deleted with the screen): drop the handle. */
    void (*closed)(void *user);
    void *user;
    /* Larger keys and spacing, as far as the screen's height allows. */
    bool roomy;
} ui_pin_pad_cfg_t;

/* Opens the popup; NULL when memory is short. */
lv_obj_t *ui_pin_pad_open(const ui_pin_pad_cfg_t *cfg);
/* Closes a popup returned by ui_pin_pad_open() (its `closed` callback runs). */
void ui_pin_pad_close(lv_obj_t *pad);
