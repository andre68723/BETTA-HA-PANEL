/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Reads the portfolio sensors of the Parqet Companion integration (cubinet-code/ha-parqet-companion) for the parqet_portfolio widget.
 * The total value sensor carries the attributes `interval`, `holdings_count` and `top_holdings` (up to five {name, value, weight});
 * every sensor of the integration reports its figures for that one interval. The return in percent is not computed here: the
 * total value includes cash, so gain / (value - gain) does not match Parqet's own figure (27.7 % against 24.4 % on the owner's portfolio, 2026-10-18). Pure functions, host-tested.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"

#define PARQET_MAX_HOLDINGS 5
#define PARQET_NAME_MAX 40 /* bytes including the terminator; longer fund names are cut at a character boundary */

typedef struct {
    char name[PARQET_NAME_MAX];
    double value;  /* in the sensor's currency */
    double weight; /* percent of the portfolio */
} parqet_holding_t;

typedef struct {
    char interval[8]; /* the integration's interval code: "max", "1y", "ytd" ... ("" when absent) */
    char unit[8];     /* unit_of_measurement of the total value sensor, "EUR" */
    char entry_id[40];     /* the integration's config entry and portfolio, for the WebSocket command of a chosen interval */
    char portfolio_id[40];
    int holdings_count;
    int count;        /* entries in holding[] */
    parqet_holding_t holding[PARQET_MAX_HOLDINGS];
} parqet_attrs_t;

/* Reads the attributes JSON of the total value sensor. False when it is not valid JSON; missing attributes leave their fields empty. */
bool parqet_attrs_parse(const char *attributes_json, parqet_attrs_t *out);
/* True for the attributes of the total value sensor: a `top_holdings` list and a `portfolio_id`. */
bool parqet_attrs_is_portfolio(const cJSON *attrs);
/* The same attributes shrunk to fit `out_size` bytes: both ids, interval, unit, holdings count, generic metadata and up to five
 * positions with names cut as parqet_attrs_parse() does. Metadata, then trailing positions, are dropped before the ids; never a cut
 * list. False (and "{}") when the ids are missing or nothing fits. Used when the full JSON exceeds APP_HA_ATTRS_MAX_LEN. */
bool parqet_attrs_compact(const cJSON *src, char *out_json, size_t out_size);
/* A sensor state as a number; false for "unknown", "unavailable", empty text or non-finite values. */
bool parqet_number(const char *state, double *out);
/* Short label of an interval code for the tile: "Max", "1Y", "YTD", "1M" ... */
void parqet_interval_label(const char *interval, char *out, size_t cap);
/* A number with thousands separators and a fixed number of decimals: 13361.42 -> "13.361" (german, 0 decimals) or "13,361" (otherwise).
 * `plus` puts "+" in front of a positive value; a negative value gets a real minus sign (U+2212, in the panel's font subset). */
void parqet_format_number(char *out, size_t cap, double value, int decimals, bool german, bool plus);
/* The currency sign of a unit_of_measurement: "EUR" -> "€", "USD" -> "$", "GBP" -> "£"; anything else is returned as it is. */
const char *parqet_currency_symbol(const char *unit);
