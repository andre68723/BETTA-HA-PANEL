/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Model of the energy timeline widget: a rolling 24 hours in 96 slots of 15 minutes, filled from Home Assistant
 * Recorder statistics (period 5minute, type change). Pure code: no LVGL, no network; the widget draws it and
 * ha_energy_timeline.c fills it. Visual idea: aharren/ios-energy-widget (MIT); this is a fresh implementation.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#define ET_SLOTS 96
#define ET_SLOT_SECONDS 900
#define ET_PART_SECONDS 300
#define ET_PARTS_PER_SLOT 3
#define ET_WINDOW_SECONDS (ET_SLOTS * ET_SLOT_SECONDS)

/* The meters the widget reads. Grid import is required; the others are optional. Battery charge and discharge
 * belong together: with only one of them the battery is left out of the balance. */
typedef enum {
    ET_CH_GRID_IMPORT = 0,
    ET_CH_GRID_EXPORT,
    ET_CH_SOLAR,
    ET_CH_BATTERY_CHARGE,
    ET_CH_BATTERY_DISCHARGE,
    ET_CH_COUNT
} et_channel_t;

typedef struct {
    int64_t window_start;                  /* UTC epoch of slot 0, a multiple of 900 s */
    bool configured[ET_CH_COUNT];
    float kwh[ET_CH_COUNT][ET_SLOTS];      /* energy per slot; only the parts present are summed */
    uint8_t parts[ET_CH_COUNT][ET_SLOTS];  /* bit n set: five-minute part n of the slot was reported */
} et_series_t;

/* The window ends with the slot that contains `now_epoch` (so the newest slot is usually partial). Clears all
 * data; `configured` says which channels the widget has an entity for. */
void et_series_init(et_series_t *series, int64_t now_epoch, const bool configured[ET_CH_COUNT]);

/* One five-minute statistic: `start_epoch` is the UTC start of the period, `change_kwh` its energy. Returns false
 * and stores nothing for a non-finite or negative change (a meter reset), a start outside the window, or one
 * that is not on a five-minute boundary. */
bool et_add_part(et_series_t *series, et_channel_t channel, int64_t start_epoch, double change_kwh);

/* Reads the array Home Assistant returns for one statistic id: objects with `start` (epoch milliseconds, or
 * seconds) and `change`. Returns how many parts were stored. */
size_t et_add_stats_json(et_series_t *series, et_channel_t channel, const cJSON *points);

typedef enum {
    ET_SLOT_EMPTY = 0,   /* no grid import part reported: a gap, never a zero bar */
    ET_SLOT_PARTIAL,     /* some five-minute parts missing (or the slot is still running) */
    ET_SLOT_COMPLETE
} et_slot_state_t;

/* One slot. The measured values are the meters; everything below "mix" is an ESTIMATE: aggregate meters cannot say
 * whether grid energy charged the battery or served the home when flows overlap. The rules, in order:
 *   home use   = import + solar + discharge - export - charge  (never negative)
 *   grid       serves the home first, then charges the battery;
 *   solar      serves the rest of the home, then the battery, then feeds in;
 *   battery    serves what the home still needs, then feeds in.
 * Without a battery the split is the plain balance (solar = production - export). */
typedef struct {
    et_slot_state_t state;
    bool battery;              /* charge and discharge meters both configured */
    float import_kwh, export_kwh, solar_kwh, charge_kwh, discharge_kwh; /* measured */
    float home_kwh;
    float home_from_solar, home_from_battery, home_from_grid;           /* mix */
    float battery_from_solar, battery_from_grid;
    float export_from_solar, export_from_battery;
} et_slot_t;

/* Home use of one slot. Without a solar meter everything is grid; without an export meter export is 0. Whether
 * the slot is complete refers to the grid import meter (the one that must exist). */
void et_slot(const et_series_t *series, size_t slot, et_slot_t *out);

typedef struct {
    float home_kwh;
    float home_from_solar, home_from_battery, home_from_grid;
    float battery_from_solar, battery_from_grid;
    float export_from_solar, export_from_battery;
    float import_kwh, export_kwh, solar_kwh, charge_kwh, discharge_kwh;
    size_t slots_with_data;   /* slots that are not empty */
    size_t slots_partial;     /* of those, how many are not complete */
    bool battery;
    bool estimated;           /* the split rests on the rules above (a battery or a solar meter is involved) */
} et_totals_t;

/* Sums over the window. */
void et_totals(const et_series_t *series, et_totals_t *out);

/* Shares 0..1 of a total that the parts make up, for a ring; all 0 when the total is 0. */
float et_share(float part, float total);

/* Optional forecast of solar production per slot, from the compact attributes ha_attrs_compact.c builds
 * ("fc_t0", "fc_step", "fc_kw": average kW per period): kWh of each 15-minute slot, only where the forecast covers it. */
typedef struct {
    bool valid;
    bool has[ET_SLOTS];
    float kwh[ET_SLOTS];
} et_forecast_t;

bool et_forecast_from_attrs(const char *attrs_json, int64_t window_start, et_forecast_t *out);
