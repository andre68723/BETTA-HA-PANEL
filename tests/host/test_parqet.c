/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
/* The Parqet portfolio sensors: attributes of the total value sensor, numbers, gain percentage and interval labels. */
#include "ha/parqet_portfolio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "Parqet %d: %s\n", __LINE__, #c); exit(1); } } while (0)
#define NEAR(a, b) (fabs((a) - (b)) < 1e-9)

int main(void)
{
    parqet_attrs_t a;

    /* the shape measured on the integration's total value sensor (invented names and values) */
    const char *json =
        "{\"state_class\":\"total\",\"entry_id\":\"01ABC\",\"portfolio_id\":\"p1\",\"holdings_count\":35,"
        "\"top_holdings\":[{\"name\":\"Alpha Corp\",\"value\":4027.72,\"weight\":5.9},"
        "{\"name\":\"Beta \xE2\x80\x98" "B\xE2\x80\x99 Holdings\",\"value\":3116.31,\"weight\":4.6},"
        "{\"name\":\"iShares STOXX Global Select Dividend 100 UCITS ETF (DE) - EUR DIS\",\"value\":2912.68,\"weight\":4.3}],"
        "\"interval\":\"max\",\"unit_of_measurement\":\"EUR\",\"device_class\":\"monetary\"}";
    CHECK(parqet_attrs_parse(json, &a));
    CHECK(strcmp(a.interval, "max") == 0 && strcmp(a.unit, "EUR") == 0 && a.holdings_count == 35 && a.count == 3);
    CHECK(strcmp(a.entry_id, "01ABC") == 0 && strcmp(a.portfolio_id, "p1") == 0);
    CHECK(strcmp(a.holding[0].name, "Alpha Corp") == 0 && NEAR(a.holding[0].value, 4027.72) && NEAR(a.holding[0].weight, 5.9));
    CHECK(strstr(a.holding[1].name, "\xE2\x80\x98" "B") != NULL); /* curly quotes survive */
    CHECK(strlen(a.holding[2].name) == PARQET_NAME_MAX - 1 && strncmp(a.holding[2].name, "iShares STOXX", 13) == 0);

    /* a name is cut at a character boundary, never inside a multi-byte sequence */
    char many[200] = "{\"top_holdings\":[{\"name\":\"";
    for (int i = 0; i < 30; i++) strcat(many, "\xC3\xA4"); /* 30 x a-umlaut = 60 bytes */
    strcat(many, "\",\"value\":1,\"weight\":1}]}");
    CHECK(parqet_attrs_parse(many, &a) && a.count == 1);
    CHECK(strlen(a.holding[0].name) % 2 == 0 && strlen(a.holding[0].name) <= PARQET_NAME_MAX - 1);
    for (const unsigned char *p = (const unsigned char *)a.holding[0].name; *p; p += 2) CHECK(p[0] == 0xC3 && p[1] == 0xA4);

    /* more than five entries, bad entries and missing parts are skipped, not fatal */
    CHECK(parqet_attrs_parse("{\"top_holdings\":[{\"name\":\"A\",\"value\":1,\"weight\":1},{\"name\":\"\",\"value\":1,\"weight\":1},"
                             "{\"name\":\"B\",\"weight\":2},{\"name\":\"C\",\"value\":3,\"weight\":-1},{\"name\":\"D\",\"value\":4,\"weight\":4},"
                             "{\"name\":\"E\",\"value\":5,\"weight\":5},{\"name\":\"F\",\"value\":6,\"weight\":6},{\"name\":\"G\",\"value\":7,\"weight\":7},"
                             "{\"name\":\"H\",\"value\":8,\"weight\":8},{\"name\":\"I\",\"value\":9,\"weight\":9}]}", &a));
    CHECK(a.count == PARQET_MAX_HOLDINGS && strcmp(a.holding[0].name, "A") == 0 && strcmp(a.holding[1].name, "D") == 0);
    CHECK(parqet_attrs_parse("{}", &a) && a.count == 0 && a.interval[0] == '\0' && a.holdings_count == 0);
    CHECK(parqet_attrs_parse("{\"top_holdings\":\"x\",\"interval\":5,\"holdings_count\":-3}", &a) && a.count == 0 && a.holdings_count == 0);
    CHECK(!parqet_attrs_parse("", &a) && !parqet_attrs_parse(NULL, &a) && !parqet_attrs_parse("[1]", &a) && !parqet_attrs_parse("{bad", &a));
    CHECK(!parqet_attrs_parse("{}", NULL));

    /* states */
    double v = 0;
    CHECK(parqet_number("68199.60473366", &v) && NEAR(v, 68199.60473366));
    CHECK(parqet_number("-136.31", &v) && NEAR(v, -136.31));
    CHECK(parqet_number("0", &v) && v == 0.0);
    CHECK(!parqet_number("unknown", &v) && !parqet_number("unavailable", &v) && !parqet_number("", &v) && !parqet_number(NULL, &v));
    CHECK(!parqet_number("12abc", &v) && !parqet_number("nan", &v) && !parqet_number("inf", &v));

    char label[8];
    parqet_interval_label("max", label, sizeof(label));
    CHECK(strcmp(label, "Max") == 0);
    parqet_interval_label("1y", label, sizeof(label));
    CHECK(strcmp(label, "1Y") == 0);
    parqet_interval_label("ytd", label, sizeof(label));
    CHECK(strcmp(label, "YTD") == 0);
    parqet_interval_label("", label, sizeof(label));
    CHECK(label[0] == '\0');
    parqet_interval_label("10y", label, 3);
    CHECK(strcmp(label, "10") == 0); /* small buffer: cut, terminated */

    /* number formats */
    char num[40];
    parqet_format_number(num, sizeof(num), 68199.60473366, 0, true, false);
    CHECK(strcmp(num, "68.200") == 0);
    parqet_format_number(num, sizeof(num), 68199.60473366, 2, true, false);
    CHECK(strcmp(num, "68.199,60") == 0);
    parqet_format_number(num, sizeof(num), 68199.60473366, 2, false, false);
    CHECK(strcmp(num, "68,199.60") == 0);
    parqet_format_number(num, sizeof(num), 13361.42, 0, true, true);
    CHECK(strcmp(num, "+13.361") == 0);
    parqet_format_number(num, sizeof(num), -136.31, 0, true, true);
    CHECK(strcmp(num, "\xE2\x88\x92" "136") == 0);
    parqet_format_number(num, sizeof(num), 1234567.0, 0, false, false);
    CHECK(strcmp(num, "1,234,567") == 0);
    parqet_format_number(num, sizeof(num), 999.0, 0, true, false);
    CHECK(strcmp(num, "999") == 0);
    parqet_format_number(num, sizeof(num), 24.366, 1, true, true);
    CHECK(strcmp(num, "+24,4") == 0);
    parqet_format_number(num, sizeof(num), -0.04, 1, true, true);
    CHECK(strcmp(num, "0,0") == 0); /* rounds to zero: no sign */
    parqet_format_number(num, sizeof(num), 0.0, 0, true, true);
    CHECK(strcmp(num, "0") == 0);
    parqet_format_number(num, sizeof(num), NAN, 0, true, false);
    CHECK(num[0] == '\0');
    parqet_format_number(num, 4, 123456.0, 0, true, false);
    CHECK(strlen(num) <= 3); /* small buffer: terminated, never overrun */
    CHECK(strcmp(parqet_currency_symbol("EUR"), "\xE2\x82\xAC") == 0 && strcmp(parqet_currency_symbol("USD"), "$") == 0);
    CHECK(strcmp(parqet_currency_symbol("CHF"), "CHF") == 0 && strcmp(parqet_currency_symbol(NULL), "") == 0);


    /* a sensor whose attributes exceed the model's buffer keeps ids, interval, unit and the positions */
    {
        char big[2400];
        int n = snprintf(big, sizeof(big),
            "{\"state_class\":\"total\",\"entry_id\":\"01JABCDEFGHJKMNPQRSTVWXYZ0\",\"portfolio_id\":\"6657aa00bb11cc22dd33ee44\","
            "\"holdings_count\":35,\"interval\":\"1y\",\"unit_of_measurement\":\"EUR\",\"device_class\":\"monetary\","
            "\"friendly_name\":\"Depot Beispiel Gesamtwert\",\"attribution\":\"%0700d\",\"top_holdings\":[", 0);
        for (int i = 0; i < 5; i++) {
            n += snprintf(big + n, sizeof(big) - (size_t)n,
                "%s{\"name\":\"iShares STOXX Global Select Dividend 100 UCITS ETF (DE) - EUR DIS %d\",\"value\":%d.123456789,\"weight\":%d.987654321}",
                i ? "," : "", i, 4000 - i, 5 - i);
        }
        snprintf(big + n, sizeof(big) - (size_t)n, "]}");
        CHECK(strlen(big) > 1279);
        cJSON *src = cJSON_Parse(big);
        CHECK(parqet_attrs_is_portfolio(src));
        char out[1280];
        CHECK(parqet_attrs_compact(src, out, sizeof(out)) && strlen(out) < sizeof(out));
        parqet_attrs_t full, small;
        CHECK(parqet_attrs_parse(big, &full) && parqet_attrs_parse(out, &small));
        CHECK(small.count == 5 && strcmp(small.entry_id, full.entry_id) == 0 && strcmp(small.portfolio_id, full.portfolio_id) == 0);
        CHECK(strcmp(small.interval, "1y") == 0 && strcmp(small.unit, "EUR") == 0 && small.holdings_count == 35);
        for (int i = 0; i < 5; i++) {
            CHECK(strcmp(small.holding[i].name, full.holding[i].name) == 0);
            CHECK(fabs(small.holding[i].value - full.holding[i].value) < 0.006 && fabs(small.holding[i].weight - full.holding[i].weight) < 0.006);
        }
        CHECK(strstr(out, "device_class") != NULL && strstr(out, "attribution") == NULL);
        /* a tighter buffer drops metadata first, then trailing positions, and stays valid JSON */
        char tight[420];
        CHECK(parqet_attrs_compact(src, tight, sizeof(tight)));
        cJSON *check = cJSON_Parse(tight);
        CHECK(cJSON_IsObject(check) && strstr(tight, "friendly_name") == NULL);
        parqet_attrs_t part;
        CHECK(parqet_attrs_parse(tight, &part) && part.count > 0 && part.count < 5 && strcmp(part.holding[0].name, full.holding[0].name) == 0);
        cJSON_Delete(check);
        /* no room for the ids: the result is the empty object, reported as false */
        char none[40];
        CHECK(!parqet_attrs_compact(src, none, sizeof(none)) && strcmp(none, "{}") == 0);
        cJSON_Delete(src);
        /* other sensors, and a portfolio sensor without ids, are not claimed */
        cJSON *other = cJSON_Parse("{\"unit_of_measurement\":\"EUR\"}");
        CHECK(!parqet_attrs_is_portfolio(other) && !parqet_attrs_compact(other, out, sizeof(out)) && strcmp(out, "{}") == 0);
        cJSON_Delete(other);
        other = cJSON_Parse("{\"portfolio_id\":\"p\",\"top_holdings\":[]}");
        CHECK(parqet_attrs_is_portfolio(other) && !parqet_attrs_compact(other, out, sizeof(out)));
        cJSON_Delete(other);
        CHECK(!parqet_attrs_is_portfolio(NULL));
    }

    printf("parqet portfolio: %u checks passed\n", checks);
    return 0;
}
