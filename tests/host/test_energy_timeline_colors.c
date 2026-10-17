/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#include <stdio.h>
#include "layout/energy_timeline_colors.h"
#include "layout/layout_validate.h"

#define COLOR_CHECK(condition) do { if (!(condition)) { \
    printf("FAIL energy timeline colors, line %d\n", __LINE__); return false; } } while (0)

static bool validate_colors(const char *colors)
{
    char json[1024];
    snprintf(json, sizeof(json), "{\"version\":1,\"pages\":[{\"id\":\"p\",\"title\":\"T\","
             "\"widgets\":[{\"id\":\"w\",\"type\":\"energy_timeline\",\"entity_id\":\"sensor.import\","
             "\"rect\":{\"x\":0,\"y\":0,\"w\":420,\"h\":260},\"energy_colors\":%s}]}]}", colors);
    layout_validation_result_t result;
    layout_validation_clear(&result);
    return layout_validate_json(json, &result);
}

bool test_energy_timeline_colors(void)
{
    COLOR_CHECK(validate_colors("{\"grid\":\"#000000\",\"solar\":\"0xabcdef\",\"export\":\"FFFFFF\",\"battery_in\":\"#ff1234\"}"));
    COLOR_CHECK(!validate_colors("false"));
    COLOR_CHECK(!validate_colors("{\"grid\":\"#12xz34\"}"));
    COLOR_CHECK(!validate_colors("{\"wrong\":\"#ffffff\"}"));
    cJSON *widget = cJSON_Parse("{\"energy_colors\":{\"grid\":\"#000000\",\"solar\":\"0xabcdef\",\"forecast\":\"invalid\"}}");
    const etl_palette_t palette = etl_palette_from_json(widget);
    cJSON_Delete(widget);
    COLOR_CHECK(palette.initialized);
    COLOR_CHECK(palette.value[ETL_GRID] == 0);
    COLOR_CHECK(palette.value[ETL_SOLAR] == 0xABCDEF);
    COLOR_CHECK(palette.value[ETL_HOME_SOLAR_DEST] == 0xABCDEF);
    COLOR_CHECK(palette.value[ETL_FORECAST] == ETL_COLOR_FORECAST);
    widget = cJSON_Parse("{}");
    const etl_palette_t original = etl_palette_from_json(widget);
    cJSON_Delete(widget);
    COLOR_CHECK(original.value[ETL_HOME_SOLAR_DEST] == ETL_COLOR_HOME_SOLAR_DEST);
    return true;
}
