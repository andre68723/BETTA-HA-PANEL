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

bool air_config_from_json(const cJSON *json, air_config_t *out)
{
    *out = air_config_default();
    const cJSON *cfg = cJSON_GetObjectItemCaseSensitive(json, "air_thresholds");
    if (!cfg) return true;
    if (!cJSON_IsObject(cfg)) return false;
    const char *keys[] = {"rh_red_low", "rh_low", "rh_high", "rh_red_high", "co2_amber", "co2_red", "pm_amber", "pm_red", "drying_margin"};
    double *values[] = {&out->rh_red_low, &out->rh_low, &out->rh_high, &out->rh_red_high,
        &out->co2_amber, &out->co2_red, &out->pm_amber, &out->pm_red, &out->drying_margin};
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(cfg, keys[i]);
        if (!v) continue;
        if (!cJSON_IsNumber(v)) return false;
        *values[i] = v->valuedouble;
    }
    return air_config_valid(out);
}
