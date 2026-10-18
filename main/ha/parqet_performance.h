/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * The reply of the Parqet Companion integration's WebSocket command parqet/get_performance (entry_id, portfolio_id, interval), reduced to
 * what the parqet_portfolio widget shows: the portfolio's figures of one interval, its largest, best and worst holdings in that interval
 * (by value and by return) and a coarse value curve. The whole reply is large (70 KB for a week, 76 KB for a month, 119 KB for the year to date, 131 KB for a year
 * and 418 KB for everything, measured 2026-10-18 on a portfolio of 49 holdings) because every holding comes along; the panel's
 * WebSocket buffer holds 128 KB. The parser takes the `result` object while the callback runs and keeps a few hundred bytes. Pure, host-tested.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"
#include "ha/parqet_portfolio.h"

#define PARQET_PERF_RANK 5 /* holdings kept in each of the two rankings */
#define PARQET_PERF_POINTS 48

typedef struct {
    char name[PARQET_NAME_MAX];
    double value;  /* current value in the portfolio's currency */
    double weight; /* percent of the portfolio value */
    double gain;   /* unrealised gain (gross) within the interval */
    double ret;    /* its return in percent (Parqet's own figure) */
    bool has_return;
} parqet_perf_holding_t;

typedef struct {
    char interval[8];            /* the interval asked for ("1d", "1w", "1m", "ytd"), as the caller passed it */
    double value_start;          /* valuation at the start and at the end of the interval */
    double value_end;
    double unrealized_gain;      /* gross, in the interval */
    double unrealized_return;    /* percent, Parqet's own figure */
    double realized_gain;
    double dividends;            /* gross */
    double taxes;
    double fees;
    double xirr;                 /* percent */
    double ttwror;               /* percent */
    int largest_count;           /* entries of largest[]: securities that are not sold, highest value first */
    parqet_perf_holding_t largest[PARQET_PERF_RANK];
    int top_count;               /* entries of top[]: the securities that are not sold with the highest return in the interval, best first */
    parqet_perf_holding_t top[PARQET_PERF_RANK];
    int flop_count;              /* entries of flop[]: the lowest returns, worst first; none of them is also in top[] */
    parqet_perf_holding_t flop[PARQET_PERF_RANK];
    int point_count;             /* entries of point[]: the portfolio value over the interval, at most PARQET_PERF_POINTS, oldest first */
    float point[PARQET_PERF_POINTS];
} parqet_perf_t;

/* Reads the `result` object of the reply. False when the figures of the portfolio are missing; holdings and the curve are optional.
 * `interval` is copied into the result. */
bool parqet_perf_parse(const cJSON *result, const char *interval, parqet_perf_t *out);
