/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
/* Recorder statistics parser: reply shapes taken from a real Home Assistant 2026.9.4 (metadata with list result,
 * hourly rows with start/end in epoch milliseconds), eligibility, windows, gaps and local-day sums across DST. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ha/ha_history_stats.h"

static int checks;
static int failed;

#define CHECK(cond)                                                      \
    do {                                                                 \
        checks++;                                                        \
        if (!(cond)) {                                                   \
            failed++;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                \
    } while (0)

#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 1e-3)

static const char *META =
    "[{\"statistic_id\":\"sensor.temp\",\"display_unit_of_measurement\":\"\\u00b0C\",\"has_mean\":true,\"mean_type\":1,"
    "\"has_sum\":false,\"name\":null,\"source\":\"recorder\",\"statistics_unit_of_measurement\":\"\\u00b0C\",\"unit_class\":\"temperature\"},"
    "{\"statistic_id\":\"sensor.hum\",\"has_mean\":true,\"mean_type\":1,\"has_sum\":false,"
    "\"statistics_unit_of_measurement\":\"%\",\"unit_class\":\"unitless\"},"
    "{\"statistic_id\":\"sensor.angle\",\"has_mean\":true,\"mean_type\":2,\"has_sum\":false,"
    "\"statistics_unit_of_measurement\":\"\\u00b0\",\"unit_class\":null},"
    "{\"statistic_id\":\"sensor.water\",\"has_mean\":false,\"mean_type\":0,\"has_sum\":true,"
    "\"statistics_unit_of_measurement\":\"L\",\"unit_class\":\"volume\"},"
    "{\"statistic_id\":\"sensor.gas\",\"has_mean\":false,\"mean_type\":0,\"has_sum\":true,"
    "\"statistics_unit_of_measurement\":\"m\\u00b3\",\"unit_class\":\"volume\"},"
    "{\"statistic_id\":\"sensor.kwh\",\"has_mean\":false,\"mean_type\":0,\"has_sum\":true,"
    "\"statistics_unit_of_measurement\":\"kWh\",\"unit_class\":\"energy\"},"
    "{\"statistic_id\":\"sensor.count\",\"has_mean\":false,\"mean_type\":0,\"has_sum\":true,"
    "\"statistics_unit_of_measurement\":\"items\",\"unit_class\":null},"
    "{\"statistic_id\":\"sensor.power\",\"has_mean\":true,\"mean_type\":1,\"has_sum\":false,"
    "\"statistics_unit_of_measurement\":\"W\",\"unit_class\":\"power\"}]";

static ha_stats_meta_t meta_of(const char *id)
{
    cJSON *root = cJSON_Parse(META);
    ha_stats_meta_t m;
    CHECK(ha_stats_parse_metadata(root, id, &m));
    cJSON_Delete(root);
    return m;
}

static void test_metadata_and_eligibility(void)
{
    ha_stats_meta_t m = meta_of("sensor.temp");
    CHECK(m.found && m.has_mean && !m.has_sum && m.mean_type == 1);
    CHECK(strcmp(m.unit_class, "temperature") == 0);
    CHECK(!meta_of("sensor.missing").found);

    ha_stats_eligibility_t e = ha_stats_classify(&m, HA_STATS_SC_MEASUREMENT, "\xC2\xB0" "C");
    CHECK(e.kind == HA_STATS_KIND_MEASUREMENT && !e.convert);
    e = ha_stats_classify(&m, HA_STATS_SC_MEASUREMENT, "\xC2\xB0" "F");
    CHECK(e.kind == HA_STATS_KIND_MEASUREMENT && e.convert && strcmp(e.convert_class, "temperature") == 0);
    e = ha_stats_classify(&m, HA_STATS_SC_UNKNOWN, "\xC2\xB0" "C");
    CHECK(e.kind == HA_STATS_KIND_NONE && e.why == HA_STATS_WHY_NO_STATE_CLASS);
    e = ha_stats_classify(&m, HA_STATS_SC_OTHER, "\xC2\xB0" "C");
    CHECK(e.kind == HA_STATS_KIND_NONE && e.why == HA_STATS_WHY_STATE_CLASS);

    /* percent has the unit class "unitless": equal units qualify, a different unit cannot convert */
    m = meta_of("sensor.hum");
    CHECK(ha_stats_classify(&m, HA_STATS_SC_MEASUREMENT, "%").kind == HA_STATS_KIND_MEASUREMENT);
    e = ha_stats_classify(&m, HA_STATS_SC_MEASUREMENT, "ppm");
    CHECK(e.kind == HA_STATS_KIND_NONE && e.why == HA_STATS_WHY_UNIT_MISMATCH);

    m = meta_of("sensor.angle"); /* circular mean */
    e = ha_stats_classify(&m, HA_STATS_SC_MEASUREMENT, "\xC2\xB0");
    CHECK(e.kind == HA_STATS_KIND_NONE && e.why == HA_STATS_WHY_NOT_ARITHMETIC);

    /* a counter with old mean metadata is not a measurement: has_mean decides, and a missing state class never qualifies */
    m = meta_of("sensor.kwh");
    e = ha_stats_classify(&m, HA_STATS_SC_MEASUREMENT, "kWh");
    CHECK(e.kind == HA_STATS_KIND_NONE && e.why == HA_STATS_WHY_NO_MEAN);
    CHECK(ha_stats_classify(&m, HA_STATS_SC_TOTAL_INCREASING, "kWh").kind == HA_STATS_KIND_CONSUMPTION);
    CHECK(ha_stats_classify(&m, HA_STATS_SC_TOTAL, "kWh").kind == HA_STATS_KIND_CONSUMPTION);
    CHECK(ha_stats_classify(&m, HA_STATS_SC_UNKNOWN, "kWh").kind == HA_STATS_KIND_NONE);
    m = meta_of("sensor.water");
    CHECK(ha_stats_classify(&m, HA_STATS_SC_TOTAL_INCREASING, "L").kind == HA_STATS_KIND_CONSUMPTION);
    m = meta_of("sensor.gas");
    CHECK(ha_stats_classify(&m, HA_STATS_SC_TOTAL_INCREASING, "m\xC2\xB3").kind == HA_STATS_KIND_CONSUMPTION);
    m = meta_of("sensor.count");
    e = ha_stats_classify(&m, HA_STATS_SC_TOTAL_INCREASING, "items");
    CHECK(e.kind == HA_STATS_KIND_NONE && e.why == HA_STATS_WHY_NOT_CONSUMPTION);
    /* a power sensor is no consumption counter, a counter no mean line */
    m = meta_of("sensor.power");
    CHECK(ha_stats_classify(&m, HA_STATS_SC_TOTAL, "W").kind == HA_STATS_KIND_NONE);
    CHECK(ha_stats_classify(&m, HA_STATS_SC_MEASUREMENT, "W").kind == HA_STATS_KIND_MEASUREMENT);

    ha_stats_meta_t none;
    cJSON *bad = cJSON_Parse("{\"x\":1}");
    CHECK(!ha_stats_parse_metadata(bad, "sensor.temp", &none));
    cJSON_Delete(bad);
    CHECK(ha_stats_state_class_from_string("measurement") == HA_STATS_SC_MEASUREMENT);
    CHECK(ha_stats_state_class_from_string("measurement_angle") == HA_STATS_SC_OTHER);
    CHECK(ha_stats_state_class_from_string(NULL) == HA_STATS_SC_UNKNOWN);
}

/* 2026-10-07T15:44:02Z: the last completed hour starts 14:00Z, the current hour (15:00Z) has no row yet */
#define NOW 1791387842LL

static void test_week_window_and_rows(void)
{
    ha_stats_window_t w;
    CHECK(ha_stats_week_window(NOW, &w));
    CHECK(w.first_slot == 1790780400LL && w.slots == 169 && w.end_epoch == NOW);
    CHECK(ha_stats_week_window(1790780400LL + 168LL * 3600, &w) && w.slots == 169); /* exactly on the hour */
    CHECK(!ha_stats_week_window(0, &w));

    ha_stats_window_t win;
    ha_stats_week_window(NOW, &win);
    /* rows as HA sent them: start and end in epoch ms; a gap at 1790791200000, a duplicate, an off-hour row, an old row */
    const char *reply =
        "{\"sensor.p\":["
        "{\"start\":1790784000000,\"end\":1790787600000,\"mean\":12.5,\"max\":150.0,\"min\":-53.0},"
        "{\"start\":1790787600000,\"end\":1790791200000,\"mean\":6.5,\"max\":10.0,\"min\":1.0},"
        "{\"start\":1790794800000,\"end\":1790798400000,\"mean\":-1.25,\"max\":5.0,\"min\":-9.0},"
        "{\"start\":1790794800000,\"end\":1790798400000,\"mean\":99.0,\"max\":99.0,\"min\":99.0},"
        "{\"start\":1790798401000,\"end\":1790802001000,\"mean\":5.0},"
        "{\"start\":1790000000000,\"end\":1790003600000,\"mean\":5.0},"
        "{\"start\":1790802000000,\"end\":1790805600000,\"mean\":null},"
        "{\"start\":1791381600000,\"end\":1791385200000,\"mean\":47.5,\"max\":50.0,\"min\":40.0}]}";
    cJSON *root = cJSON_Parse(reply);
    static ha_stats_series_t s;
    CHECK(ha_stats_parse_hourly(root, "sensor.p", &win, HA_STATS_VALUE_MEAN, true, &s) == HA_STATS_OUTCOME_OK);
    CHECK(s.valid == 4);
    CHECK(s.rejected == 3); /* duplicate, off-hour, out of window; the null mean is a gap, not a rejection */
    CHECK(s.flags[1] == (HA_STATS_FLAG_VALUE | HA_STATS_FLAG_RANGE) && NEAR(s.value[1], 12.5) && NEAR(s.min[1], -53));
    CHECK(s.flags[3] == 0);                                   /* 1790791200000: missing hour stays a gap */
    CHECK(s.flags[4] != 0 && NEAR(s.value[4], -1.25));        /* the first of two duplicates wins */
    CHECK(s.flags[168] == 0);                                  /* the current hour has no row */
    CHECK(s.flags[167] != 0 && NEAR(s.value[167], 47.5));
    cJSON_Delete(root);

    /* without min/max requested */
    root = cJSON_Parse("{\"sensor.p\":[{\"start\":1790784000000,\"end\":1790787600000,\"mean\":1.0}]}");
    CHECK(ha_stats_parse_hourly(root, "sensor.p", &win, HA_STATS_VALUE_MEAN, true, &s) == HA_STATS_OUTCOME_OK);
    CHECK(s.flags[1] == HA_STATS_FLAG_VALUE);
    cJSON_Delete(root);

    /* wrong end, non-finite value, a string instead of a row */
    root = cJSON_Parse("{\"sensor.p\":[{\"start\":1790784000000,\"end\":1790788000000,\"mean\":1.0},"
                       "{\"start\":1790787600000,\"mean\":1e999},\"x\",{\"start\":1790791200000,\"mean\":2.0}]}");
    CHECK(ha_stats_parse_hourly(root, "sensor.p", &win, HA_STATS_VALUE_MEAN, false, &s) == HA_STATS_OUTCOME_OK);
    CHECK(s.valid == 1 && s.rejected == 3 && NEAR(s.value[3], 2.0));
    cJSON_Delete(root);

    /* empty outcomes are data states, shape errors are errors */
    root = cJSON_Parse("{\"sensor.p\":[]}");
    CHECK(ha_stats_parse_hourly(root, "sensor.p", &win, HA_STATS_VALUE_MEAN, false, &s) == HA_STATS_OUTCOME_EMPTY);
    cJSON_Delete(root);
    root = cJSON_Parse("{}");
    CHECK(ha_stats_parse_hourly(root, "sensor.p", &win, HA_STATS_VALUE_MEAN, false, &s) == HA_STATS_OUTCOME_EMPTY);
    cJSON_Delete(root);
    root = cJSON_Parse("{\"sensor.p\":{\"a\":1}}");
    CHECK(ha_stats_parse_hourly(root, "sensor.p", &win, HA_STATS_VALUE_MEAN, false, &s) == HA_STATS_OUTCOME_INVALID);
    cJSON_Delete(root);
    root = cJSON_Parse("[1,2]");
    CHECK(ha_stats_parse_hourly(root, "sensor.p", &win, HA_STATS_VALUE_MEAN, false, &s) == HA_STATS_OUTCOME_INVALID);
    cJSON_Delete(root);

    /* oversized reply: more rows than any window can hold */
    cJSON *big = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(big, "sensor.p");
    for (int i = 0; i < 4 * HA_STATS_MAX_SLOTS + 1; i++) {
        cJSON *r = cJSON_CreateObject();
        cJSON_AddNumberToObject(r, "start", 1790784000000.0 + 3600000.0 * (i % 160));
        cJSON_AddNumberToObject(r, "mean", 1.0);
        cJSON_AddItemToArray(arr, r);
    }
    CHECK(ha_stats_parse_hourly(big, "sensor.p", &win, HA_STATS_VALUE_MEAN, false, &s) == HA_STATS_OUTCOME_INVALID);
    cJSON_Delete(big);
}

static void fill_change(ha_stats_series_t *s, const ha_stats_window_t *w, float per_hour)
{
    memset(s, 0, sizeof(*s));
    s->first_slot = w->first_slot;
    s->slots = (uint16_t)w->slots;
    for (size_t i = 0; i < w->slots; i++) {
        s->value[i] = per_hour;
        s->flags[i] = HA_STATS_FLAG_VALUE;
    }
}

static void test_daily_sums(void)
{
    setenv("TZ", "Europe/Berlin", 1);
    tzset();
    static ha_stats_series_t s;
    ha_stats_window_t w;
    ha_stats_days_t d;

    /* autumn DST end: Sunday 2026-10-25 has 25 hours; now = Monday 2026-10-26 11:30 CET */
    const int64_t now = 1793010600LL;
    CHECK(ha_stats_days_window(now, 7, &w));
    CHECK(w.first_slot == 1792447200LL); /* 2026-10-20T00:00+02:00 */
    CHECK(w.slots <= HA_STATS_MAX_SLOTS);
    fill_change(&s, &w, 1.0f);
    CHECK(ha_stats_sum_days(&s, now, 7, &d) && d.count == 7);
    CHECK(d.day[0].hours == 24 && d.day[0].valid == 24 && NEAR(d.day[0].total, 24));
    CHECK(d.day[5].start == 1792879200LL && d.day[5].hours == 25 && NEAR(d.day[5].total, 25)); /* 25-hour day */
    CHECK(d.day[6].today && d.day[6].hours == 11 && NEAR(d.day[6].total, 11));
    CHECK(!d.day[0].today);

    /* a missing hour makes the day partial and is not counted as zero */
    s.flags[10] = 0;
    CHECK(ha_stats_sum_days(&s, now, 7, &d) && d.day[0].valid == 23 && d.day[0].hours == 24 && NEAR(d.day[0].total, 23));
    /* a day with no row at all is a gap */
    for (int i = 24; i < 48; i++) s.flags[i] = 0;
    CHECK(ha_stats_sum_days(&s, now, 7, &d) && d.day[1].valid == 0 && d.day[1].hours == 24 && NEAR(d.day[1].total, 0));

    /* spring DST start: Sunday 2026-03-29 has 23 hours; now = Monday 2026-03-30 12:30 CEST */
    const int64_t spring = 1774866600LL;
    CHECK(ha_stats_days_window(spring, 3, &w));
    fill_change(&s, &w, 2.0f);
    CHECK(ha_stats_sum_days(&s, spring, 3, &d));
    CHECK(d.day[1].hours == 23 && NEAR(d.day[1].total, 46));
    CHECK(d.day[2].today && d.day[2].hours == 12 && NEAR(d.day[2].total, 24));

    /* counter resets are HA's job: a negative or zero change is summed as sent, never derived from raw states */
    fill_change(&s, &w, 0.0f);
    CHECK(ha_stats_sum_days(&s, spring, 3, &d) && d.day[1].valid == 23 && NEAR(d.day[1].total, 0));

    CHECK(!ha_stats_days_window(spring, 0, &w));
    CHECK(!ha_stats_days_window(spring, HA_STATS_MAX_DAYS + 1, &w));
    unsetenv("TZ");
    tzset();
}

static void test_counter_and_sources(void)
{
    /* Home Assistant's Energy dashboard knows no state class: has_sum and a volume/energy class decide */
    CHECK(ha_stats_classify_counter(&(ha_stats_meta_t){.found = true, .has_sum = true, .unit = "L", .unit_class = "volume"}).kind ==
          HA_STATS_KIND_CONSUMPTION);
    CHECK(ha_stats_classify_counter(&(ha_stats_meta_t){.found = true, .has_sum = true, .unit = "kWh", .unit_class = "energy"}).kind ==
          HA_STATS_KIND_CONSUMPTION);
    CHECK(ha_stats_classify_counter(&(ha_stats_meta_t){.found = true, .has_mean = true, .unit = "W", .unit_class = "power"}).why ==
          HA_STATS_WHY_NO_SUM);
    CHECK(ha_stats_classify_counter(&(ha_stats_meta_t){.found = true, .has_sum = true, .unit = "x"}).why == HA_STATS_WHY_NOT_CONSUMPTION);
    CHECK(ha_stats_classify_counter(&(ha_stats_meta_t){.found = false}).why == HA_STATS_WHY_NOT_FOUND);
    CHECK(ha_stats_classify_counter(NULL).kind == HA_STATS_KIND_NONE);

    setenv("TZ", "Europe/Berlin", 1);
    tzset();
    const int64_t now = 1793010600LL; /* 2026-10-26 11:30 CET */
    ha_stats_window_t w;
    CHECK(ha_stats_days_window(now, 7, &w));
    static ha_stats_series_t a, b, sum;
    ha_stats_days_t d;
    memset(&a, 0, sizeof(a));
    a.first_slot = w.first_slot; a.slots = (uint16_t)w.slots; a.sources = 1;
    for (size_t i = 0; i < w.slots; i++) { a.value[i] = 1.0f; a.flags[i] = HA_STATS_FLAG_VALUE; a.count[i] = 1; }
    a.valid = (uint16_t)w.slots;
    b = a;
    for (size_t i = 0; i < w.slots; i++) b.value[i] = 10.0f;
    for (int i = 24; i < 48; i++) { b.flags[i] = 0; b.count[i] = 0; b.value[i] = 0; } /* source b has no data for day 1 */
    b.valid = (uint16_t)(w.slots - 24);
    sum = a;
    CHECK(ha_stats_series_add(&sum, &b));
    CHECK(sum.sources == 2 && sum.valid == w.slots);
    CHECK(ha_stats_sum_days(&sum, now, 7, &d));
    CHECK(d.day[0].valid == 24 && d.day[0].complete == 24 && NEAR(d.day[0].total, 11.0 * 24));
    CHECK(d.day[1].valid == 24 && d.day[1].complete == 0 && NEAR(d.day[1].total, 24)); /* only source a: partial */
    CHECK(d.day[6].today && d.day[6].complete == d.day[6].hours);
    /* windows must match */
    ha_stats_series_t other = a;
    other.first_slot += 3600;
    CHECK(!ha_stats_series_add(&sum, &other));
    CHECK(!ha_stats_series_add(NULL, &a));
    unsetenv("TZ");
    tzset();
}

/* 30 days of daily means and 12 months of weekly means (a quote's history): windows, rows, gaps and bad rows. */
static void test_ranges(void)
{
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    const int64_t now = 1792350000; /* 2026-10-18 */
    ha_stats_window_t w;
    const char *period = NULL;
    CHECK(ha_stats_range_window(now, HA_RANGE_30_DAYS, &w, &period) && strcmp(period, "day") == 0);
    CHECK(w.end_epoch == now && w.start_epoch == ha_stats_local_day_start(now, -29));
    ha_stats_window_t wy;
    CHECK(ha_stats_range_window(now, HA_RANGE_12_MONTHS, &wy, &period) && strcmp(period, "week") == 0);
    CHECK(wy.start_epoch == ha_stats_local_day_start(now, -364));
    CHECK(!ha_stats_range_window(-1, HA_RANGE_30_DAYS, &w, &period));

    /* rows: epoch milliseconds like the hourly rows; one gap (null mean), one row out of the window, one out of order */
    char json[1024];
    const long long d0 = (long long)w.start_epoch * 1000;
    snprintf(json, sizeof(json),
             "{\"sensor.sap\":[{\"start\":%lld,\"end\":%lld,\"mean\":190.5,\"min\":188.0,\"max\":193.2},"
             "{\"start\":%lld,\"mean\":null},"
             "{\"start\":%lld,\"mean\":195.0},"
             "{\"start\":%lld,\"mean\":196.0},"
             "{\"start\":%lld,\"mean\":150.0}]}",
             d0, d0 + 86400000LL, d0 + 86400000LL * 2, d0 + 86400000LL * 3, d0 + 86400000LL * 2, d0 - 86400000LL * 9);
    cJSON *result = cJSON_Parse(json);
    ha_range_series_t series;
    CHECK(ha_stats_parse_range(result, "sensor.sap", &w, HA_RANGE_30_DAYS, &series) == HA_STATS_OUTCOME_OK);
    CHECK(series.count == 4 && series.rejected == 2);
    CHECK(series.point[0].start == w.start_epoch && NEAR(series.point[0].mean, 190.5) && series.point[0].has_range);
    CHECK(NEAR(series.point[0].min, 188.0) && NEAR(series.point[0].max, 193.2));
    CHECK(isnan(series.point[1].mean) && isnan(series.point[2].mean));
    CHECK(series.point[1].start == w.start_epoch + 86400 && series.point[2].start == w.start_epoch + 2 * 86400);
    CHECK(!series.point[3].has_range && NEAR(series.point[3].min, 195.0) && NEAR(series.point[3].max, 195.0));
    cJSON_Delete(result);

    /* A one-day move followed by a 28-day gap must retain the same calendar/time span as a complete month. */
    snprintf(json, sizeof(json), "{\"sensor.sap\":[{\"start\":%lld,\"mean\":100},{\"start\":%lld,\"mean\":101},"
             "{\"start\":%lld,\"mean\":102}]}", d0, d0 + 86400000LL,
             (long long)ha_stats_local_day_start(w.start_epoch, 29) * 1000);
    result = cJSON_Parse(json);
    CHECK(ha_stats_parse_range(result, "sensor.sap", &w, HA_RANGE_30_DAYS, &series) == HA_STATS_OUTCOME_OK);
    CHECK(series.count == 30 && NEAR(series.point[1].mean, 101) && isnan(series.point[2].mean));
    CHECK(NEAR(series.point[29].mean, 102) && series.point[29].start == ha_stats_local_day_start(w.start_epoch, 29));
    cJSON_Delete(result);

    /* Missing local days and weeks across both clock changes retain real UTC coordinates. */
    const int64_t transitions[] = {1774738800LL, 1792879200LL}; /* Sundays at local midnight */
    for (size_t i = 0; i < sizeof(transitions) / sizeof(transitions[0]); i++) {
        const int64_t begin = ha_stats_local_day_start(transitions[i], -1);
        for (int weekly = 0; weekly < 2; weekly++) {
            const int step = weekly ? 7 : 1;
            ha_stats_window_t dst = {.start_epoch = begin, .end_epoch = ha_stats_local_day_start(begin, 3 * step)};
            const int64_t last = ha_stats_local_day_start(begin, 2 * step);
            snprintf(json, sizeof(json), "{\"sensor.sap\":[{\"start\":%lld,\"mean\":100},{\"start\":%lld,\"mean\":102}]}",
                     (long long)begin * 1000, (long long)last * 1000);
            result = cJSON_Parse(json);
            CHECK(ha_stats_parse_range(result, "sensor.sap", &dst, weekly ? HA_RANGE_12_MONTHS : HA_RANGE_30_DAYS,
                                       &series) == HA_STATS_OUTCOME_OK);
            CHECK(series.count == 3 && isnan(series.point[1].mean));
            CHECK(series.point[1].start == ha_stats_local_day_start(begin, step) && series.point[2].start == last);
            CHECK(last - begin == 2 * step * 86400 + (i == 0 ? -3600 : 3600));
            cJSON_Delete(result);
        }
    }

    /* a week row may start before the window (its Monday), a day row may not */
    snprintf(json, sizeof(json), "{\"sensor.sap\":[{\"start\":%lld,\"mean\":1.5}]}", (long long)(wy.start_epoch - 3 * 86400) * 1000);
    result = cJSON_Parse(json);
    CHECK(ha_stats_parse_range(result, "sensor.sap", &wy, HA_RANGE_12_MONTHS, &series) == HA_STATS_OUTCOME_OK && series.count == 1);
    CHECK(ha_stats_parse_range(result, "sensor.sap", &wy, HA_RANGE_30_DAYS, &series) == HA_STATS_OUTCOME_EMPTY && series.rejected == 1);
    cJSON_Delete(result);

    /* no rows, a missing id, the wrong shape and too many rows */
    result = cJSON_Parse("{\"sensor.sap\":[]}");
    CHECK(ha_stats_parse_range(result, "sensor.sap", &w, HA_RANGE_30_DAYS, &series) == HA_STATS_OUTCOME_EMPTY);
    CHECK(ha_stats_parse_range(result, "sensor.other", &w, HA_RANGE_30_DAYS, &series) == HA_STATS_OUTCOME_EMPTY);
    cJSON_Delete(result);
    result = cJSON_Parse("[]");
    CHECK(ha_stats_parse_range(result, "sensor.sap", &w, HA_RANGE_30_DAYS, &series) == HA_STATS_OUTCOME_INVALID);
    cJSON_Delete(result);
    char *many = malloc(4096);
    strcpy(many, "{\"sensor.sap\":[");
    for (int i = 0; i < HA_RANGE_MAX_POINTS + 1; i++) strcat(many, i ? ",{\"start\":1,\"mean\":1}" : "{\"start\":1,\"mean\":1}");
    strcat(many, "]}");
    result = cJSON_Parse(many);
    CHECK(ha_stats_parse_range(result, "sensor.sap", &w, HA_RANGE_30_DAYS, &series) == HA_STATS_OUTCOME_INVALID);
    cJSON_Delete(result);
    free(many);
    unsetenv("TZ");
    tzset();
}

static void test_numeric_bounds(void)
{
    ha_stats_window_t window;
    CHECK(ha_stats_week_window(NOW, &window));
    const char *bad_rows[] = {
        "{\"start\":1e200,\"mean\":1}",
        "{\"start\":-1e200,\"mean\":1}",
        "{\"start\":1790784000000.5,\"mean\":1}",
        "{\"start\":1790784000000,\"end\":1e200,\"mean\":1}",
        "{\"start\":1790784000000,\"mean\":1e200}"
    };
    for (size_t i = 0; i < sizeof(bad_rows) / sizeof(bad_rows[0]); i++) {
        char json[256];
        snprintf(json, sizeof(json), "{\"sensor.p\":[%s]}", bad_rows[i]);
        cJSON *root = cJSON_Parse(json);
        ha_stats_series_t hourly;
        CHECK(ha_stats_parse_hourly(root, "sensor.p", &window, HA_STATS_VALUE_MEAN, true, &hourly) == HA_STATS_OUTCOME_EMPTY);
        CHECK(hourly.valid == 0 && hourly.rejected == 1);
        cJSON_Delete(root);
    }
    cJSON *root = cJSON_Parse("{\"sensor.p\":[{\"start\":1790784000000,\"mean\":2,\"min\":-1e200,\"max\":1e200}]}");
    ha_stats_series_t hourly;
    CHECK(ha_stats_parse_hourly(root, "sensor.p", &window, HA_STATS_VALUE_MEAN, true, &hourly) == HA_STATS_OUTCOME_OK);
    CHECK(hourly.flags[1] == HA_STATS_FLAG_VALUE && NEAR(hourly.value[1], 2));
    cJSON_Delete(root);

    CHECK(ha_stats_range_window(NOW, HA_RANGE_30_DAYS, &window, NULL));
    char json[768];
    const long long start = (long long)window.start_epoch * 1000;
    snprintf(json, sizeof(json), "{\"sensor.p\":[{\"start\":1e200,\"mean\":1},{\"start\":-1e200,\"mean\":1},"
             "{\"start\":%lld.5,\"mean\":1},{\"start\":%lld,\"mean\":1e200},"
             "{\"start\":%lld,\"mean\":2,\"min\":-1e200,\"max\":1e200}]}", start, start, start);
    root = cJSON_Parse(json);
    ha_range_series_t range;
    CHECK(ha_stats_parse_range(root, "sensor.p", &window, HA_RANGE_30_DAYS, &range) == HA_STATS_OUTCOME_OK);
    CHECK(range.count == 1 && range.rejected == 4 && NEAR(range.point[0].mean, 2));
    CHECK(!range.point[0].has_range && isfinite(range.point[0].min) && isfinite(range.point[0].max));
    cJSON_Delete(root);

    root = cJSON_Parse("[{\"statistic_id\":\"sensor.p\",\"mean_type\":1e200,\"has_mean\":true}]");
    ha_stats_meta_t metadata;
    CHECK(ha_stats_parse_metadata(root, "sensor.p", &metadata) && metadata.mean_type != 1);
    cJSON_Delete(root);
}

int main(void)
{
    test_counter_and_sources();
    test_metadata_and_eligibility();
    test_week_window_and_rows();
    test_daily_sums();
    test_ranges();
    test_numeric_bounds();
    printf("%s: %d checks, %d failed\n", failed == 0 ? "PASS statistics parser" : "FAIL statistics parser", checks, failed);
    return failed == 0 ? 0 : 1;
}
