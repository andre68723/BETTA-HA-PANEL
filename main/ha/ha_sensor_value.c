/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_sensor_value.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ha/ha_timestamp_format.h"

bool ha_sv_unit_from_string(const char *name, ha_sv_unit_t *out)
{
    static const struct {
        const char *name;
        ha_sv_unit_t unit;
    } units[] = {
        {"hide", HA_SV_UNIT_HIDE},
        {"degree", HA_SV_UNIT_DEGREE},
        {"auto", HA_SV_UNIT_AUTO},
        {"code", HA_SV_UNIT_CODE},
    };
    if (out == NULL) return false;
    if (name == NULL || name[0] == '\0') {
        *out = HA_SV_UNIT_DEFAULT;
        return true;
    }
    for (size_t i = 0; i < sizeof(units) / sizeof(units[0]); i++) {
        if (strcmp(name, units[i].name) == 0) {
            *out = units[i].unit;
            return true;
        }
    }
    return false;
}

bool ha_sv_format_valid(const char *name)
{
    if (name == NULL || name[0] == '\0') return false;
    ha_ts_style_t style;
    return ha_ts_style_from_string(name, &style) || strcmp(name, "text") == 0 || strcmp(name, "clock") == 0 ||
           strcmp(name, "raw") == 0;
}

ha_sv_duration_style_t ha_sv_duration_style(const char *format)
{
    if (format != NULL && strcmp(format, "clock") == 0) return HA_SV_DURATION_CLOCK;
    if (format != NULL && strcmp(format, "raw") == 0) return HA_SV_DURATION_RAW;
    return HA_SV_DURATION_TEXT;
}

bool ha_sv_language_decimal_comma(const char *language)
{
    static const char *const comma[] = {"de", "fr", "it", "es", "nl", "pt", "pl", "cs", "da", "sv", "nb", "fi", "ru", "tr"};
    if (language == NULL) return false;
    for (size_t i = 0; i < sizeof(comma) / sizeof(comma[0]); i++) {
        if (strncmp(language, comma[i], 2) == 0 && (language[2] == '\0' || language[2] == '-' || language[2] == '_')) {
            return true;
        }
    }
    return false;
}

static bool sv_plain_number(const char *text)
{
    const char *p = text;
    if (*p == '-' || *p == '+') p++;
    bool digits = false;
    bool point = false;
    for (; *p != '\0'; p++) {
        if (*p >= '0' && *p <= '9') {
            digits = true;
        } else if (*p == '.' && !point) {
            point = true;
        } else {
            return false;
        }
    }
    return digits;
}

void ha_sv_localize_number(char *text, const char *language)
{
    if (text == NULL || !ha_sv_language_decimal_comma(language) || !sv_plain_number(text)) return;
    char *point = strchr(text, '.');
    if (point != NULL) *point = ',';
}

/* W -> kW -> MW and the like: the index of unit in one ladder, or false. */
static bool sv_unit_ladder(const char *unit, const char *const **ladder, size_t *index)
{
    static const char *const power[] = {"W", "kW", "MW", "GW"};
    static const char *const energy[] = {"Wh", "kWh", "MWh", "GWh"};
    static const char *const apparent[] = {"VA", "kVA", "MVA", "GVA"};
    static const char *const reactive[] = {"var", "kvar", "Mvar", "Gvar"};
    static const char *const *const ladders[] = {power, energy, apparent, reactive};
    for (size_t l = 0; l < sizeof(ladders) / sizeof(ladders[0]); l++) {
        for (size_t i = 0; i < 4; i++) {
            if (strcmp(unit, ladders[l][i]) == 0) {
                *ladder = ladders[l];
                *index = i;
                return true;
            }
        }
    }
    return false;
}

static const char *sv_currency_symbol(const char *code)
{
    static const struct {
        const char *code;
        const char *symbol;
    } symbols[] = {{"EUR", "€"}, {"USD", "$"}, {"GBP", "£"}, {"JPY", "¥"}};
    for (size_t i = 0; i < sizeof(symbols) / sizeof(symbols[0]); i++) {
        if (strcmp(code, symbols[i].code) == 0) return symbols[i].symbol;
    }
    return code;
}

bool ha_sv_number_text(const char *value, const char *raw_state, const char *unit, const char *device_class,
                       ha_sv_unit_t mode, const char *language, char *out, size_t out_len)
{
    if (value == NULL || out == NULL || out_len == 0) return false;
    if (unit == NULL) unit = "";
    if (device_class == NULL) device_class = "";
    char number[48];
    snprintf(number, sizeof(number), "%s", value);
    const bool numeric = sv_plain_number(number);

    if (numeric && mode == HA_SV_UNIT_AUTO && raw_state != NULL) {
        const char *const *ladder = NULL;
        size_t index = 0;
        char *end = NULL;
        double v = strtod(raw_state, &end);
        if (end != raw_state && *end == '\0' && isfinite(v) && sv_unit_ladder(unit, &ladder, &index)) {
            const size_t start = index;
            while (fabs(v) >= 1000.0 && index < 3) {
                v /= 1000.0;
                index++;
            }
            if (index != start) {
                const double a = fabs(v);
                const int decimals = a < 10.0 ? 2 : a < 100.0 ? 1 : 0;
                snprintf(number, sizeof(number), "%.*f", decimals, v);
                unit = ladder[index];
            }
        }
    }
    if (numeric) ha_sv_localize_number(number, language);

    const char *shown = unit;
    bool attach = false;
    if (mode == HA_SV_UNIT_HIDE) {
        shown = "";
    } else if (mode == HA_SV_UNIT_DEGREE && strncmp(unit, "°", 2) == 0) {
        shown = "°";
        attach = true;
    } else if (mode != HA_SV_UNIT_CODE && strcmp(device_class, "monetary") == 0) {
        shown = sv_currency_symbol(unit);
    }
    int n = shown[0] == '\0' ? snprintf(out, out_len, "%s", number)
                             : snprintf(out, out_len, attach ? "%s%s" : "%s %s", number, shown);
    return n >= 0 && (size_t)n < out_len;
}

bool ha_sv_currency_round(const char *state, const char *unit, char *out, size_t out_len)
{
    if (state == NULL || unit == NULL || out == NULL || out_len == 0 || !sv_plain_number(state)) return false;
    if (strlen(unit) != 3 || !isupper((unsigned char)unit[0]) || !isupper((unsigned char)unit[1]) ||
        !isupper((unsigned char)unit[2])) {
        return false;
    }
    const int decimals = strcmp(unit, "JPY") == 0 ? 0 : 2;
    const int n = snprintf(out, out_len, "%.*f", decimals, strtod(state, NULL));
    return n >= 0 && (size_t)n < out_len;
}

bool ha_sv_duration_seconds(const char *state, const char *unit, int64_t *seconds)
{
    if (state == NULL || unit == NULL || seconds == NULL) return false;
    char *end = NULL;
    const double v = strtod(state, &end);
    if (end == state || *end != '\0' || !isfinite(v)) return false;
    double factor;
    if (strcmp(unit, "s") == 0) factor = 1.0;
    else if (strcmp(unit, "ms") == 0) factor = 0.001;
    else if (strcmp(unit, "min") == 0) factor = 60.0;
    else if (strcmp(unit, "h") == 0) factor = 3600.0;
    else if (strcmp(unit, "d") == 0) factor = 86400.0;
    else return false;
    const double s = v * factor;
    if (fabs(s) > 9.0e15) return false;
    *seconds = (int64_t)llround(s);
    return true;
}

bool ha_sv_duration_text(int64_t seconds, ha_sv_duration_style_t style, const char *const labels[4],
                         char *out, size_t out_len)
{
    if (out == NULL || out_len == 0 || style == HA_SV_DURATION_RAW) return false;
    const char *sign = seconds < 0 ? "-" : "";
    const int64_t s = seconds < 0 ? -seconds : seconds;
    const long long d = (long long)(s / 86400);
    const long long h = (long long)(s % 86400 / 3600);
    const long long m = (long long)(s % 3600 / 60);
    const long long sec = (long long)(s % 60);
    int n;
    if (style == HA_SV_DURATION_CLOCK) {
        n = snprintf(out, out_len, "%s%lld:%02lld:%02lld", sign, (long long)(s / 3600), m, sec);
    } else if (labels == NULL) {
        return false;
    } else if (s < 60) {
        n = snprintf(out, out_len, "%s%lld %s", sign, sec, labels[3]);
    } else if (s < 3600) {
        n = sec ? snprintf(out, out_len, "%s%lld %s %lld %s", sign, m, labels[2], sec, labels[3])
                : snprintf(out, out_len, "%s%lld %s", sign, m, labels[2]);
    } else if (s < 86400) {
        n = m ? snprintf(out, out_len, "%s%lld %s %lld %s", sign, h, labels[1], m, labels[2])
              : snprintf(out, out_len, "%s%lld %s", sign, h, labels[1]);
    } else {
        n = h ? snprintf(out, out_len, "%s%lld %s %lld %s", sign, d, labels[0], h, labels[1])
              : snprintf(out, out_len, "%s%lld %s", sign, d, labels[0]);
    }
    return n >= 0 && (size_t)n < out_len;
}

bool ha_sv_date_parse(const char *state, int *year, int *month, int *day)
{
    if (state == NULL || strlen(state) != 10 || state[4] != '-' || state[7] != '-') return false;
    for (int i = 0; i < 10; i++) {
        if (i != 4 && i != 7 && (state[i] < '0' || state[i] > '9')) return false;
    }
    const int y = atoi(state);
    const int m = atoi(state + 5);
    const int d = atoi(state + 8);
    if (m < 1 || m > 12 || d < 1 || y < 1) return false;
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
    if (d > days[m - 1] + (m == 2 && leap)) return false;
    if (year != NULL) *year = y;
    if (month != NULL) *month = m;
    if (day != NULL) *day = d;
    return true;
}

/* Days since 1970-01-01 of a proleptic Gregorian date (H. Hinnant's days_from_civil). */
static int64_t sv_days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int64_t ha_sv_days_between(int y1, int m1, int d1, int y2, int m2, int d2)
{
    return sv_days_from_civil(y2, m2, d2) - sv_days_from_civil(y1, m1, d1);
}

bool ha_sv_enum_text(const char *state, char *out, size_t out_len)
{
    if (state == NULL || out == NULL || state[0] == '\0') return false;
    const size_t n = strlen(state);
    if (n >= out_len) return false;
    bool letter = false;
    for (size_t i = 0; i < n; i++) {
        const char c = state[i];
        if (c >= 'a' && c <= 'z') letter = true;
        else if (!(c >= '0' && c <= '9') && c != '_') return false;
    }
    if (!letter) return false;
    for (size_t i = 0; i < n; i++) out[i] = state[i] == '_' ? ' ' : state[i];
    out[n] = '\0';
    out[0] = (char)toupper((unsigned char)out[0]);
    return true;
}
