/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Follows one finger through the point reports of a touch controller that sometimes sends stray points (the GSL3680 on
 * the paneljc: a ghost point in the first frame of a contact, reordered points of a second finger or a palm, one bad
 * frame at lift-off). Pure functions (no hardware), host-tested. Replaces what the GPL Silead algorithm did for the
 * earlier driver.
 *
 *  - A new contact is not reported at once: the first frame only becomes a candidate, and the contact starts when the
 *    next frame has a point near that candidate (at most TOUCH_TRACK_START_FRAMES frames, then whatever is there). A
 *    single ghost frame at touch-down therefore never reaches the UI.
 *  - While a contact is tracked the point nearest to the last one is followed. If even that one is further away than a
 *    finger can move since the last report (slack plus speed times elapsed time), the last position is held for up to
 *    TOUCH_TRACK_MAX_HELD frames, then the new position is accepted as real.
 *  - No usable point (all out of range) holds for the same number of frames and then ends the contact.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TOUCH_TRACK_SLACK_PX 80
#define TOUCH_TRACK_MAX_SPEED_PX_PER_MS 10
#define TOUCH_TRACK_TIMEOUT_US 150000
#define TOUCH_TRACK_MAX_HELD 2
#define TOUCH_TRACK_START_FRAMES 3

typedef struct {
    bool tracking;
    bool pending;          /* a first frame waits for its confirmation */
    uint8_t pending_frames;
    uint8_t held;
    uint16_t x;            /* last reported point, or the candidate while pending */
    uint16_t y;
    int64_t at_us;
} touch_track_t;

typedef enum {
    TOUCH_TRACK_NONE = 0,  /* nothing to report (contact not confirmed yet, or ended) */
    TOUCH_TRACK_REPORT,    /* out_x, out_y hold the point to report */
    TOUCH_TRACK_HELD,      /* out_x, out_y hold the previous point, kept because the new one is implausible */
} touch_track_result_t;

void touch_track_reset(touch_track_t *track);
/* A raw GSL3680 point inside the range the controller really uses; saturated values are a ghost marker. */
bool touch_track_raw_valid(uint16_t raw_x, uint16_t raw_y, uint16_t raw_max_x, uint16_t raw_max_y);
/* One frame: `n` usable points (may be 0). */
touch_track_result_t touch_track_update(touch_track_t *track, const uint16_t *x, const uint16_t *y, int n, int64_t now_us,
                                        uint16_t *out_x, uint16_t *out_y);
