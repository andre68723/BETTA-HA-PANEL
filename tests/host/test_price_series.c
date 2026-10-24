/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
/* Electricity price lists: the two shapes (ha_epex_spot with ISO times, Day Ahead Optimizer with epoch milliseconds), the regular grid, the
 * model's attribute JSON, day statistics and cheap windows. The hourly market prices are the real ones of Home Assistant's EPEX Spot
 * (Energy-Charts.info, DE-LU) for 2026-10-17 and 2026-10-18, public market data. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ha/ha_attr_serialize.h"
#include "ha/ha_price_series.h"

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

#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 1e-9)

/* 2026-10-17 00:00 CEST = 2026-10-16T22:00:00Z */
#define DAY0 1792188000LL

static const double EPEX[48] = {
    0.20038, 0.18343, 0.16618, 0.1524,  0.14999, 0.15302, 0.15022, 0.16521, 0.18674, 0.17057, 0.16114, 0.14435,
    0.12601, 0.12019, 0.12566, 0.14447, 0.16694, 0.19895, 0.21843, 0.23493, 0.20066, 0.1878,  0.17073, 0.16003,
    0.149,   0.14663, 0.14779, 0.14263, 0.14735, 0.15199, 0.14114, 0.15642, 0.18276, 0.20084, 0.19449, 0.1901,
    0.18337, 0.16312, 0.16192, 0.16977, 0.19086, 0.21263, 0.22627, 0.2206,  0.19828, 0.17955, 0.17661, 0.17101,
};

/* The quarter-hour market prices of the same two days, as Home Assistant's EPEX Spot returned them with the 15 minute option. */
static const double EPEX_Q15[192] = {
    0.20927, 0.20653, 0.19967, 0.18605, 0.19782, 0.18603, 0.17925, 0.17064,
    0.17431, 0.17009, 0.16396, 0.15637, 0.15464, 0.15257, 0.15183, 0.15054,
    0.14965, 0.14957, 0.14978, 0.15097, 0.15569, 0.15583, 0.15167, 0.14887,
    0.14703, 0.14911, 0.1503, 0.15446, 0.15742, 0.16237, 0.16796, 0.17309,
    0.18886, 0.19006, 0.18873, 0.1793, 0.17947, 0.17022, 0.16906, 0.16353,
    0.17336, 0.16039, 0.16016, 0.15067, 0.15123, 0.1456, 0.14233, 0.13824,
    0.13269, 0.12504, 0.12565, 0.12067, 0.1248, 0.1184, 0.11921, 0.11834,
    0.12323, 0.12239, 0.12948, 0.12753, 0.13699, 0.13909, 0.15015, 0.15163,
    0.15285, 0.15523, 0.16268, 0.19698, 0.16875, 0.20019, 0.20701, 0.21986,
    0.20999, 0.21089, 0.21684, 0.236, 0.24543, 0.24265, 0.23599, 0.21566,
    0.21278, 0.19773, 0.19592, 0.19619, 0.20799, 0.20021, 0.17773, 0.16529,
    0.1794, 0.1664, 0.17535, 0.16177, 0.1614, 0.16061, 0.16287, 0.15524,
    0.15328, 0.15049, 0.14565, 0.14659, 0.15099, 0.14926, 0.14252, 0.14376,
    0.14963, 0.1482, 0.14733, 0.14601, 0.14254, 0.13997, 0.14418, 0.14384,
    0.14, 0.1503, 0.1531, 0.14601, 0.14871, 0.1525, 0.15465, 0.15209,
    0.13943, 0.14041, 0.14389, 0.14084, 0.14655, 0.15055, 0.16085, 0.16772,
    0.17864, 0.1813, 0.18347, 0.18764, 0.20182, 0.20516, 0.2017, 0.19468,
    0.21124, 0.20671, 0.18101, 0.17899, 0.19903, 0.19197, 0.1853, 0.1841,
    0.19147, 0.18623, 0.18786, 0.16791, 0.18006, 0.16236, 0.15785, 0.15223,
    0.16772, 0.15829, 0.16201, 0.15964, 0.16459, 0.16839, 0.17488, 0.17121,
    0.16179, 0.18182, 0.19549, 0.22434, 0.19689, 0.21462, 0.21699, 0.222,
    0.22203, 0.22551, 0.22894, 0.22861, 0.22663, 0.22207, 0.21704, 0.21665,
    0.21265, 0.19482, 0.1997, 0.18593, 0.18667, 0.1819, 0.17842, 0.1712,
    0.18635, 0.17924, 0.1741, 0.16674, 0.17573, 0.17147, 0.16858, 0.16827,
};

static void iso(int64_t epoch, int offset_h, char *out, size_t cap)
{
    time_t t = (time_t)(epoch + offset_h * 3600);
    struct tm tm;
    gmtime_r(&t, &tm);
    snprintf(out, cap, "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:00", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
             tm.tm_sec, offset_h < 0 ? '-' : '+', abs(offset_h));
}

/* attrs of the EPEX shape: items [from, to) of the given prices, step seconds apart from start. */
static cJSON *epex_attrs(int64_t start, int step, const double *prices, int from, int to)
{
    cJSON *attrs = cJSON_CreateObject();
    cJSON *data = cJSON_AddArrayToObject(attrs, "data");
    for (int i = from; i < to; i++) {
        cJSON *item = cJSON_CreateObject();
        char text[40];
        iso(start + (int64_t)i * step, 2, text, sizeof(text));
        cJSON_AddStringToObject(item, "start_time", text);
        iso(start + (int64_t)(i + 1) * step, 2, text, sizeof(text));
        cJSON_AddStringToObject(item, "end_time", text);
        cJSON_AddNumberToObject(item, "price_per_kwh", prices[i]);
        cJSON_AddItemToArray(data, item);
    }
    return attrs;
}

static cJSON *dao_attrs(int64_t start, const double *prices, int n)
{
    cJSON *attrs = cJSON_CreateObject();
    cJSON *data = cJSON_AddArrayToObject(attrs, "data");
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "time_ts", (double)((start + (int64_t)i * 3600) * 1000));
        cJSON_AddStringToObject(item, "time", "2026-10-17 00:00"); /* local text without a zone is never used */
        cJSON_AddNumberToObject(item, "da_ex", prices[i] - 0.1);
        cJSON_AddNumberToObject(item, "da_cons", prices[i]);
        cJSON_AddNumberToObject(item, "da_prod", prices[i]);
        cJSON_AddStringToObject(item, "datatype", i < 21 ? "recorded" : "expected");
        cJSON_AddItemToArray(data, item);
    }
    return attrs;
}

static void test_iso_strict(void)
{
    int64_t t = 0;
    CHECK(ha_price_parse_iso_strict("2026-10-17T00:00:00+02:00", &t) && t == DAY0);
    CHECK(ha_price_parse_iso_strict("2026-10-16T22:00:00Z", &t) && t == DAY0);
    CHECK(ha_price_parse_iso_strict("2026-10-16T22:00:00.000+00:00", &t) && t == DAY0);
    CHECK(ha_price_parse_iso_strict("2026-10-16T17:00:00-05:00", &t) && t == DAY0);
    CHECK(!ha_price_parse_iso_strict("2026-10-17T00:00:00", &t));            /* no zone */
    CHECK(!ha_price_parse_iso_strict("2026-10-17 00:00:00+02:00", &t));      /* space instead of T */
    CHECK(!ha_price_parse_iso_strict("2026-02-30T00:00:00Z", &t));           /* not a date */
    CHECK(!ha_price_parse_iso_strict("2026-13-01T00:00:00Z", &t));
    CHECK(!ha_price_parse_iso_strict("2026-10-17T24:00:00Z", &t));
    CHECK(!ha_price_parse_iso_strict("2026-10-17T00:00:00+02:00x", &t));     /* trailing text */
    CHECK(!ha_price_parse_iso_strict("2026-10-17T00:00:00+0200", &t));
    CHECK(!ha_price_parse_iso_strict("2026-10-17T00:00:00+15:00", &t));
    CHECK(ha_price_parse_iso_strict("2028-02-29T00:00:00Z", &t));            /* leap day */
    CHECK(!ha_price_parse_iso_strict("2027-02-29T00:00:00Z", &t));
    CHECK(!ha_price_parse_iso_strict(NULL, &t));
}

static void test_epex_shape(void)
{
    cJSON *attrs = epex_attrs(DAY0, 3600, EPEX, 0, 48);
    ha_price_series_t s;
    CHECK(ha_price_series_detect(attrs));
    CHECK(ha_price_series_from_attrs(attrs, &s));
    CHECK(s.count == 48 && s.interval_s == 3600 && s.start_s == DAY0);
    CHECK(s.price[19] == 23493 && s.price[13] == 12019 && s.price[0] == 20038);
    char json[1280];
    CHECK(ha_price_series_to_json(&s, json, sizeof(json)));
    CHECK(strlen(json) < 400);
    ha_price_series_t back;
    CHECK(ha_price_series_from_json(json, &back));
    CHECK(back.count == 48 && back.start_s == s.start_s && back.interval_s == s.interval_s);
    CHECK(memcmp(back.price, s.price, 48 * sizeof(int32_t)) == 0);
    CHECK(NEAR(ha_price_value(&s, 19), 0.23493));
    cJSON_Delete(attrs);
}

static void test_dao_shape(void)
{
    cJSON *attrs = dao_attrs(DAY0, EPEX, 48);
    ha_price_series_t s;
    CHECK(ha_price_series_detect(attrs));
    CHECK(ha_price_series_from_attrs(attrs, &s));
    CHECK(s.count == 48 && s.interval_s == 3600 && s.start_s == DAY0 && s.price[19] == 23493);
    cJSON_Delete(attrs);
    /* the same series in both shapes yields the same grid */
    cJSON *a = epex_attrs(DAY0, 3600, EPEX, 0, 48);
    cJSON *b = dao_attrs(DAY0, EPEX, 48);
    ha_price_series_t sa, sb;
    CHECK(ha_price_series_from_attrs(a, &sa) && ha_price_series_from_attrs(b, &sb));
    CHECK(sa.count == sb.count && memcmp(sa.price, sb.price, sizeof(int32_t) * sa.count) == 0);
    cJSON_Delete(a);
    cJSON_Delete(b);
}

static void test_stats_and_cheap(void)
{
    cJSON *attrs = epex_attrs(DAY0, 3600, EPEX, 0, 48);
    ha_price_series_t s;
    CHECK(ha_price_series_from_attrs(attrs, &s));
    int first, count;
    ha_price_slot_range(&s, DAY0, DAY0 + 86400, &first, &count);
    CHECK(first == 0 && count == 24);
    ha_price_stats_t st = ha_price_stats(&s, first, count);
    CHECK(st.valid && st.priced == 24 && NEAR(st.min, 0.12019) && NEAR(st.max, 0.23493));
    CHECK(fabs(st.avg - 0.16826791666666666) < 1e-9);

    ha_price_window_t w[HA_PRICE_MAX_WINDOWS];
    /* 2026-10-17 20:00 CEST: today's windows 04-05 and 11-16 are over, tomorrow's 00-05 and 06-07 are ahead */
    size_t n = ha_price_cheap_windows(&s, DAY0 + 20 * 3600, w, HA_PRICE_MAX_WINDOWS);
    CHECK(n == 2);
    CHECK(w[0].start_s == DAY0 + 24 * 3600 && w[0].end_s == DAY0 + 29 * 3600);
    CHECK(w[1].start_s == DAY0 + 30 * 3600 && w[1].end_s == DAY0 + 31 * 3600);
    /* 03:00: all of them, oldest first. The prices are rounded to 0.001 EUR/kWh like the card does, so 06:00 (0.15022, the same 0.150 as
     * 04:00's 0.14999) joins the lowest quarter and today has three windows: 04-05, 06-07 and 11-16. */
    n = ha_price_cheap_windows(&s, DAY0 + 3 * 3600, w, HA_PRICE_MAX_WINDOWS);
    CHECK(n == 5);
    CHECK(w[0].start_s == DAY0 + 4 * 3600 && w[0].end_s == DAY0 + 5 * 3600);
    CHECK(w[1].start_s == DAY0 + 6 * 3600 && w[1].end_s == DAY0 + 7 * 3600);
    CHECK(w[2].start_s == DAY0 + 11 * 3600 && w[2].end_s == DAY0 + 16 * 3600);
    /* a window that is running counts, one that ended at now does not */
    n = ha_price_cheap_windows(&s, DAY0 + 12 * 3600, w, HA_PRICE_MAX_WINDOWS);
    CHECK(n == 3 && w[0].start_s == DAY0 + 11 * 3600);
    n = ha_price_cheap_windows(&s, DAY0 + 16 * 3600, w, HA_PRICE_MAX_WINDOWS);
    CHECK(n == 2);
    /* the output limit is respected */
    n = ha_price_cheap_windows(&s, DAY0, w, 1);
    CHECK(n == 1);
    int slot = ha_price_slot_at(&s, DAY0 + 19 * 3600 + 5);
    CHECK(slot == 19 && ha_price_slot_at(&s, DAY0 - 1) == -1 && ha_price_slot_at(&s, DAY0 + 48 * 3600) == -1);
    cJSON_Delete(attrs);
}

static void test_explicit_durations_and_window_capacity(void)
{
    /* Every other quarter hour is absent: its explicit end must stop the preceding price. */
    cJSON *attrs = epex_attrs(DAY0, 900, EPEX_Q15, 0, 8);
    cJSON *data = cJSON_GetObjectItemCaseSensitive(attrs, "data");
    for (int i = 7; i >= 1; i -= 2) cJSON_DeleteItemFromArray(data, i);
    ha_price_series_t s;
    CHECK(ha_price_series_from_attrs(attrs, &s) && s.interval_s == 900 && s.count == 7);
    CHECK(s.price[1] == HA_PRICE_MISSING && s.price[3] == HA_PRICE_MISSING && s.price[5] == HA_PRICE_MISSING);
    CHECK(isnan(ha_price_value(&s, ha_price_slot_at(&s, DAY0 + 20 * 60))));
    /* Missing end fields may use the other rows' duration, but conflicting or invalid ends fail closed. */
    cJSON *item = cJSON_GetArrayItem(data, 1);
    cJSON_DeleteItemFromObjectCaseSensitive(item, "end_time");
    CHECK(ha_price_series_from_attrs(attrs, &s) && s.interval_s == 900);
    cJSON_AddStringToObject(item, "end_time", "2026-10-17T01:00:00+02:00");
    CHECK(!ha_price_series_from_attrs(attrs, &s));
    cJSON_ReplaceItemInObjectCaseSensitive(item, "end_time", cJSON_CreateString("2026-10-17T00:30:00+02:00"));
    CHECK(!ha_price_series_from_attrs(attrs, &s));
    cJSON_ReplaceItemInObjectCaseSensitive(item, "end_time", cJSON_CreateString("2026-10-17T00:45:00"));
    CHECK(!ha_price_series_from_attrs(attrs, &s));
    cJSON_Delete(attrs);

    /* Six isolated cheap hours per day: neither elapsed windows nor the old eight-window cap may lose tomorrow's. */
    s = (ha_price_series_t){.start_s = DAY0, .interval_s = 3600, .count = 48};
    for (int i = 0; i < s.count; i++) s.price[i] = i % 4 == 0 ? 10000 : 30000;
    ha_price_window_t windows[HA_PRICE_MAX_WINDOWS];
    CHECK(ha_price_cheap_windows(&s, DAY0, windows, HA_PRICE_MAX_WINDOWS) == 12);
    CHECK(windows[11].start_s == DAY0 + 44 * 3600);
    CHECK(ha_price_cheap_windows(&s, DAY0 + 23 * 3600, windows, HA_PRICE_MAX_WINDOWS) == 6);
    CHECK(windows[5].start_s == DAY0 + 44 * 3600 && windows[5].end_s == DAY0 + 45 * 3600);
    s.count = HA_PRICE_MAX_SLOTS;
    for (int i = 0; i < s.count; i++) s.price[i] = 20000;
    const size_t n = ha_price_cheap_windows(&s, DAY0, windows, HA_PRICE_MAX_WINDOWS);
    CHECK(n == 9 && windows[n - 1].end_s == DAY0 + HA_PRICE_MAX_SLOTS * 3600);
}

static void test_irregular_inputs(void)
{
    ha_price_series_t s;
    /* unsorted */
    cJSON *attrs = epex_attrs(DAY0, 3600, EPEX, 0, 6);
    cJSON *data = cJSON_GetObjectItemCaseSensitive(attrs, "data");
    cJSON *last = cJSON_DetachItemFromArray(data, 5);
    cJSON_InsertItemInArray(data, 0, last);
    CHECK(ha_price_series_from_attrs(attrs, &s) && s.count == 6 && s.start_s == DAY0 && s.price[5] == 15302);
    cJSON_Delete(attrs);
    /* a gap stays a gap */
    attrs = epex_attrs(DAY0, 3600, EPEX, 0, 6);
    cJSON_DeleteItemFromArray(cJSON_GetObjectItemCaseSensitive(attrs, "data"), 2);
    CHECK(ha_price_series_from_attrs(attrs, &s) && s.count == 6 && s.price[2] == HA_PRICE_MISSING && s.price[3] == 15240);
    char json[200];
    CHECK(ha_price_series_to_json(&s, json, sizeof(json)) && strstr(json, "null") != NULL);
    ha_price_series_t back;
    CHECK(ha_price_series_from_json(json, &back) && back.price[2] == HA_PRICE_MISSING && back.price[3] == s.price[3]);
    CHECK(isnan(ha_price_value(&s, 2)));
    cJSON_Delete(attrs);
    /* a null price is a gap, a string is an error */
    attrs = epex_attrs(DAY0, 3600, EPEX, 0, 4);
    cJSON *item = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(attrs, "data"), 1);
    cJSON_ReplaceItemInObjectCaseSensitive(item, "price_per_kwh", cJSON_CreateNull());
    CHECK(ha_price_series_from_attrs(attrs, &s) && s.price[1] == HA_PRICE_MISSING);
    cJSON_ReplaceItemInObjectCaseSensitive(item, "price_per_kwh", cJSON_CreateString("0.2"));
    CHECK(!ha_price_series_from_attrs(attrs, &s));
    cJSON_Delete(attrs);
    /* a single item, an off-grid start, a day-long step, a zone-less time */
    attrs = epex_attrs(DAY0, 3600, EPEX, 0, 1);
    CHECK(!ha_price_series_detect(attrs) && !ha_price_series_from_attrs(attrs, &s));
    cJSON_Delete(attrs);
    attrs = epex_attrs(DAY0, 86400, EPEX, 0, 3);
    CHECK(!ha_price_series_from_attrs(attrs, &s));
    cJSON_Delete(attrs);
    attrs = epex_attrs(DAY0, 3600, EPEX, 0, 4);
    item = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(attrs, "data"), 3);
    cJSON_ReplaceItemInObjectCaseSensitive(item, "start_time", cJSON_CreateString("2026-10-17T03:00:00"));
    CHECK(!ha_price_series_from_attrs(attrs, &s));
    cJSON_ReplaceItemInObjectCaseSensitive(item, "start_time", cJSON_CreateString("2026-10-17T03:00:07+02:00"));
    CHECK(!ha_price_series_from_attrs(attrs, &s)); /* off the grid: the step would be 7 seconds */
    cJSON_Delete(attrs);
    /* two different prices for one slot, equal ones are fine */
    attrs = epex_attrs(DAY0, 3600, EPEX, 0, 3);
    cJSON *dup = cJSON_Duplicate(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(attrs, "data"), 1), true);
    cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(attrs, "data"), dup);
    CHECK(ha_price_series_from_attrs(attrs, &s));
    cJSON_ReplaceItemInObjectCaseSensitive(dup, "price_per_kwh", cJSON_CreateNumber(9.9));
    CHECK(!ha_price_series_from_attrs(attrs, &s));
    cJSON_Delete(attrs);
    /* epoch milliseconds that are not whole seconds, a huge price */
    attrs = dao_attrs(DAY0, EPEX, 4);
    item = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(attrs, "data"), 2);
    cJSON_ReplaceItemInObjectCaseSensitive(item, "time_ts", cJSON_CreateNumber((double)((DAY0 + 7200) * 1000 + 5)));
    CHECK(!ha_price_series_from_attrs(attrs, &s));
    cJSON_Delete(attrs);
    attrs = epex_attrs(DAY0, 3600, EPEX, 0, 4);
    cJSON_ReplaceItemInObjectCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(attrs, "data"), 0), "price_per_kwh",
                                           cJSON_CreateNumber(5000));
    CHECK(!ha_price_series_from_attrs(attrs, &s));
    cJSON_Delete(attrs);
    /* too many items or slots are refused, never cut */
    double many[HA_PRICE_MAX_ITEMS + 10];
    for (size_t i = 0; i < sizeof(many) / sizeof(many[0]); i++) many[i] = 0.2;
    attrs = epex_attrs(DAY0, 900, many, 0, HA_PRICE_MAX_ITEMS + 1);
    CHECK(!ha_price_series_from_attrs(attrs, &s));
    cJSON_Delete(attrs);
    attrs = epex_attrs(DAY0, 900, many, 0, HA_PRICE_MAX_SLOTS + 1);
    CHECK(!ha_price_series_from_attrs(attrs, &s));
    cJSON_Delete(attrs);
    attrs = epex_attrs(DAY0, 900, many, 0, 192);
    CHECK(ha_price_series_from_attrs(attrs, &s) && s.count == 192 && s.interval_s == 900);
    cJSON_Delete(attrs);
    /* no data attribute, or one that is not a price list */
    attrs = cJSON_Parse("{\"data\":[{\"x\":1},{\"x\":2}]}");
    CHECK(!ha_price_series_detect(attrs) && !ha_price_series_from_attrs(attrs, &s));
    cJSON_Delete(attrs);
    attrs = cJSON_Parse("{\"friendly_name\":\"x\"}");
    CHECK(!ha_price_series_detect(attrs) && !ha_price_series_from_attrs(attrs, &s));
    cJSON_Delete(attrs);
}

static void test_json_budget(void)
{
    ha_price_series_t s;
    memset(&s, 0, sizeof(s));
    s.start_s = DAY0;
    s.interval_s = 900;
    s.count = 192;
    for (int i = 0; i < 192; i++) s.price[i] = 20038 + (i % 7) * 311; /* 5 digit prices, the usual case */
    char json[1280];
    CHECK(ha_price_series_to_json(&s, json, sizeof(json)));
    CHECK(strlen(json) < 1279);
    /* the worst case does not fit and is reported, not cut */
    for (int i = 0; i < 192; i++) s.price[i] = (i % 2) ? 1000000 : -1000000; /* steps of 2e6: eight digits each */
    CHECK(!ha_price_series_to_json(&s, json, sizeof(json)));
    CHECK(!ha_price_series_to_json(&s, json, 20));
    CHECK(!ha_price_series_from_json("{\"pf\":{\"v\":1,\"s\":1792188000,\"d\":3600,\"p\":[1]}}", &s));  /* the absolute v1 is not read */
    CHECK(!ha_price_series_from_json("{\"pf\":{\"v\":3,\"s\":1,\"d\":3600,\"p\":[1]}}", &s));
    CHECK(!ha_price_series_from_json("{\"pf\":{\"v\":2,\"s\":1,\"d\":3600,\"p\":[\"x\"]}}", &s));
    CHECK(!ha_price_series_from_json("{\"pf\":{\"v\":2,\"s\":1,\"d\":3600,\"p\":[]}}", &s));
    CHECK(!ha_price_series_from_json("{}", &s) && !ha_price_series_from_json("", &s) && !ha_price_series_from_json("{bad", &s));
    const char *invalid[] = {
        "{\"pf\":{\"v\":2,\"s\":1e300,\"d\":3600,\"p\":[1]}}",
        "{\"pf\":{\"v\":2,\"s\":1.5,\"d\":3600,\"p\":[1]}}",
        "{\"pf\":{\"v\":2.5,\"s\":1,\"d\":3600,\"p\":[1]}}",
        "{\"pf\":{\"v\":2,\"s\":1,\"d\":3600.5,\"p\":[1]}}",
        "{\"pf\":{\"v\":2,\"s\":1,\"d\":901,\"p\":[1]}}",
        "{\"pf\":{\"v\":2,\"s\":1,\"d\":3600,\"p\":[1.5]}}",
        "{\"pf\":{\"v\":2,\"s\":1,\"d\":3600,\"p\":[100000001]}}",
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) CHECK(!ha_price_series_from_json(invalid[i], &s));
    CHECK(ha_price_series_from_json("{\"unit_of_measurement\":\"EUR/kWh\",\"pf\":{\"v\":2,\"s\":1792188000,\"d\":3600,\"p\":[1,null,-3]}}", &s));
    CHECK(s.count == 3 && s.price[0] == 1 && s.price[1] == HA_PRICE_MISSING && s.price[2] == -2); /* steps: 1, then -3 from 1 */
}

static void test_equal_and_negative(void)
{
    double flat[24];
    for (int i = 0; i < 24; i++) flat[i] = 0.25;
    cJSON *attrs = epex_attrs(DAY0, 3600, flat, 0, 24);
    ha_price_series_t s;
    ha_price_window_t w[HA_PRICE_MAX_WINDOWS];
    CHECK(ha_price_series_from_attrs(attrs, &s));
    size_t n = ha_price_cheap_windows(&s, DAY0, w, HA_PRICE_MAX_WINDOWS);
    CHECK(n == 1 && w[0].start_s == DAY0 && w[0].end_s == DAY0 + 86400); /* an equal-price day is cheap all day */
    cJSON_Delete(attrs);

    double neg[24];
    for (int i = 0; i < 24; i++) neg[i] = (i >= 10 && i < 14) ? -0.02 : 0.10 + i * 0.001;
    attrs = epex_attrs(DAY0, 3600, neg, 0, 24);
    CHECK(ha_price_series_from_attrs(attrs, &s) && s.price[11] == -2000);
    ha_price_stats_t st = ha_price_stats(&s, 0, 24);
    CHECK(NEAR(st.min, -0.02));
    n = ha_price_cheap_windows(&s, DAY0, w, HA_PRICE_MAX_WINDOWS);
    /* the lowest quarter (6 of 24 hours) is the 4 negative hours and the 2 cheapest others: 00-02 and 10-14 */
    CHECK(n == 2 && w[0].start_s == DAY0 && w[0].end_s == DAY0 + 2 * 3600 && w[1].start_s == DAY0 + 10 * 3600 &&
          w[1].end_s == DAY0 + 14 * 3600);
    cJSON_Delete(attrs);

    /* a cheap run broken by a gap is two windows, and one shorter than an hour is dropped */
    double v[24];
    for (int i = 0; i < 24; i++) v[i] = 0.30;
    v[2] = v[3] = v[4] = v[10] = v[15] = v[16] = v[17] = 0.10;
    attrs = epex_attrs(DAY0, 3600, v, 0, 24);
    cJSON_DeleteItemFromArray(cJSON_GetObjectItemCaseSensitive(attrs, "data"), 3); /* 03:00 has no price */
    CHECK(ha_price_series_from_attrs(attrs, &s) && s.price[3] == HA_PRICE_MISSING);
    n = ha_price_cheap_windows(&s, DAY0, w, HA_PRICE_MAX_WINDOWS);
    CHECK(n == 4);
    CHECK(w[0].start_s == DAY0 + 2 * 3600 && w[0].end_s == DAY0 + 3 * 3600);   /* cut by the gap, exactly one hour: kept */
    CHECK(w[1].start_s == DAY0 + 4 * 3600 && w[1].end_s == DAY0 + 5 * 3600);
    CHECK(w[2].start_s == DAY0 + 10 * 3600 && w[3].start_s == DAY0 + 15 * 3600 && w[3].end_s == DAY0 + 18 * 3600);
    cJSON_Delete(attrs);
}

static void test_dst_days(void)
{
    /* 2026-10-25: the clocks go back, a local day of 25 hours; the series starts at local midnight (22:00Z the day before) */
    const int64_t d = 1792879200LL; /* 2026-10-24T22:00:00Z */
    double p[49];
    for (int i = 0; i < 49; i++) p[i] = 0.30;
    for (int i = 2; i <= 8; i++) p[i] = 0.05; /* local 02:00 (CEST), the repeated 02:00 (CET), 03:00 and four more: the lowest quarter */
    cJSON *attrs = epex_attrs(d, 3600, p, 0, 49);
    ha_price_series_t s;
    CHECK(ha_price_series_from_attrs(attrs, &s) && s.count == 49);
    int first, count;
    ha_price_slot_range(&s, d, d + 25 * 3600, &first, &count);
    CHECK(count == 25);
    ha_price_window_t w[HA_PRICE_MAX_WINDOWS];
    size_t n = ha_price_cheap_windows(&s, d, w, HA_PRICE_MAX_WINDOWS);
    CHECK(n >= 1 && w[0].start_s == d + 2 * 3600 && w[0].end_s == d + 9 * 3600); /* seven consecutive hours across the repeated one */
    /* the next local day starts after 25 hours */
    ha_price_slot_range(&s, d + 25 * 3600, d + 49 * 3600, &first, &count);
    CHECK(count == 24 && first == 25);
    cJSON_Delete(attrs);

    /* 2026-03-29: the clocks go forward, a local day of 23 hours */
    const int64_t sp = 1774738800LL; /* 2026-03-28T23:00:00Z = 2026-03-29 00:00 CET */
    attrs = epex_attrs(sp, 3600, p, 0, 47);
    CHECK(ha_price_series_from_attrs(attrs, &s) && s.count == 47);
    n = ha_price_cheap_windows(&s, sp, w, HA_PRICE_MAX_WINDOWS);
    CHECK(n >= 1);
    cJSON_Delete(attrs);
}

static void test_serializer(void)
{
    cJSON *attrs = epex_attrs(DAY0, 3600, EPEX, 0, 48);
    cJSON_AddStringToObject(attrs, "unit_of_measurement", "\xE2\x82\xAC/kWh");
    cJSON_AddStringToObject(attrs, "friendly_name", "EPEX Spot Data Market Price");
    cJSON_AddStringToObject(attrs, "ignored_attribute", "x");
    char *raw = cJSON_PrintUnformatted(attrs);
    CHECK(raw != NULL && strlen(raw) > 4000); /* the source is several times the model's attribute buffer */
    cJSON_free(raw);
    char out[1280];
    CHECK(ha_client_attrs_is_price_series(attrs));
    CHECK(ha_client_serialize_price_series_compact(attrs, out, sizeof(out)));
    CHECK(strlen(out) < sizeof(out));
    cJSON *parsed = cJSON_Parse(out);
    CHECK(parsed != NULL);
    CHECK(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(parsed, "unit_of_measurement")));
    CHECK(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(parsed, "friendly_name")));
    CHECK(cJSON_GetObjectItemCaseSensitive(parsed, "ignored_attribute") == NULL);
    cJSON_Delete(parsed);
    ha_price_series_t a, b;
    CHECK(ha_price_series_from_attrs(attrs, &a) && ha_price_series_from_json(out, &b));
    CHECK(a.count == b.count && memcmp(a.price, b.price, a.count * sizeof(int32_t)) == 0);

    /* without scalars only the grid remains, and the object is still valid JSON */
    cJSON_DeleteItemFromObjectCaseSensitive(attrs, "unit_of_measurement");
    cJSON_DeleteItemFromObjectCaseSensitive(attrs, "friendly_name");
    CHECK(ha_client_serialize_price_series_compact(attrs, out, sizeof(out)));
    parsed = cJSON_Parse(out);
    CHECK(parsed != NULL && cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(parsed, "pf")));
    cJSON_Delete(parsed);

    /* a buffer that cannot hold it fails instead of cutting, and a list that is not representable fails */
    CHECK(!ha_client_serialize_price_series_compact(attrs, out, 100));
    cJSON_ReplaceItemInObjectCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(attrs, "data"), 0), "price_per_kwh",
                                           cJSON_CreateString("n/a"));
    CHECK(!ha_client_serialize_price_series_compact(attrs, out, sizeof(out)));
    cJSON_Delete(attrs);
    CHECK(!ha_client_serialize_price_series_compact(NULL, out, sizeof(out)));

    /* attributes of other sensors are not price lists */
    attrs = cJSON_Parse("{\"unit_of_measurement\":\"kWh\",\"data\":[1,2,3]}");
    CHECK(!ha_client_attrs_is_price_series(attrs));
    cJSON_Delete(attrs);
}

static void test_quarter_hours(void)
{
    cJSON *attrs = epex_attrs(DAY0, 900, EPEX_Q15, 0, 192);
    cJSON_AddStringToObject(attrs, "unit_of_measurement", "\xE2\x82\xAC/kWh");
    cJSON_AddStringToObject(attrs, "state_class", "measurement");
    cJSON_AddStringToObject(attrs, "friendly_name", "EPEX Spot Data Market Price");
    ha_price_series_t a, b;
    CHECK(ha_price_series_from_attrs(attrs, &a) && a.count == 192 && a.interval_s == 900);
    CHECK(a.price[0] == 20927 && a.price[191] == 16827);
    /* the steps keep two days of real quarter-hour prices, with the unit, the state class and the name, within the model's buffer;
     * the absolute prices did not (1301 bytes, 22 too many) and the tile said "no prices" */
    char out[1280];
    CHECK(ha_client_serialize_price_series_compact(attrs, out, sizeof(out)));
    CHECK(strlen(out) < 1100);
    cJSON *parsed = cJSON_Parse(out);
    CHECK(parsed != NULL && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(parsed, "unit_of_measurement")) &&
          cJSON_IsString(cJSON_GetObjectItemCaseSensitive(parsed, "friendly_name")));
    cJSON_Delete(parsed);
    CHECK(ha_price_series_from_json(out, &b) && b.count == 192 && b.interval_s == 900);
    CHECK(memcmp(a.price, b.price, 192 * sizeof(int32_t)) == 0);
    /* the quarter-hour windows: the lowest quarter of today by count (24 slots), at least an hour long */
    ha_price_window_t w[HA_PRICE_MAX_WINDOWS];
    size_t n = ha_price_cheap_windows(&b, DAY0, w, HA_PRICE_MAX_WINDOWS);
    CHECK(n >= 2);
    for (size_t i = 0; i < n; i++) CHECK(w[i].end_s - w[i].start_s >= 3600 && w[i].start_s % 900 == 0);

    /* when the scalars do not fit they are left out, the grid never is */
    char tight[1280];
    char pf_only[1280];
    CHECK(ha_price_series_to_json(&a, pf_only, sizeof(pf_only)));
    const size_t cap = strlen(pf_only) + 4;
    CHECK(ha_client_serialize_price_series_compact(attrs, tight, cap));
    parsed = cJSON_Parse(tight);
    CHECK(parsed != NULL && cJSON_GetObjectItemCaseSensitive(parsed, "friendly_name") == NULL && cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(parsed, "pf")));
    cJSON_Delete(parsed);
    cJSON_Delete(attrs);

    /* steps too big for 192 slots: whole hours are averaged instead of dropping the list, and the hours are exact */
    double wild[192];
    for (int i = 0; i < 192; i++) wild[i] = (i % 2) ? 9.5 : -9.5;
    attrs = epex_attrs(DAY0, 900, wild, 0, 192);
    CHECK(ha_price_series_from_attrs(attrs, &a));
    CHECK(!ha_price_series_to_json(&a, out, sizeof(out)));
    CHECK(ha_client_serialize_price_series_compact(attrs, out, sizeof(out)));
    CHECK(ha_price_series_from_json(out, &b) && b.interval_s == 3600 && b.count == 48 && b.price[0] == 0);
    cJSON_Delete(attrs);

    /* the hourly mean of real quarter hours, a group with a gap stays a gap */
    attrs = epex_attrs(DAY0, 900, EPEX_Q15, 0, 192);
    cJSON_DeleteItemFromArray(cJSON_GetObjectItemCaseSensitive(attrs, "data"), 5);
    CHECK(ha_price_series_from_attrs(attrs, &a) && a.price[5] == HA_PRICE_MISSING);
    ha_price_series_t hourly;
    CHECK(ha_price_series_to_hourly(&a, &hourly) && hourly.count == 48 && hourly.interval_s == 3600);
    CHECK(hourly.price[1] == HA_PRICE_MISSING);                      /* slots 4..7 miss slot 5 */
    CHECK(hourly.price[0] == (int32_t)llround((20927 + 20653 + 19967 + 18605) / 4.0));
    CHECK(!ha_price_series_to_hourly(&hourly, &b));                  /* already hourly */
    cJSON_Delete(attrs);
}

static cJSON *raw_list(int64_t start, const double *prices, int n)
{
    cJSON *list = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_CreateObject();
        char text[40];
        time_t t = (time_t)(start + (int64_t)i * 900);
        struct tm tm;
        gmtime_r(&t, &tm);
        snprintf(text, sizeof(text), "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        cJSON_AddStringToObject(item, "start", text);
        t += 900;
        gmtime_r(&t, &tm);
        snprintf(text, sizeof(text), "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        cJSON_AddStringToObject(item, "end", text);
        cJSON_AddNumberToObject(item, "value", prices[i]);
        cJSON_AddItemToArray(list, item);
    }
    return list;
}

/* Energy Price Forecast (and the Nord Pool custom component): raw_today holds only the slots from now on, raw_tomorrow the whole next day and
 * raw_forecast the predicted days after it. The values are the ones of Home Assistant at 22:20 local time on 2026-10-17. */
static void test_raw_shape(void)
{
    static const double REST_OF_TODAY[7] = {0.1664, 0.17535, 0.16177, 0.1614, 0.16061, 0.16287, 0.15524};
    const int64_t first = DAY0 + 22 * 3600 + 900; /* 2026-10-17T20:15:00Z */
    cJSON *attrs = cJSON_CreateObject();
    cJSON_AddItemToObject(attrs, "raw_today", raw_list(first, REST_OF_TODAY, 7));
    cJSON_AddItemToObject(attrs, "raw_tomorrow", raw_list(DAY0 + 24 * 3600, EPEX_Q15 + 96, 96));
    cJSON_AddItemToObject(attrs, "raw_forecast", raw_list(DAY0 + 48 * 3600, EPEX_Q15, 96)); /* predicted, never read */
    cJSON_AddStringToObject(attrs, "unit_of_measurement", "EUR/kWh");
    cJSON_AddStringToObject(attrs, "friendly_name", "Energy Price Forecast EU DE Preisreihe");
    cJSON_AddNumberToObject(attrs, "average", 0.163377);
    ha_price_series_t s;
    CHECK(ha_price_series_detect(attrs));
    CHECK(ha_price_series_from_attrs(attrs, &s));
    CHECK(s.start_s == first && s.interval_s == 900 && s.count == 103); /* 7 left of today and the 96 of tomorrow */
    CHECK(s.price[0] == 16640 && s.price[6] == 15524 && s.price[7] == 15328);
    char out[1280];
    CHECK(ha_client_serialize_price_series_compact(attrs, out, sizeof(out)));
    ha_price_series_t b;
    CHECK(ha_price_series_from_json(out, &b) && b.count == 103 && memcmp(s.price, b.price, 103 * sizeof(int32_t)) == 0);

    /* today is a part of a day: the seven quarter hours from 22:15, and the card's average of them is 16 cent (mean 16.33) */
    int f, c;
    ha_price_slot_range(&s, DAY0, DAY0 + 24 * 3600, &f, &c);
    CHECK(f == 0 && c == 7);
    CHECK(ha_price_average_cent(&s, f, c) == 16.0);
    ha_price_stats_t st = ha_price_stats(&s, f, c);
    CHECK(st.valid && NEAR(st.min, 0.15524) && NEAR(st.max, 0.17535));
    CHECK(ha_price_slot_at(&s, DAY0 + 22 * 3600 + 20 * 60) == 0 && ha_price_slot_at(&s, DAY0 + 22 * 3600) == -1);
    cJSON_Delete(attrs);

    /* tomorrow alone, or today alone with two slots, is enough; a single slot or other keys are not a price list */
    attrs = cJSON_CreateObject();
    cJSON_AddItemToObject(attrs, "raw_today", raw_list(first, REST_OF_TODAY, 2));
    cJSON_AddStringToObject(attrs, "unit_of_measurement", "EUR/kWh");
    CHECK(ha_price_series_detect(attrs) && ha_price_series_from_attrs(attrs, &s) && s.count == 2);
    cJSON_Delete(attrs);
    attrs = cJSON_CreateObject();
    cJSON_AddItemToObject(attrs, "raw_today", raw_list(first, REST_OF_TODAY, 1));
    CHECK(!ha_price_series_detect(attrs));
    cJSON_Delete(attrs);
    attrs = cJSON_Parse("{\"raw_today\":[{\"x\":1},{\"x\":2}],\"raw_tomorrow\":[]}");
    CHECK(!ha_price_series_detect(attrs));
    cJSON_Delete(attrs);
}

static void test_raw_units(void)
{
    const double values[] = {20.0, -5.0};
    cJSON *attrs = cJSON_CreateObject();
    cJSON_AddItemToObject(attrs, "raw_today", raw_list(DAY0, values, 2));
    cJSON_AddStringToObject(attrs, "currency", "EUR");
    ha_price_series_t s;
    CHECK(ha_price_series_detect(attrs) && !ha_price_series_from_attrs(attrs, &s)); /* unknown raw unit */
    static const struct { const char *unit; double expected; } cases[] = {
        {"c/kWh", 0.2}, {"ct/kWh", 0.2}, {"¢/kWh", 0.2},
        {"EUR/kWh", 20.0}, {"€/kWh", 20.0}, {"EUR/MWh", 0.02}, {"€/MWh", 0.02},
        {"c/MWh", 0.0002}, {"ct/Wh", 200.0},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        cJSON_DeleteItemFromObjectCaseSensitive(attrs, "unit_of_measurement");
        cJSON_AddStringToObject(attrs, "unit_of_measurement", cases[i].unit);
        CHECK(ha_price_series_from_attrs(attrs, &s));
        CHECK(NEAR(ha_price_value(&s, 0), cases[i].expected));
        CHECK(NEAR(ha_price_value(&s, 1), -cases[i].expected / 4));
    }
    cJSON_ReplaceItemInObjectCaseSensitive(attrs, "unit_of_measurement", cJSON_CreateString("c/kWh"));
    char compact[1280];
    CHECK(ha_client_serialize_price_series_compact(attrs, compact, sizeof(compact)));
    CHECK(ha_price_series_from_json(compact, &s) && NEAR(ha_price_value(&s, 0), 0.2));
    CHECK(strstr(compact, "c/kWh") != NULL); /* the source state still has its original unit for a Sensor tile */
    cJSON_ReplaceItemInObjectCaseSensitive(attrs, "currency", cJSON_CreateString("USD"));
    CHECK(!ha_price_series_from_attrs(attrs, &s)); /* cents of another currency are not euro cents */
    cJSON_ReplaceItemInObjectCaseSensitive(attrs, "currency", cJSON_CreateString("EUR"));
    cJSON_ReplaceItemInObjectCaseSensitive(attrs, "unit_of_measurement", cJSON_CreateString("NOK/kWh"));
    CHECK(!ha_price_series_from_attrs(attrs, &s));
    cJSON_DeleteItemFromObjectCaseSensitive(attrs, "currency");
    CHECK(!ha_price_series_from_attrs(attrs, &s));
    cJSON_ReplaceItemInObjectCaseSensitive(attrs, "unit_of_measurement", cJSON_CreateString("EUR/Wh"));
    CHECK(!ha_price_series_from_attrs(attrs, &s)); /* the converted value, not the input, gets the magnitude check */
    cJSON_Delete(attrs);
}

static void test_card_rounding(void)
{
    /* the card's colour rule compares whole cents: 16.64 ct is 17, 15.524 ct is 16 (half up), 15.449 ct is 15 */
    CHECK(ha_price_cent(16640) == 17);
    CHECK(ha_price_cent(15524) == 16);
    CHECK(ha_price_cent(15449) == 15);
    CHECK(ha_price_cent(15450) == 16);                  /* 15.45 ct: 154.5 tenths round up to 155, 15.5 round up to 16 */
    CHECK(ha_price_cent(-1000) == -1 && ha_price_cent(0) == 0);
    /* the average of a whole day: the mean of tenth-cent prices, in whole cents */
    cJSON *attrs = epex_attrs(DAY0, 3600, EPEX, 0, 48);
    ha_price_series_t s;
    CHECK(ha_price_series_from_attrs(attrs, &s));
    CHECK(ha_price_average_cent(&s, 0, 24) == 17.0);   /* 16.83 */
    CHECK(ha_price_average_cent(&s, 24, 24) == 18.0);  /* tomorrow's mean is 17.9 */
    CHECK(isnan(ha_price_average_cent(&s, 0, 0)));
    cJSON_Delete(attrs);
}

int main(void)
{
    setenv("TZ", "Europe/Berlin", 1);
    tzset();
    test_iso_strict();
    test_epex_shape();
    test_dao_shape();
    test_stats_and_cheap();
    test_explicit_durations_and_window_capacity();
    test_irregular_inputs();
    test_json_budget();
    test_equal_and_negative();
    test_dst_days();
    test_serializer();
    test_quarter_hours();
    test_raw_shape();
    test_raw_units();
    test_card_rounding();
    printf("price series: %d checks, %d failed\n", checks, failed);
    return failed == 0 ? 0 : 1;
}
