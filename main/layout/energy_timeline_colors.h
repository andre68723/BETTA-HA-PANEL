/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "cJSON.h"

enum { ETL_GRID, ETL_SOLAR, ETL_EXPORT, ETL_BATTERY_OUT, ETL_BATTERY_IN, ETL_FORECAST,
       ETL_HOME_SOLAR_DEST, ETL_PALETTE_COUNT };
#define ETL_CUSTOM_COLOR_COUNT 6
#define ETL_COLOR_GRID 0x039BEF
#define ETL_COLOR_SOLAR 0xFF9800
#define ETL_COLOR_EXPORT 0xE45E65
#define ETL_COLOR_BATTERY_OUT 0x26A69A
#define ETL_COLOR_BATTERY_IN 0xE84393
#define ETL_COLOR_FORECAST 0xFFD54F
#define ETL_COLOR_HOME_SOLAR_DEST 0x8BC34A

typedef struct { uint32_t value[ETL_PALETTE_COUNT]; bool initialized; } etl_palette_t;
static const char *const ETL_COLOR_KEYS[ETL_CUSTOM_COLOR_COUNT] = {
    "grid", "solar", "export", "battery_out", "battery_in", "forecast"
};

static inline etl_palette_t etl_palette_defaults(void)
{
    return (etl_palette_t) { .value = { ETL_COLOR_GRID, ETL_COLOR_SOLAR, ETL_COLOR_EXPORT,
        ETL_COLOR_BATTERY_OUT, ETL_COLOR_BATTERY_IN, ETL_COLOR_FORECAST, ETL_COLOR_HOME_SOLAR_DEST },
        .initialized = true };
}

/* The model uses RGB24; display conversion remains the renderer's responsibility. */
static inline etl_palette_t etl_palette_from_json(const cJSON *widget)
{
    etl_palette_t palette = etl_palette_defaults();
    const cJSON *colors = cJSON_GetObjectItemCaseSensitive(widget, "energy_colors");
    for (int i = 0; i < ETL_CUSTOM_COLOR_COUNT; i++) {
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(colors, ETL_COLOR_KEYS[i]);
        if (!cJSON_IsString(item) || !item->valuestring) continue;
        const char *hex = item->valuestring;
        if (*hex == '#') hex++;
        else if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex += 2;
        if (strlen(hex) != 6) continue;
        bool valid = true;
        for (int j = 0; j < 6; j++) valid = valid && isxdigit((unsigned char)hex[j]);
        if (!valid) continue;
        palette.value[i] = (uint32_t)strtoul(hex, NULL, 16);
        if (i == ETL_SOLAR) palette.value[ETL_HOME_SOLAR_DEST] = palette.value[i];
    }
    return palette;
}
