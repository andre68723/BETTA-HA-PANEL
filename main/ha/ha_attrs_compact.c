/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * See ha_attrs_compact.h.
 */
#include "ha_attrs_compact.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

#include "util/iso_time.h"

#define HA_ATTRS_FORECAST_MAX 96

/* The audited field set: every generic (non-domain-specific) widget that reads attributes_json
 * directly was inventoried (binary_sensor, contact, camera, sensor, graph, gauge, room, car_widget,
 * the notification popup's camera picture, the energy page) and needs at most these four keys. Keep
 * this list in sync with that inventory rather than adding fields speculatively; a domain that turns
 * out to need more belongs in its own compact serializer in ha_client.c instead (see ha_attrs_compact.h).
 */
static const char *const HA_ATTRS_FALLBACK_KEYS[] = {
    "device_class",
    "unit_of_measurement",
    "state_class",
    "friendly_name",
    "entity_picture",
};

static bool ha_attrs_copy_key(cJSON *dst_obj, cJSON *src_obj, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(src_obj, key);
    if (item == NULL) {
        return false;
    }
    cJSON *dup = cJSON_Duplicate(item, true);
    if (dup == NULL) {
        return false;
    }
    cJSON_AddItemToObject(dst_obj, key, dup);
    return true;
}

/* A Solcast-style "detailedForecast" (objects with period_start and pv_estimate in kW) as three small fields the
 * energy timeline reads: "fc_t0" (UTC epoch of the first period), "fc_step" (seconds between periods) and "fc_kw"
 * (the estimates, two decimals). Only evenly spaced series of at most 96 periods are kept. */
static bool ha_attrs_copy_forecast_series(cJSON *dst_obj, cJSON *src_obj)
{
    cJSON *series = cJSON_GetObjectItemCaseSensitive(src_obj, "detailedForecast");
    const int count = cJSON_IsArray(series) ? cJSON_GetArraySize(series) : 0;
    if (count < 2 || count > HA_ATTRS_FORECAST_MAX) {
        return false;
    }
    int64_t t0 = 0, prev = 0, step = 0;
    cJSON *kw = cJSON_CreateArray();
    if (kw == NULL) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(series, i);
        cJSON *start = cJSON_GetObjectItemCaseSensitive(item, "period_start");
        cJSON *value = cJSON_GetObjectItemCaseSensitive(item, "pv_estimate");
        int64_t at;
        if (!cJSON_IsString(start) || !cJSON_IsNumber(value) || !isfinite(value->valuedouble) ||
            !iso_time_parse(start->valuestring, &at) || (i > 0 && (at <= prev || (i > 1 && at - prev != step)))) {
            cJSON_Delete(kw);
            return false;
        }
        if (i == 0) t0 = at;
        if (i == 1) step = at - prev;
        prev = at;
        cJSON_AddItemToArray(kw, cJSON_CreateNumber(floor(value->valuedouble * 100.0 + 0.5) / 100.0));
    }
    cJSON_AddNumberToObject(dst_obj, "fc_t0", (double)t0);
    cJSON_AddNumberToObject(dst_obj, "fc_step", (double)step);
    cJSON_AddItemToObject(dst_obj, "fc_kw", kw);
    return true;
}

bool ha_attrs_build_generic_overflow_fallback(cJSON *src_attrs, char *out_json, size_t out_json_size)
{
    if (out_json == NULL || out_json_size == 0) {
        return false;
    }
    snprintf(out_json, out_json_size, "{}"); /* the safe result if anything below bails out */
    if (!cJSON_IsObject(src_attrs)) {
        return false;
    }

    cJSON *compact = cJSON_CreateObject();
    if (compact == NULL) {
        return false;
    }

    bool any = false;
    for (size_t i = 0; i < sizeof(HA_ATTRS_FALLBACK_KEYS) / sizeof(HA_ATTRS_FALLBACK_KEYS[0]); i++) {
        any |= ha_attrs_copy_key(compact, src_attrs, HA_ATTRS_FALLBACK_KEYS[i]);
    }
    any |= ha_attrs_copy_forecast_series(compact, src_attrs);

    if (!any) {
        cJSON_Delete(compact);
        return false;
    }

    char *compact_json = cJSON_PrintUnformatted(compact);
    cJSON_Delete(compact);
    if (compact_json == NULL) {
        return false;
    }

    size_t len = strlen(compact_json);
    bool fits = len < out_json_size;
    if (fits) {
        memcpy(out_json, compact_json, len + 1U);
    } /* else out_json keeps the "{}" written above -- still valid JSON, unlike a raw truncated copy */
    cJSON_free(compact_json);
    return fits;
}
