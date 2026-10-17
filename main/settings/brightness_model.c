/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "settings/brightness_model.h"

#include <stddef.h>

int brightness_clamp_level(int percent)
{
    if (percent < BRIGHTNESS_MIN_PERCENT) {
        return BRIGHTNESS_MIN_PERCENT;
    }
    return percent > BRIGHTNESS_MAX_PERCENT ? BRIGHTNESS_MAX_PERCENT : percent;
}

int brightness_clamp_night(int percent)
{
    if (percent < 0) {
        return 0;
    }
    return percent > BRIGHTNESS_MAX_PERCENT ? BRIGHTNESS_MAX_PERCENT : percent;
}

void brightness_levels_normalize(brightness_levels_t *levels)
{
    if (levels == NULL) {
        return;
    }
    levels->active = brightness_clamp_level(levels->active);
    levels->dimmed = brightness_clamp_level(levels->dimmed);
    levels->night = brightness_clamp_night(levels->night);
    if (levels->dimmed > levels->active) {
        levels->dimmed = levels->active;
    }
    if (levels->night > levels->dimmed) {
        levels->night = levels->dimmed;
    }
}

int brightness_scale_for_room_light(int level_percent, int room_light_percent)
{
    if (room_light_percent > BRIGHTNESS_MAX_PERCENT) {
        room_light_percent = BRIGHTNESS_MAX_PERCENT;
    }
    if (room_light_percent < 0) {
        room_light_percent = 0;
    }
    const int floor_percent = level_percent < BRIGHTNESS_ROOM_FLOOR_PERCENT ? level_percent : BRIGHTNESS_ROOM_FLOOR_PERCENT;
    const int scaled = (level_percent * room_light_percent + 50) / 100;
    return scaled < floor_percent ? floor_percent : scaled;
}

int brightness_resolve(const brightness_levels_t *levels, brightness_mode_t mode, int room_light_percent)
{
    if (levels == NULL) {
        return BRIGHTNESS_MAX_PERCENT;
    }
    switch (mode) {
    case BRIGHTNESS_MODE_NIGHT:
        return brightness_clamp_night(levels->night);
    case BRIGHTNESS_MODE_DIMMED:
        return brightness_scale_for_room_light(brightness_clamp_level(levels->dimmed), room_light_percent);
    case BRIGHTNESS_MODE_ACTIVE:
    default:
        return brightness_scale_for_room_light(brightness_clamp_level(levels->active), room_light_percent);
    }
}
