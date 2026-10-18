/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_timestamp_format.h"

#include <string.h>
#include <time.h>

#include "ha/ha_datetime_value.h"
#include "util/iso_time.h"

bool ha_ts_style_from_string(const char *name, ha_ts_style_t *out)
{
    static const struct {
        const char *name;
        ha_ts_style_t style;
    } styles[] = {
        {"datetime", HA_TS_DATETIME},
        {"date", HA_TS_DATE},
        {"time", HA_TS_TIME},
        {"relative", HA_TS_RELATIVE},
    };
    if (out == NULL) return false;
    if (name == NULL || name[0] == '\0') {
        *out = HA_TS_DATETIME;
        return true;
    }
    for (size_t i = 0; i < sizeof(styles) / sizeof(styles[0]); i++) {
        if (strcmp(name, styles[i].name) == 0) {
            *out = styles[i].style;
            return true;
        }
    }
    return false;
}

bool ha_timestamp_parse(const char *state, int64_t *epoch_s)
{
    if (state == NULL || epoch_s == NULL) return false;
    char text[40];
    const size_t n = strlen(state);
    if (n < 20 || n >= sizeof(text)) return false;
    memcpy(text, state, n + 1);
    /* Some integrations write a space between date and time (str() of a Python datetime). */
    if (text[10] == ' ') text[10] = 'T';
    /* iso_time_parse() also takes a missing offset; a timestamp sensor's state always names one. */
    const char *tz = text + 19;
    if (*tz == '.') {
        tz++;
        while (*tz >= '0' && *tz <= '9') tz++;
    }
    if (*tz != 'Z' && *tz != '+' && *tz != '-') return false;
    return iso_time_parse(text, epoch_s);
}

bool ha_timestamp_format_absolute(int64_t epoch_s, ha_ts_style_t style, const char *language, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) return false;
    const time_t epoch = (time_t)epoch_s;
    if ((int64_t)epoch != epoch_s) return false;
    struct tm local;
    if (localtime_r(&epoch, &local) == NULL) return false;
    ha_datetime_value_t v = {
        .kind = HA_DATETIME_UTC,
        .has_date = style != HA_TS_TIME,
        .has_time = style != HA_TS_DATE,
        .year = local.tm_year + 1900,
        .month = local.tm_mon + 1,
        .day = local.tm_mday,
        .hour = local.tm_hour,
        .minute = local.tm_min,
        .second = 0,
    };
    return ha_datetime_format(&v, language, out, out_len);
}

void ha_timestamp_relative(int64_t epoch_s, int64_t now_s, ha_ts_rel_unit_t *unit, int *amount, bool *future)
{
    const int64_t diff = epoch_s - now_s;
    const int64_t span = diff < 0 ? -diff : diff;
    if (future != NULL) *future = diff > 0;
    ha_ts_rel_unit_t u;
    int64_t a;
    if (span < 60) {
        u = HA_TS_REL_NOW;
        a = 0;
    } else if (span < 3600) {
        u = HA_TS_REL_MINUTES;
        a = span / 60;
    } else if (span < 48 * 3600) {
        u = HA_TS_REL_HOURS;
        a = span / 3600;
    } else {
        u = HA_TS_REL_DAYS;
        a = span / 86400;
        if (a > 99999) a = 99999;
    }
    if (unit != NULL) *unit = u;
    if (amount != NULL) *amount = (int)a;
}
