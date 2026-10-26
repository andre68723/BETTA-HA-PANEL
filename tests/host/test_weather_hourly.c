/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#include "ha/ha_weather_hourly.h"
#include "util/iso_time.h"

int main(void)
{
    int64_t now;
    assert(iso_time_parse("2026-10-15T10:00:00Z", &now));
    ha_weather_hourly_t hours;
    cJSON *root = cJSON_Parse("{\"response\":{\"weather.test\":{\"forecast\":["
        "{\"datetime\":\"2026-10-15T09:00:00Z\",\"temperature\":9},"
        "{\"datetime\":\"2026-10-15T12:00:00+02:00\",\"temperature\":11,\"condition\":\"cloudy\"},"
        "{\"datetime\":\"2026-10-15T10:00:00Z\",\"temperature\":99},"
        "{\"datetime\":\"bad\",\"temperature\":12},"
        "{\"datetime\":\"2026-10-15T11:00:00Z\",\"temperature\":\"12\"},"
        "{\"datetime\":\"2026-10-15T11:00:00Z\",\"temperature\":12},"
        "{\"datetime\":\"2026-10-15T10:30:00Z\",\"temperature\":10000}"
        "]}}}");
    assert(ha_weather_hourly_parse(root, "weather.test", now, &hours));
    assert(hours.count == 2 && hours.hour[0].temperature == 11 && hours.hour[1].temperature == 12);
    assert(hours.hour[0].at == now && hours.hour[1].at == now + 3600);
    assert(!ha_weather_hourly_parse(root, "weather.other", now, &hours) && hours.count == 0);
    assert(!ha_weather_hourly_parse(root, "weather.test", 0, &hours));
    cJSON_Delete(root);
    root = cJSON_CreateObject();
    cJSON *response = cJSON_AddObjectToObject(root, "response");
    cJSON *entity = cJSON_AddObjectToObject(response, "weather.test");
    cJSON *forecast = cJSON_AddArrayToObject(entity, "forecast");
    for (unsigned i = 0; i < 48; i++) {
        cJSON *item = cJSON_CreateObject();
        time_t at = (time_t)(now + i * 3600);
        struct tm utc;
        char date[32];
        gmtime_r(&at, &utc); strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%SZ", &utc);
        cJSON_AddStringToObject(item, "datetime", date);
        cJSON_AddNumberToObject(item, "temperature", i == 0 ? NAN : (double)i);
        cJSON_AddItemToArray(forecast, item);
    }
    assert(ha_weather_hourly_parse(root, "weather.test", now, &hours) && hours.count == HA_WEATHER_HOURS_MAX);
    assert(hours.hour[0].temperature == 1 && hours.hour[23].temperature == 24);
    cJSON_Delete(root);
    root = cJSON_Parse("{\"response\":{\"weather.test\":{\"forecast\":["
        "{\"datetime\":\"2026-10-25T02:00:00+02:00\",\"temperature\":1},"
        "{\"datetime\":\"2026-10-25T02:00:00+01:00\",\"temperature\":2}"
        "]}}}");
    assert(ha_weather_hourly_parse(root, "weather.test", now, &hours));
    assert(hours.count == 2 && hours.hour[1].at - hours.hour[0].at == 3600);
    cJSON_Delete(root);
    puts("hourly weather: exact entity, future data, malformed/duplicate/order/NaN, cap and DST passed");
}
