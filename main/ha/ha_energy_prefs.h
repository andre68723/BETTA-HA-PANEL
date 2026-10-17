/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Statistic ids of the Energy dashboard: pure parsing of the energy/get_prefs reply, no ESP-IDF.
 * Home Assistant 2026.10 stores one flat entry per grid source (stat_energy_from/stat_energy_to, either may
 * be null when only a power sensor is set); the pre-2022.4 flow_from[]/flow_to[] arrays are still read.
 * Battery power sensors (stat_rate, power_config) and stat_soc/capacity are not needed: the energy
 * counters stat_energy_from/to stay required for a battery.
 */
#pragma once

#include <stddef.h>

#include "cJSON.h"

#define HA_ENERGY_STAT_REF_MAX 16U
/* Equal to APP_MAX_ENTITY_ID_LEN and APP_MAX_UNIT_LEN, which ha_client.c asserts; app_config.h needs sdkconfig.h. */
#define HA_ENERGY_STAT_ID_LEN 96
#define HA_ENERGY_STAT_UNIT_LEN 24

typedef enum {
    HA_ENERGY_STAT_GRID_FROM = 0,
    HA_ENERGY_STAT_GRID_TO,
    HA_ENERGY_STAT_SOLAR,
    HA_ENERGY_STAT_BATTERY_FROM,
    HA_ENERGY_STAT_BATTERY_TO,
    HA_ENERGY_STAT_GAS,
    HA_ENERGY_STAT_WATER,
} ha_energy_stat_kind_t;

typedef struct {
    char id[HA_ENERGY_STAT_ID_LEN];
    char unit[HA_ENERGY_STAT_UNIT_LEN];
    ha_energy_stat_kind_t kind;
} ha_energy_stat_ref_t;

/* Fills refs from the "result" object of energy/get_prefs, skipping empty ids and duplicates of one kind.
 * Returns the number of refs (at most max_refs); 0 for anything that is not a prefs object. */
size_t ha_energy_prefs_parse(const cJSON *result, ha_energy_stat_ref_t *refs, size_t max_refs);
