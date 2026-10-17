/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Built-in camera used only as an ambient light and presence sensor
 * (OV02C10 on JC8012P4A1, SC2356 on Tab5). It never streams continuously and never produces
 * images for the user: every few seconds it runs a short burst of frames,
 * reduces the last one to a 16x16 luma grid plus exposure and gain, and stops
 * again.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool valid;
    float luma;        /* mean luma of the last frame, 0..255 */
    float exposure;    /* sensor exposure register value */
    float gain;        /* sensor gain, 1.0 = base gain */
    float light_index; /* log10 of relative scene brightness (bigger = brighter) */
    int motion_pct;    /* share of grid cells that changed since the previous sample */
    int motion_level;  /* mean absolute luma change, 0..255 */
} ambient_camera_reading_t;

/* Turn the two features on or off (both default to off; sensitivity 1..10, night_wake lets movement wake the display during night mode). Call before start. */
void ambient_camera_configure(bool auto_brightness, bool presence_wake, int sensitivity, bool night_wake);

/* Start the sampling task (no-op on variants without the camera). */
void ambient_camera_start(void);

/* Latest sample; false while none is available. */
bool ambient_camera_get(ambient_camera_reading_t *out);

/* Built for a variant with the camera at all (false: every call below is a no-op / returns 0). */
bool ambient_camera_available(void);

/* Current presence sensitivity threshold (share of grid cells that counts as movement, see
 * ambient_camera_reading_t.motion_pct): compare the two to tell whether presence would be
 * detected right now. */
int ambient_camera_get_presence_threshold_pct(void);

/* Forces presence sampling for `duration_ms` even while the display stays active (normally
 * sampling only runs once the display is already dimmed, to wake it -- Settings > Display keeps
 * it active while testing the sensitivity, so without this there would be nothing to show). */
void ambient_camera_start_test(uint32_t duration_ms);

/* True with the remaining seconds while a test started by ambient_camera_start_test is running. */
bool ambient_camera_test_active(int *out_remaining_s);
