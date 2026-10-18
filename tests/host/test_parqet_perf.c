/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
/* The reply of parqet/get_performance reduced for the portfolio widget. The shape is the measured one (2026-10-18); names and figures are invented. */
#include "ha/parqet_performance.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "Parqet perf %d: %s\n", __LINE__, #c); exit(1); } } while (0)
#define NEAR(a, b) (fabs((a) - (b)) < 1e-6)

static const char *HOLDING =
    "{\"id\":\"h%d\",\"nickname\":%s,\"asset\":%s,\"position\":{\"currentValue\":%.2f,\"isSold\":%s},"
    "\"performance\":{\"unrealizedGains\":{\"inInterval\":{\"gainGross\":%.2f,\"returnGross\":%.2f}}}}";

static cJSON *reply(int chart_points)
{
    char holdings[4096] = "";
    char one[512];
    snprintf(one, sizeof(one), HOLDING, 1, "\"Cash account\"", "{\"type\":\"cash\"}", 720.5, "false", 0.0, 0.0);
    strcat(holdings, one);
    snprintf(one, sizeof(one), HOLDING, 2, "null", "{\"type\":\"security\",\"name\":\"Alpha Utilities\"}", 4000.0, "false", 80.5, 2.05);
    strcat(holdings, ","); strcat(holdings, one);
    snprintf(one, sizeof(one), HOLDING, 3, "null", "{\"type\":\"security\",\"name\":\"Old Corp\"}", 9999.0, "true", 1.0, 1.0);
    strcat(holdings, ","); strcat(holdings, one);
    snprintf(one, sizeof(one), HOLDING, 4, "null", "{\"type\":\"security\",\"name\":\"Beta Global Dividend ETF\"}", 3000.0, "false", -11.0, -0.37);
    strcat(holdings, ","); strcat(holdings, one);
    snprintf(one, sizeof(one), HOLDING, 5, "\"Crypto nick\"", "{\"type\":\"crypto\"}", 1500.0, "false", 20.0, 1.35);
    strcat(holdings, ","); strcat(holdings, one);
    char charts[32768] = "";
    for (int i = 0; i < chart_points; i++) {
        snprintf(one, sizeof(one), "%s{\"mark\":\"eod\",\"date\":\"2026-09-%02d\",\"values\":{\"history\":%.2f,\"capitalHistory\":1}}", i ? "," : "", 1 + i % 28, 1000.0 + i);
        strcat(charts, one);
    }
    char json[40000];
    snprintf(json, sizeof(json),
        "{\"performance\":{\"kpis\":{\"inInterval\":{\"xirr\":0,\"ttwror\":1.02}},\"fees\":{\"inInterval\":{\"fees\":1.5}},\"taxes\":{\"inInterval\":{\"taxes\":52.46}},"
        "\"unrealizedGains\":{\"inInterval\":{\"gainGross\":469.59,\"returnGross\":0.69}},\"realizedGains\":{\"inInterval\":{\"gainGross\":-3.2}},"
        "\"dividends\":{\"inInterval\":{\"gainGross\":211.21}},\"valuation\":{\"atIntervalStart\":66371.98,\"atIntervalEnd\":68199.6}},"
        "\"holdings\":[%s],\"interval\":{\"start\":\"2026-09-18\",\"end\":\"2026-10-18\"},\"charts\":[%s]}", holdings, charts);
    return cJSON_Parse(json);
}

int main(void)
{
    parqet_perf_t p;
    cJSON *r = reply(32);
    CHECK(r != NULL && parqet_perf_parse(r, "1m", &p));
    CHECK(strcmp(p.interval, "1m") == 0 && NEAR(p.value_start, 66371.98) && NEAR(p.value_end, 68199.6));
    CHECK(NEAR(p.unrealized_gain, 469.59) && NEAR(p.unrealized_return, 0.69) && NEAR(p.realized_gain, -3.2));
    CHECK(NEAR(p.dividends, 211.21) && NEAR(p.taxes, 52.46) && NEAR(p.fees, 1.5) && NEAR(p.xirr, 0.0) && NEAR(p.ttwror, 1.02));

    /* securities still held, largest first: the cash account and the sold holding are left out, a crypto holding is named by its nickname */
    CHECK(p.largest_count == 3);
    CHECK(strcmp(p.largest[0].name, "Alpha Utilities") == 0 && NEAR(p.largest[0].value, 4000.0) && NEAR(p.largest[0].gain, 80.5) && p.largest[0].has_return);
    CHECK(NEAR(p.largest[0].weight, 4000.0 / 68199.6 * 100.0) && NEAR(p.largest[0].ret, 2.05));
    CHECK(strcmp(p.largest[1].name, "Beta Global Dividend ETF") == 0 && NEAR(p.largest[1].ret, -0.37));
    CHECK(strcmp(p.largest[2].name, "Crypto nick") == 0);
    /* by return: three holdings are all in the best list, nothing is left for the worst */
    CHECK(p.top_count == 3 && strcmp(p.top[0].name, "Alpha Utilities") == 0 && strcmp(p.top[1].name, "Crypto nick") == 0 && strcmp(p.top[2].name, "Beta Global Dividend ETF") == 0);
    CHECK(p.flop_count == 0);

    /* the curve: 32 points are kept as they are, the first and the last stay */
    CHECK(p.point_count == 32 && p.point[0] == 1000.0f && p.point[31] == 1031.0f);
    cJSON_Delete(r);

    /* a long curve is thinned to the limit with both ends kept */
    r = reply(200);
    CHECK(r != NULL && parqet_perf_parse(r, "ytd", &p));
    CHECK(p.point_count == PARQET_PERF_POINTS && p.point[0] == 1000.0f && p.point[PARQET_PERF_POINTS - 1] == 1199.0f);
    for (int i = 1; i < p.point_count; i++) CHECK(p.point[i] >= p.point[i - 1]); /* still monotonic: nothing out of order */
    cJSON_Delete(r);

    /* without holdings or curve the figures still come through; without the valuation nothing does */
    r = cJSON_Parse("{\"performance\":{\"valuation\":{\"atIntervalStart\":10,\"atIntervalEnd\":12}}}");
    CHECK(parqet_perf_parse(r, "1d", &p) && p.largest_count == 0 && p.top_count == 0 && p.flop_count == 0 && p.point_count == 0 && NEAR(p.value_end, 12) && NEAR(p.unrealized_gain, 0));
    cJSON_Delete(r);
    r = cJSON_Parse("{\"performance\":{\"valuation\":{\"atIntervalStart\":10}}}");
    CHECK(!parqet_perf_parse(r, "1d", &p));
    cJSON_Delete(r);
    r = cJSON_Parse("{\"performance\":[]}");
    CHECK(!parqet_perf_parse(r, "1d", &p));
    cJSON_Delete(r);
    CHECK(!parqet_perf_parse(NULL, "1d", &p) && !parqet_perf_parse(cJSON_CreateArray(), "1d", &p));
    CHECK(!parqet_perf_parse(NULL, "1d", NULL));

    /* twelve holdings: five largest by value, five best and five worst by return, each in order */
    char big[8192] = "{\"performance\":{\"valuation\":{\"atIntervalStart\":1,\"atIntervalEnd\":100000}},\"holdings\":[";
    for (int i = 0; i < 12; i++) {
        char one[300];
        snprintf(one, sizeof(one), "%s{\"asset\":{\"type\":\"security\",\"name\":\"H%02d\"},\"position\":{\"currentValue\":%d,\"isSold\":false},"
            "\"performance\":{\"unrealizedGains\":{\"inInterval\":{\"gainGross\":1,\"returnGross\":%d}}}}", i ? "," : "", i, (i * 7) % 12 * 100 + 100, (i * 5) % 12 - 6);
        strcat(big, one);
    }
    strcat(big, "]}");
    r = cJSON_Parse(big);
    CHECK(parqet_perf_parse(r, "1w", &p) && p.largest_count == PARQET_PERF_RANK && p.top_count == PARQET_PERF_RANK && p.flop_count == PARQET_PERF_RANK);
    for (int i = 1; i < PARQET_PERF_RANK; i++) {
        CHECK(p.largest[i - 1].value >= p.largest[i].value);
        CHECK(p.top[i - 1].ret >= p.top[i].ret);
        CHECK(p.flop[i - 1].ret <= p.flop[i].ret);
    }
    CHECK(NEAR(p.largest[0].value, 1200.0) && NEAR(p.top[0].ret, 5.0) && NEAR(p.flop[0].ret, -6.0));
    CHECK(p.top[PARQET_PERF_RANK - 1].ret > p.flop[PARQET_PERF_RANK - 1].ret); /* the two lists do not overlap */
    cJSON_Delete(r);

    printf("parqet performance: %u checks passed\n", checks);
    return 0;
}
