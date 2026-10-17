/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Colored weather icons (Meteocons by Bas Milius, MIT, see LICENSE-meteocons.txt)
 * embedded as small PNGs and drawn through LVGL's PNG decoder. Two sizes: a
 * small one for forecast columns and a large one for the current condition.
 */
#pragma once

#include <stdbool.h>

#include "lvgl.h"

#ifndef APP_HAVE_WEATHER_PNG_ICONS
#define APP_HAVE_WEATHER_PNG_ICONS 0
#endif

#define WEATHER_ICON_SMALL_PX 64
#define WEATHER_ICON_LARGE_PX 112

typedef enum {
    WEATHER_ICON_NONE = -1,
    WEATHER_ICON_CLEAR_DAY,
    WEATHER_ICON_CLEAR_NIGHT,
    WEATHER_ICON_PARTLY_CLOUDY_DAY,
    WEATHER_ICON_PARTLY_CLOUDY_NIGHT,
    WEATHER_ICON_OVERCAST,
    WEATHER_ICON_FOG,
    WEATHER_ICON_HAIL,
    WEATHER_ICON_THUNDERSTORMS,
    WEATHER_ICON_THUNDERSTORMS_RAIN,
    WEATHER_ICON_RAIN,
    WEATHER_ICON_EXTREME_RAIN,
    WEATHER_ICON_SNOW,
    WEATHER_ICON_SLEET,
    WEATHER_ICON_WIND,
    WEATHER_ICON_EXTREME,
    WEATHER_ICON_COUNT
} weather_icon_id_t;

#if APP_HAVE_WEATHER_PNG_ICONS
/* Icon for a normalized Home Assistant condition key ("partlycloudy",
 * "clear-night", ...); `night` selects the night variant where one exists.
 * Returns WEATHER_ICON_NONE for an unknown condition. */
weather_icon_id_t weather_icon_from_condition(const char *normalized_key, bool night);

/* Image descriptor for an icon, or NULL. The data lives in flash. */
const lv_image_dsc_t *weather_icon_image(weather_icon_id_t id, bool large);
#else
static inline weather_icon_id_t weather_icon_from_condition(const char *normalized_key, bool night)
{
    (void)normalized_key;
    (void)night;
    return WEATHER_ICON_NONE;
}

static inline const lv_image_dsc_t *weather_icon_image(weather_icon_id_t id, bool large)
{
    (void)id;
    (void)large;
    return NULL;
}
#endif
