/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * See ha_price_series.h.
 */
#include "ha/ha_price_series.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ha/ha_history_stats.h"
#include "util/iso_time.h"

#define PRICE_LIMIT_EUR 1000.0 /* a larger magnitude is a unit mistake, not a price */

/* The shapes of a price list, recognised by their keys (see the header):
 *   data:                  items {start_time | time_ts, price_per_kwh | da_cons}        ha_epex_spot, Day Ahead Optimizer, Tibber/Nord Pool templates
 *   raw_today/raw_tomorrow items {start, value}                                        Nord Pool custom component, Energy Price Forecast */
typedef struct {
    const cJSON *lists[2];
    int count;
    bool raw; /* raw_today/raw_tomorrow items: start and value */
    const char *price_key;
} price_source_t;

static bool item_has_time(const cJSON *item)
{
    return cJSON_GetObjectItemCaseSensitive((cJSON *)item, "time_ts") != NULL ||
           cJSON_GetObjectItemCaseSensitive((cJSON *)item, "start_time") != NULL;
}

static const char *item_price_key(const cJSON *item)
{
    static const char *const keys[] = {"price_per_kwh", "da_cons"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (cJSON_GetObjectItemCaseSensitive((cJSON *)item, keys[i]) != NULL) return keys[i];
    }
    return NULL;
}

static const cJSON *first_item(const cJSON *list)
{
    return cJSON_IsArray(list) && cJSON_GetArraySize(list) > 0 ? cJSON_GetArrayItem(list, 0) : NULL;
}

static bool find_source(const cJSON *attrs, price_source_t *src)
{
    memset(src, 0, sizeof(*src));
    const cJSON *data = cJSON_GetObjectItemCaseSensitive((cJSON *)attrs, "data");
    if (cJSON_IsArray(data) && cJSON_GetArraySize(data) >= 2) {
        const cJSON *first = first_item(data);
        if (cJSON_IsObject(first) && item_has_time(first) && item_price_key(first) != NULL) {
            src->lists[0] = data;
            src->count = 1;
            src->price_key = item_price_key(first);
            return true;
        }
    }
    const cJSON *today = cJSON_GetObjectItemCaseSensitive((cJSON *)attrs, "raw_today");
    const cJSON *tomorrow = cJSON_GetObjectItemCaseSensitive((cJSON *)attrs, "raw_tomorrow");
    int total = 0;
    const cJSON *first = NULL;
    if (cJSON_IsArray(today)) {
        src->lists[src->count++] = today;
        total += cJSON_GetArraySize(today);
        first = first_item(today);
    }
    if (cJSON_IsArray(tomorrow)) {
        src->lists[src->count++] = tomorrow;
        total += cJSON_GetArraySize(tomorrow);
        if (first == NULL) first = first_item(tomorrow);
    }
    if (total >= 2 && cJSON_IsObject(first) && cJSON_GetObjectItemCaseSensitive((cJSON *)first, "start") != NULL &&
        cJSON_GetObjectItemCaseSensitive((cJSON *)first, "value") != NULL) {
        src->raw = true;
        src->price_key = "value";
        return true;
    }
    memset(src, 0, sizeof(*src));
    return false;
}

bool ha_price_series_detect(const cJSON *attrs)
{
    price_source_t src;
    return attrs != NULL && find_source(attrs, &src);
}

static bool is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

static bool digits(const char *s, int n)
{
    for (int i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

static int num(const char *s, int n)
{
    int v = 0;
    for (int i = 0; i < n; i++) v = v * 10 + (s[i] - '0');
    return v;
}

bool ha_price_parse_iso_strict(const char *text, int64_t *epoch_s)
{
    if (text == NULL || epoch_s == NULL || strlen(text) < 20) return false;
    if (!digits(text, 4) || text[4] != '-' || !digits(text + 5, 2) || text[7] != '-' || !digits(text + 8, 2) ||
        text[10] != 'T' || !digits(text + 11, 2) || text[13] != ':' || !digits(text + 14, 2) || text[16] != ':' ||
        !digits(text + 17, 2)) {
        return false;
    }
    const int year = num(text, 4), month = num(text + 5, 2), day = num(text + 8, 2);
    const int hour = num(text + 11, 2), minute = num(text + 14, 2), second = num(text + 17, 2);
    static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (year < 1970 || month < 1 || month > 12 || day < 1 || hour > 23 || minute > 59 || second > 59) return false;
    if (day > dim[month - 1] + (month == 2 && is_leap(year) ? 1 : 0)) return false;
    const char *p = text + 19;
    if (*p == '.') {
        p++;
        int n = 0;
        while (*p >= '0' && *p <= '9') { p++; n++; }
        if (n < 1 || n > 9) return false;
    }
    if (*p == 'Z') {
        if (p[1] != '\0') return false;
    } else if (*p == '+' || *p == '-') {
        if (!digits(p + 1, 2) || p[3] != ':' || !digits(p + 4, 2) || p[6] != '\0') return false;
        if (num(p + 1, 2) > 14 || num(p + 4, 2) > 59) return false;
    } else {
        return false; /* a zone is mandatory */
    }
    return iso_time_parse(text, epoch_s);
}

/* One item's start. False when it is missing or not exact (epoch milliseconds that are not whole seconds are refused). */
static bool item_start(const cJSON *item, bool raw, int64_t *t)
{
    if (!raw) {
        const cJSON *ts = cJSON_GetObjectItemCaseSensitive((cJSON *)item, "time_ts");
        if (cJSON_IsNumber(ts)) {
            const double v = ts->valuedouble;
            if (!isfinite(v) || v < 0.0 || v > 4.0e12) return false;
            if (trunc(v) != v) return false;
            const long long ms = (long long)v;
            if (ms % 1000 != 0) return false;
            *t = ms / 1000;
            return true;
        }
    }
    const cJSON *st = cJSON_GetObjectItemCaseSensitive((cJSON *)item, raw ? "start" : "start_time");
    return cJSON_IsString(st) && st->valuestring != NULL && ha_price_parse_iso_strict(st->valuestring, t);
}

/* Explicit price_per_kwh / da_cons fields are EUR/kWh by contract. Raw values use the sensor's unit. */
static bool source_price_factor(const cJSON *attrs, bool raw, double *factor)
{
    *factor = 1.0;
    if (!raw) return true;
    const cJSON *currency = cJSON_GetObjectItemCaseSensitive((cJSON *)attrs, "currency");
    if (currency != NULL && (!cJSON_IsString(currency) || strcmp(currency->valuestring, "EUR") != 0)) return false;
    const cJSON *unit = cJSON_GetObjectItemCaseSensitive((cJSON *)attrs, "unit_of_measurement");
    if (!cJSON_IsString(unit)) return false;
    static const struct { const char *unit; double factor; } units[] = {
        {"EUR/kWh", 1.0}, {"€/kWh", 1.0},
        {"EUR/MWh", 0.001}, {"€/MWh", 0.001},
        {"EUR/Wh", 1000.0}, {"€/Wh", 1000.0},
        {"c/kWh", 0.01}, {"ct/kWh", 0.01}, {"¢/kWh", 0.01},
        {"c/MWh", 0.00001}, {"ct/MWh", 0.00001}, {"¢/MWh", 0.00001},
        {"c/Wh", 10.0}, {"ct/Wh", 10.0}, {"¢/Wh", 10.0},
    };
    for (size_t i = 0; i < sizeof(units) / sizeof(units[0]); i++) {
        if (strcmp(unit->valuestring, units[i].unit) == 0) {
            *factor = units[i].factor;
            return true;
        }
    }
    return false;
}

/* End times, when supplied, establish the duration even if whole intervening intervals are absent. */
static bool item_duration(const cJSON *item, bool raw, int64_t start, int64_t *duration)
{
    const cJSON *end = cJSON_GetObjectItemCaseSensitive((cJSON *)item, raw ? "end" : "end_time");
    *duration = 0;
    if (end == NULL) return true;
    int64_t at;
    if (!cJSON_IsString(end) || !ha_price_parse_iso_strict(end->valuestring, &at) || at <= start) return false;
    *duration = at - start;
    return *duration >= HA_PRICE_MIN_INTERVAL_S && *duration <= HA_PRICE_MAX_INTERVAL_S && *duration % 60 == 0;
}

/* Scaled price; JSON null is a gap. False for anything else that is not a usable number. */
static bool item_price(const cJSON *item, const char *key, double factor, int32_t *scaled)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive((cJSON *)item, key);
    if (cJSON_IsNull(v)) {
        *scaled = HA_PRICE_MISSING;
        return true;
    }
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || fabs(v->valuedouble * factor) > PRICE_LIMIT_EUR) return false;
    *scaled = (int32_t)llround(v->valuedouble * factor * HA_PRICE_SCALE);
    return true;
}

static int64_t gcd64(int64_t a, int64_t b)
{
    while (b != 0) {
        const int64_t r = a % b;
        a = b;
        b = r;
    }
    return a;
}

bool ha_price_series_from_attrs(const cJSON *attrs, ha_price_series_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    price_source_t src;
    if (attrs == NULL || !find_source(attrs, &src)) return false;
    double factor;
    if (!source_price_factor(attrs, src.raw, &factor)) return false;
    int n = 0;
    for (int l = 0; l < src.count; l++) n += cJSON_GetArraySize(src.lists[l]);
    if (n < 2 || n > HA_PRICE_MAX_ITEMS) return false;

    int64_t t0 = INT64_MAX, duration = 0;
    for (int l = 0; l < src.count; l++) {
        const cJSON *item;
        cJSON_ArrayForEach(item, src.lists[l]) {
            int64_t t, item_step;
            if (!cJSON_IsObject(item) || !item_start(item, src.raw, &t) || !item_duration(item, src.raw, t, &item_step)) return false;
            if (item_step != 0) {
                if (duration != 0 && duration != item_step) return false;
                duration = item_step;
            }
            if (t < t0) t0 = t;
        }
    }
    int64_t g = 0;
    for (int l = 0; l < src.count; l++) {
        const cJSON *item;
        cJSON_ArrayForEach(item, src.lists[l]) {
            int64_t t;
            (void)item_start(item, src.raw, &t);
            g = gcd64(g, t - t0);
        }
    }
    if (duration != 0) {
        if (g % duration != 0) return false;
        g = duration;
    }
    if (g < HA_PRICE_MIN_INTERVAL_S || g > HA_PRICE_MAX_INTERVAL_S || g % 60 != 0) return false;

    for (int i = 0; i < HA_PRICE_MAX_SLOTS; i++) out->price[i] = HA_PRICE_MISSING;
    int slots = 0;
    for (int l = 0; l < src.count; l++) {
        const cJSON *item;
        cJSON_ArrayForEach(item, src.lists[l]) {
            int64_t t;
            int32_t p;
            (void)item_start(item, src.raw, &t);
            if (!item_price(item, src.price_key, factor, &p)) return false;
            const int64_t idx = (t - t0) / g;
            if (idx >= HA_PRICE_MAX_SLOTS) return false;
            if (out->price[idx] != HA_PRICE_MISSING && p != HA_PRICE_MISSING && out->price[idx] != p) return false;
            if (p != HA_PRICE_MISSING) out->price[idx] = p;
            if (idx + 1 > slots) slots = (int)idx + 1;
        }
    }
    out->start_s = t0;
    out->interval_s = (int32_t)g;
    out->count = (uint16_t)slots;
    return true;
}

bool ha_price_series_to_json(const ha_price_series_t *s, char *out, size_t cap)
{
    if (s == NULL || out == NULL || cap < 2 || s->count == 0) return false;
    size_t used = 0;
    int w = snprintf(out, cap, "{\"pf\":{\"v\":2,\"s\":%lld,\"d\":%d,\"p\":[", (long long)s->start_s, (int)s->interval_s);
    if (w < 0 || (size_t)w >= cap) return false;
    used = (size_t)w;
    int64_t prev = 0;
    for (uint16_t i = 0; i < s->count; i++) {
        if (s->price[i] == HA_PRICE_MISSING) {
            w = snprintf(out + used, cap - used, "%snull", i ? "," : "");
        } else {
            w = snprintf(out + used, cap - used, "%s%lld", i ? "," : "", (long long)(s->price[i] - prev));
            prev = s->price[i];
        }
        if (w < 0 || (size_t)w >= cap - used) return false;
        used += (size_t)w;
    }
    w = snprintf(out + used, cap - used, "]}}");
    return w >= 0 && (size_t)w < cap - used;
}

bool ha_price_series_from_json(const char *attributes_json, ha_price_series_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    if (attributes_json == NULL || attributes_json[0] == '\0') return false;
    cJSON *root = cJSON_Parse(attributes_json);
    bool ok = false;
    if (root != NULL) {
        const cJSON *pf = cJSON_GetObjectItemCaseSensitive(root, "pf");
        const cJSON *v = cJSON_GetObjectItemCaseSensitive((cJSON *)pf, "v");
        const cJSON *s = cJSON_GetObjectItemCaseSensitive((cJSON *)pf, "s");
        const cJSON *d = cJSON_GetObjectItemCaseSensitive((cJSON *)pf, "d");
        const cJSON *p = cJSON_GetObjectItemCaseSensitive((cJSON *)pf, "p");
        if (cJSON_IsObject(pf) && cJSON_IsNumber(v) && v->valueint == 2 && cJSON_IsNumber(s) && cJSON_IsNumber(d) && cJSON_IsArray(p) &&
            isfinite(s->valuedouble) && s->valuedouble >= 0 && s->valuedouble <= 253402300799.0 &&
            trunc(s->valuedouble) == s->valuedouble &&
            d->valueint >= HA_PRICE_MIN_INTERVAL_S && d->valueint <= HA_PRICE_MAX_INTERVAL_S &&
            d->valuedouble == d->valueint && d->valueint % 60 == 0 && v->valuedouble == 2) {
            const int n = cJSON_GetArraySize(p);
            if (n > 0 && n <= HA_PRICE_MAX_SLOTS) {
                int i = 0;
                bool good = true;
                int64_t prev = 0;
                const cJSON *e;
                cJSON_ArrayForEach(e, p) {
                    if (cJSON_IsNull(e)) {
                        out->price[i] = HA_PRICE_MISSING;
                    } else if (cJSON_IsNumber(e) && isfinite(e->valuedouble) && fabs(e->valuedouble) < 2.0e9 &&
                               trunc(e->valuedouble) == e->valuedouble) {
                        prev += (int64_t)e->valuedouble;
                        if (llabs(prev) > (int64_t)(PRICE_LIMIT_EUR * HA_PRICE_SCALE)) {
                            good = false;
                            break;
                        }
                        out->price[i] = (int32_t)prev;
                    } else {
                        good = false;
                        break;
                    }
                    i++;
                }
                if (good) {
                    out->start_s = (int64_t)s->valuedouble;
                    out->interval_s = d->valueint;
                    out->count = (uint16_t)n;
                    ok = true;
                }
            }
        }
        cJSON_Delete(root);
    }
    if (!ok) memset(out, 0, sizeof(*out));
    return ok;
}

bool ha_price_series_to_hourly(const ha_price_series_t *s, ha_price_series_t *out)
{
    if (s == NULL || out == NULL || s->count == 0 || s->interval_s >= 3600 || 3600 % s->interval_s != 0 || s->start_s % 3600 != 0) return false;
    const int group = 3600 / s->interval_s;
    memset(out, 0, sizeof(*out));
    out->start_s = s->start_s;
    out->interval_s = 3600;
    out->count = (uint16_t)(s->count / group);
    for (int h = 0; h < out->count; h++) {
        int64_t sum = 0;
        bool whole = true;
        for (int k = 0; k < group && whole; k++) {
            const int32_t v = s->price[h * group + k];
            if (v == HA_PRICE_MISSING) whole = false;
            else sum += v;
        }
        out->price[h] = whole ? (int32_t)llround((double)sum / group) : HA_PRICE_MISSING;
    }
    for (int h = out->count; h < HA_PRICE_MAX_SLOTS; h++) out->price[h] = HA_PRICE_MISSING;
    return out->count > 0;
}

int ha_price_slot_at(const ha_price_series_t *s, int64_t t)
{
    if (s == NULL || s->count == 0 || t < s->start_s) return -1;
    const int64_t idx = (t - s->start_s) / s->interval_s;
    return idx < s->count ? (int)idx : -1;
}

double ha_price_value(const ha_price_series_t *s, int slot)
{
    if (s == NULL || slot < 0 || slot >= s->count || s->price[slot] == HA_PRICE_MISSING) return NAN;
    return (double)s->price[slot] / HA_PRICE_SCALE;
}

int64_t ha_price_slot_start(const ha_price_series_t *s, int slot)
{
    return s->start_s + (int64_t)slot * s->interval_s;
}

void ha_price_slot_range(const ha_price_series_t *s, int64_t from_s, int64_t to_s, int *first, int *count)
{
    int f = -1, c = 0;
    if (s != NULL) {
        for (int i = 0; i < s->count; i++) {
            const int64_t t = ha_price_slot_start(s, i);
            if (t >= from_s && t < to_s) {
                if (f < 0) f = i;
                c++;
            }
        }
    }
    *first = f < 0 ? 0 : f;
    *count = c;
}

ha_price_stats_t ha_price_stats(const ha_price_series_t *s, int first, int count)
{
    ha_price_stats_t st = {0};
    double sum = 0.0;
    for (int i = first; s != NULL && i < first + count && i < s->count; i++) {
        const double v = ha_price_value(s, i);
        if (isnan(v)) continue;
        if (st.priced == 0 || v < st.min) st.min = v;
        if (st.priced == 0 || v > st.max) st.max = v;
        sum += v;
        st.priced++;
    }
    if (st.priced > 0) {
        st.valid = true;
        st.avg = sum / st.priced;
    }
    return st;
}

/* The price card compares prices rounded to 0.001 EUR/kWh when it looks for cheap intervals. */
static int32_t price_milli(int32_t scaled)
{
    return (int32_t)floor(scaled / 100.0 + 0.5);
}

int ha_price_cent(int32_t scaled)
{
    /* Math.round(Math.round(p * 100 * 10) / 10): tenths of a cent first, then whole cents (half up, like JavaScript) */
    const double tenths = floor(scaled / 100.0 + 0.5);
    return (int)floor(tenths / 10.0 + 0.5);
}

double ha_price_average_cent(const ha_price_series_t *s, int first, int count)
{
    double sum = 0.0;
    int n = 0;
    for (int i = first; s != NULL && i < first + count && i < s->count; i++) {
        if (s->price[i] == HA_PRICE_MISSING) continue;
        sum += floor(s->price[i] / 100.0 + 0.5) / 10.0; /* each price in cent, rounded to a tenth */
        n++;
    }
    return n > 0 ? floor(sum / n + 0.5) : NAN;
}

static int cmp_i32(const void *a, const void *b)
{
    const int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    return (x > y) - (x < y);
}

size_t ha_price_cheap_windows(const ha_price_series_t *s, int64_t now_s, ha_price_window_t *out, size_t max)
{
    size_t found = 0;
    if (s == NULL || s->count == 0 || out == NULL) return 0;
    const int64_t series_end = s->start_s + (int64_t)s->count * s->interval_s;
    for (int day = 0; day < HA_PRICE_MAX_SLOTS; day++) {
        const int64_t ds = ha_stats_local_day_start(s->start_s, day);
        const int64_t de = ha_stats_local_day_start(s->start_s, day + 1);
        if (ds < 0 || de < 0 || ds >= series_end) break;
        int first, count;
        ha_price_slot_range(s, ds, de, &first, &count);
        int32_t sorted[HA_PRICE_MAX_SLOTS];
        int n = 0;
        for (int i = first; i < first + count; i++) {
            if (s->price[i] != HA_PRICE_MISSING) sorted[n++] = price_milli(s->price[i]);
        }
        if (n == 0) continue;
        qsort(sorted, (size_t)n, sizeof(sorted[0]), cmp_i32);
        const int32_t threshold = sorted[(int)floor(0.25 * (n - 1))];
        int run = -1;
        for (int i = first; i <= first + count; i++) {
            const bool cheap = i < first + count && s->price[i] != HA_PRICE_MISSING && price_milli(s->price[i]) <= threshold;
            if (cheap && run < 0) run = i;
            if (!cheap && run >= 0) {
                const int64_t ws = ha_price_slot_start(s, run);
                const int64_t we = ha_price_slot_start(s, i);
                if (we > now_s && we - ws >= HA_PRICE_CHEAP_MIN_S && found < max) {
                    out[found].start_s = ws;
                    out[found].end_s = we;
                    found++;
                }
                run = -1;
            }
        }
    }
    return found;
}
