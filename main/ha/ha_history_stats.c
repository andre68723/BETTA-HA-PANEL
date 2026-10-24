/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_history_stats.h"

#include <math.h>
#include <float.h>
#include <string.h>
#include <time.h>

#define STATS_MAX_ROWS (4 * HA_STATS_MAX_SLOTS)
#define STATS_MAX_META_ROWS 64

static const char *json_str(const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive((cJSON *)object, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static bool json_bool(const cJSON *object, const char *key)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive((cJSON *)object, key));
}

static int64_t floor_hour(int64_t epoch)
{
    int64_t h = epoch / HA_STATS_HOUR_S;
    if (epoch < 0 && epoch % HA_STATS_HOUR_S != 0) h--;
    return h * HA_STATS_HOUR_S;
}

ha_stats_state_class_t ha_stats_state_class_from_string(const char *value)
{
    if (value == NULL || value[0] == '\0') return HA_STATS_SC_UNKNOWN;
    if (strcmp(value, "measurement") == 0) return HA_STATS_SC_MEASUREMENT;
    if (strcmp(value, "total") == 0) return HA_STATS_SC_TOTAL;
    if (strcmp(value, "total_increasing") == 0) return HA_STATS_SC_TOTAL_INCREASING;
    return HA_STATS_SC_OTHER;
}

bool ha_stats_parse_metadata(const cJSON *result, const char *stat_id, ha_stats_meta_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    if (stat_id == NULL || !cJSON_IsArray(result) || cJSON_GetArraySize(result) > STATS_MAX_META_ROWS) return false;
    const cJSON *row;
    cJSON_ArrayForEach(row, result) {
        if (!cJSON_IsObject(row)) continue;
        const char *id = json_str(row, "statistic_id");
        if (id == NULL || strcmp(id, stat_id) != 0) continue;
        out->found = true;
        out->has_mean = json_bool(row, "has_mean");
        out->has_sum = json_bool(row, "has_sum");
        const cJSON *mean_type = cJSON_GetObjectItemCaseSensitive((cJSON *)row, "mean_type");
        out->mean_type = cJSON_IsNumber(mean_type) ? mean_type->valueint : 0;
        const char *unit = json_str(row, "statistics_unit_of_measurement");
        if (unit != NULL) {
            if (strlen(unit) >= sizeof(out->unit)) { out->found = false; return true; } /* unit we cannot compare: unusable */
            strcpy(out->unit, unit);
        }
        const char *cls = json_str(row, "unit_class");
        if (cls != NULL) {
            if (strlen(cls) >= sizeof(out->unit_class)) { out->found = false; return true; }
            strcpy(out->unit_class, cls);
        }
        return true;
    }
    return true;
}

static bool class_converts(const char *unit_class)
{
    return unit_class[0] != '\0' && strcmp(unit_class, "unitless") != 0;
}

ha_stats_eligibility_t ha_stats_classify(const ha_stats_meta_t *meta, ha_stats_state_class_t state_class,
                                         const char *display_unit)
{
    ha_stats_eligibility_t r = {.kind = HA_STATS_KIND_NONE, .why = HA_STATS_WHY_NOT_FOUND};
    if (meta == NULL || !meta->found) return r;
    if (display_unit == NULL) display_unit = "";

    switch (state_class) {
    case HA_STATS_SC_UNKNOWN:
        r.why = HA_STATS_WHY_NO_STATE_CLASS;
        return r;
    case HA_STATS_SC_MEASUREMENT:
        if (!meta->has_mean) { r.why = HA_STATS_WHY_NO_MEAN; return r; }
        if (meta->mean_type != 1) { r.why = HA_STATS_WHY_NOT_ARITHMETIC; return r; }
        if (strcmp(meta->unit, display_unit) != 0) {
            if (!class_converts(meta->unit_class) || display_unit[0] == '\0') {
                r.why = HA_STATS_WHY_UNIT_MISMATCH;
                return r;
            }
            r.convert = true;
            strcpy(r.convert_class, meta->unit_class);
        }
        r.kind = HA_STATS_KIND_MEASUREMENT;
        r.why = HA_STATS_WHY_OK;
        return r;
    case HA_STATS_SC_TOTAL:
    case HA_STATS_SC_TOTAL_INCREASING:
        if (!meta->has_sum) { r.why = HA_STATS_WHY_NO_SUM; return r; }
        if (strcmp(meta->unit_class, "volume") != 0 && strcmp(meta->unit_class, "energy") != 0) {
            r.why = HA_STATS_WHY_NOT_CONSUMPTION;
            return r;
        }
        r.kind = HA_STATS_KIND_CONSUMPTION;
        r.why = HA_STATS_WHY_OK;
        return r;
    default:
        r.why = HA_STATS_WHY_STATE_CLASS;
        return r;
    }
}

ha_stats_eligibility_t ha_stats_classify_counter(const ha_stats_meta_t *meta)
{
    ha_stats_eligibility_t r = {.kind = HA_STATS_KIND_NONE, .why = HA_STATS_WHY_NOT_FOUND};
    if (meta == NULL || !meta->found) return r;
    if (!meta->has_sum) { r.why = HA_STATS_WHY_NO_SUM; return r; }
    if (strcmp(meta->unit_class, "volume") != 0 && strcmp(meta->unit_class, "energy") != 0) {
        r.why = HA_STATS_WHY_NOT_CONSUMPTION;
        return r;
    }
    r.kind = HA_STATS_KIND_CONSUMPTION;
    r.why = HA_STATS_WHY_OK;
    return r;
}

bool ha_stats_series_add(ha_stats_series_t *dst, const ha_stats_series_t *src)
{
    if (dst == NULL || src == NULL || dst->first_slot != src->first_slot || dst->slots != src->slots) return false;
    for (size_t i = 0; i < src->slots; i++) {
        if (!(src->flags[i] & HA_STATS_FLAG_VALUE)) continue;
        if (dst->flags[i] & HA_STATS_FLAG_VALUE) dst->value[i] += src->value[i];
        else dst->value[i] = src->value[i];
        dst->flags[i] = HA_STATS_FLAG_VALUE; /* a sum has no per-hour range */
        dst->min[i] = dst->max[i] = 0.0f;
        dst->count[i] = (uint8_t)(dst->count[i] + src->count[i]);
    }
    dst->sources = (uint8_t)(dst->sources + src->sources);
    dst->rejected = (uint16_t)(dst->rejected + src->rejected);
    uint16_t valid = 0;
    for (size_t i = 0; i < dst->slots; i++) valid += (dst->flags[i] & HA_STATS_FLAG_VALUE) ? 1 : 0;
    dst->valid = valid;
    return true;
}

bool ha_stats_week_window(int64_t now, ha_stats_window_t *out)
{
    if (out == NULL || now < HA_STATS_HOUR_S * 24) return false;
    out->end_epoch = now;
    out->start_epoch = now - 168 * HA_STATS_HOUR_S;
    out->first_slot = floor_hour(out->start_epoch);
    out->slots = (size_t)((floor_hour(now) - out->first_slot) / HA_STATS_HOUR_S) + 1;
    return out->slots <= HA_STATS_MAX_SLOTS;
}

int64_t ha_stats_local_day_start(int64_t epoch, int day_offset)
{
    time_t t = (time_t)epoch;
    struct tm tm;
    if (localtime_r(&t, &tm) == NULL) return -1;
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_mday += day_offset;
    tm.tm_isdst = -1;
    time_t r = mktime(&tm);
    return r == (time_t)-1 ? -1 : (int64_t)r;
}

bool ha_stats_days_window(int64_t now, int days, ha_stats_window_t *out)
{
    if (out == NULL || days < 1 || days > HA_STATS_MAX_DAYS || now < HA_STATS_HOUR_S * 24) return false;
    int64_t first = ha_stats_local_day_start(now, -(days - 1));
    if (first < 0) return false;
    out->start_epoch = first;
    out->end_epoch = now;
    out->first_slot = floor_hour(first); /* a local midnight can sit on a half hour in some zones */
    out->slots = (size_t)((floor_hour(now) - out->first_slot) / HA_STATS_HOUR_S) + 1;
    return out->slots <= HA_STATS_MAX_SLOTS;
}

static bool row_number(const cJSON *row, const char *key, double *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive((cJSON *)row, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) return false;
    *out = item->valuedouble;
    return true;
}

static bool row_float(const cJSON *row, const char *key, double *out)
{
    return row_number(row, key, out) && fabs(*out) <= FLT_MAX;
}

/* HA uses integral epoch milliseconds. Bound before every integer cast, including a malformed end timestamp. */
static bool row_time(const cJSON *row, const char *key, double *out)
{
    return row_number(row, key, out) && *out >= 0 && *out <= 253402300799000.0 && trunc(*out) == *out;
}

ha_stats_outcome_t ha_stats_parse_hourly(const cJSON *result, const char *stat_id, const ha_stats_window_t *window,
                                         ha_stats_value_t which, bool want_range, ha_stats_series_t *out)
{
    if (out == NULL || window == NULL || window->slots == 0 || window->slots > HA_STATS_MAX_SLOTS) {
        return HA_STATS_OUTCOME_INVALID;
    }
    memset(out, 0, sizeof(*out));
    out->first_slot = window->first_slot;
    out->slots = (uint16_t)window->slots;
    out->sources = 1;
    if (stat_id == NULL || !cJSON_IsObject(result)) return HA_STATS_OUTCOME_INVALID;

    const cJSON *rows = cJSON_GetObjectItemCaseSensitive((cJSON *)result, stat_id);
    if (rows == NULL || cJSON_IsNull(rows)) return HA_STATS_OUTCOME_EMPTY;
    if (!cJSON_IsArray(rows) || cJSON_GetArraySize(rows) > STATS_MAX_ROWS) return HA_STATS_OUTCOME_INVALID;

    const char *value_key = which == HA_STATS_VALUE_MEAN ? "mean" : "change";
    const cJSON *row;
    cJSON_ArrayForEach(row, rows) {
        double start_ms, v;
        if (!cJSON_IsObject(row) || !row_time(row, "start", &start_ms)) { out->rejected++; continue; }
        const int64_t start_s_x = (int64_t)(start_ms / 1000.0);
        if ((int64_t)start_ms % 1000 != 0 || start_s_x % HA_STATS_HOUR_S != 0) { out->rejected++; continue; }
        double end_ms;
        const cJSON *end = cJSON_GetObjectItemCaseSensitive(row, "end");
        if (end && (!row_time(row, "end", &end_ms) || (int64_t)end_ms != (int64_t)start_ms + (int64_t)HA_STATS_HOUR_S * 1000)) {
            out->rejected++;
            continue;
        }
        const int64_t offset = start_s_x - window->first_slot;
        if (offset < 0 || offset / HA_STATS_HOUR_S >= (int64_t)window->slots) { out->rejected++; continue; }
        const size_t slot = (size_t)(offset / HA_STATS_HOUR_S);
        const cJSON *value_item = cJSON_GetObjectItemCaseSensitive((cJSON *)row, value_key);
        if (value_item == NULL || cJSON_IsNull(value_item)) continue; /* HA had no value for this hour: a gap */
        if (!row_float(row, value_key, &v)) { out->rejected++; continue; }
        if (out->flags[slot] != 0) { out->rejected++; continue; } /* duplicate: the first row wins */
        out->value[slot] = (float)v;
        out->flags[slot] = HA_STATS_FLAG_VALUE;
        out->count[slot] = 1;
        out->valid++;
        double lo, hi;
        if (want_range && row_float(row, "min", &lo) && row_float(row, "max", &hi) && lo <= hi) {
            out->min[slot] = (float)lo;
            out->max[slot] = (float)hi;
            out->flags[slot] |= HA_STATS_FLAG_RANGE;
        }
    }
    return out->valid > 0 ? HA_STATS_OUTCOME_OK : HA_STATS_OUTCOME_EMPTY;
}

bool ha_stats_sum_days(const ha_stats_series_t *series, int64_t now, int days, ha_stats_days_t *out)
{
    if (series == NULL || out == NULL || days < 1 || days > HA_STATS_MAX_DAYS) return false;
    memset(out, 0, sizeof(*out));
    const int64_t base = ha_stats_local_day_start(now, -(days - 1));
    if (base < 0) return false;
    const int64_t now_hour = floor_hour(now);
    for (int d = 0; d < days; d++) {
        const int64_t start = ha_stats_local_day_start(base, d);
        const int64_t end = ha_stats_local_day_start(base, d + 1);
        if (start < 0 || end <= start) return false;
        ha_stats_day_t *day = &out->day[d];
        day->start = start;
        day->today = d == days - 1;
        const int64_t last = end < now_hour ? end : now_hour; /* completed hours only */
        for (int64_t slot_epoch = floor_hour(start); slot_epoch < last; slot_epoch += HA_STATS_HOUR_S) {
            if (slot_epoch < start) continue;
            if (day->hours < 255) day->hours++;
            const int64_t offset = slot_epoch - series->first_slot;
            if (offset < 0) continue;
            const size_t slot = (size_t)(offset / HA_STATS_HOUR_S);
            if (slot >= series->slots || !(series->flags[slot] & HA_STATS_FLAG_VALUE)) continue;
            day->total += series->value[slot];
            if (day->valid < 255) day->valid++;
            if (series->count[slot] >= series->sources && day->complete < 255) day->complete++;
        }
        out->count++;
    }
    return true;
}

bool ha_stats_range_window(int64_t now, ha_range_kind_t kind, ha_stats_window_t *out, const char **period)
{
    if (out == NULL || now < 0) return false;
    memset(out, 0, sizeof(*out));
    const bool days = kind == HA_RANGE_30_DAYS;
    const int64_t start = ha_stats_local_day_start(now, days ? -29 : -364);
    if (start < 0) return false;
    out->start_epoch = start;
    out->end_epoch = now;
    out->first_slot = start;
    if (period != NULL) *period = days ? "day" : "week";
    return true;
}

ha_stats_outcome_t ha_stats_parse_range(const cJSON *result, const char *stat_id, const ha_stats_window_t *window,
                                        ha_range_kind_t kind, ha_range_series_t *out)
{
    if (out == NULL || window == NULL) return HA_STATS_OUTCOME_INVALID;
    memset(out, 0, sizeof(*out));
    out->kind = kind;
    if (stat_id == NULL || !cJSON_IsObject(result)) return HA_STATS_OUTCOME_INVALID;
    const cJSON *rows = cJSON_GetObjectItemCaseSensitive((cJSON *)result, stat_id);
    if (rows == NULL || cJSON_IsNull(rows)) return HA_STATS_OUTCOME_EMPTY;
    if (!cJSON_IsArray(rows) || cJSON_GetArraySize(rows) > HA_RANGE_MAX_POINTS) return HA_STATS_OUTCOME_INVALID;
    /* a week row may begin up to a week before the window (its Monday), a day row only on the window's first midnight */
    const int64_t lead = kind == HA_RANGE_12_MONTHS ? 7 * 86400 : 0;
    const cJSON *row;
    cJSON_ArrayForEach(row, rows) {
        double start_ms, mean;
        if (!cJSON_IsObject(row) || !row_time(row, "start", &start_ms)) { out->rejected++; continue; }
        const cJSON *mean_item = cJSON_GetObjectItemCaseSensitive((cJSON *)row, "mean");
        if (mean_item == NULL || cJSON_IsNull(mean_item)) continue; /* no value for the period: a gap */
        if (!row_float(row, "mean", &mean)) { out->rejected++; continue; }
        const int64_t start = (int64_t)(start_ms / 1000.0);
        if (start < window->start_epoch - lead || start >= window->end_epoch ||
            (out->count > 0 && start <= out->point[out->count - 1].start)) {
            out->rejected++;
            continue;
        }
        if (out->count > 0) {
            const int days = kind == HA_RANGE_12_MONTHS ? 7 : 1;
            const int64_t first_gap = ha_stats_local_day_start(out->point[out->count - 1].start, days);
            int64_t next = first_gap;
            size_t gaps = 0;
            /* Calendar stepping preserves the 23/25-hour days; check the grid before changing the series. */
            while (next > 0 && next < start && gaps < HA_RANGE_MAX_POINTS) {
                gaps++;
                const int64_t following = ha_stats_local_day_start(next, days);
                if (following <= next) return HA_STATS_OUTCOME_INVALID;
                next = following;
            }
            if (next != start) { out->rejected++; continue; }
            if (out->count + gaps >= HA_RANGE_MAX_POINTS) return HA_STATS_OUTCOME_INVALID;
            next = first_gap;
            for (size_t i = 0; i < gaps; i++) {
                out->point[out->count++] = (ha_range_point_t){.start = next, .mean = NAN, .min = NAN, .max = NAN};
                next = ha_stats_local_day_start(next, days);
            }
        }
        if (out->count >= HA_RANGE_MAX_POINTS) return HA_STATS_OUTCOME_INVALID;
        ha_range_point_t *p = &out->point[out->count++];
        p->start = start;
        p->mean = (float)mean;
        double lo, hi;
        p->has_range = row_float(row, "min", &lo) && row_float(row, "max", &hi) && lo <= hi;
        p->min = p->has_range ? (float)lo : p->mean;
        p->max = p->has_range ? (float)hi : p->mean;
    }
    return out->count > 0 ? HA_STATS_OUTCOME_OK : HA_STATS_OUTCOME_EMPTY;
}
