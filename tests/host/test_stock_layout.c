/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#include "layout/layout_validate.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "Stock layout %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static bool valid(const char *options)
{
    char json[1024];
    snprintf(json, sizeof(json), "{\"version\":1,\"pages\":[{\"id\":\"p\",\"title\":\"P\",\"widgets\":["
        "{\"id\":\"w\",\"type\":\"stock_quotes\",\"entity_id\":\"sensor.a\",\"rect\":{\"x\":0,\"y\":0,\"w\":300,\"h\":200}%s}]}]}", options);
    layout_validation_result_t result = {0};
    return layout_validate_json(json, &result);
}
int main(void)
{
    CHECK(valid(""));
    CHECK(valid(",\"stock_view\":\"chart\",\"stock_cards\":true,\"stock_previous\":true"));
    CHECK(valid(",\"stock_view\":\"grid\"")); /* the former Tiles view stays valid */
    CHECK(valid(",\"stock_cards\":false,\"stock_previous\":false"));
    CHECK(!valid(",\"stock_cards\":1"));
    CHECK(!valid(",\"stock_previous\":\"yes\""));
    CHECK(!valid(",\"stock_view\":\"tiles\""));
    printf("Stock layout: %u checks passed\n", checks);
}
