/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_quote_series.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "ha/ha_quote.h"

#define DAY_S 86400

static const char *const PERIOD_CODE[HA_QUOTE_PERIOD_COUNT] = {"1d", "1w", "30d", "ytd", "1y"};

bool ha_quote_period_from_code(const char *code, ha_quote_period_t *out)
{
    for (int i = 0; code != NULL && i < HA_QUOTE_PERIOD_COUNT; i++) {
        if (strcmp(code, PERIOD_CODE[i]) == 0) {
            *out = (ha_quote_period_t)i;
            return true;
        }
    }
    return false;
}

const char *ha_quote_period_code(ha_quote_period_t period)
{
    return period >= 0 && period < HA_QUOTE_PERIOD_COUNT ? PERIOD_CODE[period] : NULL;
}

/* Days since 1970-01-01 of a proleptic Gregorian date (H. Hinnant's days_from_civil). */
static int32_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int32_t)(era * 146097 + (int)doe - 719468);
}

static void civil_from_days(int32_t z, int *y, int *m, int *d)
{
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)yoe + era * 400 + (*m <= 2);
}

/* The local calendar day of an epoch, as days since 1970-01-01. */
static bool local_day(int64_t epoch, int32_t *out)
{
    const time_t at = (time_t)epoch;
    struct tm local;
    if (localtime_r(&at, &local) == NULL) return false;
    *out = days_from_civil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    return true;
}

/* The local midnight starting a calendar day. */
static int64_t day_start(int32_t day)
{
    int y, m, d;
    civil_from_days(day, &y, &m, &d);
    struct tm local = {.tm_year = y - 1900, .tm_mon = m - 1, .tm_mday = d, .tm_isdst = -1};
    return (int64_t)mktime(&local);
}

bool ha_quote_period_start(ha_quote_period_t period, int64_t now, int64_t *out)
{
    if (now < 1700000000LL || out == NULL) return false;
    int32_t today;
    if (!local_day(now, &today)) return false;
    int y, m, d;
    civil_from_days(today, &y, &m, &d);
    switch (period) {
    case HA_QUOTE_PERIOD_1D: *out = day_start(today); return true;
    case HA_QUOTE_PERIOD_1W: *out = now - 7 * DAY_S; return true;
    case HA_QUOTE_PERIOD_30D: *out = day_start(today - 30); return true;
    case HA_QUOTE_PERIOD_YTD: *out = day_start(days_from_civil(y, 1, 1)); return true;
    case HA_QUOTE_PERIOD_1Y: *out = day_start(days_from_civil(y - 1, m, m == 2 && d == 29 ? 28 : d)); return true;
    default: return false;
    }
}

static bool parse_date(const char *text, int32_t *out)
{
    int y, m, d;
    char tail;
    if (text == NULL || strlen(text) != 10 || sscanf(text, "%4d-%2d-%2d%c", &y, &m, &d, &tail) != 3) return false;
    if (y < 1970 || y > 2200 || m < 1 || m > 12 || d < 1 || d > 31) return false;
    int cy, cm, cd;
    *out = days_from_civil(y, m, d);
    civil_from_days(*out, &cy, &cm, &cd);
    return cy == y && cm == m && cd == d; /* 2026-02-30 is no date */
}

static bool same_symbol(const char *a, const char *b)
{
    if (a == NULL || b == NULL || a[0] == '\0') return false;
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return false;
    }
    return *a == *b;
}

ha_quote_history_outcome_t ha_quote_easy_history_parse(const char *json, size_t len, const char *symbol, ha_quote_daily_t *out)
{
    if (out == NULL) return HA_QUOTE_HISTORY_INVALID;
    memset(out, 0, sizeof(*out));
    if (json == NULL || len == 0 || len > HA_QUOTE_HISTORY_MAX_BYTES) return HA_QUOTE_HISTORY_INVALID;
    cJSON *root = cJSON_ParseWithLength(json, len);
    const cJSON *reply_symbol = cJSON_GetObjectItemCaseSensitive(root, "symbol");
    const cJSON *history = cJSON_GetObjectItemCaseSensitive(root, "history");
    if (!cJSON_IsObject(root) || !cJSON_IsString(reply_symbol) || !same_symbol(reply_symbol->valuestring, symbol) ||
        !cJSON_IsArray(history)) {
        cJSON_Delete(root);
        return HA_QUOTE_HISTORY_INVALID;
    }
    /* Only the newest closes are kept: skip the rows before them. */
    const int rows = cJSON_GetArraySize(history);
    const int skip = rows > HA_QUOTE_DAILY_MAX ? rows - HA_QUOTE_DAILY_MAX : 0;
    int index = 0;
    const cJSON *row = NULL;
    cJSON_ArrayForEach(row, history) {
        if (index++ < skip) continue;
        const cJSON *date = cJSON_GetArrayItem(row, 0), *price = cJSON_GetArrayItem(row, 1);
        int32_t day;
        if (!cJSON_IsArray(row) || cJSON_GetArraySize(row) != 2 || !cJSON_IsString(date) || !parse_date(date->valuestring, &day) ||
            !cJSON_IsNumber(price) || !isfinite(price->valuedouble) || price->valuedouble <= 0.0 ||
            (out->count > 0 && day <= out->day[out->count - 1])) {
            out->rejected++;
            continue;
        }
        out->day[out->count] = day;
        out->close[out->count] = (float)price->valuedouble;
        out->count++;
    }
    cJSON_Delete(root);
    return out->count > 0 ? HA_QUOTE_HISTORY_OK : HA_QUOTE_HISTORY_EMPTY;
}

static void series_init(ha_quote_series_t *out, ha_quote_series_kind_t kind, ha_quote_period_t period, int64_t start, int64_t end)
{
    memset(out, 0, sizeof(*out));
    out->kind = kind;
    out->period = period;
    out->window_start = start;
    out->window_end = end;
}

static void series_add(ha_quote_series_t *out, int64_t t, float v)
{
    if (out->count < HA_QUOTE_SERIES_MAX) out->point[out->count++] = (ha_quote_point_t){.t = t, .v = v};
}

bool ha_quote_series_from_daily(const ha_quote_daily_t *daily, ha_quote_period_t period, int64_t now, ha_quote_series_t *out)
{
    int64_t start;
    int32_t start_day, today;
    if (daily == NULL || out == NULL || period == HA_QUOTE_PERIOD_1D || !ha_quote_period_start(period, now, &start) ||
        !local_day(start, &start_day) || !local_day(now, &today)) {
        return false;
    }
    series_init(out, HA_QUOTE_SERIES_CLOSES, period, start, now);
    out->rejected = daily->rejected;
    for (uint16_t i = 0; i < daily->count; i++) {
        if (daily->day[i] > today) break; /* a date ahead of the panel's clock */
        if (daily->day[i] < start_day) {
            out->has_reference = true;
            out->reference = (ha_quote_point_t){.t = day_start(daily->day[i]), .v = daily->close[i]};
            continue;
        }
        series_add(out, day_start(daily->day[i]), daily->close[i]);
        out->last_provisional = daily->day[i] == today;
    }
    /* Without a close before the window the source starts inside it: the period is not fully covered. */
    out->partial = !out->has_reference;
    return out->count > 0;
}

bool ha_quote_series_from_snapshot(const ha_history_snapshot_t *snapshot, ha_quote_series_t *out)
{
    if (snapshot == NULL || out == NULL || !snapshot->numeric || snapshot->end_epoch <= snapshot->start_epoch) return false;
    series_init(out, HA_QUOTE_SERIES_STATES, HA_QUOTE_PERIOD_1D, snapshot->start_epoch, snapshot->end_epoch);
    const int64_t span = snapshot->end_epoch - snapshot->start_epoch;
    size_t valid = 0;
    for (size_t i = 0; i < HA_HISTORY_BUCKETS; i++) {
        const ha_history_bucket_t *b = &snapshot->buckets[i];
        const bool value = b->count > 0 && !b->gap && isfinite(b->sum);
        series_add(out, snapshot->start_epoch + span * (int64_t)i / HA_HISTORY_BUCKETS, value ? b->sum / (float)b->count : NAN);
        valid += value;
    }
    out->partial = snapshot->shortened;
    return valid > 0;
}

bool ha_quote_series_from_week(const ha_stats_series_t *series, int64_t first_slot, ha_quote_series_t *out)
{
    if (series == NULL || out == NULL || series->slots == 0) return false;
    series_init(out, HA_QUOTE_SERIES_HOURLY_MEANS, HA_QUOTE_PERIOD_1W, first_slot,
                first_slot + (int64_t)series->slots * HA_STATS_HOUR_S);
    for (uint16_t i = 0; i < series->slots && i < HA_STATS_MAX_SLOTS; i++) {
        const bool value = (series->flags[i] & HA_STATS_FLAG_VALUE) != 0 && isfinite(series->value[i]);
        series_add(out, first_slot + (int64_t)i * HA_STATS_HOUR_S, value ? series->value[i] : NAN);
    }
    return series->valid > 0;
}

bool ha_quote_series_from_range(const ha_range_series_t *range, ha_quote_period_t period, int64_t now, ha_quote_series_t *out)
{
    int64_t start;
    if (range == NULL || out == NULL || !ha_quote_period_start(period, now, &start)) return false;
    const bool weeks = range->kind == HA_RANGE_12_MONTHS;
    series_init(out, weeks ? HA_QUOTE_SERIES_WEEKLY_MEANS : HA_QUOTE_SERIES_DAILY_MEANS, period, start, now);
    size_t valid = 0;
    int64_t first = 0;
    for (uint16_t i = 0; i < range->count && i < HA_RANGE_MAX_POINTS; i++) {
        /* a week that began before January 1 still holds days of the year: keep it when it ends after the start */
        const int64_t length = (weeks ? 7 : 1) * (int64_t)DAY_S;
        if (range->point[i].start + length <= start) continue;
        const float mean = range->point[i].mean;
        if (valid == 0 && !isfinite(mean)) continue; /* the series starts with a value */
        if (valid == 0) first = range->point[i].start;
        series_add(out, range->point[i].start, mean);
        valid += isfinite(mean);
    }
    /* Statistics exist only since Home Assistant recorded the sensor: a later first mean is a shorter history. */
    out->partial = valid == 0 || first > start + (weeks ? 7 : 1) * (int64_t)DAY_S;
    return valid > 0;
}

static bool first_value(const ha_quote_series_t *s, size_t *index)
{
    for (size_t i = 0; i < s->count; i++) {
        if (isfinite(s->point[i].v)) { *index = i; return true; }
    }
    return false;
}

static bool last_value(const ha_quote_series_t *s, size_t *index)
{
    for (size_t i = s->count; i-- > 0;) {
        if (isfinite(s->point[i].v)) { *index = i; return true; }
    }
    return false;
}

bool ha_quote_series_change(const ha_quote_series_t *series, bool has_current, double current, ha_quote_change_t *out)
{
    if (series == NULL || out == NULL) return false;
    memset(out, 0, sizeof(*out));
    size_t first, last;
    if (!first_value(series, &first) || !last_value(series, &last)) return false;
    const bool prices = series->kind == HA_QUOTE_SERIES_CLOSES || series->kind == HA_QUOTE_SERIES_STATES;
    if (series->kind == HA_QUOTE_SERIES_CLOSES && series->has_reference && isfinite(series->reference.v)) {
        out->base = series->reference.v;
        out->base_t = series->reference.t;
        out->from_reference = true;
    } else {
        if (first == last && !(prices && has_current)) return false; /* one value is no change */
        out->base = series->point[first].v;
        out->base_t = series->point[first].t;
    }
    /* Means end at the last mean: the live price is not comparable with an average. */
    out->end = prices && has_current && isfinite(current) ? current : series->point[last].v;
    if (out->base == 0.0) return false;
    out->change = out->end - out->base;
    out->change_pct = out->change / fabs(out->base) * 100.0;
    return true;
}

bool ha_quote_day_change(const ha_quote_t *quote, ha_quote_change_t *out)
{
    if (quote == NULL || out == NULL || !quote->available) return false;
    const bool flat = quote->has_traded_today && !quote->traded_today;
    const double base = flat ? quote->price : quote->prev_close;
    if ((!flat && !quote->has_prev_close) || base <= 0.0) return false;
    *out = (ha_quote_change_t){.base = base, .end = quote->price, .from_reference = !flat,
                              .change = quote->price - base, .change_pct = (quote->price - base) / base * 100.0};
    return true;
}

size_t ha_quote_day_plot(ha_quote_point_t *point, size_t count, size_t cap, int64_t now, const ha_quote_t *quote)
{
    int64_t start;
    if (point == NULL || quote == NULL || cap < 4 || !ha_quote_period_start(HA_QUOTE_PERIOD_1D, now, &start)) return count;
    if (quote->available && quote->has_traded_today && !quote->traded_today) {
        point[0] = (ha_quote_point_t){.t = start, .v = (float)quote->price};
        point[1] = (ha_quote_point_t){.t = now, .v = (float)quote->price};
        return 2;
    }
    size_t n = 0;
    bool has_value = false;
    for (size_t i = 0; i < count && n < cap - 3; i++) {
        if (point[i].t >= start && point[i].t <= now) {
            has_value |= isfinite(point[i].v);
            point[n++] = point[i];
        }
    }
    if (!has_value) n = 0;
    ha_quote_change_t change;
    if (ha_quote_day_change(quote, &change)) {
        /* Anchor the previous close at midnight and the first recorded value; the open can jump. */
        const int64_t first = n > 0 ? point[0].t : now;
        const size_t anchors = n > 0 ? 2 : 1;
        memmove(point + anchors, point, n * sizeof(*point));
        point[0] = (ha_quote_point_t){.t = start, .v = (float)change.base};
        if (anchors == 2) point[1] = (ha_quote_point_t){.t = first, .v = (float)change.base};
        n += anchors;
    }
    if (quote->available) point[n++] = (ha_quote_point_t){.t = now, .v = (float)quote->price};
    return n;
}

bool ha_quote_series_period_change(const ha_quote_series_t *series, const ha_quote_t *quote, ha_quote_change_t *out)
{
    if (series == NULL || quote == NULL || out == NULL) return false;
    if (series->period == HA_QUOTE_PERIOD_1D && ha_quote_day_change(quote, out)) return true;
    const bool ok = ha_quote_series_change(series, quote->available, quote->price, out);
    if (series->kind != HA_QUOTE_SERIES_STATES || !quote->has_prev_close || !quote->available || quote->prev_close <= 0.0) return ok;
    out->base = quote->prev_close;
    out->base_t = 0;
    out->from_reference = true;
    out->end = quote->price;
    out->change = out->end - out->base;
    out->change_pct = out->change / out->base * 100.0;
    return true;
}

size_t ha_quote_series_reduce(const ha_quote_series_t *series, ha_quote_point_t *out, size_t max)
{
    if (series == NULL || out == NULL || max < 4) return 0;
    if (series->count <= max) {
        memcpy(out, series->point, series->count * sizeof(*out));
        return series->count;
    }
    /* Keep both endpoints and the extrema of each interior interval. If an interval contains a gap,
     * keep a gap instead: joining extrema across missing records invents a continuous price history. */
    const size_t intervals = (max - 2) / 2;
    size_t n = 0;
    out[n++] = series->point[0];
    for (size_t k = 0; k < intervals; k++) {
        const size_t from = 1 + k * (series->count - 2) / intervals;
        const size_t to = 1 + (k + 1) * (series->count - 2) / intervals;
        size_t lo = to, hi = to;
        bool gap = false;
        for (size_t i = from; i < to; i++) {
            const float v = series->point[i].v;
            if (!isfinite(v)) { gap = true; continue; }
            if (lo == to || v < series->point[lo].v) lo = i;
            if (hi == to || v > series->point[hi].v) hi = i;
        }
        if (gap || lo == to) {
            out[n++] = (ha_quote_point_t){.t = series->point[from].t, .v = NAN};
            continue;
        }
        const size_t a = lo < hi ? lo : hi, b = lo < hi ? hi : lo;
        out[n++] = series->point[a];
        if (b != a) out[n++] = series->point[b];
    }
    out[n++] = series->point[series->count - 1];
    return n;
}
