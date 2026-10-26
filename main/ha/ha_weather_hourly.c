/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#include "ha/ha_weather_hourly.h"
#include "util/iso_time.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

bool ha_weather_hourly_parse(const cJSON *result, const char *entity, int64_t now, ha_weather_hourly_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    if (entity == NULL || now < 1700000000) return false;
    const cJSON *response = cJSON_GetObjectItemCaseSensitive(result, "response");
    const cJSON *weather = cJSON_GetObjectItemCaseSensitive(response, entity);
    const cJSON *forecast = cJSON_GetObjectItemCaseSensitive(weather, "forecast");
    if (!cJSON_IsArray(forecast)) return false;
    const cJSON *item;
    unsigned scanned = 0;
    cJSON_ArrayForEach(item, forecast) {
        if (++scanned > 512 || out->count == HA_WEATHER_HOURS_MAX) break;
        const cJSON *date = cJSON_GetObjectItemCaseSensitive(item, "datetime");
        const cJSON *temp = cJSON_GetObjectItemCaseSensitive(item, "temperature");
        const cJSON *condition = cJSON_GetObjectItemCaseSensitive(item, "condition");
        int64_t at;
        if (!cJSON_IsString(date) || !iso_time_parse(date->valuestring, &at) || at < now ||
            !cJSON_IsNumber(temp) || !isfinite(temp->valuedouble) || fabs(temp->valuedouble) > 10000 ||
            (out->count && at <= out->hour[out->count - 1].at)) continue;
        ha_weather_hour_t *hour = &out->hour[out->count++];
        hour->at = at;
        hour->temperature = (float)temp->valuedouble;
        if (cJSON_IsString(condition)) snprintf(hour->condition, sizeof(hour->condition), "%s", condition->valuestring);
    }
    return out->count > 0;
}
