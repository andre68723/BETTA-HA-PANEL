/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
/* Stocks & ETFs quotes (ha_quote): Easy Stock, the session's change, numbers, currencies and formatting. Synthetic values. */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "cJSON.h"
#include "ha/ha_display_precision.h"
#include "ha/ha_layout_entities.h"
#include "ha/ha_quote.h"

static int s_failures = 0;

#define Q_CHECK(cond) do { if (!(cond)) { s_failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define Q_NEAR(a, b) Q_CHECK(fabs((a) - (b)) < 1e-6)

/* Easy Stock v0.5.1. */
static const char *const EASY_ATTRS =
    "{\"state_class\":\"measurement\",\"symbol\":\"DEMO.DE\",\"long_name\":\"Demo Global Equity UCITS ETF USD Acc\","
    "\"currency\":\"EUR\",\"market_state\":\"CLOSED\",\"change\":0.095,\"change_pct\":0.38,\"previous_close\":25.145,"
    "\"price_is_live\":false,\"traded_today\":true,\"unit_of_measurement\":\"EUR\",\"friendly_name\":\"Demo ETF\"}";

static int s_platform_changes = 0;
static void test_platform_changed(const char *entity_id)
{
    (void)entity_id;
    s_platform_changes++;
}

/* The registry entries also name each entity's integration, which the Stocks & ETFs widget reads (ha_quote.h). */
static void test_registry_platform(void)
{
    static char ids[4][APP_MAX_ENTITY_ID_LEN];
    memset(ids, 0, sizeof(ids));
    snprintf(ids[0], sizeof(ids[0]), "%s", "sensor.quote_first");
    snprintf(ids[1], sizeof(ids[1]), "%s", "sensor.quote_easy");
    snprintf(ids[2], sizeof(ids[2]), "%s", "sensor.quote_other");
    snprintf(ids[3], sizeof(ids[3]), "%s", "sensor.quote_missing");
    Q_CHECK(ha_display_precision_platform("sensor.quote_first") == NULL); /* never asked: pending */
    ha_display_precision_set_wanted(&ids[0][0], 4);
    cJSON *req = ha_display_precision_next_request(21, 5000);
    cJSON_Delete(req);
    cJSON *result = cJSON_Parse("{\"sensor.quote_first\":{\"platform\":\"easy_stock\"},"
        "\"sensor.quote_easy\":{\"platform\":\"easy_stock\"},\"sensor.quote_other\":{\"platform\":\"template\"}}");
    const int before = s_platform_changes;
    Q_CHECK(ha_display_precision_handle_result(21, true, result, test_platform_changed));
    cJSON_Delete(result);
    Q_CHECK(s_platform_changes - before == 4);
    Q_CHECK(strcmp(ha_display_precision_platform("sensor.quote_first"), "easy_stock") == 0);
    Q_CHECK(strcmp(ha_display_precision_platform("sensor.quote_easy"), "easy_stock") == 0);
    Q_CHECK(strcmp(ha_display_precision_platform("sensor.quote_other"), "other") == 0);
    Q_CHECK(strcmp(ha_display_precision_platform("sensor.quote_missing"), "other") == 0); /* no registry entry */
    Q_CHECK(ha_quote_provider_from_platform(ha_display_precision_platform("sensor.quote_easy")) == HA_QUOTE_PROVIDER_EASY_STOCK);

    /* An unanswered batch is a failed lookup, not a pending one: the widget can say so. */
    ha_display_precision_set_wanted(&ids[0][0], 1);
    req = ha_display_precision_next_request(22, 6000);
    cJSON_Delete(req);
    Q_CHECK(!ha_display_precision_is_due(6000 + HA_DISPLAY_PRECISION_TIMEOUT_MS));
    Q_CHECK(strcmp(ha_display_precision_platform("sensor.quote_first"), "") == 0);
    Q_CHECK(ha_quote_provider_from_platform("") == HA_QUOTE_PROVIDER_FAILED);
    /* the next lookup answers again */
    ha_display_precision_set_wanted(&ids[0][0], 1);
    req = ha_display_precision_next_request(23, 7000);
    cJSON_Delete(req);
    result = cJSON_Parse("{\"sensor.quote_first\":{\"platform\":\"easy_stock\"}}");
    Q_CHECK(ha_display_precision_handle_result(23, true, result, test_platform_changed));
    cJSON_Delete(result);
    Q_CHECK(strcmp(ha_display_precision_platform("sensor.quote_first"), "easy_stock") == 0);
    /* More layout changes than the cache can hold must not leave the new quote permanently pending. */
    for (int i = 0; i < HA_LAYOUT_ENTITY_CAPACITY + 8; i++) {
        snprintf(ids[1], sizeof(ids[1]), "sensor.changed_quote_%d", i);
        ha_display_precision_set_wanted(&ids[0][0], 2);
        const uint32_t msg = (uint32_t)(100 + i);
        req = ha_display_precision_next_request(msg, 8000 + i);
        cJSON_Delete(req);
        result = cJSON_CreateObject();
        cJSON *entry = cJSON_AddObjectToObject(result, ids[0]);
        cJSON_AddStringToObject(entry, "platform", "easy_stock");
        entry = cJSON_AddObjectToObject(result, ids[1]);
        cJSON_AddStringToObject(entry, "platform", "easy_stock");
        Q_CHECK(ha_display_precision_handle_result(msg, true, result, test_platform_changed));
        cJSON_Delete(result);
        Q_CHECK(strcmp(ha_display_precision_platform(ids[1]), "easy_stock") == 0);
        Q_CHECK(strcmp(ha_display_precision_platform(ids[0]), "easy_stock") == 0);
    }
    /* A multi-page stock layout can exceed the old 256-entry registry cache in one lookup. */
    static char many[1056][APP_MAX_ENTITY_ID_LEN];
    for (size_t i = 0; i < 1056; i++) snprintf(many[i], sizeof(many[i]), "sensor.stock_%zu", i);
    Q_CHECK(ha_display_precision_set_wanted((const char *)many, 1056));
    uint32_t message = 10000;
    size_t loaded = 0;
    while (ha_display_precision_is_due(0)) {
        cJSON *request = ha_display_precision_next_request(message, 0);
        Q_CHECK(request != NULL);
        if (!request) break;
        cJSON *result = cJSON_CreateObject();
        const cJSON *requested;
        cJSON_ArrayForEach(requested, cJSON_GetObjectItemCaseSensitive(request, "entity_ids")) {
            cJSON *entry = cJSON_AddObjectToObject(result, requested->valuestring);
            cJSON_AddStringToObject(entry, "platform", "easy_stock");
            loaded++;
        }
        Q_CHECK(ha_display_precision_handle_result(message++, true, result, test_platform_changed));
        cJSON_Delete(result);
        cJSON_Delete(request);
    }
    Q_CHECK(loaded == 1056);
    Q_CHECK(strcmp(ha_display_precision_platform(many[1055]), "easy_stock") == 0);
    Q_CHECK(ha_display_precision_set_wanted(NULL, 0));
    Q_CHECK(!ha_display_precision_is_due(0));
    Q_CHECK(ha_display_precision_platform(many[1055]) == NULL);
}

bool test_quote(void)
{
    const int failures = s_failures;
    ha_quote_t q;
    test_registry_platform();

    Q_CHECK(ha_quote_provider_from_platform(NULL) == HA_QUOTE_PROVIDER_PENDING);
    Q_CHECK(ha_quote_provider_from_platform("") == HA_QUOTE_PROVIDER_FAILED);
    Q_CHECK(ha_quote_provider_from_platform("yahoofinance") == HA_QUOTE_PROVIDER_UNSUPPORTED); /* not read any more */
    Q_CHECK(ha_quote_provider_from_platform("easy_stock") == HA_QUOTE_PROVIDER_EASY_STOCK);
    Q_CHECK(ha_quote_provider_from_platform("template") == HA_QUOTE_PROVIDER_UNSUPPORTED);

    double v = 0.0;
    Q_CHECK(ha_quote_number("208.65", &v) && v == 208.65);
    Q_CHECK(ha_quote_number("-0.5", &v) && v == -0.5);
    Q_CHECK(!ha_quote_number("", &v) && !ha_quote_number("unknown", &v) && !ha_quote_number("12 EUR", &v));
    Q_CHECK(!ha_quote_number("nan", &v) && !ha_quote_number("inf", &v) && !ha_quote_number("1e999", &v));
    Q_CHECK(!ha_quote_number(" 1", &v) && !ha_quote_number(NULL, &v));

    /* Easy Stock names the currency and the long name; the change is the last session's. */
    Q_CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, "25.24", EASY_ATTRS, &q));
    Q_CHECK(q.available && strcmp(q.currency, "EUR") == 0);
    Q_CHECK(strcmp(q.name, "Demo ETF") == 0 && strcmp(q.long_name, "Demo Global Equity UCITS ETF USD Acc") == 0);
    Q_CHECK(q.has_change && q.change == 0.095 && q.change_pct == 0.38 && q.prev_close == 25.145);
    Q_CHECK(q.market == HA_QUOTE_MARKET_CLOSED && q.has_live && !q.price_is_live);

    /* A closed market keeps the last session's change; it is not zero. */
    Q_CHECK(q.change != 0.0);

    /* Missing change: derived from a valid previous close, never assumed zero; no previous close, no change. */
    Q_CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, "110",
                           "{\"currency\":\"USD\",\"previous_close\":100}", &q));
    Q_CHECK(q.has_change && fabs(q.change - 10.0) < 1e-9 && q.has_change_pct && fabs(q.change_pct - 10.0) < 1e-9);
    Q_CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, "110", "{\"currency\":\"USD\",\"previous_close\":0}", &q));
    Q_CHECK(!q.has_change && !q.has_change_pct && !q.has_prev_close);
    Q_CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, "110", "{\"currency\":\"USD\"}", &q));
    Q_CHECK(q.available && !q.has_change && !q.has_change_pct);
    /* a change and a previous close that describe another price are dropped */
    Q_CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, "110", "{\"change\":3,\"change_pct\":3,\"previous_close\":100}", &q));
    Q_CHECK(!q.has_change && !q.has_change_pct);

    /* Pence stay pence; numbers given as strings count. */
    Q_CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, "1234.5",
                           "{\"unit_of_measurement\":\"GBp\",\"change\":\"-4.5\",\"previous_close\":1239}", &q));
    Q_CHECK(strcmp(q.currency, "GBp") == 0 && q.has_change && q.change == -4.5 && q.has_change_pct);
    Q_NEAR(q.change_pct, -4.5 / 1239.0 * 100.0);

    /* Unknown and unavailable states: not available, attributes still read. */
    Q_CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, "unavailable", EASY_ATTRS, &q));
    Q_CHECK(!q.available && !q.has_change && strcmp(q.symbol, "DEMO.DE") == 0);
    Q_CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, "unknown", "not json", &q) && !q.available);
    Q_CHECK(!ha_quote_parse(HA_QUOTE_PROVIDER_UNSUPPORTED, "1", "{}", &q));
    Q_CHECK(!ha_quote_parse(HA_QUOTE_PROVIDER_PENDING, "1", "{}", &q));
    Q_CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, "1", "{\"market_state\":\"REGULAR\"}", &q) && q.market == HA_QUOTE_MARKET_OPEN);
    Q_CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, "1", "{\"market_state\":\"PREPRE\"}", &q) && q.market == HA_QUOTE_MARKET_PRE);
    Q_CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, "1", "{\"market_state\":\"HALTED\"}", &q) && q.market == HA_QUOTE_MARKET_UNKNOWN);

    /* A name padded to a fixed width reads as words. */
    Q_CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, "1", "{\"friendly_name\":\"  SAP SE                        I \"}", &q));
    Q_CHECK(strcmp(q.name, "SAP SE I") == 0);

    /* A long Unicode name is cut at the buffer, not overflowed. */
    char attrs[600];
    char name[300] = "";
    for (int i = 0; i < 60; i++) strcat(name, "\xC3\x84");
    snprintf(attrs, sizeof(attrs), "{\"friendly_name\":\"%s\"}", name);
    Q_CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, "1", attrs, &q) && strlen(q.name) == 62); /* 31 whole characters */
    Q_CHECK(((unsigned char)q.name[61] & 0xC0) == 0x80);

    /* The precision of the source, between 2 and 4 digits. */
    Q_CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, "25.145", "{}", &q) && ha_quote_price_decimals(&q) == 3);
    Q_CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, "25", "{}", &q) && ha_quote_price_decimals(&q) == 2);
    Q_CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, "0.123456", "{}", &q) && ha_quote_price_decimals(&q) == 6);

    Q_CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, "4.35e-06",
        "{\"traded_today\":false,\"previous_close\":0.000004,\"change\":0.00000035,\"change_pct\":8.75}", &q));
    Q_CHECK(q.has_traded_today && !q.traded_today && q.has_change_pct && q.change_pct == 8.75);
    Q_CHECK(q.price_decimals == 8 && ha_quote_price_decimals(&q) == 8);
    char small[48];
    ha_quote_format_number(small, sizeof(small), q.price, ha_quote_price_decimals(&q), true, false);
    Q_CHECK(strcmp(small, "0,00000435") == 0);
    Q_CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, "0.00000435", "{\"traded_today\":true}", &q));
    Q_CHECK(q.has_traded_today && q.traded_today && ha_quote_price_decimals(&q) == 8);
    Q_CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, "1e-30", "{\"traded_today\":\"false\"}", &q));
    Q_CHECK(!q.has_traded_today);
    ha_quote_format_number(small, sizeof(small), q.price, ha_quote_price_decimals(&q), false, false);
    Q_CHECK(strcmp(small, "1e-30") == 0);
    Q_CHECK(ha_quote_parse(HA_QUOTE_PROVIDER_EASY_STOCK, "2.5241234e+1", "{}", &q));
    Q_CHECK(q.price_decimals == 6 && ha_quote_price_decimals(&q) == 4);

    char text[48];
    ha_quote_format_number(text, sizeof(text), 13361.4, 2, true, false);
    Q_CHECK(strcmp(text, "13.361,40") == 0);
    ha_quote_format_number(text, sizeof(text), 13361.4, 2, false, false);
    Q_CHECK(strcmp(text, "13,361.40") == 0);
    ha_quote_format_number(text, sizeof(text), 0.38, 2, true, true);
    Q_CHECK(strcmp(text, "+0,38") == 0);
    ha_quote_format_number(text, sizeof(text), -0.004, 2, false, true);
    Q_CHECK(strcmp(text, "0.00") == 0);
    ha_quote_format_number(text, sizeof(text), -1234567.891, 1, false, true);
    Q_CHECK(strcmp(text, "-1,234,567.9") == 0);
    ha_quote_format_number(text, sizeof(text), 999.999, 2, true, false);
    Q_CHECK(strcmp(text, "1.000,00") == 0);
    ha_quote_format_number(text, 5, 13361.4, 2, true, false);
    Q_CHECK(strlen(text) == 4);

    Q_CHECK(strcmp(ha_quote_currency_sign("EUR"), "\xE2\x82\xAC") == 0);
    Q_CHECK(strcmp(ha_quote_currency_sign("USD"), "$") == 0);
    Q_CHECK(strcmp(ha_quote_currency_sign("GBp"), "GBp") == 0);
    return s_failures == failures;
}
