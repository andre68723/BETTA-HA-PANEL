/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Price histories of the Stocks & ETFs widget for one period (1D, 1W, 30D, YTD, 1Y), from whichever source has them:
 * the daily closes of Easy Stock's history endpoint, or Home Assistant's own records (recorded states for a day, hourly,
 * daily or weekly statistics means). Every series names its kind, so a caller never presents a mean as a close or the
 * change between two means as a market return. A series covers the period as far as its source does: the covered dates
 * are kept, a short history is marked partial, gaps stay gaps. Pure logic (host tested); time is UTC epoch seconds,
 * calendar days come from the C library time zone (TZ).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ha/ha_history_reduce.h"
#include "ha/ha_history_stats.h"

typedef enum {
    HA_QUOTE_PERIOD_1D = 0,
    HA_QUOTE_PERIOD_1W,
    HA_QUOTE_PERIOD_30D,
    HA_QUOTE_PERIOD_YTD,
    HA_QUOTE_PERIOD_1Y,
    HA_QUOTE_PERIOD_COUNT,
} ha_quote_period_t;

typedef enum {
    HA_QUOTE_SERIES_CLOSES = 0,    /* daily closes (Easy Stock); today's may still change */
    HA_QUOTE_SERIES_STATES,        /* recorded states, 5-minute means (Home Assistant) */
    HA_QUOTE_SERIES_HOURLY_MEANS,  /* Home Assistant statistics */
    HA_QUOTE_SERIES_DAILY_MEANS,
    HA_QUOTE_SERIES_WEEKLY_MEANS,
} ha_quote_series_kind_t;

/* The longest series: a year of daily closes is about 260 points, a day of 5-minute means 288. */
#define HA_QUOTE_SERIES_MAX 400
/* Daily closes kept from the history endpoint: a year and its reference, plus the start of this year. */
#define HA_QUOTE_DAILY_MAX 420
/* The history endpoint's reply is refused above this many bytes: Easy Stock keeps every day it stored, so a reply grows
 * by about 22 bytes a trading day (a few KB a year). */
#define HA_QUOTE_HISTORY_MAX_BYTES (64U * 1024U)

typedef struct {
    int64_t t;   /* UTC epoch: a local midnight for daily values, the interval's start for means */
    float v;     /* NAN for a gap */
} ha_quote_point_t;

typedef struct {
    ha_quote_series_kind_t kind;
    ha_quote_period_t period;
    int64_t window_start;          /* the period's start */
    int64_t window_end;            /* now */
    uint16_t count;
    bool has_reference;            /* the last close before the window (a close's period return starts here) */
    ha_quote_point_t reference;
    bool partial;                  /* the source covers less than the period */
    bool last_provisional;         /* the last close is today's and can still change */
    uint16_t rejected;             /* source rows dropped: not finite, out of order, duplicate, invalid date */
    ha_quote_point_t point[HA_QUOTE_SERIES_MAX];
} ha_quote_series_t;

/* Daily closes as the history endpoint stores them: calendar dates in increasing order. */
typedef struct {
    uint16_t count;
    uint16_t rejected;
    int32_t day[HA_QUOTE_DAILY_MAX];  /* days since 1970-01-01 of the calendar date */
    float close[HA_QUOTE_DAILY_MAX];
} ha_quote_daily_t;

typedef enum {
    HA_QUOTE_HISTORY_OK = 0,
    HA_QUOTE_HISTORY_EMPTY,        /* a valid reply without a usable close */
    HA_QUOTE_HISTORY_INVALID,      /* wrong shape or another symbol */
} ha_quote_history_outcome_t;

/* "1d", "1w", "30d", "ytd", "1y" <-> period; false / NULL for anything else. */
bool ha_quote_period_from_code(const char *code, ha_quote_period_t *out);
const char *ha_quote_period_code(ha_quote_period_t period);

/* The period's start: local midnight for 1D, 168 hours back for 1W; for 30D, YTD and 1Y a local midnight (30 days back, January 1,
 * the same date a year back). False without a valid clock. */
bool ha_quote_period_start(ha_quote_period_t period, int64_t now, int64_t *out);

/* Reads Easy Stock's GET /api/easy_stock/history reply {"symbol": ..., "history": [["YYYY-MM-DD", price], ...]}. The symbol
 * must match (case-insensitive). Rows with an invalid date or price, or not after the previous date, are dropped and
 * counted; only the newest HA_QUOTE_DAILY_MAX closes are kept, so older years never enlarge the result. */
ha_quote_history_outcome_t ha_quote_easy_history_parse(const char *json, size_t len, const char *symbol, ha_quote_daily_t *out);

/* The period from daily closes: the closes from the window's first local day on, the last close before it as the reference.
 * The series is partial when the source starts later than the window (no reference); 1D has no daily series. */
bool ha_quote_series_from_daily(const ha_quote_daily_t *daily, ha_quote_period_t period, int64_t now, ha_quote_series_t *out);

/* A day of recorded states (ha_history.h): the means of its 5-minute buckets, gaps where nothing was recorded. */
bool ha_quote_series_from_snapshot(const ha_history_snapshot_t *snapshot, ha_quote_series_t *out);
/* A week of hourly statistics means (ha_history_week.h). */
bool ha_quote_series_from_week(const ha_stats_series_t *series, int64_t first_slot, ha_quote_series_t *out);
/* 30 days of daily or 12 months of weekly statistics means, cut to the period (YTD keeps the weeks from January 1). */
bool ha_quote_series_from_range(const ha_range_series_t *range, ha_quote_period_t period, int64_t now, ha_quote_series_t *out);

/* The change over the period. A close series starts at its reference (else its first close, and the result says partial);
 * means start at the first mean. It ends at `current` when has_current (the live price of a close series), else at the
 * last value. False without two values or with a zero base. */
typedef struct {
    double base;
    int64_t base_t;
    double end;
    double change;
    double change_pct;
    bool from_reference;  /* the base is the close before the period */
} ha_quote_change_t;
bool ha_quote_series_change(const ha_quote_series_t *series, bool has_current, double current, ha_quote_change_t *out);

/* The period change shown for a quote: ha_quote_series_change() ending at the live price, except that a day of recorded
 * states starts at the quote's previous close when it has one (the session's reference; base_t is 0 then). */
struct ha_quote;
bool ha_quote_series_period_change(const ha_quote_series_t *series, const struct ha_quote *quote, ha_quote_change_t *out);

/* Today's return (zero when Easy Stock explicitly reports no trading) and a plot from local midnight to now.
 * day_plot edits a reduced copy, with three spare slots for the baseline and current price. */
bool ha_quote_day_change(const struct ha_quote *quote, ha_quote_change_t *out);
size_t ha_quote_day_plot(ha_quote_point_t *point, size_t count, size_t cap, int64_t now, const struct ha_quote *quote);

/* At most max points for a small chart, keeping the lowest and highest value of every reduced interval and every gap. */
size_t ha_quote_series_reduce(const ha_quote_series_t *series, ha_quote_point_t *out, size_t max);
