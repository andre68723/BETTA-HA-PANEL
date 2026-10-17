/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "drivers/touch_track.h"

#include <string.h>

void touch_track_reset(touch_track_t *track)
{
    if (track != NULL) {
        memset(track, 0, sizeof(*track));
    }
}

bool touch_track_raw_valid(uint16_t raw_x, uint16_t raw_y, uint16_t raw_max_x, uint16_t raw_max_y)
{
    return raw_x <= raw_max_x && raw_y <= raw_max_y && !(raw_x == 0 && raw_y == 0);
}

static int nearest(const uint16_t *x, const uint16_t *y, int n, uint16_t px, uint16_t py, int64_t *d2_out)
{
    int best = 0;
    int64_t best_d2 = INT64_MAX;
    for (int i = 0; i < n; i++) {
        const int64_t dx = (int64_t)x[i] - px;
        const int64_t dy = (int64_t)y[i] - py;
        const int64_t d2 = dx * dx + dy * dy;
        if (d2 < best_d2) {
            best_d2 = d2;
            best = i;
        }
    }
    *d2_out = best_d2;
    return best;
}

touch_track_result_t touch_track_update(touch_track_t *t, const uint16_t *x, const uint16_t *y, int n, int64_t now_us,
                                        uint16_t *out_x, uint16_t *out_y)
{
    if (t == NULL || out_x == NULL || out_y == NULL || (n > 0 && (x == NULL || y == NULL))) {
        return TOUCH_TRACK_NONE;
    }

    if (t->tracking && now_us - t->at_us > TOUCH_TRACK_TIMEOUT_US) {
        touch_track_reset(t); /* the reports stopped for too long: the next one is a new contact */
    }

    if (n <= 0) {
        if (t->tracking && t->held < TOUCH_TRACK_MAX_HELD) {
            t->held++;
            *out_x = t->x;
            *out_y = t->y;
            return TOUCH_TRACK_HELD;
        }
        touch_track_reset(t);
        return TOUCH_TRACK_NONE;
    }

    if (!t->tracking) {
        if (!t->pending || now_us - t->at_us > TOUCH_TRACK_TIMEOUT_US) {
            t->pending = true;
            t->pending_frames = 1;
            t->x = x[0];
            t->y = y[0];
            t->at_us = now_us;
            return TOUCH_TRACK_NONE;
        }
        int64_t d2;
        const int i = nearest(x, y, n, t->x, t->y, &d2);
        t->pending_frames++;
        const bool confirmed = d2 <= (int64_t)TOUCH_TRACK_SLACK_PX * TOUCH_TRACK_SLACK_PX;
        if (!confirmed && t->pending_frames < TOUCH_TRACK_START_FRAMES) {
            t->x = x[0]; /* the earlier point was probably the stray one: wait for this one to repeat */
            t->y = y[0];
            t->at_us = now_us;
            return TOUCH_TRACK_NONE;
        }
        t->tracking = true;
        t->pending = false;
        t->held = 0;
        t->x = x[i];
        t->y = y[i];
        t->at_us = now_us;
        *out_x = t->x;
        *out_y = t->y;
        return TOUCH_TRACK_REPORT;
    }

    int64_t d2;
    const int i = nearest(x, y, n, t->x, t->y, &d2);
    const int64_t dt_ms = (now_us - t->at_us) / 1000;
    const int64_t limit = TOUCH_TRACK_SLACK_PX + (int64_t)TOUCH_TRACK_MAX_SPEED_PX_PER_MS * dt_ms;
    if (d2 > limit * limit && t->held < TOUCH_TRACK_MAX_HELD) {
        t->held++;
        *out_x = t->x;
        *out_y = t->y;
        return TOUCH_TRACK_HELD; /* t->at_us stays: the allowed distance keeps growing with the time since the last report */
    }
    t->held = 0;
    t->x = x[i];
    t->y = y[i];
    t->at_us = now_us;
    *out_x = t->x;
    *out_y = t->y;
    return TOUCH_TRACK_REPORT;
}
