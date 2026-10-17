/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t display_init(void);
bool display_is_ready(void);
bool display_lock(uint32_t timeout_ms);
void display_unlock(void);
esp_err_t display_set_brightness_percent(int percent);
void display_configure_brightness(int active_brightness_percent, int dim_brightness_percent, int dim_timeout_seconds);
void display_note_activity(void);

/* Ambient light control (paneljc and Tab5 with cameras): upper limit for the backlight in
 * percent, 100 = no limit. display_is_dimmed() is true while the display is dimmed
 * or in night mode, i.e. waiting for someone to wake it. */
void display_set_ambient_limit_percent(int percent);
/* Backlight now (0..100, -1 before the display is up) and the room light factor (100 = none). */
int display_get_brightness_percent(void);
int display_get_ambient_limit_percent(void);
bool display_is_dimmed(void);
