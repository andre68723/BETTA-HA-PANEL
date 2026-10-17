/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/widgets/weather_condition_icon.h"
#include <string.h>
#include <stdbool.h>

static bool has_token(const char *key, const char *token)
{
    return strstr(key, token) != NULL;
}

uint32_t weather_icon_codepoint_for_key(const char *key)
{
    /* MDI codepoints (matching your ESPHome glyph list). */
    const uint32_t CP_WEATHER_CLOUDY = 0xF0590U;
    const uint32_t CP_WEATHER_FOG = 0xF0591U;
    const uint32_t CP_WEATHER_HAIL = 0xF0592U;
    const uint32_t CP_WEATHER_LIGHTNING = 0xF0593U;
    const uint32_t CP_WEATHER_NIGHT = 0xF0594U;
    const uint32_t CP_WEATHER_PARTLY_CLOUDY = 0xF0595U;
    const uint32_t CP_WEATHER_POURING = 0xF0596U;
    const uint32_t CP_WEATHER_RAINY = 0xF0597U;
    const uint32_t CP_WEATHER_SNOWY = 0xF0598U;
    const uint32_t CP_WEATHER_SUNNY = 0xF0599U;
    const uint32_t CP_WEATHER_SUNSET = 0xF059AU;
    const uint32_t CP_WEATHER_WINDY = 0xF059DU;
    const uint32_t CP_WEATHER_WINDY_VARIANT = 0xF059EU;
    const uint32_t CP_WEATHER_LIGHTNING_RAINY = 0xF067EU;
    const uint32_t CP_WEATHER_SNOWY_RAINY = 0xF067FU;
    const uint32_t CP_WEATHER_HURRICANE = 0xF0898U;
    const uint32_t CP_WEATHER_NIGHT_PARTLY_CLOUDY = 0xF0F31U;
    const uint32_t CP_WEATHER_PARTLY_LIGHTNING = 0xF0F32U;
    const uint32_t CP_WEATHER_PARTLY_RAINY = 0xF0F33U;
    const uint32_t CP_WEATHER_PARTLY_SNOWY = 0xF0F34U;
    const uint32_t CP_WEATHER_PARTLY_SNOWY_RAINY = 0xF0F35U;
    const uint32_t CP_WEATHER_SNOWY_HEAVY = 0xF0F36U;
    const uint32_t CP_WEATHER_TORNADO = 0xF0F38U;

    if (key == NULL || key[0] == '\0') {
        return 0U;
    }

    if (strcmp(key, "clear-night") == 0) {
        return CP_WEATHER_NIGHT;
    }
    if (strcmp(key, "partlycloudy") == 0 || strcmp(key, "partly-cloudy") == 0) {
        return CP_WEATHER_PARTLY_CLOUDY;
    }

    if (has_token(key, "tornado")) {
        return CP_WEATHER_TORNADO;
    }
    if (has_token(key, "hurricane")) {
        return CP_WEATHER_HURRICANE;
    }
    if (has_token(key, "lightning") && has_token(key, "rain")) {
        return CP_WEATHER_LIGHTNING_RAINY;
    }
    if (has_token(key, "partly") && has_token(key, "lightning")) {
        return CP_WEATHER_PARTLY_LIGHTNING;
    }
    if (has_token(key, "lightning")) {
        return CP_WEATHER_LIGHTNING;
    }
    if (has_token(key, "hail")) {
        return CP_WEATHER_HAIL;
    }
    if (has_token(key, "fog") || has_token(key, "hazy") || has_token(key, "mist")) {
        return CP_WEATHER_FOG;
    }
    if (has_token(key, "partly") && has_token(key, "snow") && has_token(key, "rain")) {
        return CP_WEATHER_PARTLY_SNOWY_RAINY;
    }
    if (has_token(key, "partly") && has_token(key, "snow")) {
        return CP_WEATHER_PARTLY_SNOWY;
    }
    if (has_token(key, "snow") && has_token(key, "rain")) {
        return CP_WEATHER_SNOWY_RAINY;
    }
    if (has_token(key, "snow") && has_token(key, "heavy")) {
        return CP_WEATHER_SNOWY_HEAVY;
    }
    if (has_token(key, "snow")) {
        return CP_WEATHER_SNOWY;
    }
    if (has_token(key, "partly") && has_token(key, "rain")) {
        return CP_WEATHER_PARTLY_RAINY;
    }
    if (has_token(key, "pouring")) {
        return CP_WEATHER_POURING;
    }
    if (has_token(key, "rain")) {
        return CP_WEATHER_RAINY;
    }
    if (has_token(key, "night") && has_token(key, "partly")) {
        return CP_WEATHER_NIGHT_PARTLY_CLOUDY;
    }
    if (has_token(key, "night")) {
        return CP_WEATHER_NIGHT;
    }
    if (has_token(key, "sunset")) {
        return CP_WEATHER_SUNSET;
    }
    if (has_token(key, "sunny") || has_token(key, "clear")) {
        return CP_WEATHER_SUNNY;
    }
    if (has_token(key, "wind") && has_token(key, "variant")) {
        return CP_WEATHER_WINDY_VARIANT;
    }
    if (has_token(key, "wind")) {
        return CP_WEATHER_WINDY;
    }
    if (has_token(key, "partly")) {
        return CP_WEATHER_PARTLY_CLOUDY;
    }
    if (has_token(key, "cloud")) {
        return CP_WEATHER_CLOUDY;
    }
    return 0U;
}

