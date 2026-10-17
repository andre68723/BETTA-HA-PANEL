/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * The brightness rules as pure functions (no hardware, no FreeRTOS), so the display drivers, the
 * settings and the host tests (tests/host) share one implementation.
 *
 * One scale, three levels: night <= dimmed <= active, each 1..100 % (night also 0 % = display off).
 * Room light adaption (camera) scales active and dimmed with a factor of 10..100 %, never below
 * BRIGHTNESS_ROOM_FLOOR_PERCENT (or the level itself when that is lower); night is not scaled.
 */
#pragma once

#include <stdbool.h>

#define BRIGHTNESS_MIN_PERCENT 1
#define BRIGHTNESS_MAX_PERCENT 100
#define BRIGHTNESS_ROOM_FLOOR_PERCENT 5

typedef struct {
    int active;
    int dimmed;
    int night;
} brightness_levels_t;

typedef enum {
    BRIGHTNESS_MODE_ACTIVE,
    BRIGHTNESS_MODE_DIMMED,
    BRIGHTNESS_MODE_NIGHT,
} brightness_mode_t;

/* 1..100 for active and dimmed. */
int brightness_clamp_level(int percent);
/* 0..100 for night (0 = off). */
int brightness_clamp_night(int percent);
/* Clamp all three and enforce night <= dimmed <= active (the lower levels follow). */
void brightness_levels_normalize(brightness_levels_t *levels);
/* A level scaled with the room light factor (10..100, 100 = no adaption). */
int brightness_scale_for_room_light(int level_percent, int room_light_percent);
/* The backlight for a mode: night as configured, active / dimmed scaled with the room light. */
int brightness_resolve(const brightness_levels_t *levels, brightness_mode_t mode, int room_light_percent);
