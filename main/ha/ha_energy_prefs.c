/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_energy_prefs.h"

#include <string.h>

static const char *json_string(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_IsObject(obj) ? cJSON_GetObjectItemCaseSensitive(obj, key) : NULL;
    return (cJSON_IsString(item) && item->valuestring != NULL) ? item->valuestring : "";
}

static void add_ref(ha_energy_stat_ref_t *refs, size_t max_refs, size_t *count, ha_energy_stat_kind_t kind,
                    const char *stat_id, const char *unit)
{
    if (stat_id == NULL || stat_id[0] == '\0' || *count >= max_refs) {
        return;
    }
    for (size_t i = 0; i < *count; i++) {
        if (refs[i].kind == kind && strncmp(refs[i].id, stat_id, sizeof(refs[i].id)) == 0) {
            return;
        }
    }
    ha_energy_stat_ref_t *ref = &refs[(*count)++];
    memset(ref, 0, sizeof(*ref));
    ref->kind = kind;
    strncpy(ref->id, stat_id, sizeof(ref->id) - 1);
    if (unit != NULL) {
        strncpy(ref->unit, unit, sizeof(ref->unit) - 1);
    }
}

size_t ha_energy_prefs_parse(const cJSON *result, ha_energy_stat_ref_t *refs, size_t max_refs)
{
    if (refs == NULL || max_refs == 0) {
        return 0;
    }
    memset(refs, 0, max_refs * sizeof(*refs));
    const cJSON *sources = cJSON_IsObject(result) ? cJSON_GetObjectItemCaseSensitive(result, "energy_sources") : NULL;
    if (!cJSON_IsArray(sources)) {
        return 0;
    }

    size_t count = 0;
    const cJSON *source = NULL;
    cJSON_ArrayForEach(source, sources)
    {
        const char *type = json_string(source, "type");
        if (strcmp(type, "grid") == 0) {
            /* Current format: one flat entry per grid source, several entries allowed (tariffs). */
            add_ref(refs, max_refs, &count, HA_ENERGY_STAT_GRID_FROM, json_string(source, "stat_energy_from"), NULL);
            add_ref(refs, max_refs, &count, HA_ENERGY_STAT_GRID_TO, json_string(source, "stat_energy_to"), NULL);
            /* Pre-2022.4 layout, migrated by Home Assistant on load; kept for a stale or imported config. */
            const cJSON *flow_from = cJSON_GetObjectItemCaseSensitive(source, "flow_from");
            const cJSON *entry = NULL;
            if (cJSON_IsArray(flow_from)) {
                cJSON_ArrayForEach(entry, flow_from)
                {
                    add_ref(refs, max_refs, &count, HA_ENERGY_STAT_GRID_FROM, json_string(entry, "stat_energy_from"),
                            NULL);
                }
            }
            const cJSON *flow_to = cJSON_GetObjectItemCaseSensitive(source, "flow_to");
            if (cJSON_IsArray(flow_to)) {
                cJSON_ArrayForEach(entry, flow_to)
                {
                    add_ref(refs, max_refs, &count, HA_ENERGY_STAT_GRID_TO, json_string(entry, "stat_energy_to"), NULL);
                }
            }
        } else if (strcmp(type, "solar") == 0) {
            add_ref(refs, max_refs, &count, HA_ENERGY_STAT_SOLAR, json_string(source, "stat_energy_from"), NULL);
        } else if (strcmp(type, "battery") == 0) {
            add_ref(refs, max_refs, &count, HA_ENERGY_STAT_BATTERY_FROM, json_string(source, "stat_energy_from"), NULL);
            add_ref(refs, max_refs, &count, HA_ENERGY_STAT_BATTERY_TO, json_string(source, "stat_energy_to"), NULL);
        } else if (strcmp(type, "gas") == 0) {
            add_ref(refs, max_refs, &count, HA_ENERGY_STAT_GAS, json_string(source, "stat_energy_from"),
                    json_string(source, "unit_of_measurement"));
        } else if (strcmp(type, "water") == 0) {
            add_ref(refs, max_refs, &count, HA_ENERGY_STAT_WATER, json_string(source, "stat_energy_from"),
                    json_string(source, "unit_of_measurement"));
        }
    }
    return count;
}
