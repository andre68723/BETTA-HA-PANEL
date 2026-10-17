/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#include "layout/layout_validate.h"
#include "ha/ha_layout_entities.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "Camera layout %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static bool valid(const char *type, const char *entity, const char *options)
{
    char json[1024];
    snprintf(json, sizeof(json), "{\"version\":1,\"pages\":[{\"id\":\"p\",\"title\":\"P\",\"widgets\":["
        "{\"id\":\"w\",\"type\":\"%s\",\"entity_id\":\"%s\",\"rect\":{\"x\":0,\"y\":0,\"w\":320,\"h\":320}%s}]}]}", type, entity, options);
    layout_validation_result_t result = {0};
    return layout_validate_json(json, &result);
}
int main(void)
{
    CHECK(valid("camera_tile", "camera.printer", ""));
    CHECK(valid("camera_tile", "camera.printer", ",\"camera_live\":false"));
    CHECK(valid("camera_tile", "camera.printer", ",\"camera_live\":true"));
    CHECK(valid("camera_tile", "image.snapshot", ""));
    CHECK(!valid("camera_tile", "image.snapshot", ",\"camera_live\":true"));
    CHECK(!valid("camera_tile", "camera.printer", ",\"camera_live\":\"true\""));
    CHECK(!valid("sensor", "sensor.printer", ",\"camera_live\":true"));
    CHECK(valid("bambu_printer", "sensor.printer", ",\"camera_entity_id\":\"camera.printer\""));
    CHECK(!valid("bambu_printer", "sensor.printer", ",\"camera_entity_id\":\"sensor.printer\""));
    CHECK(!valid("bambu_printer", "sensor.printer", ",\"camera_entity_id\":true"));
    printf("Camera layout: %u checks passed\n", checks);
}
