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
 *  - No usable point (all out of range) holds for the same number of frames and then ends the contact. While a new
 *    contact still waits for its confirmation, a frame without a usable point only skips that frame: the candidate stays
 *    (a weak first signal must not restart the confirmation each time).
 *
 * Counters (touch_track_stats) tell how often that happens; they are read with GET /api/version ("touch").
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

/* Cumulative since boot, not cleared by touch_track_reset(). The driver adds frames, flagged and invalid. */
typedef struct {
    uint32_t frames;     /* controller reports with at least one point */
    uint32_t flagged;    /* points that carried the controller's weak-signal flag (0x4000 in the Y word) */
    uint32_t invalid;    /* points still out of range after the flag was removed */
    uint32_t contacts;   /* new contacts started (a first frame became a candidate) */
    uint32_t confirmed;  /* contacts that were reported to the UI */
    uint32_t lost;       /* contacts that ended before they were confirmed: a tap the user made and never saw */
    uint32_t held;       /* frames where a stray point was held back */
    uint32_t reads;      /* controller polls, with or without a finger */
    uint32_t empty;      /* polls that reported no finger */
    uint32_t i2c_errors; /* polls whose I2C read failed */
    uint32_t late;       /* polls more than TOUCH_TRACK_LATE_US after the previous one: the UI task was busy */
    uint32_t max_gap_ms; /* longest time between two polls */
    uint32_t ui_pressed; /* LVGL pointer events (what the UI saw of the contacts above) */
    uint32_t ui_released;
    uint32_t ui_clicked;
    uint32_t ui_scrolled; /* a press that turned into a scroll: the click is cancelled */
    uint32_t ui_long;
    uint32_t ui_lost;    /* the press moved away from its object (press lost) */
} touch_track_stats_t;
typedef enum { TOUCH_UI_PRESSED, TOUCH_UI_RELEASED, TOUCH_UI_CLICKED, TOUCH_UI_SCROLLED, TOUCH_UI_LONG, TOUCH_UI_LOST } touch_ui_event_t;
#define TOUCH_TRACK_LATE_US 50000

void touch_track_reset(touch_track_t *track);
const touch_track_stats_t *touch_track_stats(void);
void touch_track_stats_clear(void);
void touch_track_note_frame(unsigned flagged, unsigned invalid);
/* One poll of the controller: its time, whether the read failed and whether it reported no finger. */
void touch_track_note_ui(touch_ui_event_t event);
void touch_track_note_read(int64_t now_us, bool i2c_error, bool empty);
/* The weak-signal flag in the Y word of a point, and the coordinate without it. */
#define TOUCH_TRACK_Y_FLAG 0x4000
#define TOUCH_TRACK_Y_MASK 0x0FFF
/* A raw GSL3680 point inside the range the controller really uses; saturated values are a ghost marker. */
bool touch_track_raw_valid(uint16_t raw_x, uint16_t raw_y, uint16_t raw_max_x, uint16_t raw_max_y);
/* One frame: `n` usable points (may be 0). */
touch_track_result_t touch_track_update(touch_track_t *track, const uint16_t *x, const uint16_t *y, int n, int64_t now_us,
                                        uint16_t *out_x, uint16_t *out_y);
