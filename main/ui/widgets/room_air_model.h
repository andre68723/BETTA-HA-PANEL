/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    AIR_HUMIDITY, AIR_TEMPERATURE, AIR_CO2, AIR_PM25, AIR_VOC,
    AIR_OUT_TEMPERATURE, AIR_OUT_HUMIDITY, AIR_WINDOW, AIR_RATING, AIR_ROLE_COUNT
} air_role_t;
typedef enum { AIR_NONE = -1, AIR_GOOD, AIR_FAIR, AIR_POOR } air_band_t;
typedef struct {
    double rh_red_low, rh_low, rh_high, rh_red_high;
    double co2_amber, co2_red, pm_amber, pm_red, drying_margin;
} air_config_t;
typedef struct { bool bound, valid; double value; } air_input_t;
enum {
    AIR_DRY = 1, AIR_HUMID = 2, AIR_FRESH_AIR = 4, AIR_PARTICLES = 8,
    AIR_DRYING = 16, AIR_NO_DRYING = 32, AIR_CONFLICT = 64
};
typedef struct {
    air_band_t band[AIR_ROLE_COUNT], overall;
    bool partial, comparison;
    unsigned measured, reasons;
    double indoor_ah, outdoor_ah, difference;
} air_result_t;
typedef struct {
    air_band_t band[AIR_ROLE_COUNT];
    unsigned advice, pending, valid_inputs;
    int64_t pending_since_ms;
    int rh_side; /* -1 dry, +1 humid: side of the last humidity excursion, for hysteresis and reason */
    bool initialized;
} air_memory_t;

air_config_t air_config_default(void);
bool air_config_valid(const air_config_t *c);
/* Normalises only explicit supported units; VOC ppb is display-only. */
bool air_normalize(air_role_t role, const char *text, const char *unit, double *out);
double air_absolute_humidity(double celsius, double rh);
void air_memory_reset(air_memory_t *m);
/* Rising severity is immediate. Exits use metric hysteresis; advice improvements settle for 60s.
 * now_ms is monotonic receipt/render time, never a device sample timestamp. */
air_result_t air_evaluate(const air_config_t *c, const air_input_t in[AIR_ROLE_COUNT],
                          air_memory_t *m, int64_t now_ms);
