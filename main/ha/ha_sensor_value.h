/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * A sensor state as the Sensor tile shows it, by device class: the unit choice (sensor_unit), durations,
 * dates, readable enum values and the decimal separator of the panel language. Timestamps are formatted by
 * ha_timestamp_format. Independent of LVGL; translated words come from the caller.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    HA_SV_UNIT_DEFAULT, /* as Home Assistant gives it; a currency code becomes its symbol */
    HA_SV_UNIT_HIDE,
    HA_SV_UNIT_DEGREE,  /* temperature: "21.5°" */
    HA_SV_UNIT_AUTO,    /* power, energy: W to kW, Wh to kWh, kWh to MWh from 1000 */
    HA_SV_UNIT_CODE,    /* monetary: the currency code as Home Assistant gives it */
} ha_sv_unit_t;

typedef enum {
    HA_SV_DURATION_TEXT, /* default: "1 h 2 min" */
    HA_SV_DURATION_CLOCK,
    HA_SV_DURATION_RAW,
} ha_sv_duration_style_t;

/* NULL or "" is the default; false for an unknown name. Names: "hide", "degree", "auto", "code". */
bool ha_sv_unit_from_string(const char *name, ha_sv_unit_t *out);
/* Every sensor_format name: the timestamp styles of ha_timestamp_format, "text", "clock" and "raw" for a
 * duration, "date" and "relative" for a date. */
bool ha_sv_format_valid(const char *name);
/* "clock" and "raw"; anything else (NULL, "", "text") is the text default. */
ha_sv_duration_style_t ha_sv_duration_style(const char *format);

/* A comma as decimal separator for the languages that write one ("de", "fr", ...). */
bool ha_sv_language_decimal_comma(const char *language);
/* Replaces the decimal point of a plain number in place; other text stays unchanged. */
void ha_sv_localize_number(char *text, const char *language);

/* value is the state already rounded to the entity's display precision, raw_state the state as reported
 * (for the kW conversion). Writes "value unit" with the unit chosen by mode and the device class. */
bool ha_sv_number_text(const char *value, const char *raw_state, const char *unit, const char *device_class,
                       ha_sv_unit_t mode, const char *language, char *out, size_t out_len);

/* A money state without a display precision of its own, rounded like Home Assistant's frontend: two decimals
 * for a currency code ("EUR"), none for "JPY"; false for another unit ("ct/kWh") or a state that is not a number. */
bool ha_sv_currency_round(const char *state, const char *unit, char *out, size_t out_len);

/* A duration state in seconds; units "d", "h", "min", "s" and "ms". */
bool ha_sv_duration_seconds(const char *state, const char *unit, int64_t *seconds);
/* labels: day, hour, minute and second words ("d", "h", "min", "s"). Text drops the seconds from an hour
 * and the minutes from a day ("2 d 3 h"); clock is "1:02:05" with the hours counted on past a day. */
bool ha_sv_duration_text(int64_t seconds, ha_sv_duration_style_t style, const char *const labels[4],
                         char *out, size_t out_len);

/* A date state "YYYY-MM-DD". */
bool ha_sv_date_parse(const char *state, int *year, int *month, int *day);
/* Days from the first date to the second (negative when the second is earlier). */
int64_t ha_sv_days_between(int y1, int m1, int d1, int y2, int m2, int d2);

/* An enum state like "no_risk" as "No risk"; false (and nothing written) for anything that is not a plain
 * lowercase identifier, which then stays as it is. */
bool ha_sv_enum_text(const char *state, char *out, size_t out_len);
