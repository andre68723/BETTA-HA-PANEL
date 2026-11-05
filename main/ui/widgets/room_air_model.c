/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/widgets/room_air_model.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

air_config_t air_config_default(void)
{
    return (air_config_t){30, 40, 60, 70, 1000, 2000, 15, 35, 1};
}

bool air_config_valid(const air_config_t *c)
{
    return c && isfinite(c->rh_red_low) && isfinite(c->rh_low) &&
        isfinite(c->rh_high) && isfinite(c->rh_red_high) &&
        c->rh_red_low >= 0 && c->rh_red_high <= 100 &&
        c->rh_low - c->rh_red_low > 4 && c->rh_high - c->rh_low > 4 &&
        c->rh_red_high - c->rh_high > 4 &&
        isfinite(c->co2_amber) && isfinite(c->co2_red) && c->co2_amber > 50 &&
        c->co2_red - c->co2_amber > 100 &&
        isfinite(c->pm_amber) && isfinite(c->pm_red) && c->pm_amber > 3 &&
        c->pm_red - c->pm_amber > 6 && isfinite(c->drying_margin) &&
        c->drying_margin > 0 && c->drying_margin <= 20;
}

static bool unit_is(const char *unit, const char *expected) { return !strcmp(unit, expected); }

bool air_normalize(air_role_t role, const char *text, const char *unit, double *out)
{
    if (!text || !*text || !unit || !out || isspace((unsigned char)*text)) return false;
    const char *digits = text;
    if (*digits == '-' || *digits == '+') ++digits;
    if (digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) return false;
    char *end;
    double v = strtod(text, &end);
    if (end == text || *end || !isfinite(v)) return false;
    switch (role) {
    case AIR_HUMIDITY: case AIR_OUT_HUMIDITY:
        if (!unit_is(unit, "%") || v < 0 || v > 100) return false;
        break;
    case AIR_TEMPERATURE: case AIR_OUT_TEMPERATURE:
        if (unit_is(unit, "°F")) v = (v - 32) * 5 / 9;
        else if (!unit_is(unit, "°C")) return false;
        if (v < -80 || v > 80) return false;
        break;
    case AIR_CO2:
        if (!unit_is(unit, "ppm") || v < 0) return false;
        break;
    case AIR_PM25:
        if ((!unit_is(unit, "µg/m³") && !unit_is(unit, "μg/m³")) || v < 0) return false;
        break;
    case AIR_VOC:
        if (!unit_is(unit, "ppb") || v < 0) return false;
        break;
    default: return false;
    }
    *out = v;
    return true;
}

double air_absolute_humidity(double celsius, double rh)
{
    return 216.7 * (rh / 100) * 6.112 * exp(17.62 * celsius / (243.12 + celsius)) / (273.15 + celsius);
}

void air_memory_reset(air_memory_t *m)
{
    memset(m, 0, sizeof(*m));
    for (int i = 0; i < AIR_ROLE_COUNT; ++i) m->band[i] = AIR_NONE;
}

static air_band_t pollutant(double v, double amber, double red, double h, air_band_t old)
{
    air_band_t band = v >= red ? AIR_POOR : v >= amber ? AIR_FAIR : AIR_GOOD;
    if (old == AIR_POOR && v >= red - h) return AIR_POOR;
    if (old >= AIR_FAIR && band == AIR_GOOD && v >= amber - h) return AIR_FAIR;
    return band;
}

static air_band_t humidity(const air_config_t *c, double v, air_memory_t *m)
{
    air_band_t old = m->band[AIR_HUMIDITY];
    int side = v < c->rh_low ? -1 : v > c->rh_high ? 1 : 0;
    air_band_t band = v < c->rh_red_low || v > c->rh_red_high ? AIR_POOR : side ? AIR_FAIR : AIR_GOOD;
    /* Hysteresis applies only on the side of the previous excursion. */
    if (old == AIR_POOR && band != AIR_POOR &&
        ((m->rh_side < 0 && v < c->rh_red_low + 2) || (m->rh_side > 0 && v > c->rh_red_high - 2))) band = AIR_POOR;
    if (old >= AIR_FAIR && band == AIR_GOOD &&
        ((m->rh_side < 0 && v < c->rh_low + 2) || (m->rh_side > 0 && v > c->rh_high - 2))) band = AIR_FAIR;
    if (side) m->rh_side = side;
    else if (band == AIR_GOOD) m->rh_side = 0;
    return band;
}

air_result_t air_evaluate(const air_config_t *c, const air_input_t in[AIR_ROLE_COUNT],
                          air_memory_t *m, int64_t now_ms)
{
    air_result_t r = {.overall = AIR_NONE};
    unsigned valid_inputs = 0;
    bool valid[AIR_ROLE_COUNT] = {0};
    for (int i = 0; i < AIR_RATING; ++i) {
        valid[i] = in[i].bound && in[i].valid && isfinite(in[i].value);
        if (i == AIR_HUMIDITY || i == AIR_OUT_HUMIDITY) valid[i] &= in[i].value >= 0 && in[i].value <= 100;
        if (i == AIR_CO2 || i == AIR_PM25 || i == AIR_VOC) valid[i] &= in[i].value >= 0;
        if (i == AIR_TEMPERATURE || i == AIR_OUT_TEMPERATURE) valid[i] &= in[i].value >= -80 && in[i].value <= 80;
        if (valid[i] && i != AIR_WINDOW) valid_inputs |= 1U << i;
    }
    bool lost = (m->valid_inputs & ~valid_inputs) != 0;
    m->valid_inputs = valid_inputs;
    for (int i = 0; i < AIR_ROLE_COUNT; ++i) {
        r.band[i] = AIR_NONE;
        if (i != AIR_HUMIDITY && i != AIR_CO2 && i != AIR_PM25 && i != AIR_VOC) continue;
        if (!in[i].bound) { m->band[i] = AIR_NONE; continue; }
        if (i == AIR_VOC) continue; /* display only; never evaluated, never coverage */
        if (!valid[i]) {
            r.partial = true;
            lost |= m->band[i] != AIR_NONE;
            m->band[i] = AIR_NONE;
            continue;
        }
        ++r.measured;
        air_band_t band = i == AIR_HUMIDITY ? humidity(c, in[i].value, m) :
            i == AIR_CO2 ? pollutant(in[i].value, c->co2_amber, c->co2_red, 50, m->band[i]) :
            pollutant(in[i].value, c->pm_amber, c->pm_red, 3, m->band[i]);
        m->band[i] = r.band[i] = band;
        if (band > r.overall) r.overall = band;
        if (band > AIR_GOOD) {
            if (i == AIR_HUMIDITY) r.reasons |= m->rh_side < 0 ? AIR_DRY : AIR_HUMID;
            if (i == AIR_CO2) r.reasons |= AIR_FRESH_AIR;
            if (i == AIR_PM25) r.reasons |= AIR_PARTICLES;
        }
    }
    if (r.partial && r.overall == AIR_GOOD) r.overall = AIR_NONE;
    if (valid[AIR_HUMIDITY] && valid[AIR_TEMPERATURE] &&
        valid[AIR_OUT_HUMIDITY] && valid[AIR_OUT_TEMPERATURE]) {
        r.comparison = true;
        r.indoor_ah = air_absolute_humidity(in[AIR_TEMPERATURE].value, in[AIR_HUMIDITY].value);
        r.outdoor_ah = air_absolute_humidity(in[AIR_OUT_TEMPERATURE].value, in[AIR_OUT_HUMIDITY].value);
        r.difference = r.indoor_ah - r.outdoor_ah;
        if (r.reasons & AIR_HUMID) {
            r.reasons |= r.difference >= c->drying_margin ? AIR_DRYING : AIR_NO_DRYING;
            if ((r.reasons & AIR_FRESH_AIR) && r.difference < 0) r.reasons |= AIR_CONFLICT;
        }
    }
    /* Data loss cancels pending advice. Warnings and new conflicts must never wait. */
    if (!m->initialized || lost || (r.reasons & ~m->advice)) {
        m->advice = m->pending = r.reasons;
        m->pending_since_ms = now_ms;
        m->initialized = true;
    } else if (r.reasons != m->advice) {
        if (m->pending != r.reasons) { m->pending = r.reasons; m->pending_since_ms = now_ms; }
        if (now_ms - m->pending_since_ms >= 60000) m->advice = r.reasons;
    } else { m->pending = r.reasons; m->pending_since_ms = now_ms; }
    r.reasons = m->advice;
    return r;
}
