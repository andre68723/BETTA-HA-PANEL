/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
/* The price forecast widget in layouts: accepted entity domain and size limits, and that the panel subscribes to its sensor. */
#include "layout/layout_validate.h"
#include "layout/widget_types.h"
#include "ha/ha_layout_entities.h"
#include "app_config.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static size_t dense_overflow_count;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "Price layout %d: %s\n", __LINE__, #c); exit(1); } } while (0)

static void dense_overflow(size_t capacity, const char *id)
{
    (void)capacity;
    (void)id;
    dense_overflow_count++;
}

static void test_dense_stock_layout(void)
{
    cJSON *layout = cJSON_CreateObject();
    cJSON_AddNumberToObject(layout, "version", 1);
    cJSON *pages = cJSON_AddArrayToObject(layout, "pages");
    const int columns = APP_CONTENT_BOX_WIDTH / 240;
    for (int p = 0; p < APP_MAX_PAGES; p++) {
        char name[32];
        cJSON *page = cJSON_CreateObject();
        cJSON_AddItemToArray(pages, page);
        snprintf(name, sizeof(name), "p%d", p);
        cJSON_AddStringToObject(page, "id", name);
        cJSON_AddStringToObject(page, "title", "P");
        cJSON *widgets = cJSON_AddArrayToObject(page, "widgets");
        for (int w = 0; w < 11; w++) {
            const int first = (p * 11 + w) * 8;
            cJSON *widget = cJSON_CreateObject();
            cJSON_AddItemToArray(widgets, widget);
            snprintf(name, sizeof(name), "w%d_%d", p, w);
            cJSON_AddStringToObject(widget, "id", name);
            cJSON_AddStringToObject(widget, "title", "S");
            cJSON_AddStringToObject(widget, "type", "stock_quotes");
            snprintf(name, sizeof(name), "sensor.s%d", first);
            cJSON_AddStringToObject(widget, "entity_id", name);
            cJSON *more = cJSON_AddArrayToObject(widget, "stock_entities");
            for (int i = 1; i < 8; i++) {
                snprintf(name, sizeof(name), "sensor.s%d", first + i);
                cJSON_AddItemToArray(more, cJSON_CreateString(name));
            }
            cJSON *rect = cJSON_AddObjectToObject(widget, "rect");
            cJSON_AddNumberToObject(rect, "x", (w % columns) * 240);
            cJSON_AddNumberToObject(rect, "y", (w / columns) * 120);
            cJSON_AddNumberToObject(rect, "w", 240);
            cJSON_AddNumberToObject(rect, "h", 120);
        }
    }
    char *json = cJSON_PrintUnformatted(layout);
    CHECK(json != NULL && strlen(json) <= APP_LAYOUT_MAX_JSON_LEN);
    layout_validation_result_t errors = {0};
    CHECK(layout_validate_json(json, &errors));
    char *ids = calloc(HA_LAYOUT_ENTITY_CAPACITY, APP_MAX_ENTITY_ID_LEN);
    CHECK(ids != NULL);
    ha_layout_entity_set_t set = {.ids = ids, .capacity = HA_LAYOUT_ENTITY_CAPACITY, .on_overflow = dense_overflow};
    ha_layout_entities_collect(layout, &set);
    const size_t expected = APP_MAX_PAGES * 11 * 8;
    CHECK(set.count == expected && dense_overflow_count == 0);
    char last[32];
    snprintf(last, sizeof(last), "sensor.s%zu", expected - 1);
    CHECK(ha_layout_entities_contains(ids, set.count, last));
    memset(ids, 0, HA_LAYOUT_ENTITY_CAPACITY * APP_MAX_ENTITY_ID_LEN);
    set.capacity = 16;
    ha_layout_entities_collect(layout, &set);
    CHECK(set.count == 16 && dense_overflow_count == expected - 16);
    free(ids);
    cJSON_free(json);
    cJSON_Delete(layout);
}

static bool valid(const char *entity, int w, int h)
{
    char json[1024];
    snprintf(json, sizeof(json), "{\"version\":1,\"pages\":[{\"id\":\"p\",\"title\":\"P\",\"widgets\":["
        "{\"id\":\"w\",\"type\":\"energy_price_forecast\",\"title\":\"Price\",\"entity_id\":\"%s\",\"rect\":{\"x\":0,\"y\":0,\"w\":%d,\"h\":%d}}]}]}",
        entity, w, h);
    layout_validation_result_t result = {0};
    return layout_validate_json(json, &result);
}

static void test_weather_options(void)
{
    const char *head = "{\"version\":1,\"pages\":[{\"id\":\"p\",\"widgets\":[{\"id\":\"w\",\"type\":\"weather_tile\",\"entity_id\":\"weather.test\",";
    const char *tail = "\"rect\":{\"x\":0,\"y\":0,\"w\":680,\"h\":360}}]}]}";
    const char *cases[] = {"\"weather_hourly\":true,\"secondary_entity_id\":\"sensor.report\",", "\"weather_hourly\":false,", "\"weather_hourly\":1,", "\"weather_hourly\":\"true\",", "\"secondary_entity_id\":\"light.test\","};
    char json[1024];
    layout_validation_result_t result;
    for (unsigned i = 0; i < 5; i++) {
        snprintf(json, sizeof(json), "%s%s%s", head, cases[i], tail);
        CHECK(layout_validate_json(json, &result) == (i < 2));
    }
    snprintf(json, sizeof(json), "%s%s%s", head, cases[0], tail);
    cJSON *layout = cJSON_Parse(json);
    char ids[4][APP_MAX_ENTITY_ID_LEN];
    ha_layout_entity_set_t set = {.ids = (char *)ids, .capacity = 4};
    ha_layout_entities_collect(layout, &set);
    CHECK(set.count == 2);
    CHECK(!strcmp(ids[0], "weather.test") && !strcmp(ids[1], "sensor.report"));
    cJSON_Delete(layout);
}

int main(void)
{
    test_weather_options();
    CHECK(widget_type_known("energy_price_forecast"));
    CHECK(widget_type_supports_name("energy_price_forecast"));
    const widget_size_limits_t limits = widget_type_size_limits("energy_price_forecast");
    CHECK(limits.min_w == 240 && limits.min_h == 150);
    CHECK(valid("sensor.epex_spot_data_market_price", 240, 150));
    CHECK(valid("sensor.da_price", 700, 300));
    CHECK(!valid("light.kitchen", 400, 200));            /* only a sensor has the price list */
    CHECK(!valid("sensor.da_price", 239, 150));          /* below the minimum width */
    CHECK(!valid("sensor.da_price", 400, 149));          /* below the minimum height */
    CHECK(!valid("", 400, 200));                         /* the price sensor is required */

    /* the options of the price timeline card: day switch, cheap times and the average (a fixed value, or a sensor in secondary_entity_id) */
    char json[1024];
    const char *tail = "\"rect\":{\"x\":0,\"y\":0,\"w\":400,\"h\":200}}]}]}";
    const char *head = "{\"version\":1,\"pages\":[{\"id\":\"p\",\"title\":\"P\",\"widgets\":[{\"id\":\"w\",\"type\":\"energy_price_forecast\",\"title\":\"P\",\"entity_id\":\"sensor.p\",";
    layout_validation_result_t result = {0};
    const char *good[] = {"\"price_selector\":true,", "\"price_cheap_times\":false,", "\"price_line\":true,", "\"price_average\":0.25,", "\"secondary_entity_id\":\"sensor.price_average\",", ""};
    const char *bad[] = {"\"price_selector\":\"yes\",", "\"price_cheap_times\":1,", "\"price_line\":\"line\",", "\"price_average\":-0.1,", "\"price_average\":9,", "\"price_average\":\"0.25\",",
                         "\"secondary_entity_id\":\"light.kitchen\","};
    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
        snprintf(json, sizeof(json), "%s%s%s", head, good[i], tail);
        CHECK(layout_validate_json(json, &result));
    }
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        snprintf(json, sizeof(json), "%s%s%s", head, bad[i], tail);
        CHECK(!layout_validate_json(json, &result));
    }

    const char *layout = "{\"version\":1,\"pages\":[{\"id\":\"p\",\"title\":\"P\",\"widgets\":["
        "{\"id\":\"w\",\"type\":\"energy_price_forecast\",\"title\":\"Price\",\"entity_id\":\"sensor.epex_spot_data_market_price\","
        "\"rect\":{\"x\":0,\"y\":0,\"w\":400,\"h\":200}}]}]}";
    cJSON *root = cJSON_Parse(layout);
    CHECK(root != NULL);
    char ids[8 * APP_MAX_ENTITY_ID_LEN] = {0};
    ha_layout_entity_set_t set = {.ids = ids, .capacity = 8};
    ha_layout_entities_collect(root, &set);
    CHECK(set.count == 1 && ha_layout_entities_contains(ids, set.count, "sensor.epex_spot_data_market_price"));
    cJSON_Delete(root);

    /* the Parqet portfolio tile: a sensor, optional gain (secondary), XIRR (tertiary) and dividends sensors, the percentages-only option */
    CHECK(widget_type_known("parqet_portfolio") && widget_type_supports_name("parqet_portfolio"));
    const widget_size_limits_t pq = widget_type_size_limits("parqet_portfolio");
    CHECK(pq.min_w == 240 && pq.min_h == 150);
    const char *pq_head = "{\"version\":1,\"pages\":[{\"id\":\"p\",\"title\":\"P\",\"widgets\":[{\"id\":\"w\",\"type\":\"parqet_portfolio\",\"title\":\"P\",\"entity_id\":\"sensor.value\",";
    const char *pq_good[] = {"\"secondary_entity_id\":\"sensor.gain\",", "\"tertiary_entity_id\":\"sensor.xirr\",", "\"dividends_entity_id\":\"sensor.dividends\",", "\"return_entity_id\":\"sensor.return\",",
                             "\"parqet_selector\":true,", "\"parqet_view\":\"holdings\",", "\"parqet_interval\":\"ytd\",", "\"parqet_interval\":\"max\",",
                             "\"parqet_hide_amounts\":true,", "\"secondary_entity_id\":\"\",", ""};
    const char *pq_bad[] = {"\"secondary_entity_id\":\"light.kitchen\",", "\"tertiary_entity_id\":\"switch.a\",", "\"dividends_entity_id\":\"not an id\",",
                            "\"dividends_entity_id\":\"sensor.A\",", "\"return_entity_id\":\"climate.a\",", "\"parqet_hide_amounts\":\"yes\",", "\"parqet_hide_amounts\":1,", "\"parqet_selector\":\"on\",",
                            "\"parqet_view\":\"table\",", "\"parqet_view\":1,", "\"parqet_interval\":\"1y\",", "\"parqet_interval\":5,"};
    for (size_t i = 0; i < sizeof(pq_good) / sizeof(pq_good[0]); i++) {
        snprintf(json, sizeof(json), "%s%s%s", pq_head, pq_good[i], tail);
        CHECK(layout_validate_json(json, &result));
    }
    for (size_t i = 0; i < sizeof(pq_bad) / sizeof(pq_bad[0]); i++) {
        snprintf(json, sizeof(json), "%s%s%s", pq_head, pq_bad[i], tail);
        CHECK(!layout_validate_json(json, &result));
    }
    snprintf(json, sizeof(json), "{\"version\":1,\"pages\":[{\"id\":\"p\",\"title\":\"P\",\"widgets\":[{\"id\":\"w\",\"type\":\"parqet_portfolio\",\"title\":\"P\",\"entity_id\":\"light.kitchen\",%s", tail);
    CHECK(!layout_validate_json(json, &result));      /* the total value is a sensor */
    snprintf(json, sizeof(json), "%s\"secondary_entity_id\":\"sensor.gain\",\"tertiary_entity_id\":\"sensor.xirr\",\"dividends_entity_id\":\"sensor.dividends\",\"return_entity_id\":\"sensor.return\",%s", pq_head, tail);
    root = cJSON_Parse(json);
    CHECK(root != NULL);
    memset(ids, 0, sizeof(ids));
    set = (ha_layout_entity_set_t){.ids = ids, .capacity = 8};
    ha_layout_entities_collect(root, &set);
    CHECK(set.count == 5 && ha_layout_entities_contains(ids, set.count, "sensor.return") && ha_layout_entities_contains(ids, set.count, "sensor.value") && ha_layout_entities_contains(ids, set.count, "sensor.gain") &&
          ha_layout_entities_contains(ids, set.count, "sensor.xirr") && ha_layout_entities_contains(ids, set.count, "sensor.dividends"));
    cJSON_Delete(root);
    test_dense_stock_layout();
    printf("Price layout: %u checks passed\n", checks);
    return 0;
}
