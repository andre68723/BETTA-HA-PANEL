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
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "Price layout %d: %s\n", __LINE__, #c); exit(1); } } while (0)

static bool valid(const char *entity, int w, int h)
{
    char json[1024];
    snprintf(json, sizeof(json), "{\"version\":1,\"pages\":[{\"id\":\"p\",\"title\":\"P\",\"widgets\":["
        "{\"id\":\"w\",\"type\":\"price_forecast\",\"title\":\"Price\",\"entity_id\":\"%s\",\"rect\":{\"x\":0,\"y\":0,\"w\":%d,\"h\":%d}}]}]}",
        entity, w, h);
    layout_validation_result_t result = {0};
    return layout_validate_json(json, &result);
}

int main(void)
{
    CHECK(widget_type_known("price_forecast"));
    CHECK(widget_type_supports_name("price_forecast"));
    const widget_size_limits_t limits = widget_type_size_limits("price_forecast");
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
    const char *head = "{\"version\":1,\"pages\":[{\"id\":\"p\",\"title\":\"P\",\"widgets\":[{\"id\":\"w\",\"type\":\"price_forecast\",\"title\":\"P\",\"entity_id\":\"sensor.p\",";
    layout_validation_result_t result = {0};
    const char *good[] = {"\"price_day_switch\":true,", "\"price_cheap_times\":false,", "\"price_line\":true,", "\"price_average\":0.25,", "\"secondary_entity_id\":\"sensor.price_average\",", ""};
    const char *bad[] = {"\"price_day_switch\":\"yes\",", "\"price_cheap_times\":1,", "\"price_line\":\"line\",", "\"price_average\":-0.1,", "\"price_average\":9,", "\"price_average\":\"0.25\",",
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
        "{\"id\":\"w\",\"type\":\"price_forecast\",\"title\":\"Price\",\"entity_id\":\"sensor.epex_spot_data_market_price\","
        "\"rect\":{\"x\":0,\"y\":0,\"w\":400,\"h\":200}}]}]}";
    cJSON *root = cJSON_Parse(layout);
    CHECK(root != NULL);
    char ids[8 * APP_MAX_ENTITY_ID_LEN] = {0};
    ha_layout_entity_set_t set = {.ids = ids, .capacity = 8};
    ha_layout_entities_collect(root, &set);
    CHECK(set.count == 1 && ha_layout_entities_contains(ids, set.count, "sensor.epex_spot_data_market_price"));
    cJSON_Delete(root);
    printf("Price layout: %u checks passed\n", checks);
    return 0;
}
