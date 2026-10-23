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
/* The optional header above the controls (evcc_header): the charging power as a bar up to evcc_power_max_kw, or the
 * site's energy flow from these four power sensors (evcc signs: battery > 0 discharges, grid > 0 imports). */
enum { EVCC_SITE_PV, EVCC_SITE_BATTERY, EVCC_SITE_GRID, EVCC_SITE_HOME, EVCC_SITE_COUNT };
extern const evcc_binding_t k_evcc_site_bindings[EVCC_SITE_COUNT];
#define EVCC_POWER_MAX_KW_DEFAULT 11.0f
#define EVCC_POWER_MAX_KW_MIN 1.0f
#define EVCC_POWER_MAX_KW_MAX 50.0f
bool evcc_binding_domain(size_t role, const char *id);
void evcc_slots_from_json(const cJSON *widget, char *primary, char *secondary, char *tertiary,
                          char (*extra)[APP_MAX_ENTITY_ID_LEN], size_t extra_count);
/* The header of a wallbox: mode ("power", "flow" or "" when off) into mode, the maximum charging power in kW (default 11)
 * and the four site sensors into site[EVCC_SITE_COUNT]. The widget keeps them in fields it does not otherwise use (the
 * forecast slots, style_variant and gauge_max), so they are routed and subscribed without new storage. */
void evcc_header_from_json(const cJSON *widget, char *mode, size_t mode_len, float *max_kw, char *const site[EVCC_SITE_COUNT]);
/* Registry identity, not an entity-id suffix: evcc_intg.<instance>_<loadpoint>_<role>.
 * Returns the full instance/loadpoint stem so two instances cannot be merged. */
bool evcc_registry_role(const char *id, const char *unique_id, char *stem, size_t capacity, size_t *role);
/* A site power sensor of the instance (evcc_intg.<instance>_pv_power ...): the counterpart of a loadpoint role, whose stem
 * has no further underscore. */
bool evcc_registry_site_role(const char *id, const char *unique_id, size_t *role);
