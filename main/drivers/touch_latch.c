/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "drivers/touch_latch.h"

#include <string.h>

void touch_latch_reset(touch_latch_t *latch)
{
    if (latch != NULL) {
        memset(latch, 0, sizeof(*latch));
    }
}

static touch_latch_tap_t *newest(touch_latch_t *latch)
{
    return &latch->tap[(latch->head + latch->count - 1) % TOUCH_LATCH_CAPACITY];
}

static void pop(touch_latch_t *latch)
{
    latch->head = (uint8_t)((latch->head + 1) % TOUCH_LATCH_CAPACITY);
    latch->count--;
    latch->active = false;
}

void touch_latch_scan(touch_latch_t *latch, int64_t now_us, bool down, uint16_t x, uint16_t y)
{
    if (latch == NULL) {
        return;
    }
    if (down && !latch->was_down) {
        latch->tracking = latch->count < TOUCH_LATCH_CAPACITY; /* a full queue drops the new tap */
        if (latch->tracking) {
            latch->count++;
            *newest(latch) = (touch_latch_tap_t){.x = x, .y = y, .up = false, .at_us = now_us};
        }
    } else if (down && latch->tracking) {
        touch_latch_tap_t *tap = newest(latch);
        tap->x = x; /* the finger moves */
        tap->y = y;
    } else if (!down && latch->was_down && latch->tracking) {
        newest(latch)->up = true;
    }
    latch->was_down = down;
}

bool touch_latch_read(touch_latch_t *latch, int64_t now_us, uint16_t *x, uint16_t *y)
{
    if (latch == NULL || x == NULL || y == NULL) {
        return false;
    }
    while (!latch->active && latch->count > 0 && latch->tap[latch->head].up &&
           now_us - latch->tap[latch->head].at_us > TOUCH_LATCH_MAX_AGE_US) {
        pop(latch); /* finished and too old to act on */
    }
    if (latch->count == 0) {
        return false;
    }
    touch_latch_tap_t *head = &latch->tap[latch->head];
    if (!latch->active) {
        latch->active = true;
        *x = head->x;
        *y = head->y;
        return true;
    }
    if (!head->up) {
        *x = head->x;
        *y = head->y;
        return true;
    }
    pop(latch); /* the finger lifted: this read is the release, the next read starts the next tap */
    return false;
}
