/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"

#define HA_WEATHER_HOURS_MAX 24

typedef struct {
    int64_t at;
    float temperature;
    char condition[32];
} ha_weather_hour_t;

typedef struct {
    unsigned count;
    ha_weather_hour_t hour[HA_WEATHER_HOURS_MAX];
} ha_weather_hourly_t;

/* A weather.get_forecasts(type=hourly) result for the exact entity. Keeps the next 24 finite,
 * ordered samples; past, malformed and duplicate timestamps are skipped. No daily fallback. */
bool ha_weather_hourly_parse(const cJSON *result, const char *entity, int64_t now, ha_weather_hourly_t *out);
