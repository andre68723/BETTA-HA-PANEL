/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
/* Stocks & ETFs histories (ha_quote_series): Easy Stock's history reply, periods with their reference, Home Assistant
 * sources, the period change and point reduction. Run with TZ=Europe/Berlin. Synthetic prices. */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ha/ha_quote.h"
#include "ha/ha_quote_series.h"

static int s_checks = 0, s_failures = 0;
#define CHECK(cond) do { s_checks++; if (!(cond)) { s_failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int64_t local_epoch(int y, int m, int d, int hh, int mm)
{
    struct tm t = {.tm_year = y - 1900, .tm_mon = m - 1, .tm_mday = d, .tm_hour = hh, .tm_min = mm, .tm_isdst = -1};
    return (int64_t)mktime(&t);
}

/* A reply of `days` daily closes ending at the given date, price 100 + day index / 10. */
static char *easy_reply(const char *symbol, int days, int end_y, int end_m, int end_d)
{
    char *json = malloc((size_t)days * 32 + 64);
    size_t n = (size_t)sprintf(json, "{\"symbol\":\"%s\",\"history\":[", symbol);
    struct tm end = {.tm_year = end_y - 1900, .tm_mon = end_m - 1, .tm_mday = end_d, .tm_hour = 12};
    const time_t last = timegm(&end);
    for (int i = 0; i < days; i++) {
        time_t at = last - (time_t)(days - 1 - i) * 86400;
        struct tm day;
        gmtime_r(&at, &day);
        n += (size_t)sprintf(json + n, "%s[\"%04d-%02d-%02d\",%.2f]", i ? "," : "", day.tm_year + 1900, day.tm_mon + 1,
                             day.tm_mday, 100.0 + i / 10.0);
    }
    sprintf(json + n, "]}");
    return json;
}

static void test_periods(void)
{
    ha_quote_period_t p;
    CHECK(ha_quote_period_from_code("ytd", &p) && p == HA_QUOTE_PERIOD_YTD);
    CHECK(!ha_quote_period_from_code("max", &p) && !ha_quote_period_from_code(NULL, &p));
    CHECK(strcmp(ha_quote_period_code(HA_QUOTE_PERIOD_30D), "30d") == 0 && ha_quote_period_code(HA_QUOTE_PERIOD_COUNT) == NULL);

    const int64_t now = local_epoch(2026, 10, 21, 15, 30);
    int64_t start;
    CHECK(ha_quote_period_start(HA_QUOTE_PERIOD_1D, now, &start) && start == local_epoch(2026, 10, 21, 0, 0));
    CHECK(ha_quote_period_start(HA_QUOTE_PERIOD_30D, now, &start) && start == local_epoch(2026, 9, 21, 0, 0));
    CHECK(ha_quote_period_start(HA_QUOTE_PERIOD_YTD, now, &start) && start == local_epoch(2026, 1, 1, 0, 0));
    CHECK(ha_quote_period_start(HA_QUOTE_PERIOD_1Y, now, &start) && start == local_epoch(2025, 10, 21, 0, 0));
    /* a leap day a year later is February 28 */
    CHECK(ha_quote_period_start(HA_QUOTE_PERIOD_1Y, local_epoch(2028, 2, 29, 9, 0), &start) && start == local_epoch(2027, 2, 28, 0, 0));
    /* across the DST change the start is still the local midnight */
    CHECK(ha_quote_period_start(HA_QUOTE_PERIOD_30D, local_epoch(2026, 11, 10, 8, 0), &start) && start == local_epoch(2026, 10, 11, 0, 0));
    CHECK(!ha_quote_period_start(HA_QUOTE_PERIOD_30D, 1000, &start)); /* no clock */
}

static void test_quote_day(void)
{
    ha_quote_t q = {.available = true, .price = 110, .has_prev_close = true, .prev_close = 100,
                    .has_traded_today = true, .traded_today = false};
    const int64_t now = local_epoch(2026, 10, 25, 23, 30);
    int64_t start;
    CHECK(ha_quote_period_start(HA_QUOTE_PERIOD_1D, now, &start));
    CHECK(now - start == 24 * 3600 + 1800); /* autumn's 25-hour day */
    CHECK(ha_quote_period_start(HA_QUOTE_PERIOD_1D, local_epoch(2026, 3, 29, 23, 30), &start));
    CHECK(start == local_epoch(2026, 3, 29, 0, 0));
    start = local_epoch(2026, 10, 25, 0, 0);
    ha_quote_point_t points[8] = {{.t = start - 100, .v = 90}, {.t = start + 3600, .v = 105}};
    ha_quote_series_t series = {.kind = HA_QUOTE_SERIES_STATES, .period = HA_QUOTE_PERIOD_1D};
    ha_quote_change_t c;
    CHECK(ha_quote_series_period_change(&series, &q, &c) && c.change_pct == 0 && c.base == 110);
    size_t n = ha_quote_day_plot(points, 2, 8, now, &q);
    CHECK(n == 2 && points[0].t == start && points[1].t == now && points[0].v == points[1].v);
    q.traded_today = true;
    points[0] = (ha_quote_point_t){.t = start - 100, .v = 90};
    points[1] = (ha_quote_point_t){.t = start + 3600, .v = 105};
    n = ha_quote_day_plot(points, 2, 8, now, &q);
    CHECK(n == 4 && points[0].t == start && points[0].v == 100 && points[1].t == points[2].t);
    CHECK(points[1].v == 100 && points[2].v == 105 && points[3].t == now && points[3].v == 110);
    CHECK(ha_quote_series_period_change(&series, &q, &c) && c.change_pct == 10);
    q.has_traded_today = false; /* Older Easy Stock versions keep the previous-close reference. */
    CHECK(ha_quote_series_period_change(&series, &q, &c) && c.change_pct == 10);
    n = ha_quote_day_plot(points, 0, 8, now, &q);
    CHECK(n == 2 && points[0].v == 100 && points[1].v == 110);
    points[0] = (ha_quote_point_t){.t = start, .v = NAN};
    n = ha_quote_day_plot(points, 1, 8, now, &q);
    CHECK(n == 2 && points[0].t == start && points[1].t == now);
    q.available = false;
    CHECK(!ha_quote_day_change(&q, &c));
}

static void test_easy_reply(void)
{
    static ha_quote_daily_t daily;
    /* Three years: the reply grows, the kept closes do not. */
    char *json = easy_reply("DEMO.DE", 1100, 2026, 10, 21);
    CHECK(strlen(json) < HA_QUOTE_HISTORY_MAX_BYTES);
    CHECK(ha_quote_easy_history_parse(json, strlen(json), "demo.de", &daily) == HA_QUOTE_HISTORY_OK);
    CHECK(daily.count == HA_QUOTE_DAILY_MAX && daily.rejected == 0);
    CHECK(fabsf(daily.close[daily.count - 1] - (float)(100.0 + 1099 / 10.0)) < 0.01f);
    CHECK(ha_quote_easy_history_parse(json, strlen(json), "OTHER.DE", &daily) == HA_QUOTE_HISTORY_INVALID);
    free(json);

    const char *bad = "{\"symbol\":\"DEMO.DE\",\"history\":[[\"2024-02-28\",10],[\"2024-02-29\",11],[\"2024-02-29\",12],"
                      "[\"2024-02-27\",9],[\"2023-02-29\",1],[\"2024-03-01\",\"x\"],[\"2024-03-02\",-1],[\"2024-03-03\",13,1],"
                      "[\"2024-3-4\",14],[\"2024-03-05\",15]]}";
    CHECK(ha_quote_easy_history_parse(bad, strlen(bad), "DEMO.DE", &daily) == HA_QUOTE_HISTORY_OK);
    CHECK(daily.count == 3 && daily.rejected == 7); /* 02-28, the leap day 02-29, 03-05 */
    const char *empty = "{\"symbol\":\"DEMO.DE\",\"history\":[]}";
    CHECK(ha_quote_easy_history_parse(empty, strlen(empty), "DEMO.DE", &daily) == HA_QUOTE_HISTORY_EMPTY);
    const char *wrong = "{\"symbol\":\"DEMO.DE\",\"history\":{}}";
    CHECK(ha_quote_easy_history_parse(wrong, strlen(wrong), "DEMO.DE", &daily) == HA_QUOTE_HISTORY_INVALID);
    CHECK(ha_quote_easy_history_parse("{", 1, "DEMO.DE", &daily) == HA_QUOTE_HISTORY_INVALID);
    CHECK(ha_quote_easy_history_parse(empty, HA_QUOTE_HISTORY_MAX_BYTES + 1, "DEMO.DE", &daily) == HA_QUOTE_HISTORY_INVALID);
}

static void test_daily_periods(void)
{
    static ha_quote_daily_t daily;
    static ha_quote_series_t s;
    const int64_t now = local_epoch(2026, 10, 21, 15, 30);
    char *json = easy_reply("DEMO.DE", 900, 2026, 10, 21);
    CHECK(ha_quote_easy_history_parse(json, strlen(json), "DEMO.DE", &daily) == HA_QUOTE_HISTORY_OK);
    free(json);

    /* 30 days: the closes from the window's first day, the day before as the reference; today's close may change. */
    CHECK(ha_quote_series_from_daily(&daily, HA_QUOTE_PERIOD_30D, now, &s));
    CHECK(s.kind == HA_QUOTE_SERIES_CLOSES && s.count == 31 && s.has_reference && !s.partial && s.last_provisional);
    CHECK(s.point[0].t == local_epoch(2026, 9, 21, 0, 0) && s.reference.t == local_epoch(2026, 9, 20, 0, 0));
    /* YTD: the reference is the last close of the previous year */
    CHECK(ha_quote_series_from_daily(&daily, HA_QUOTE_PERIOD_YTD, now, &s));
    CHECK(s.point[0].t == local_epoch(2026, 1, 1, 0, 0) && s.reference.t == local_epoch(2025, 12, 31, 0, 0));
    /* 1Y never grows with old closes: 366 days from the same date a year back */
    CHECK(ha_quote_series_from_daily(&daily, HA_QUOTE_PERIOD_1Y, now, &s) && s.count == 366 && s.has_reference);
    CHECK(!ha_quote_series_from_daily(&daily, HA_QUOTE_PERIOD_1D, now, &s));

    ha_quote_change_t c;
    CHECK(ha_quote_series_change(&s, true, 200.0, &c) && c.from_reference && c.end == 200.0);
    CHECK(fabs(c.base - s.reference.v) < 1e-6 && fabs(c.change - (200.0 - c.base)) < 1e-9);

    /* A history that starts inside the window: partial, the period change starts at the first close. */
    json = easy_reply("DEMO.DE", 10, 2026, 10, 20);
    CHECK(ha_quote_easy_history_parse(json, strlen(json), "DEMO.DE", &daily) == HA_QUOTE_HISTORY_OK);
    free(json);
    CHECK(ha_quote_series_from_daily(&daily, HA_QUOTE_PERIOD_30D, now, &s));
    CHECK(s.partial && !s.has_reference && s.count == 10 && !s.last_provisional);
    CHECK(ha_quote_series_change(&s, false, 0.0, &c) && !c.from_reference && fabs(c.base - 100.0) < 1e-6);
    CHECK(fabs(c.end - 100.9) < 1e-4);
}

static void test_ha_sources(void)
{
    static ha_quote_series_t s;
    static ha_history_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.numeric = true;
    snap.start_epoch = 1792500000;
    snap.end_epoch = snap.start_epoch + 86400;
    for (int i = 0; i < HA_HISTORY_BUCKETS; i++) {
        if (i >= 100 && i < 110) continue; /* nothing recorded: a gap */
        snap.buckets[i] = (ha_history_bucket_t){.min = 10, .max = 12, .sum = 22, .count = 2};
    }
    CHECK(ha_quote_series_from_snapshot(&snap, &s) && s.kind == HA_QUOTE_SERIES_STATES && s.count == HA_HISTORY_BUCKETS);
    CHECK(s.point[0].v == 11.0f && isnan(s.point[105].v) && s.point[1].t == snap.start_epoch + 300);
    ha_quote_change_t c;
    CHECK(ha_quote_series_change(&s, true, 12.1, &c) && c.end == 12.1 && fabs(c.change - 1.1) < 1e-6);
    /* a day of a quote starts at its previous close, the session's reference */
    ha_quote_t quote = {.available = true, .price = 12.1, .has_prev_close = true, .prev_close = 11.0};
    CHECK(ha_quote_series_period_change(&s, &quote, &c) && c.from_reference && c.base == 11.0 && c.base_t == 0);
    CHECK(fabs(c.change_pct - 10.0) < 1e-6);
    quote.has_prev_close = false;
    CHECK(ha_quote_series_period_change(&s, &quote, &c) && !c.from_reference && c.base == 11.0f);

    /* Means end at the last mean, whatever the live price is. */
    static ha_stats_series_t week;
    memset(&week, 0, sizeof(week));
    week.slots = 169;
    for (int i = 5; i < 169; i++) {
        week.value[i] = 50.0f + (float)i / 10.0f;
        week.flags[i] = HA_STATS_FLAG_VALUE;
        week.valid++;
    }
    CHECK(ha_quote_series_from_week(&week, 1792000800, &s) && s.kind == HA_QUOTE_SERIES_HOURLY_MEANS && s.count == 169);
    CHECK(ha_quote_series_change(&s, true, 999.0, &c) && fabs(c.base - 50.5) < 1e-4 && fabs(c.end - 66.8) < 1e-4);

    /* YTD from weekly means keeps the weeks reaching into the year; a later first week is a shorter history. */
    static ha_range_series_t range;
    memset(&range, 0, sizeof(range));
    range.kind = HA_RANGE_12_MONTHS;
    const int64_t now = local_epoch(2026, 10, 21, 15, 30);
    const int64_t monday = local_epoch(2025, 10, 20, 0, 0);
    for (int i = 0; i < 53; i++) {
        range.point[i] = (ha_range_point_t){.start = monday + (int64_t)i * 7 * 86400, .mean = 80.0f + (float)i,
                                            .min = 79.0f, .max = 90.0f, .has_range = true};
    }
    range.count = 53;
    CHECK(ha_quote_series_from_range(&range, HA_QUOTE_PERIOD_YTD, now, &s) && s.kind == HA_QUOTE_SERIES_WEEKLY_MEANS);
    CHECK(s.point[0].t <= local_epoch(2026, 1, 1, 0, 0) && s.point[0].t + 7 * 86400 > local_epoch(2026, 1, 1, 0, 0));
    CHECK(!s.partial);
    CHECK(ha_quote_series_from_range(&range, HA_QUOTE_PERIOD_1Y, now, &s) && s.count == 53 && !s.partial);
    range.point[0].mean = NAN;
    range.point[1].mean = NAN;
    CHECK(ha_quote_series_from_range(&range, HA_QUOTE_PERIOD_1Y, now, &s) && s.partial && s.count == 51);
}

static void test_reduce(void)
{
    static ha_quote_series_t s;
    memset(&s, 0, sizeof(s));
    s.count = 300;
    for (int i = 0; i < 300; i++) s.point[i] = (ha_quote_point_t){.t = i, .v = (float)(i % 50)};
    s.point[123].v = -40.0f; /* a dip */
    s.point[200].v = 400.0f; /* a spike */
    for (int i = 240; i < 260; i++) s.point[i].v = NAN;
    ha_quote_point_t out[60];
    const size_t n = ha_quote_series_reduce(&s, out, 60);
    CHECK(n <= 60 && n >= 30);
    bool dip = false, spike = false, gap = false;
    for (size_t i = 0; i < n; i++) {
        dip |= out[i].v == -40.0f;
        spike |= out[i].v == 400.0f;
        gap |= isnan(out[i].v);
        if (i > 0) CHECK(out[i].t > out[i - 1].t);
    }
    CHECK(dip && spike && gap);
    CHECK(out[0].t == 0 && out[n - 1].t == 299);
    /* A single absent sample inside a reduction interval must still break the line. */
    for (int i = 0; i < 300; i++) s.point[i].v = 10.0f;
    s.point[123].v = NAN;
    const size_t short_n = ha_quote_series_reduce(&s, out, 60);
    gap = false;
    for (size_t i = 0; i < short_n; i++) gap |= isnan(out[i].v);
    CHECK(gap && out[0].t == 0 && out[short_n - 1].t == 299);
    s.count = 20;
    CHECK(ha_quote_series_reduce(&s, out, 60) == 20);
}

int main(void)
{
    test_periods();
    test_quote_day();
    test_easy_reply();
    test_daily_periods();
    test_ha_sources();
    test_reduce();
    printf("quote series: %d checks, %d failed\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
