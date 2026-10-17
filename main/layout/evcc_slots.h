/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "app_config.h"
#include "cJSON.h"

enum { EVCC_MODE, EVCC_VEHICLE, EVCC_TARGET, EVCC_POWER, EVCC_SOC, EVCC_CONNECTED,
       EVCC_CHARGING, EVCC_DISABLED, EVCC_EFFECTIVE, EVCC_ENERGY, EVCC_REMAINING, EVCC_ROLE_COUNT };
typedef struct { const char *key; const char *role; const char *domain; } evcc_binding_t;
extern const evcc_binding_t k_evcc_bindings[EVCC_ROLE_COUNT];
bool evcc_binding_domain(size_t role, const char *id);
void evcc_slots_from_json(const cJSON *widget, char *primary, char *secondary, char *tertiary,
                          char (*extra)[APP_MAX_ENTITY_ID_LEN], size_t extra_count);
/* Registry identity, not an entity-id suffix: evcc_intg.<instance>_<loadpoint>_<role>.
 * Returns the full instance/loadpoint stem so two instances cannot be merged. */
bool evcc_registry_role(const char *id, const char *unique_id, char *stem, size_t capacity, size_t *role);
