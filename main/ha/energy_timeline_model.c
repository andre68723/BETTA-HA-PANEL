/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/energy_timeline_model.h"

#include <math.h>
#include <string.h>

void et_series_init(et_series_t *series, int64_t now_epoch, const bool configured[ET_CH_COUNT])
{
    if (series == NULL) {
        return;
    }
    memset(series, 0, sizeof(*series));
    const int64_t current_slot = now_epoch - (now_epoch % ET_SLOT_SECONDS);
    series->window_start = current_slot - (int64_t)(ET_SLOTS - 1) * ET_SLOT_SECONDS;
    for (int c = 0; c < ET_CH_COUNT; c++) {
        series->configured[c] = configured != NULL && configured[c];
    }
}

bool et_add_part(et_series_t *series, et_channel_t channel, int64_t start_epoch, double change_kwh)
{
    if (series == NULL || channel < 0 || channel >= ET_CH_COUNT || !isfinite(change_kwh) || change_kwh < 0.0) {
        return false;
    }
    const int64_t offset = start_epoch - series->window_start;
    if (offset < 0 || offset >= ET_WINDOW_SECONDS || offset % ET_PART_SECONDS != 0) {
        return false;
    }
    const size_t slot = (size_t)(offset / ET_SLOT_SECONDS);
    const unsigned part = (unsigned)((offset % ET_SLOT_SECONDS) / ET_PART_SECONDS);
    const uint8_t bit = (uint8_t)(1u << part);
    if (series->parts[channel][slot] & bit) {
        return false; /* a repeated part is ignored, not added twice */
    }
    series->parts[channel][slot] |= bit;
    series->kwh[channel][slot] += (float)change_kwh;
    return true;
}

size_t et_add_stats_json(et_series_t *series, et_channel_t channel, const cJSON *points)
{
    size_t stored = 0;
    if (!cJSON_IsArray(points)) {
        return 0;
    }
    const cJSON *point = NULL;
    cJSON_ArrayForEach(point, points)
    {
        const cJSON *start = cJSON_IsObject(point) ? cJSON_GetObjectItemCaseSensitive(point, "start") : NULL;
        const cJSON *change = cJSON_IsObject(point) ? cJSON_GetObjectItemCaseSensitive(point, "change") : NULL;
        if (!cJSON_IsNumber(start) || !cJSON_IsNumber(change)) {
            continue; /* a null change is a hole, not a zero */
        }
        int64_t epoch = (int64_t)start->valuedouble;
        if (epoch > 100000000000LL) {
            epoch /= 1000; /* milliseconds */
        }
        if (et_add_part(series, channel, epoch, change->valuedouble)) {
            stored++;
        }
    }
    return stored;
}

static float slot_kwh(const et_series_t *s, et_channel_t c, size_t slot)
{
    return s->configured[c] ? s->kwh[c][slot] : 0.0f;
}

static float min_f(float a, float b) { return a < b ? a : b; }
static float pos_f(float a) { return a > 0.0f ? a : 0.0f; }

void et_slot(const et_series_t *series, size_t slot, et_slot_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (series == NULL || slot >= ET_SLOTS) {
        return;
    }
    const uint8_t import_parts = series->parts[ET_CH_GRID_IMPORT][slot];
    if (!series->configured[ET_CH_GRID_IMPORT] || import_parts == 0) {
        out->state = ET_SLOT_EMPTY;
        return;
    }
    out->state = import_parts == 0x07 ? ET_SLOT_COMPLETE : ET_SLOT_PARTIAL;
    out->battery = series->configured[ET_CH_BATTERY_CHARGE] && series->configured[ET_CH_BATTERY_DISCHARGE];
    out->import_kwh = slot_kwh(series, ET_CH_GRID_IMPORT, slot);
    out->export_kwh = slot_kwh(series, ET_CH_GRID_EXPORT, slot);
    out->solar_kwh = slot_kwh(series, ET_CH_SOLAR, slot);
    out->charge_kwh = out->battery ? slot_kwh(series, ET_CH_BATTERY_CHARGE, slot) : 0.0f;
    out->discharge_kwh = out->battery ? slot_kwh(series, ET_CH_BATTERY_DISCHARGE, slot) : 0.0f;

    const float import = out->import_kwh, solar = out->solar_kwh, charge = out->charge_kwh;
    const float discharge = out->discharge_kwh, exported = out->export_kwh;
    const float home = pos_f(import + solar + discharge - exported - charge);
    out->home_kwh = home;

    out->home_from_grid = min_f(import, home);
    out->battery_from_grid = min_f(pos_f(import - out->home_from_grid), charge);
    float home_rest = home - out->home_from_grid;
    out->home_from_solar = min_f(solar, home_rest);
    float solar_rest = solar - out->home_from_solar;
    out->battery_from_solar = min_f(pos_f(charge - out->battery_from_grid), solar_rest);
    solar_rest -= out->battery_from_solar;
    out->export_from_solar = min_f(exported, solar_rest);
    home_rest -= out->home_from_solar;
    out->home_from_battery = min_f(discharge, home_rest);
    out->export_from_battery = min_f(pos_f(exported - out->export_from_solar), pos_f(discharge - out->home_from_battery));
}

void et_totals(const et_series_t *series, et_totals_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    for (size_t slot = 0; series != NULL && slot < ET_SLOTS; slot++) {
        et_slot_t s;
        et_slot(series, slot, &s);
        if (s.state == ET_SLOT_EMPTY) {
            continue;
        }
        out->slots_with_data++;
        if (s.state == ET_SLOT_PARTIAL) out->slots_partial++;
        out->battery = s.battery;
        out->home_kwh += s.home_kwh;
        out->home_from_solar += s.home_from_solar;
        out->home_from_battery += s.home_from_battery;
        out->home_from_grid += s.home_from_grid;
        out->battery_from_solar += s.battery_from_solar;
        out->battery_from_grid += s.battery_from_grid;
        out->export_from_solar += s.export_from_solar;
        out->export_from_battery += s.export_from_battery;
        out->import_kwh += s.import_kwh;
        out->export_kwh += s.export_kwh;
        out->solar_kwh += s.solar_kwh;
        out->charge_kwh += s.charge_kwh;
        out->discharge_kwh += s.discharge_kwh;
    }
    if (series != NULL) {
        out->estimated = out->battery || series->configured[ET_CH_SOLAR];
    }
}

float et_share(float part, float total)
{
    if (!(total > 0.0f) || !(part > 0.0f)) {
        return 0.0f;
    }
    const float share = part / total;
    return share > 1.0f ? 1.0f : share;
}

bool et_forecast_from_attrs(const char *attrs_json, int64_t window_start, et_forecast_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    cJSON *attrs = attrs_json != NULL ? cJSON_Parse(attrs_json) : NULL;
    if (attrs == NULL) {
        return false;
    }
    const cJSON *t0 = cJSON_GetObjectItemCaseSensitive(attrs, "fc_t0");
    const cJSON *step = cJSON_GetObjectItemCaseSensitive(attrs, "fc_step");
    const cJSON *kw = cJSON_GetObjectItemCaseSensitive(attrs, "fc_kw");
    if (cJSON_IsNumber(t0) && cJSON_IsNumber(step) && step->valuedouble >= 60.0 && cJSON_IsArray(kw)) {
        const int64_t start = (int64_t)t0->valuedouble, period = (int64_t)step->valuedouble;
        const int count = cJSON_GetArraySize(kw);
        for (size_t slot = 0; slot < ET_SLOTS; slot++) {
            const int64_t mid = window_start + (int64_t)slot * ET_SLOT_SECONDS + ET_SLOT_SECONDS / 2;
            if (mid < start) continue;
            const int64_t index = (mid - start) / period;
            if (index >= count) continue;
            const cJSON *value = cJSON_GetArrayItem(kw, (int)index);
            if (cJSON_IsNumber(value) && isfinite(value->valuedouble) && value->valuedouble >= 0.0) {
                out->has[slot] = true;
                out->kwh[slot] = (float)(value->valuedouble * ET_SLOT_SECONDS / 3600.0);
                out->valid = true;
            }
        }
    }
    cJSON_Delete(attrs);
    return out->valid;
}
