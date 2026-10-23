/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"
#include "app_config.h"
#include "ha/ha_select_options.h"

bool evcc_number(const char *raw, float *value);
bool evcc_soc(const char *raw, float *value);
bool evcc_select_parse(const char *attributes, ha_select_options_t *options);
bool evcc_number_bounds(const char *attributes, float *min, float *max, float *step);
bool evcc_target_valid(float value, float min, float max, float step);
bool evcc_number_payload(const char *entity, float value, char *out, size_t capacity);
/* Keep complete options or no writable list. Never preserve the large nested vehicle object. */
bool ha_evcc_control_attrs(const cJSON *attributes, bool number, char *out, size_t capacity);

/* A power reading in kW from its state and unit_of_measurement (W, kW or MW); false for anything else. */
bool evcc_power_kw(const char *raw, const char *unit, float *kw);

/* The wallbox header's energy flow, in kW: what flows in (PV, battery discharge, grid import) and out (home, the
 * loadpoint, battery charge, export), from evcc's signed site readings (battery > 0 discharges, grid > 0 imports).
 * scale is the larger side, so both bars share one length. A missing reading counts as 0. */
enum { EVCC_FLOW_PV, EVCC_FLOW_BATTERY_OUT, EVCC_FLOW_GRID_IN, EVCC_FLOW_IN_COUNT };
enum { EVCC_FLOW_HOME, EVCC_FLOW_CAR, EVCC_FLOW_BATTERY_IN, EVCC_FLOW_EXPORT, EVCC_FLOW_OUT_COUNT };
typedef struct {
    float in[EVCC_FLOW_IN_COUNT];
    float out[EVCC_FLOW_OUT_COUNT];
    float scale;
} evcc_flow_t;
void evcc_flow_compute(float pv, float battery, float grid, float home, float car, evcc_flow_t *flow);
