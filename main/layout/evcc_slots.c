/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#include "layout/evcc_slots.h"
#include <string.h>

const evcc_binding_t k_evcc_bindings[EVCC_ROLE_COUNT] = {
    {"entity_id", "mode", "select"},
    {"evcc_vehicle_entity_id", "vehicle_name", "select"},
    {"evcc_target_soc_entity_id", "limit_soc", "number"},
    {"evcc_power_entity_id", "charge_power", "sensor"},
    {"evcc_soc_entity_id", "vehicle_soc", "sensor"},
    {"evcc_connected_entity_id", "connected", "binary_sensor"},
    {"evcc_charging_entity_id", "charging", "binary_sensor"},
    {"evcc_disabled_entity_id", "disabled_in_config", "binary_sensor"},
    {"evcc_effective_soc_entity_id", "effective_limit_soc", "sensor"},
    {"evcc_energy_entity_id", "charged_energy", "sensor"},
    {"evcc_remaining_entity_id", "charge_remaining_duration", "sensor"},
};

static bool domain(const char *id, const char *name)
{
    const size_t n = strlen(name);
    return id && strncmp(id, name, n) == 0 && id[n] == '.';
}
bool evcc_binding_domain(size_t role, const char *id)
{
    return role < EVCC_ROLE_COUNT && (domain(id, k_evcc_bindings[role].domain) ||
        (role == EVCC_TARGET && domain(id, "select")));
}
void evcc_slots_from_json(const cJSON *widget, char *primary, char *secondary, char *tertiary,
                          char (*extra)[APP_MAX_ENTITY_ID_LEN], size_t extra_count)
{
    char *first[] = {primary, secondary, tertiary};
    for (size_t i = 0; i < EVCC_ROLE_COUNT; i++) {
        const char *value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(widget, k_evcc_bindings[i].key));
        char *out = i < 3 ? first[i] : extra && i - 3 < extra_count ? extra[i - 3] : NULL;
        if (out && value && strlen(value) < APP_MAX_ENTITY_ID_LEN) strcpy(out, value);
    }
}
bool evcc_registry_role(const char *id, const char *uid, char *stem, size_t capacity, size_t *role)
{
    const char *prefix = "evcc_intg.";
    if (!id || !uid || !stem || !role || strncmp(uid, prefix, strlen(prefix))) return false;
    const char *slug = uid + strlen(prefix);
    const size_t n = strlen(slug);
    /* Longest suffix wins: effective_limit_soc must never match limit_soc. */
    size_t chosen = EVCC_ROLE_COUNT, longest = 0;
    for (size_t i = 0; i < EVCC_ROLE_COUNT; i++) {
        size_t len = strlen(k_evcc_bindings[i].role);
        if (n > len + 1 && slug[n - len - 1] == '_' && !strcmp(slug + n - len, k_evcc_bindings[i].role)
            && len > longest) { chosen = i; longest = len; }
    }
    if (chosen == EVCC_ROLE_COUNT || !evcc_binding_domain(chosen, id)) return false;
    size_t count = n - longest - 1;
    if (count >= capacity || !memchr(slug, '_', count)) return false; /* site entities are not loadpoints */
    memcpy(stem, slug, count); stem[count] = '\0'; *role = chosen;
    return true;
}
