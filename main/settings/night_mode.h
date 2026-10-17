/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Night mode: between a start and an end time of day the backlight drops to
 * the night brightness (0 = off). A touch wakes the display for the normal
 * dim timeout, then it returns to the night level. Windows may cross midnight (22:00 - 06:00).
 */
#pragma once

#include <stdbool.h>

/* Start/end are minutes since midnight. Safe to call again after a change. */
void night_mode_configure(bool enabled, int start_minutes, int end_minutes, int brightness_percent);

/* Backlight level (0..100, 0 = off) used while night mode is active. */
int night_mode_brightness_percent(void);

/* True while the display should be off (schedule enabled, inside the window,
 * and the clock is set). Used by the display drivers' dim timers. */
bool night_mode_active(void);
