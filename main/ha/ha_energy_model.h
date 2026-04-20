/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "esp_err.h"

typedef struct {
    bool available;
    bool has_grid;
    bool has_solar;
    bool has_battery;
    bool has_gas;
    bool has_water;
    float from_grid_kwh;
    float to_grid_kwh;
    float solar_kwh;
    float to_battery_kwh;
    float from_battery_kwh;
    float gas_value;
    float water_value;
    char gas_unit[APP_MAX_UNIT_LEN];
    char water_unit[APP_MAX_UNIT_LEN];
    int64_t updated_unix_ms;
} ha_energy_snapshot_t;

esp_err_t ha_energy_model_init(void);
void ha_energy_model_reset(void);
void ha_energy_model_update(const ha_energy_snapshot_t *snapshot);
bool ha_energy_model_get_snapshot(ha_energy_snapshot_t *out_snapshot);
uint32_t ha_energy_model_revision(void);
