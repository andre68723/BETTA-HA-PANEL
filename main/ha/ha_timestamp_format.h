/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Timestamp sensor states (device_class timestamp) shown as a local date, time or relative distance.
 * Independent of LVGL; the relative wording comes from the caller's translations.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    HA_TS_DATETIME, /* default */
    HA_TS_DATE,
    HA_TS_TIME,
    HA_TS_RELATIVE,
} ha_ts_style_t;

typedef enum {
    HA_TS_REL_NOW,
    HA_TS_REL_MINUTES,
    HA_TS_REL_HOURS,
    HA_TS_REL_DAYS,
} ha_ts_rel_unit_t;

/* NULL or "" is the default; false for an unknown name. Names: "datetime", "date", "time", "relative". */
bool ha_ts_style_from_string(const char *name, ha_ts_style_t *out);
/* ISO 8601 with an offset or Z, "T" or a space between date and time, optional fractional seconds. */
bool ha_timestamp_parse(const char *state, int64_t *epoch_s);
/* Date, time or both in the process's local time zone, without seconds; the date follows the language
 * like the Date/Time tile. HA_TS_RELATIVE is formatted as date and time here. */
bool ha_timestamp_format_absolute(int64_t epoch_s, ha_ts_style_t style, const char *language, char *out, size_t out_len);
/* The distance from now, rounded down like the sensor's update age: under a minute is "now", then minutes,
 * hours below 48 and days. */
void ha_timestamp_relative(int64_t epoch_s, int64_t now_s, ha_ts_rel_unit_t *unit, int *amount, bool *future);
