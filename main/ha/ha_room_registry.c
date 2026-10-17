/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Moved out of ha_client.c (area/floor registry state and parsing); see ha_room_registry.h.
 */
#include "ha/ha_room_registry.h"

#include <stdlib.h>
#include <string.h>

#include "app_config.h"

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#define ROOM_REGISTRY_CALLOC(n, size) heap_caps_calloc((n), (size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#else
#define ROOM_REGISTRY_CALLOC(n, size) calloc((n), (size))
#endif

typedef struct {
    char name[APP_MAX_NAME_LEN];
    char floor_id[24];
    char temp[APP_MAX_ENTITY_ID_LEN];
    char hum[APP_MAX_ENTITY_ID_LEN];
} ha_room_area_t;

typedef struct {
    char id[24];
    char name[APP_MAX_NAME_LEN];
    int level;
} ha_room_floor_t;

#define HA_ROOM_AREAS_MAX 48
#define HA_ROOM_FLOORS_MAX 12
static ha_room_area_t *s_areas = NULL;
static ha_room_floor_t *s_floors = NULL;
static uint16_t s_area_count = 0;
static uint16_t s_floor_count = 0;
static uint32_t s_area_req = 0;
static uint32_t s_floor_req = 0;
static bool s_due = false;
static int64_t s_sent_ms = 0;

static void copy_string(const cJSON *obj, const char *key, char *dst, size_t dst_size)
{
    const char *value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(obj, key));
    if (value == NULL) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, value, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
}

void ha_room_registry_request(void)
{
    s_due = true;
}

bool ha_room_registry_is_due(void)
{
    return s_due;
}

bool ha_room_registry_begin(uint32_t area_req_id, uint32_t floor_req_id, int64_t now_ms)
{
    if (s_areas == NULL) {
        s_areas = ROOM_REGISTRY_CALLOC(HA_ROOM_AREAS_MAX, sizeof(ha_room_area_t));
    }
    if (s_floors == NULL) {
        s_floors = ROOM_REGISTRY_CALLOC(HA_ROOM_FLOORS_MAX, sizeof(ha_room_floor_t));
    }
    if (s_areas == NULL || s_floors == NULL) {
        return false;
    }
    s_area_req = area_req_id;
    s_floor_req = floor_req_id;
    s_due = false;
    s_sent_ms = now_ms;
    return true;
}

void ha_room_registry_cancel(void)
{
    s_area_req = 0;
    s_floor_req = 0;
}

bool ha_room_registry_handle_result(uint32_t msg_id, bool success, const cJSON *result)
{
    bool is_area = msg_id != 0 && msg_id == s_area_req;
    bool is_floor = !is_area && msg_id != 0 && msg_id == s_floor_req;
    if (is_area) {
        s_area_req = 0;
    } else if (is_floor) {
        s_floor_req = 0;
    } else {
        return false;
    }
    if (!success || !cJSON_IsArray(result) || s_areas == NULL || s_floors == NULL) {
        return true;
    }
    const cJSON *entry = NULL;
    if (is_area) {
        s_area_count = 0;
        cJSON_ArrayForEach(entry, result) {
            if (!cJSON_IsObject(entry) || s_area_count >= HA_ROOM_AREAS_MAX) {
                continue;
            }
            ha_room_area_t *a = &s_areas[s_area_count++];
            memset(a, 0, sizeof(*a));
            copy_string(entry, "name", a->name, sizeof(a->name));
            copy_string(entry, "floor_id", a->floor_id, sizeof(a->floor_id));
            copy_string(entry, "temperature_entity_id", a->temp, sizeof(a->temp));
            copy_string(entry, "humidity_entity_id", a->hum, sizeof(a->hum));
        }
    } else {
        s_floor_count = 0;
        cJSON_ArrayForEach(entry, result) {
            if (!cJSON_IsObject(entry) || s_floor_count >= HA_ROOM_FLOORS_MAX) {
                continue;
            }
            ha_room_floor_t *f = &s_floors[s_floor_count++];
            memset(f, 0, sizeof(*f));
            copy_string(entry, "floor_id", f->id, sizeof(f->id));
            copy_string(entry, "name", f->name, sizeof(f->name));
            const cJSON *level = cJSON_GetObjectItemCaseSensitive(entry, "level");
            f->level = cJSON_IsNumber(level) ? level->valueint : 0;
        }
    }
    return true;
}

bool ha_room_registry_poll(bool connected, bool refresh, int64_t now_ms)
{
    bool in_flight = (s_area_req != 0 || s_floor_req != 0) && now_ms - s_sent_ms < HA_ROOM_REGISTRY_TIMEOUT_MS;
    if (connected && !in_flight && !s_due && (refresh || s_area_count == 0)) {
        s_due = true;
    }
    return s_due || in_flight;
}

bool ha_room_registry_has_areas(void)
{
    return s_area_count > 0;
}

void ha_room_registry_add_areas_json(cJSON *root)
{
    cJSON *areas = cJSON_CreateArray();
    if (areas == NULL) {
        return;
    }
    for (uint16_t i = 0; s_areas != NULL && i < s_area_count; i++) {
        const ha_room_area_t *a = &s_areas[i];
        cJSON *o = cJSON_CreateObject();
        if (o == NULL) {
            continue;
        }
        cJSON_AddStringToObject(o, "name", a->name);
        cJSON_AddStringToObject(o, "temperature_entity_id", a->temp);
        cJSON_AddStringToObject(o, "humidity_entity_id", a->hum);
        const ha_room_floor_t *floor = NULL;
        for (uint16_t f = 0; s_floors != NULL && f < s_floor_count; f++) {
            if (strcmp(s_floors[f].id, a->floor_id) == 0) {
                floor = &s_floors[f];
                break;
            }
        }
        cJSON_AddStringToObject(o, "floor", floor != NULL ? floor->name : "");
        if (floor != NULL) {
            cJSON_AddNumberToObject(o, "floor_level", floor->level);
        }
        cJSON_AddItemToArray(areas, o);
    }
    cJSON_AddItemToObject(root, "areas", areas);
}
