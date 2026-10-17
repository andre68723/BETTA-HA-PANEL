/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdbool.h>

/* Called from any task by display_note_activity(). Returns true while the
 * saver owns the display, so the driver must wait for the UI to remove its
 * overlay before turning the backlight up. */
bool ui_display_protection_note_activity(void);
bool ui_display_protection_is_active(void);
/* Atomic override for callers outside the UI task; -1 means normal policy. */
int ui_display_brightness_test_percent(void);

/* These functions run with the display lock held. */
void ui_display_protection_init(void);
void ui_display_protection_reload(void);
void ui_display_protection_poll(void);
bool ui_display_protection_is_off(void);
bool ui_display_protection_is_pages(void);
/* Called with the display lock held. Start/update return false if unavailable/expired. */
bool ui_display_brightness_test_start(int percent, int kind);
bool ui_display_brightness_test_update(int percent);
void ui_display_brightness_test_stop(void);
int ui_display_brightness_test_remaining_seconds(void);
const char *ui_display_brightness_test_kind(void);
