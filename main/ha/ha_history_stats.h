/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Recorder long-term statistics for the history popup and the Energy page: pure logic, no ESP-IDF.
 * Parses the replies of recorder/get_statistics_metadata and recorder/statistics_during_period (period hour),
 * decides which view an entity qualifies for and sums hourly changes into local calendar days.
 * Time is UTC epoch seconds; local day boundaries come from the C library time zone (TZ).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

/* 7 local days are at most 7 * 25 hours plus the current hour; a 168-hour week needs 169 slots. */
#define HA_STATS_MAX_SLOTS 180
#define HA_STATS_UNIT_MAX 24
#define HA_STATS_CLASS_MAX 24
#define HA_STATS_MAX_DAYS 8
#define HA_STATS_HOUR_S 3600

typedef struct {
    bool found;
    bool has_mean;
    bool has_sum;
    int mean_type;                          /* 1 = arithmetic; other values (circular, ...) are not drawn as a mean */
    char unit[HA_STATS_UNIT_MAX];           /* statistics_unit_of_measurement */
    char unit_class[HA_STATS_CLASS_MAX];    /* "" when HA sends null */
} ha_stats_meta_t;

typedef enum {
    HA_STATS_SC_UNKNOWN = 0,   /* the panel does not know the entity's state class (not retained or absent) */
    HA_STATS_SC_MEASUREMENT,
    HA_STATS_SC_TOTAL,
    HA_STATS_SC_TOTAL_INCREASING,
    HA_STATS_SC_OTHER          /* measurement_angle and any future value */
} ha_stats_state_class_t;

typedef enum {
    HA_STATS_KIND_NONE = 0,
    HA_STATS_KIND_MEASUREMENT, /* hourly mean line */
    HA_STATS_KIND_CONSUMPTION  /* daily consumption bars from hourly change */
} ha_stats_kind_t;

typedef enum {
    HA_STATS_WHY_OK = 0,
    HA_STATS_WHY_NOT_FOUND,         /* no metadata: no statistics for this entity */
    HA_STATS_WHY_NO_STATE_CLASS,    /* state class missing or unverified */
    HA_STATS_WHY_STATE_CLASS,       /* a state class this view does not draw (angle, ...) */
    HA_STATS_WHY_NO_MEAN,
    HA_STATS_WHY_NOT_ARITHMETIC,
    HA_STATS_WHY_NO_SUM,
    HA_STATS_WHY_UNIT_MISMATCH,     /* statistics and display unit differ and HA cannot convert */
    HA_STATS_WHY_NOT_CONSUMPTION    /* a counter whose unit class is not volume or energy */
} ha_stats_why_t;

typedef struct {
    ha_stats_kind_t kind;
    ha_stats_why_t why;
    bool convert;                           /* ask HA to convert: units {convert_class: display unit} */
    char convert_class[HA_STATS_CLASS_MAX];
} ha_stats_eligibility_t;

typedef struct {
    int64_t start_epoch;   /* request start_time */
    int64_t end_epoch;     /* request end_time: the frozen "now" */
    int64_t first_slot;    /* UTC epoch of the first hourly interval */
    size_t slots;          /* intervals from first_slot, the current (incomplete) hour included */
} ha_stats_window_t;

typedef enum {
    HA_STATS_OUTCOME_OK = 0,
    HA_STATS_OUTCOME_EMPTY,     /* a valid reply without a usable row: a data state, not an error */
    HA_STATS_OUTCOME_INVALID    /* wrong shape or oversized: an error */
} ha_stats_outcome_t;

typedef enum { HA_STATS_VALUE_MEAN = 0, HA_STATS_VALUE_CHANGE } ha_stats_value_t;

#define HA_STATS_FLAG_VALUE 0x01
#define HA_STATS_FLAG_RANGE 0x02

typedef struct {
    int64_t first_slot;
    uint16_t slots;
    uint16_t valid;       /* slots with a value */
    uint16_t rejected;    /* rows dropped: out of window, misaligned, duplicate, not finite */
    uint8_t sources;      /* series summed into this one (1 for a parsed reply) */
    uint8_t count[HA_STATS_MAX_SLOTS]; /* sources that have a value in the slot */
    float value[HA_STATS_MAX_SLOTS];  /* mean or change */
    float min[HA_STATS_MAX_SLOTS];
    float max[HA_STATS_MAX_SLOTS];
    uint8_t flags[HA_STATS_MAX_SLOTS];
} ha_stats_series_t;

typedef struct {
    int64_t start;        /* UTC epoch of the local midnight */
    uint8_t hours;        /* completed hours expected so far (the current hour has no statistics yet) */
    uint8_t valid;        /* hours with a value from at least one source */
    uint8_t complete;     /* hours with a value from every source */
    bool today;
    float total;          /* sum of the valid hourly changes */
} ha_stats_day_t;

typedef struct {
    size_t count;
    ha_stats_day_t day[HA_STATS_MAX_DAYS];
} ha_stats_days_t;

ha_stats_state_class_t ha_stats_state_class_from_string(const char *value);

/* Reads the "result" array of recorder/get_statistics_metadata for one statistic ID. Returns false when the reply
 * is not an array; an ID HA left out gives out->found == false. */
bool ha_stats_parse_metadata(const cJSON *result, const char *stat_id, ha_stats_meta_t *out);

/* Decides the view an entity qualifies for. display_unit is what the panel shows ("" or NULL for none). */
ha_stats_eligibility_t ha_stats_classify(const ha_stats_meta_t *meta, ha_stats_state_class_t state_class,
                                         const char *display_unit);

/* A cumulative counter as Home Assistant's Energy dashboard uses it: statistics with a sum in a volume or energy unit.
 * The state class is not known there, so the metadata alone decides. */
ha_stats_eligibility_t ha_stats_classify_counter(const ha_stats_meta_t *meta);

/* Adds src to dst slot by slot (same window). A slot keeps a value when any source has one; count[] says how many
 * did, so a day can be marked partial when a source was missing. False when the windows differ. */
bool ha_stats_series_add(ha_stats_series_t *dst, const ha_stats_series_t *src);

/* Rolling 168 elapsed hours ending at now. */
bool ha_stats_week_window(int64_t now, ha_stats_window_t *out);
/* The last `days` local calendar days, today included, from the first local midnight. */
bool ha_stats_days_window(int64_t now, int days, ha_stats_window_t *out);
/* UTC epoch of the local midnight of the day containing epoch, moved by day_offset calendar days. */
int64_t ha_stats_local_day_start(int64_t epoch, int day_offset);

/* Reads the "result" object of recorder/statistics_during_period (one statistic ID, period hour). `which` selects
 * the field in value[]; with want_range min and max are read too. Rows outside the window, off the hour, with an end
 * other than start + 1 h, duplicated or not finite are dropped and counted; a missing hour stays empty. */
ha_stats_outcome_t ha_stats_parse_hourly(const cJSON *result, const char *stat_id, const ha_stats_window_t *window,
                                         ha_stats_value_t which, bool want_range, ha_stats_series_t *out);

/* Sums the hourly changes of `series` (window from ha_stats_days_window) into local days. */
bool ha_stats_sum_days(const ha_stats_series_t *series, int64_t now, int days, ha_stats_days_t *out);

/* Longer ranges for a quote or any other measurement: 30 days as daily means or 12 months as weekly means (Recorder period day / week). */
#define HA_RANGE_MAX_POINTS 64
typedef enum { HA_RANGE_30_DAYS = 0, HA_RANGE_12_MONTHS } ha_range_kind_t;

typedef struct {
    int64_t start;         /* UTC epoch of the period's start (a local midnight, for weeks a Monday) */
    float mean;           /* NAN for a missing calendar period between recorded points */
    float min;
    float max;
    bool has_range;
} ha_range_point_t;

typedef struct {
    ha_range_kind_t kind;
    uint16_t count;        /* includes interior gaps; first and last have a value */
    uint16_t rejected;     /* rows dropped: outside the window, out of order, not finite */
    ha_range_point_t point[HA_RANGE_MAX_POINTS];
} ha_range_series_t;

/* The request window ending at now (start at a local midnight) and the period name for recorder/statistics_during_period. */
bool ha_stats_range_window(int64_t now, ha_range_kind_t kind, ha_stats_window_t *out, const char **period);
/* Reads increasing points, inserting NAN means for missing local days/weeks between values. Leading/trailing gaps are
 * omitted; a span exceeding HA_RANGE_MAX_POINTS is rejected, never compressed. */
ha_stats_outcome_t ha_stats_parse_range(const cJSON *result, const char *stat_id, const ha_stats_window_t *window,
                                        ha_range_kind_t kind, ha_range_series_t *out);
