/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "drivers/touch_track.h"

#include <string.h>

static touch_track_stats_t s_stats;

const touch_track_stats_t *touch_track_stats(void)
{
    return &s_stats;
}

void touch_track_stats_clear(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
}

void touch_track_note_frame(unsigned flagged, unsigned invalid)
{
    s_stats.frames++;
    s_stats.flagged += flagged;
    s_stats.invalid += invalid;
}

void touch_track_note_ui(touch_ui_event_t event)
{
    switch (event) {
    case TOUCH_UI_PRESSED: s_stats.ui_pressed++; break;
    case TOUCH_UI_RELEASED: s_stats.ui_released++; break;
    case TOUCH_UI_CLICKED: s_stats.ui_clicked++; break;
    case TOUCH_UI_SCROLLED: s_stats.ui_scrolled++; break;
    case TOUCH_UI_LONG: s_stats.ui_long++; break;
    case TOUCH_UI_LOST: s_stats.ui_lost++; break;
    }
}

void touch_track_note_read(int64_t now_us, bool i2c_error, bool empty)
{
    static int64_t last_us;
    if (last_us != 0 && now_us > last_us) {
        const int64_t gap = now_us - last_us;
        if (gap > TOUCH_TRACK_LATE_US) {
            s_stats.late++;
        }
        if ((uint64_t)(gap / 1000) > s_stats.max_gap_ms) {
            s_stats.max_gap_ms = (uint32_t)(gap / 1000);
        }
    }
    last_us = now_us;
    s_stats.reads++;
    if (i2c_error) {
        s_stats.i2c_errors++;
    } else if (empty) {
        s_stats.empty++;
    }
}

void touch_track_reset(touch_track_t *track)
{
    if (track != NULL) {
        if (track->pending && !track->tracking) {
            s_stats.lost++; /* the contact ended before anything was reported */
        }
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
            s_stats.held++;
            *out_x = t->x;
            *out_y = t->y;
            return TOUCH_TRACK_HELD;
        }
        if (t->pending && now_us - t->at_us <= TOUCH_TRACK_TIMEOUT_US) {
            return TOUCH_TRACK_NONE; /* a weak frame while a contact waits: keep the candidate, skip this frame */
        }
        touch_track_reset(t);
        return TOUCH_TRACK_NONE;
    }

    if (!t->tracking) {
        if (!t->pending || now_us - t->at_us > TOUCH_TRACK_TIMEOUT_US) {
            if (t->pending) {
                s_stats.lost++; /* the candidate went stale without a confirmation */
            }
            s_stats.contacts++;
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
        s_stats.confirmed++;
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
        s_stats.held++;
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
