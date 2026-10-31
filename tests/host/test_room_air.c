/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "layout/room_air_slots.h"
#include "ha/ha_layout_entities.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static air_config_t config;
static air_input_t input[AIR_ROLE_COUNT];
static air_memory_t memory;

static air_result_t evaluate(void) { return air_evaluate(&config, input, &memory, 0); }
static void clean(void)
{
    memset(input, 0, sizeof(input));
    config = air_config_default(); air_memory_reset(&memory);
    input[AIR_HUMIDITY] = (air_input_t){true, true, 50};
}

static void boundaries(void)
{
    const double rh[] = {0, 29.999, 30, 39.999, 40, 60, 60.001, 70, 70.001, 100};
    const air_band_t expected[] = {AIR_POOR, AIR_POOR, AIR_FAIR, AIR_FAIR, AIR_GOOD, AIR_GOOD, AIR_FAIR, AIR_FAIR, AIR_POOR, AIR_POOR};
    for (unsigned i = 0; i < sizeof(rh) / sizeof(rh[0]); ++i) {
        clean(); input[AIR_HUMIDITY].value = rh[i]; assert(evaluate().overall == expected[i]);
    }
    for (int role = AIR_CO2; role <= AIR_PM25; ++role) {
        double amber = role == AIR_CO2 ? 1000 : 15, red = role == AIR_CO2 ? 2000 : 35;
        const double values[] = {0, amber - .001, amber, red - .001, red};
        const air_band_t bands[] = {AIR_GOOD, AIR_GOOD, AIR_FAIR, AIR_FAIR, AIR_POOR};
        for (int i = 0; i < 5; ++i) { clean(); input[role] = (air_input_t){true, true, values[i]}; assert(evaluate().overall == bands[i]); }
    }
}

static void normalisation(void)
{
    double v;
    assert(air_normalize(AIR_TEMPERATURE, "68", "°F", &v) && fabs(v - 20) < .001);
    assert(air_normalize(AIR_PM25, "0", "μg/m³", &v) && v == 0);
    assert(air_normalize(AIR_PM25, "15", "µg/m³", &v) && v == 15);
    assert(air_normalize(AIR_VOC, "718", "ppb", &v));
    assert(!air_normalize(AIR_VOC, "718", "µg/m³", &v));
    assert(!air_normalize(AIR_VOC, "100", "VOC index", &v));
    const char *bad[] = {"", "nan", "inf", "-1", "12 junk", " 15", "0x10", "1e999", "unknown", "unavailable"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) assert(!air_normalize(AIR_PM25, bad[i], "µg/m³", &v));
    assert(!air_normalize(AIR_HUMIDITY, "101", "%", &v));
    assert(!air_normalize(AIR_HUMIDITY, "50", "ppm", &v));
    assert(!air_normalize(AIR_TEMPERATURE, "22", "", &v));
    assert(air_config_valid(&config));
    config.co2_red = config.co2_amber; assert(!air_config_valid(&config));
    config = air_config_default(); config.rh_low = 31; assert(!air_config_valid(&config));
    config = air_config_default(); config.pm_red = NAN; assert(!air_config_valid(&config));
}

static void coverage(void)
{
    clean(); assert(evaluate().overall == AIR_GOOD && evaluate().measured == 1);
    input[AIR_CO2] = (air_input_t){true, false, 0};
    assert(evaluate().partial && evaluate().overall == AIR_NONE);
    input[AIR_PM25] = (air_input_t){true, true, 40};
    assert(evaluate().partial && evaluate().overall == AIR_POOR);
    input[AIR_HUMIDITY].valid = false; assert(evaluate().overall == AIR_POOR);
    input[AIR_PM25].valid = false; assert(evaluate().overall == AIR_NONE);
    clean(); input[AIR_VOC] = (air_input_t){true, true, 718};
    assert(!evaluate().partial && evaluate().overall == AIR_GOOD && evaluate().band[AIR_VOC] == AIR_NONE);
    input[AIR_VOC].valid = false; assert(!evaluate().partial && evaluate().overall == AIR_GOOD); /* VOC is display only */
    input[AIR_HUMIDITY].valid = false; assert(evaluate().overall == AIR_NONE);
    clean(); input[AIR_RATING] = (air_input_t){true, false, 0};
    input[AIR_TEMPERATURE] = (air_input_t){true, false, 0};
    assert(!evaluate().partial && evaluate().overall == AIR_GOOD);
}

static void ventilation(void)
{
    clean(); input[AIR_HUMIDITY].value = 75;
    assert(evaluate().reasons == AIR_HUMID && !evaluate().comparison);
    input[AIR_TEMPERATURE] = (air_input_t){true, true, 22};
    input[AIR_OUT_TEMPERATURE] = (air_input_t){true, true, 5};
    input[AIR_OUT_HUMIDITY] = (air_input_t){true, true, 90};
    air_result_t r = evaluate();
    assert(r.comparison && r.difference > 1 && (r.reasons & AIR_DRYING));
    assert(fabs(air_absolute_humidity(20, 50) - 8.62) < .1);
    input[AIR_CO2] = (air_input_t){true, true, 2200};
    input[AIR_PM25] = (air_input_t){true, true, 50};
    r = evaluate(); assert((r.reasons & AIR_FRESH_AIR) && (r.reasons & AIR_PARTICLES) && !(r.reasons & AIR_CONFLICT));
    input[AIR_OUT_TEMPERATURE].value = 30;
    r = evaluate(); assert((r.reasons & AIR_CONFLICT) && (r.reasons & AIR_FRESH_AIR) && (r.reasons & AIR_NO_DRYING));
    clean(); input[AIR_HUMIDITY].value = 20; input[AIR_CO2] = (air_input_t){true, true, 2200};
    r = evaluate(); assert((r.reasons & AIR_DRY) && (r.reasons & AIR_FRESH_AIR));
}

static void hysteresis(void)
{
    clean(); input[AIR_CO2] = (air_input_t){true, true, 2100}; assert(evaluate().overall == AIR_POOR);
    input[AIR_CO2].value = 1970; assert(evaluate().overall == AIR_POOR);
    input[AIR_CO2].value = 1900; assert(evaluate().overall == AIR_FAIR);
    input[AIR_CO2].value = 970; assert(evaluate().overall == AIR_FAIR);
    input[AIR_CO2].value = 900;
    assert(evaluate().overall == AIR_GOOD && (evaluate().reasons & AIR_FRESH_AIR));
    air_result_t r = air_evaluate(&config, input, &memory, 60000);
    assert(!r.reasons); /* timer can settle without a new sensor event */
    input[AIR_CO2].value = 2100; assert(evaluate().overall == AIR_POOR);
    input[AIR_CO2].valid = false; assert(evaluate().overall == AIR_NONE && !evaluate().reasons);
    air_memory_reset(&memory); input[AIR_CO2].valid = true; input[AIR_CO2].value = 990;
    assert(evaluate().overall == AIR_GOOD);
    /* humidity hysteresis stays on the side of the excursion and keeps its reason */
    clean(); input[AIR_HUMIDITY].value = 38; assert(evaluate().reasons == AIR_DRY);
    input[AIR_HUMIDITY].value = 41; assert(evaluate().overall == AIR_FAIR && evaluate().reasons == AIR_DRY);
    input[AIR_HUMIDITY].value = 59; assert(evaluate().overall == AIR_GOOD); /* other side's zone does not apply */
    clean(); input[AIR_HUMIDITY].value = 65; assert(evaluate().reasons == AIR_HUMID);
    input[AIR_HUMIDITY].value = 41; assert(evaluate().overall == AIR_GOOD);
}

static void slots_and_subscriptions(void)
{
    cJSON *root = cJSON_Parse("{\"pages\":[{\"id\":\"demo\",\"widgets\":[{\"type\":\"air_quality_tile\",\"entity_id\":\"sensor.humidity\",\"temperature_entity_id\":\"sensor.temp\",\"co2_entity_id\":\"sensor.co2\",\"pm25_entity_id\":\"sensor.pm\",\"voc_entity_id\":\"sensor.voc\",\"outdoor_temperature_entity_id\":\"weather.demo\",\"outdoor_humidity_entity_id\":\"weather.demo\",\"window_entity_id\":\"binary_sensor.window\",\"native_rating_entity_id\":\"sensor.rating\"}]}]}");
    assert(root);
    char ids[16][APP_MAX_ENTITY_ID_LEN] = {0};
    ha_layout_entity_set_t set = {.ids = (char *)ids, .capacity = 16};
    ha_layout_entities_collect(root, &set); assert(set.count == 8);
    for (int i = 0; i < 8; ++i) assert(ids[i][0]);
    cJSON *w = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "pages"), 0), "widgets"), 0);
    char primary[APP_MAX_ENTITY_ID_LEN], second[APP_MAX_ENTITY_ID_LEN], third[APP_MAX_ENTITY_ID_LEN], extra[8][APP_MAX_ENTITY_ID_LEN] = {0};
    air_slots_from_json(w, primary, second, third, extra);
    assert(!strcmp(primary, "sensor.humidity") && !strcmp(second, "sensor.temp") && !strcmp(third, "sensor.co2"));
    assert(!strcmp(extra[0], "sensor.pm") && !strcmp(extra[1], "sensor.voc") && !strcmp(extra[2], extra[3]));
    assert(!strcmp(extra[4], "binary_sensor.window") && !strcmp(extra[5], "sensor.rating"));
    cJSON_AddBoolToObject(w, "air_outdoor_enabled", false);
    air_slots_from_json(w, primary, second, third, extra);
    assert(!extra[2][0] && !extra[3][0]);
    assert(cJSON_GetObjectItemCaseSensitive(w, "outdoor_temperature_entity_id")); /* retained JSON */
    assert(air_config_from_json(w, &config) && config.co2_red == 2000);
    cJSON_AddItemToObject(w, "air_thresholds", cJSON_Parse("{\"co2_red\":900}")); /* an older layout: ignored */
    assert(air_config_from_json(w, &config) && config.co2_red == 2000);
    cJSON_Delete(root);
}

int main(void)
{
    clean(); boundaries(); clean(); normalisation(); coverage(); ventilation(); hysteresis(); slots_and_subscriptions();
    puts("room air: boundaries, units, coverage, advice, hysteresis, slots and subscriptions passed");
    return 0;
}
