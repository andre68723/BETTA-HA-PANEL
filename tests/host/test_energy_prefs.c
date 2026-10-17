/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
/* energy/get_prefs parser. REAL is the reply of a Home Assistant 2026.10.0 instance (2026-10-10) with entity ids
 * replaced: one flat grid source with a power sensor, a battery with a signed power sensor, two solar sources,
 * water and gas, and device consumption with names and rate sensors. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ha/ha_energy_prefs.h"

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

static const char *REAL =
    "{\"energy_sources\":[{\"type\":\"grid\",\"cost_adjustment_day\":0,\"stat_energy_from\":\"sensor.meter_total_in\","
    "\"stat_cost\":null,\"entity_energy_price\":null,\"number_energy_price\":0.33,"
    "\"stat_energy_to\":\"sensor.meter_total_out\",\"stat_compensation\":null,\"entity_energy_price_export\":null,"
    "\"number_energy_price_export\":0.08,\"stat_rate\":\"sensor.meter_active_power\"},"
    "{\"type\":\"battery\",\"stat_energy_from\":\"sensor.bat_discharge\",\"stat_energy_to\":\"sensor.bat_charge\","
    "\"stat_rate\":\"sensor.bat_power_signed\"},"
    "{\"type\":\"solar\",\"stat_energy_from\":\"sensor.roof_today\",\"config_entry_solar_forecast\":[\"01AAA\",\"01BBB\"],"
    "\"stat_rate\":\"sensor.roof_power\"},"
    "{\"type\":\"solar\",\"stat_energy_from\":\"sensor.inverter_pv_total\",\"config_entry_solar_forecast\":[\"01BBB\"],"
    "\"stat_rate\":\"sensor.inverter_dc_power\"},"
    "{\"type\":\"water\",\"stat_energy_from\":\"sensor.water_total\",\"stat_cost\":null,\"entity_energy_price\":null,"
    "\"number_energy_price\":null},"
    "{\"type\":\"gas\",\"stat_energy_from\":\"sensor.gas_total\",\"stat_cost\":null,\"entity_energy_price\":null,"
    "\"number_energy_price\":1}],"
    "\"device_consumption\":[{\"stat_consumption\":\"sensor.fridge_energy\",\"name\":\"Fridge\","
    "\"stat_rate\":\"sensor.fridge_power\"}],\"device_consumption_water\":[]}";

static bool has(const ha_energy_stat_ref_t *refs, size_t n, ha_energy_stat_kind_t kind, const char *id)
{
    for (size_t i = 0; i < n; i++) {
        if (refs[i].kind == kind && strcmp(refs[i].id, id) == 0) return true;
    }
    return false;
}

static size_t parse(const char *json, ha_energy_stat_ref_t *refs, size_t max)
{
    cJSON *root = cJSON_Parse(json);
    size_t n = ha_energy_prefs_parse(root, refs, max);
    cJSON_Delete(root);
    return n;
}

int main(void)
{
    ha_energy_stat_ref_t refs[HA_ENERGY_STAT_REF_MAX];

    size_t n = parse(REAL, refs, HA_ENERGY_STAT_REF_MAX);
    CHECK(n == 8);
    CHECK(has(refs, n, HA_ENERGY_STAT_GRID_FROM, "sensor.meter_total_in"));
    CHECK(has(refs, n, HA_ENERGY_STAT_GRID_TO, "sensor.meter_total_out"));
    CHECK(has(refs, n, HA_ENERGY_STAT_BATTERY_FROM, "sensor.bat_discharge"));
    CHECK(has(refs, n, HA_ENERGY_STAT_BATTERY_TO, "sensor.bat_charge"));
    CHECK(has(refs, n, HA_ENERGY_STAT_SOLAR, "sensor.roof_today"));
    CHECK(has(refs, n, HA_ENERGY_STAT_SOLAR, "sensor.inverter_pv_total"));
    CHECK(has(refs, n, HA_ENERGY_STAT_WATER, "sensor.water_total"));
    CHECK(has(refs, n, HA_ENERGY_STAT_GAS, "sensor.gas_total"));

    /* A grid source that only has a power sensor (stat_energy_from is null) and a battery with power_config. */
    n = parse("{\"energy_sources\":[{\"type\":\"grid\",\"stat_energy_from\":null,\"stat_energy_to\":null,"
              "\"power_config\":{\"stat_rate_from\":\"sensor.imp\",\"stat_rate_to\":\"sensor.exp\"}},"
              "{\"type\":\"battery\",\"stat_energy_from\":\"sensor.d\",\"stat_energy_to\":\"sensor.c\","
              "\"power_config\":{\"stat_rate_inverted\":\"sensor.p\"},\"stat_soc\":\"sensor.soc\",\"capacity\":10.2}]}",
              refs, HA_ENERGY_STAT_REF_MAX);
    CHECK(n == 2);
    CHECK(has(refs, n, HA_ENERGY_STAT_BATTERY_FROM, "sensor.d"));
    CHECK(has(refs, n, HA_ENERGY_STAT_BATTERY_TO, "sensor.c"));

    /* Two flat grid sources (tariffs) and the same statistic twice are summed once. */
    n = parse("{\"energy_sources\":[{\"type\":\"grid\",\"stat_energy_from\":\"sensor.day\",\"stat_energy_to\":\"sensor.out\"},"
              "{\"type\":\"grid\",\"stat_energy_from\":\"sensor.night\",\"stat_energy_to\":\"sensor.out\"}]}",
              refs, HA_ENERGY_STAT_REF_MAX);
    CHECK(n == 3);
    CHECK(has(refs, n, HA_ENERGY_STAT_GRID_FROM, "sensor.day"));
    CHECK(has(refs, n, HA_ENERGY_STAT_GRID_FROM, "sensor.night"));

    /* Legacy flow arrays. */
    n = parse("{\"energy_sources\":[{\"type\":\"grid\",\"flow_from\":[{\"stat_energy_from\":\"sensor.a\"}],"
              "\"flow_to\":[{\"stat_energy_to\":\"sensor.b\"}]}]}", refs, HA_ENERGY_STAT_REF_MAX);
    CHECK(n == 2);
    CHECK(has(refs, n, HA_ENERGY_STAT_GRID_TO, "sensor.b"));

    /* Units of gas and water are kept; limits and garbage. */
    n = parse("{\"energy_sources\":[{\"type\":\"gas\",\"stat_energy_from\":\"sensor.g\",\"unit_of_measurement\":\"m\\u00b3\"}]}",
              refs, HA_ENERGY_STAT_REF_MAX);
    CHECK(n == 1 && strcmp(refs[0].unit, "m\xc2\xb3") == 0);
    CHECK(parse(REAL, refs, 3) == 3);
    CHECK(parse("[]", refs, HA_ENERGY_STAT_REF_MAX) == 0);
    CHECK(parse("{\"energy_sources\":{}}", refs, HA_ENERGY_STAT_REF_MAX) == 0);
    CHECK(ha_energy_prefs_parse(NULL, refs, HA_ENERGY_STAT_REF_MAX) == 0);

    printf("energy prefs: %d checks, %d failed\n", checks, failed);
    return failed == 0 ? 0 : 1;
}
