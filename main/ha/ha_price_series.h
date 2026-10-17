/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Electricity price list of a Home Assistant sensor (the attribute `data` of ha_epex_spot, of the Tibber and Nord Pool template
 * sensors, of the Day Ahead Optimizer): pure logic, no ESP-IDF.
 *
 * A known list shape is recognised by its keys, never configured; the compact series is always in EUR/kWh:
 *   `data`                       items {start_time (ISO 8601 with a zone) | time_ts (epoch ms), price_per_kwh | da_cons}
 *                                (ha_epex_spot, the Day Ahead Optimizer, the Tibber and Nord Pool template sensors)
 *   `raw_today` + `raw_tomorrow` items {start (ISO 8601 with a zone), value}
 *                                (the Nord Pool custom component, Energy Price Forecast; raw units are converted to EUR/kWh;
 *                                 `raw_forecast` beyond tomorrow is not read)
 * The list becomes a regular grid (first start, interval, one price per slot, gaps stay missing) that is small enough for the model's attribute
 * buffer: {"pf":{"v":2,"s":<first start, epoch s>,"d":<interval s>,"p":[<price step>|null,...]}}. The prices are in 1e-5 EUR/kWh and stored as the
 * step from the previous priced slot (the first one from 0): a day-ahead curve moves a few hundred units per slot, an absolute price needs five
 * digits, so the steps keep two days of quarter-hour prices well inside 1279 bytes.
 * Day boundaries come from the C library time zone (TZ), like the Recorder statistics.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

/* Two local days of quarter-hour slots are 192 (188 on a spring and 196 on an autumn day change); keep a little headroom. */
#define HA_PRICE_MAX_SLOTS 200
#define HA_PRICE_MAX_ITEMS 400   /* source items looked at; a longer list is rejected, never truncated */
#define HA_PRICE_MISSING INT32_MIN
#define HA_PRICE_SCALE 100000    /* stored unit: 1e-5 EUR/kWh, covers every observed market value exactly */
#define HA_PRICE_MIN_INTERVAL_S 300
#define HA_PRICE_MAX_INTERVAL_S 7200
#define HA_PRICE_CHEAP_MIN_S 3600
/* At most one window per slot, including windows split at local midnight. No accepted list can exhaust this bound. */
#define HA_PRICE_MAX_WINDOWS HA_PRICE_MAX_SLOTS

typedef struct {
    int64_t start_s;                    /* UTC epoch seconds of slot 0 */
    int32_t interval_s;                 /* slot length */
    uint16_t count;                     /* slots, 0 = no series */
    int32_t price[HA_PRICE_MAX_SLOTS];  /* scaled price, HA_PRICE_MISSING for a gap */
} ha_price_series_t;

typedef struct {
    int64_t start_s;
    int64_t end_s;
} ha_price_window_t;

typedef struct {
    bool valid;      /* at least one priced slot */
    int priced;      /* priced slots */
    double min;      /* EUR/kWh */
    double max;
    double avg;
} ha_price_stats_t;

/* True when attrs has a `data` list whose first item has a known time key and a known price key (cheap check, no allocation). */
bool ha_price_series_detect(const cJSON *attrs);

/* Reads the list into a regular grid. False for anything that cannot be represented exactly: fewer than two items, more than
 * HA_PRICE_MAX_ITEMS items or HA_PRICE_MAX_SLOTS slots, an interval outside 5 minutes..2 hours, a start that does not sit on the grid,
 * two different prices in one slot, a non-numeric or not finite price or a start time that is not strict ISO 8601 with a zone.
 * Explicit end_time/end durations must agree and define the grid; without them it is inferred from the starts.
 * Raw lists require a recognized EUR/cent unit per Wh, kWh or MWh; an explicit currency other than EUR is rejected. */
bool ha_price_series_from_attrs(const cJSON *attrs, ha_price_series_t *out);

/* Strict ISO 8601 "YYYY-MM-DDTHH:MM:SS[.fff](Z|+hh:mm|-hh:mm)": real calendar dates, a mandatory zone, nothing after it. */
bool ha_price_parse_iso_strict(const char *text, int64_t *epoch_s);

/* The model's attribute JSON, {"pf":{...}} only. False when it does not fit in cap bytes including the NUL. */
bool ha_price_series_to_json(const ha_price_series_t *series, char *out, size_t cap);
/* Reads attributes_json written by ha_price_series_to_json (other keys are ignored). */
bool ha_price_series_from_json(const char *attributes_json, ha_price_series_t *out);

/* Averages whole hours of a series whose slots are shorter (15 or 30 minutes) into hourly slots: the last resort when the quarter-hour steps
 * do not fit the attribute budget. A group with a missing slot stays missing, a partial group at the end is dropped; nothing is invented. False when
 * the series is not shorter than an hour, does not divide it or starts off the hour. */
bool ha_price_series_to_hourly(const ha_price_series_t *series, ha_price_series_t *out);

/* Slot containing t, or -1 outside the series. */
int ha_price_slot_at(const ha_price_series_t *series, int64_t t);
double ha_price_value(const ha_price_series_t *series, int slot); /* EUR/kWh, NAN for a gap */
int64_t ha_price_slot_start(const ha_price_series_t *series, int slot);

/* Slots whose start lies in [from_s, to_s): first slot and count (count 0 when none). */
void ha_price_slot_range(const ha_price_series_t *series, int64_t from_s, int64_t to_s, int *first, int *count);
ha_price_stats_t ha_price_stats(const ha_price_series_t *series, int first, int count);

/* Whole cents of a price, as the price timeline card rounds it for its colours: to a tenth of a cent, then to a cent. */
int ha_price_cent(int32_t scaled);
/* The card's average of a day: the mean of the prices rounded to a tenth of a cent, rounded to a whole cent. NAN without a priced slot. */
double ha_price_average_cent(const ha_price_series_t *series, int first, int count);

/* Cheap windows, evaluated per local day on prices rounded to 0.001 EUR/kWh like the card: relative price (p - min) / (max - min, 1 when equal), threshold = the value at index
 * floor(0.25 * (n - 1)) of the sorted relative prices, every slot at or below it is cheap (ties included, so an equal-price day is cheap all
 * day), neighbouring cheap slots without a gap form a window. Only windows that are not over at now_s and last at least one hour are
 * returned, oldest first. */
size_t ha_price_cheap_windows(const ha_price_series_t *series, int64_t now_s, ha_price_window_t *out, size_t max);
