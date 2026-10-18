/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Hands every tap that was scanned between two LVGL reads over to LVGL, one at a time. A task scans the touch controller
 * every few milliseconds, but LVGL reads the pointer only when its own timer runs, which can be tens of milliseconds
 * apart and much more while a page is being built. A quick tap that starts and ends between two reads would never be
 * seen, and several quick taps would melt into one. The queue keeps each contact: LVGL gets press, release, press,
 * release in order. A finished tap that LVGL still has not read after TOUCH_LATCH_MAX_AGE_US is dropped (it would act
 * on a screen the user no longer sees). Pure state, no locking: the caller serialises scan and read. Host-tested.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TOUCH_LATCH_CAPACITY 6
#define TOUCH_LATCH_MAX_AGE_US 600000

typedef struct {
    uint16_t x;
    uint16_t y;
    bool up;           /* the finger has lifted */
    int64_t at_us;     /* when the contact started */
} touch_latch_tap_t;

typedef struct {
    touch_latch_tap_t tap[TOUCH_LATCH_CAPACITY];
    uint8_t head;
    uint8_t count;
    bool active;       /* the head tap has been reported pressed to LVGL */
    bool tracking;     /* the newest scan belongs to the newest queued tap */
    bool was_down;     /* the previous scan saw a finger */
} touch_latch_t;

void touch_latch_reset(touch_latch_t *latch);
/* One scan result at `now_us`: the finger is down at (x, y), or `down` is false. */
void touch_latch_scan(touch_latch_t *latch, int64_t now_us, bool down, uint16_t x, uint16_t y);
/* One LVGL read: true = report a pressed finger at (x, y), false = report released (or nothing to report). */
bool touch_latch_read(touch_latch_t *latch, int64_t now_us, uint16_t *x, uint16_t *y);
