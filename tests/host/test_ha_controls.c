/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ha/ha_registry_discovery.h"
#include "ha/ha_control_options.h"
#include "ha/ha_attr_serialize.h"

static unsigned checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "HA controls line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

static void registry(void)
{
    cJSON *list = cJSON_Parse("[{\"id\":\"child\",\"parent_device_id\":\"parent\",\"area_id\":null,\"name\":\"Socket\"},"
        "{\"id\":\"parent\",\"area_id\":\"office\",\"name\":\"Hub\",\"name_by_user\":\"My hub\"},"
        "{\"id\":\"override\",\"parent_device_id\":\"parent\",\"area_id\":\"kitchen\"},"
        "{\"id\":\"orphan\",\"parent_device_id\":\"missing\"},"
        "{\"id\":\"cycle1\",\"parent_device_id\":\"cycle2\"},"
        "{\"id\":\"cycle2\",\"parent_device_id\":\"cycle1\"}]");
    ha_registry_device_t devices[6];
    for (int i = 0; i < 6; i++) CHECK(ha_registry_device_parse(cJSON_GetArrayItem(list, i), &devices[i]));
    CHECK(strcmp(devices[1].name, "My hub") == 0);
    CHECK(strcmp(ha_registry_device_area(devices, 6, "child"), "office") == 0);
    CHECK(strcmp(ha_registry_device_area(devices, 6, "override"), "kitchen") == 0);
    CHECK(strcmp(ha_registry_device_area(devices, 6, "orphan"), "") == 0);
    CHECK(strcmp(ha_registry_device_area(devices, 6, "cycle1"), "") == 0);
    CHECK(strcmp(ha_registry_device_area(NULL, 0, "child"), "") == 0);
    const struct { const char *json; bool display; const char *expected; } cases[] = {
        {"{\"en\":\"Power\",\"hn\":true}", true, "Socket Power"},
        {"{\"en\":\"\",\"hn\":true}", true, "Socket"},
        {"{\"name\":\"\",\"original_name\":\"Power\"}", false, "Socket"},
        {"{\"name\":\"My power\",\"original_name\":\"Power\"}", false, "My power"},
        {"{\"name\":null,\"original_name\":\"Power\"}", false, "Socket Power"},
        {"{\"original_name\":\"Socket Power\"}", false, "Socket Power"},
        {"{\"original_name\":\"Socket\"}", false, "Socket"},
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        cJSON *entry = cJSON_Parse(cases[i].json);
        ha_registry_name_t name;
        char out[APP_MAX_NAME_LEN];
        ha_registry_name_parse(entry, cases[i].display, &name);
        ha_registry_name_resolve(&name, "sensor.power", "child", devices, 6, out, sizeof(out));
        CHECK(strcmp(out, cases[i].expected) == 0);
        cJSON_Delete(entry);
    }
    ha_registry_name_t empty = {0};
    char out[APP_MAX_NAME_LEN];
    ha_registry_name_resolve(&empty, "sensor.power", "missing", devices, 6, out, sizeof(out));
    CHECK(strcmp(out, "sensor.power") == 0);
    cJSON_Delete(list);
}

static void options(void)
{
    ha_control_options_t choices;
    ha_control_options_parse("{\"effect\":\"A\\\"B\\\\C\",\"effect_list\":[null,1,\"\",\"A\\\"B\\\\C\",\"A\\\"B\\\\C\",\"Rainbow\"]}",
        "effect_list", "effect", &choices);
    CHECK(choices.count == 2);
    CHECK(ha_control_options_find(&choices, choices.current) == 0);
    char payload[768];
    CHECK(ha_control_option_payload("light.test", "effect", choices.current, payload, sizeof(payload)));
    cJSON *data = cJSON_Parse(payload);
    CHECK(strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(data, "effect")), "A\"B\\C") == 0);
    CHECK(strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(data, "entity_id")), "light.test") == 0);
    cJSON_Delete(data);
    char small[8] = "keep";
    CHECK(!ha_control_option_payload("light.test", "effect", "Rainbow", small, sizeof(small)));
    CHECK(strcmp(small, "keep") == 0);
    ha_control_options_parse("invalid", "fan_speed_list", "fan_speed", &choices);
    CHECK(choices.count == 0 && choices.current[0] == '\0');

    cJSON *src = cJSON_CreateObject();
    cJSON_AddNumberToObject(src, "brightness", 128);
    cJSON_AddNumberToObject(src, "supported_features", 32);
    cJSON_AddStringToObject(src, "effect", "Rainbow");
    cJSON_AddStringToObject(src, "fan_speed", "Normal");
    cJSON_AddStringToObject(src, "unused", "drop this");
    const char *keys[] = {"effect_list", "fan_speed_list"};
    for (size_t k = 0; k < 2; k++) {
        cJSON *list = cJSON_AddArrayToObject(src, keys[k]);
        char long_name[HA_CONTROL_OPTION_LEN + 1];
        memset(long_name, 'x', sizeof(long_name)-1); long_name[sizeof(long_name)-1] = '\0';
        cJSON_AddItemToArray(list, cJSON_CreateString(long_name));
        for (int i = 0; i < 200; i++) {
            char value[80]; snprintf(value, sizeof(value), "Choice %03d with quotes \" and backslashes \\ and a long description", i);
            cJSON_AddItemToArray(list, cJSON_CreateString(value));
        }
    }
    for (size_t k = 0; k < 2; k++) {
        char out[APP_HA_ATTRS_MAX_LEN];
        CHECK(k == 0 ? ha_client_serialize_light_attrs_compact(src, out, sizeof(out)) :
            ha_client_serialize_vacuum_attrs_compact(src, out, sizeof(out)));
        data = cJSON_Parse(out);
        CHECK(cJSON_IsObject(data));
        const cJSON *list = cJSON_GetObjectItemCaseSensitive(data, keys[k]);
        CHECK(cJSON_GetArraySize(list) > 0 && cJSON_GetArraySize(list) <= HA_CONTROL_MAX_OPTIONS);
        CHECK(cJSON_GetObjectItemCaseSensitive(data, "supported_features") != NULL);
        CHECK(cJSON_GetObjectItemCaseSensitive(data, k == 0 ? "brightness" : "fan_speed") != NULL);
        CHECK(cJSON_GetObjectItemCaseSensitive(data, "unused") == NULL);
        ha_control_options_parse(out, keys[k], k == 0 ? "effect" : "fan_speed", &choices);
        CHECK(choices.count == (size_t)cJSON_GetArraySize(list));
        CHECK(strncmp(choices.values[0], "Choice 000", 10) == 0);
        cJSON_Delete(data);
    }
    cJSON_Delete(src);
}

int main(void)
{
    registry(); options();
    printf("HA controls: %u checks passed\n", checks);
    return 0;
}
