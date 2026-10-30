/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "layout/room_air_slots.h"
#include <string.h>

const air_binding_t air_bindings[AIR_ROLE_COUNT] = {
    {"entity_id", "sensor", -1},
    {"temperature_entity_id", "sensor", -2},
    {"co2_entity_id", "sensor", -3},
    {"pm25_entity_id", "sensor", 0},
    {"voc_entity_id", "sensor", 1},
    {"outdoor_temperature_entity_id", "sensor|weather", 2},
    {"outdoor_humidity_entity_id", "sensor|weather", 3},
    {"window_entity_id", "binary_sensor", 4},
    {"native_rating_entity_id", "sensor", 5},
};

void air_slots_from_json(const cJSON *json, char *primary, char *secondary, char *tertiary,
                         char (*extra)[APP_MAX_ENTITY_ID_LEN])
{
    primary[0] = secondary[0] = tertiary[0] = 0;
    memset(extra, 0, 6 * APP_MAX_ENTITY_ID_LEN);
    for (int i = 0; i < AIR_ROLE_COUNT; ++i) {
        if ((i == AIR_OUT_TEMPERATURE || i == AIR_OUT_HUMIDITY) &&
            cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(json, "air_outdoor_enabled"))) continue;
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(json, air_bindings[i].key);
        if (!cJSON_IsString(v) || strlen(v->valuestring) >= APP_MAX_ENTITY_ID_LEN) continue;
        int slot = air_bindings[i].slot;
        char *dst = slot == -1 ? primary : slot == -2 ? secondary : slot == -3 ? tertiary : extra[slot];
        memcpy(dst, v->valuestring, strlen(v->valuestring) + 1);
    }
}

/* The evaluation bands are fixed. A layout saved with air_thresholds keeps loading; the values are ignored. */
bool air_config_from_json(const cJSON *json, air_config_t *out)
{
    (void)json;
    *out = air_config_default();
    return true;
}
