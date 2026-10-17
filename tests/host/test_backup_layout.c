/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md */
/*
 * The layout of tests/fixtures/backup_layout.json carries every optional field of every widget type, a house page, an
 * energy page and an alert: what a backup restore sends to PUT /api/layout. The editor test (tests/webui/
 * backup_roundtrip.test.mjs) proves the editor keeps all of it; this one proves the firmware accepts it and subscribes
 * to every entity in it, so a restored field is neither rejected nor silently unused.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "ha/ha_layout_entities.h"
#include "layout/layout_validate.h"

static int checks, failures;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++failures; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = calloc((size_t)size + 1U, 1U);
    if (text != NULL && fread(text, 1U, (size_t)size, f) != (size_t)size) { free(text); text = NULL; }
    fclose(f);
    return text;
}

/* Named but not a state subscription: an alert's button is only pressed, and the energy timeline's battery counters are
 * Recorder statistics ids (only its state of charge and forecast are live states, see w_energy_timeline.c). */
static bool not_subscribed_by_design(const char *key)
{
    return strcmp(key, "action_entity_id") == 0 || strcmp(key, "battery_charge_entity_id") == 0 ||
           strcmp(key, "battery_discharge_entity_id") == 0;
}

/* Every string that names an entity: `*_entity_id` keys, room_entities, energy page keys, alert entities. */
static void expect_entities(const cJSON *node, const char *key, const ha_layout_entity_set_t *set, const char *page)
{
    if (cJSON_IsString(node) && node->valuestring[0] != '\0' && key != NULL &&
        !not_subscribed_by_design(key) && (strstr(key, "entity_id") != NULL || strcmp(key, "room_entities") == 0)) {
        CHECK(ha_layout_entities_contains(set->ids, set->count, node->valuestring),
              "page %s: %s=%s is not subscribed", page, key, node->valuestring);
    }
    if (cJSON_IsObject(node) || cJSON_IsArray(node)) {
        for (const cJSON *child = node->child; child != NULL; child = child->next) {
            expect_entities(child, cJSON_IsArray(node) ? key : child->string, set, page);
        }
    }
}

int main(void)
{
    char *text = read_file("tests/fixtures/backup_layout.json");
    if (text == NULL) { printf("cannot read tests/fixtures/backup_layout.json (run from the repository root)\n"); return 2; }
    cJSON *fixture = cJSON_Parse(text);
    free(text);
    CHECK(fixture != NULL, "fixture is not JSON");
    if (fixture == NULL) return 1;
    const cJSON *pages = cJSON_GetObjectItemCaseSensitive(fixture, "pages");
    const cJSON *alerts = cJSON_GetObjectItemCaseSensitive(fixture, "alerts");
    CHECK(cJSON_GetArraySize(pages) >= 15, "fixture has %d pages", cJSON_GetArraySize(pages));

    /* One page at a time: the whole fixture has more pages than a panel takes. */
    const cJSON *page = NULL;
    cJSON_ArrayForEach(page, pages) {
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(page, "id"));
        cJSON *layout = cJSON_CreateObject();
        cJSON_AddNumberToObject(layout, "version", 1);
        cJSON *one = cJSON_AddArrayToObject(layout, "pages");
        cJSON_AddItemToArray(one, cJSON_Duplicate(page, 1));
        cJSON_AddItemToObject(layout, "alerts", cJSON_Duplicate(alerts, 1));

        char *json = cJSON_PrintUnformatted(layout);
        layout_validation_result_t result;
        layout_validation_clear(&result);
        const bool ok = layout_validate_json(json, &result);
        CHECK(ok, "page %s rejected: %s", id, result.count > 0 ? result.messages[0] : "?");
        free(json);

        static char ids[160 * APP_MAX_ENTITY_ID_LEN];
        memset(ids, 0, sizeof(ids));
        ha_layout_entity_set_t set = { .ids = ids, .capacity = 160 };
        ha_layout_entities_collect(layout, &set);
        expect_entities(layout, NULL, &set, id != NULL ? id : "?");
        cJSON_Delete(layout);
    }
    cJSON_Delete(fixture);
    printf("backup layout: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
