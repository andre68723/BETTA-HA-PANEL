/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Date/time state parsing and service-data generation, independent of LVGL and transport.
 */
#include "ha/ha_datetime_value.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"

static bool digits(const char *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (p[i] < '0' || p[i] > '9') return false;
    }
    return true;
}

static int number(const char *p, size_t n)
{
    int value = 0;
    for (size_t i = 0; i < n; i++) value = value * 10 + p[i] - '0';
    return value;
}

static bool leap(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static bool valid_date(int year, int month, int day)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return year >= 1970 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 &&
           day <= days[month - 1] + (month == 2 && leap(year));
}

static bool valid_time(int hour, int minute, int second)
{
    return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 && second >= 0 && second <= 59;
}

static bool parse_date(const char *s, ha_datetime_value_t *v)
{
    if (!digits(s, 4) || s[4] != '-' || !digits(s + 5, 2) || s[7] != '-' || !digits(s + 8, 2)) return false;
    v->year = number(s, 4);
    v->month = number(s + 5, 2);
    v->day = number(s + 8, 2);
    return valid_date(v->year, v->month, v->day);
}

static bool parse_time(const char *s, size_t len, ha_datetime_value_t *v)
{
    if (len != 5 && len != 8) return false;
    if (!digits(s, 2) || s[2] != ':' || !digits(s + 3, 2) ||
        (len == 8 && (s[5] != ':' || !digits(s + 6, 2)))) return false;
    v->hour = number(s, 2);
    v->minute = number(s + 3, 2);
    v->second = len == 8 ? number(s + 6, 2) : 0;
    return valid_time(v->hour, v->minute, v->second);
}

/* Days since 1970-01-01; this Gregorian calculation avoids a process-wide TZ change. */
static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = year / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? (unsigned)-3 : 9)) + 2) / 5 + day - 1;
    const unsigned yoe_days = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + yoe_days - 719468;
}

static bool local_fields_equal(const struct tm *tm, const ha_datetime_value_t *v)
{
    return tm->tm_year + 1900 == v->year && tm->tm_mon + 1 == v->month && tm->tm_mday == v->day &&
           tm->tm_hour == v->hour && tm->tm_min == v->minute && tm->tm_sec == v->second;
}

/* Ask libc about both DST interpretations; take the earlier one at a fall-back overlap. */
static bool local_to_epoch(const ha_datetime_value_t *v, time_t *out)
{
    bool found = false;
    time_t earliest = 0;
    for (int dst = 0; dst <= 1; dst++) {
        struct tm local = {.tm_year = v->year - 1900, .tm_mon = v->month - 1, .tm_mday = v->day,
                           .tm_hour = v->hour, .tm_min = v->minute, .tm_sec = v->second, .tm_isdst = dst};
        time_t epoch = mktime(&local);
        if (epoch == (time_t)-1 || !local_fields_equal(&local, v)) continue;
        if (!found || epoch < earliest) earliest = epoch;
        found = true;
    }
    if (found) *out = earliest;
    return found;
}

static ha_datetime_kind_t kind_for_entity(const char *entity_id)
{
    if (strncmp(entity_id, "input_datetime.", 15) == 0) return HA_DATETIME_INPUT;
    if (strncmp(entity_id, "time.", 5) == 0) return HA_DATETIME_TIME;
    if (strncmp(entity_id, "date.", 5) == 0) return HA_DATETIME_DATE;
    return HA_DATETIME_UTC;
}

static bool supported_entity(const char *entity_id)
{
    if (entity_id == NULL) return false;
    const char *prefixes[] = {"input_datetime.", "time.", "date.", "datetime."};
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        size_t n = strlen(prefixes[i]);
        if (strncmp(entity_id, prefixes[i], n) == 0 && entity_id[n] != '\0') return true;
    }
    return false;
}

bool ha_datetime_parse(const char *entity_id, const char *state, const char *attributes_json,
                       ha_datetime_value_t *out)
{
    if (!supported_entity(entity_id) || state == NULL || out == NULL) return false;
    ha_datetime_value_t v = {.kind = kind_for_entity(entity_id)};
    size_t n = strlen(state);
    if (v.kind == HA_DATETIME_TIME) {
        v.has_time = parse_time(state, n, &v);
        if (!v.has_time) return false;
    } else if (v.kind == HA_DATETIME_DATE) {
        if (n != 10 || !parse_date(state, &v)) return false;
        v.has_date = true;
    } else if (v.kind == HA_DATETIME_INPUT) {
        if (n >= 10 && parse_date(state, &v)) {
            v.has_date = true;
            if (n > 10) {
                if (state[10] != ' ' || !parse_time(state + 11, n - 11, &v)) return false;
                v.has_time = true;
            }
        } else if (parse_time(state, n, &v)) {
            v.has_time = true;
        } else {
            return false;
        }
        if (attributes_json != NULL && attributes_json[0] != '\0') {
            cJSON *attrs = cJSON_Parse(attributes_json);
            if (attrs != NULL) {
                cJSON *date = cJSON_GetObjectItemCaseSensitive(attrs, "has_date");
                cJSON *time = cJSON_GetObjectItemCaseSensitive(attrs, "has_time");
                bool mismatch = (cJSON_IsBool(date) && cJSON_IsTrue(date) != v.has_date) ||
                                (cJSON_IsBool(time) && cJSON_IsTrue(time) != v.has_time);
                cJSON_Delete(attrs);
                if (mismatch) return false;
            }
        }
    } else {
        /* ISO 8601 with an explicit offset; HA datetime.* states represent UTC instants. */
        if (n < 20 || !parse_date(state, &v) || state[10] != 'T' || !parse_time(state + 11, 8, &v)) return false;
        const char *tz = state + 19;
        int offset_minutes = 0;
        if (strcmp(tz, "Z") != 0) {
            if (strlen(tz) != 6 || (tz[0] != '+' && tz[0] != '-') || !digits(tz + 1, 2) || tz[3] != ':' ||
                !digits(tz + 4, 2)) return false;
            int hours = number(tz + 1, 2);
            int minutes = number(tz + 4, 2);
            if (hours > 14 || minutes > 59 || (hours == 14 && minutes != 0)) return false;
            offset_minutes = (hours * 60 + minutes) * (tz[0] == '+' ? 1 : -1);
        }
        int64_t epoch_s = days_from_civil(v.year, (unsigned)v.month, (unsigned)v.day) * 86400 +
                          v.hour * 3600 + v.minute * 60 + v.second - offset_minutes * 60;
        time_t epoch = (time_t)epoch_s;
        if ((int64_t)epoch != epoch_s) return false;
        struct tm local;
        if (localtime_r(&epoch, &local) == NULL) return false;
        v.year = local.tm_year + 1900;
        v.month = local.tm_mon + 1;
        v.day = local.tm_mday;
        v.hour = local.tm_hour;
        v.minute = local.tm_min;
        v.second = local.tm_sec;
        v.has_date = true;
        v.has_time = true;
    }
    *out = v;
    return true;
}

bool ha_datetime_format(const ha_datetime_value_t *v, const char *language, char *out, size_t out_len)
{
    if (v == NULL || out == NULL || out_len == 0 || (!v->has_date && !v->has_time)) return false;
    if ((v->has_date && !valid_date(v->year, v->month, v->day)) ||
        (v->has_time && !valid_time(v->hour, v->minute, v->second))) return false;
    char date[16] = "";
    char time[12] = "";
    if (v->has_date) {
        if (language != NULL && strncmp(language, "en", 2) == 0)
            snprintf(date, sizeof(date), "%02d/%02d/%04d", v->month, v->day, v->year);
        else if (language != NULL && strncmp(language, "de", 2) == 0)
            snprintf(date, sizeof(date), "%02d.%02d.%04d", v->day, v->month, v->year);
        else
            snprintf(date, sizeof(date), "%02d/%02d/%04d", v->day, v->month, v->year);
    }
    if (v->has_time) {
        if (v->second != 0) snprintf(time, sizeof(time), "%02d:%02d:%02d", v->hour, v->minute, v->second);
        else snprintf(time, sizeof(time), "%02d:%02d", v->hour, v->minute);
    }
    int n = snprintf(out, out_len, "%s%s%s", date, v->has_date && v->has_time ? " " : "", time);
    return n >= 0 && (size_t)n < out_len;
}

bool ha_datetime_service_payload(const char *entity_id, const ha_datetime_value_t *v,
                                 const char **domain, const char **service, char *out, size_t out_len)
{
    if (!supported_entity(entity_id) || v == NULL || domain == NULL || service == NULL || out == NULL || out_len == 0 ||
        v->kind != kind_for_entity(entity_id) ||
        (v->has_date && !valid_date(v->year, v->month, v->day)) ||
        (v->has_time && !valid_time(v->hour, v->minute, v->second))) return false;
    const char *dot = strchr(entity_id, '.');
    for (const char *p = entity_id; *p != '\0'; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || p == dot)) return false;
    }
    char field[96];
    const char *key;
    if (v->kind == HA_DATETIME_UTC) {
        if (!v->has_date || !v->has_time) return false;
        time_t epoch;
        if (!local_to_epoch(v, &epoch)) return false;
        struct tm utc;
        if (gmtime_r(&epoch, &utc) == NULL) return false;
        snprintf(field, sizeof(field), "%04d-%02d-%02dT%02d:%02d:%02d+00:00", utc.tm_year + 1900,
                 utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec);
        key = "datetime";
        *domain = "datetime";
        *service = "set_value";
    } else if (v->kind == HA_DATETIME_DATE || (v->kind == HA_DATETIME_INPUT && v->has_date && !v->has_time)) {
        if (!v->has_date) return false;
        snprintf(field, sizeof(field), "%04d-%02d-%02d", v->year, v->month, v->day);
        key = "date";
        *domain = v->kind == HA_DATETIME_INPUT ? "input_datetime" : "date";
        *service = v->kind == HA_DATETIME_INPUT ? "set_datetime" : "set_value";
    } else if (v->kind == HA_DATETIME_TIME || (v->kind == HA_DATETIME_INPUT && v->has_time && !v->has_date)) {
        if (!v->has_time) return false;
        snprintf(field, sizeof(field), "%02d:%02d:%02d", v->hour, v->minute, v->second);
        key = "time";
        *domain = v->kind == HA_DATETIME_INPUT ? "input_datetime" : "time";
        *service = v->kind == HA_DATETIME_INPUT ? "set_datetime" : "set_value";
    } else if (v->kind == HA_DATETIME_INPUT && v->has_date && v->has_time) {
        snprintf(field, sizeof(field), "%04d-%02d-%02d %02d:%02d:%02d", v->year, v->month, v->day,
                 v->hour, v->minute, v->second);
        key = "datetime";
        *domain = "input_datetime";
        *service = "set_datetime";
    } else {
        return false;
    }
    int n = snprintf(out, out_len, "{\"entity_id\":\"%s\",\"%s\":\"%s\"}", entity_id, key, field);
    return n >= 0 && (size_t)n < out_len;
}
