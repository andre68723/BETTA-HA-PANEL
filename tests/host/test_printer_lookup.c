/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Host test of the Bambu printer lookup (ha/ha_printer_lookup.c): the three WebSocket steps are answered with synthetic
 * Home Assistant replies, including a second printer, more entities than one get_entries batch and failures.
 */
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "ha/ha_printer_lookup.h"

static int s_checks;
static int s_failures;
#define CHECK(cond) do { s_checks++; if (!(cond)) { s_failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static uint32_t s_id = 100;

static cJSON *next(int64_t now)
{
    cJSON *request = ha_printer_lookup_next_request(++s_id, now);
    return request;
}

static const char *type_of(const cJSON *request)
{
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(request, "type");
    return cJSON_IsString(type) ? type->valuestring : "";
}

static bool answer(const cJSON *request, const char *json, bool success)
{
    cJSON *result = cJSON_Parse(json);
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(request, "id");
    const bool handled = ha_printer_lookup_handle_result((uint32_t)id->valuedouble, success, result);
    cJSON_Delete(result);
    return handled;
}

/* a get_entries answer naming translation keys for the given "entity:key" pairs */
static void answer_entries(const cJSON *request, const char *const *pairs, size_t count)
{
    cJSON *result = cJSON_CreateObject();
    const cJSON *ids = cJSON_GetObjectItemCaseSensitive(request, "entity_ids");
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, ids)
    {
        cJSON *entry = cJSON_AddObjectToObject(result, item->valuestring);
        for (size_t i = 0; i < count; i++) {
            const char *colon = strchr(pairs[i], ':');
            if (strncmp(pairs[i], item->valuestring, (size_t)(colon - pairs[i])) == 0 &&
                strlen(item->valuestring) == (size_t)(colon - pairs[i])) {
                cJSON_AddStringToObject(entry, "translation_key", colon + 1);
            }
        }
        if (cJSON_GetObjectItemCaseSensitive(entry, "translation_key") == NULL) cJSON_AddNullToObject(entry, "translation_key");
    }
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(request, "id");
    CHECK(ha_printer_lookup_handle_result((uint32_t)id->valuedouble, true, result));
    cJSON_Delete(result);
}

static void test_full_lookup(void)
{
    int64_t now = 1000;
    ha_printer_lookup_request();
    CHECK(ha_printer_lookup_is_due(now));
    cJSON *r = next(now);
    CHECK(r != NULL && !strcmp(type_of(r), "config_entries/get"));
    CHECK(!strcmp(cJSON_GetObjectItemCaseSensitive(r, "domain")->valuestring, "bambu_lab"));
    CHECK(!ha_printer_lookup_is_due(now)); /* one request in flight */
    CHECK(answer(r, "[{\"entry_id\":\"e1\",\"title\":\"A1 Workshop\",\"domain\":\"bambu_lab\"},"
                    "{\"entry_id\":\"e2\",\"title\":\"P1S\",\"domain\":\"bambu_lab\"}]", true));
    cJSON_Delete(r);

    /* printer 1: 30 related entities, more than one batch of 24 */
    r = next(now);
    CHECK(r != NULL && !strcmp(type_of(r), "search/related"));
    CHECK(!strcmp(cJSON_GetObjectItemCaseSensitive(r, "item_id")->valuestring, "e1"));
    char related[2048] = "{\"entity\":[\"sensor.a1_status\",\"sensor.a1_nozzle\",\"button.a1_stop\",\"sensor.a1_stop_text\",\"camera.a1\"";
    for (int i = 0; i < 26; i++) {
        char one[48];
        snprintf(one, sizeof(one), ",\"sensor.a1_other_%d\"", i);
        strcat(related, one);
    }
    strcat(related, "],\"device\":[\"d1\"]}");
    CHECK(answer(r, related, true));
    cJSON_Delete(r);

    const char *pairs1[] = {"sensor.a1_status:print_status", "sensor.a1_nozzle:nozzle_temp", "button.a1_stop:stop",
                            "sensor.a1_stop_text:stop", "camera.a1:camera"}; /* a sensor with the key of a button must not be taken */
    int batches = 0;
    while ((r = next(now)) != NULL && !strcmp(type_of(r), "config/entity_registry/get_entries")) {
        batches++;
        CHECK(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(r, "entity_ids")) <= HA_PRINTER_LOOKUP_BATCH);
        answer_entries(r, pairs1, 5);
        cJSON_Delete(r);
        r = NULL;
    }
    CHECK(batches == 2);
    /* the loop ended on printer 2's search/related */
    CHECK(r != NULL && !strcmp(type_of(r), "search/related") &&
          !strcmp(cJSON_GetObjectItemCaseSensitive(r, "item_id")->valuestring, "e2"));
    CHECK(answer(r, "{\"entity\":[\"sensor.p1_status\"]}", true));
    cJSON_Delete(r);
    r = next(now);
    CHECK(r != NULL && !strcmp(type_of(r), "config/entity_registry/get_entries"));
    const char *pairs2[] = {"sensor.p1_status:print_status"};
    answer_entries(r, pairs2, 1);
    cJSON_Delete(r);
    CHECK(next(now) == NULL);

    char *json = ha_printer_lookup_json();
    CHECK(json != NULL);
    cJSON *out = cJSON_Parse(json);
    CHECK(!strcmp(cJSON_GetObjectItemCaseSensitive(out, "status")->valuestring, "ready"));
    const cJSON *printers = cJSON_GetObjectItemCaseSensitive(out, "printers");
    CHECK(cJSON_GetArraySize(printers) == 2);
    const cJSON *a1 = cJSON_GetArrayItem(printers, 0);
    CHECK(!strcmp(cJSON_GetObjectItemCaseSensitive(a1, "name")->valuestring, "A1 Workshop"));
    const cJSON *entities = cJSON_GetObjectItemCaseSensitive(a1, "entities");
    CHECK(!strcmp(cJSON_GetObjectItemCaseSensitive(entities, "entity_id")->valuestring, "sensor.a1_status"));
    CHECK(!strcmp(cJSON_GetObjectItemCaseSensitive(entities, "nozzle_temp_entity_id")->valuestring, "sensor.a1_nozzle"));
    CHECK(!strcmp(cJSON_GetObjectItemCaseSensitive(entities, "stop_entity_id")->valuestring, "button.a1_stop"));
    CHECK(!strcmp(cJSON_GetObjectItemCaseSensitive(entities, "camera_entity_id")->valuestring, "camera.a1"));
    CHECK(cJSON_GetObjectItemCaseSensitive(entities, "bed_temp_entity_id") == NULL); /* not found stays absent */
    const cJSON *p1 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(printers, 1), "entities");
    CHECK(cJSON_GetArraySize(p1) == 1);
    cJSON_Delete(out);
    cJSON_free(json);
}

static void test_no_integration_and_failures(void)
{
    int64_t now = 5000;
    ha_printer_lookup_request();
    cJSON *r = next(now);
    CHECK(answer(r, "[]", true)); /* no Bambu Lab integration: ready with no printers */
    cJSON_Delete(r);
    char *json = ha_printer_lookup_json();
    CHECK(strstr(json, "\"ready\"") != NULL && strstr(json, "\"printers\":[]") != NULL);
    cJSON_free(json);

    ha_printer_lookup_request();
    r = next(now);
    CHECK(answer(r, "null", false)); /* Home Assistant refused */
    cJSON_Delete(r);
    json = ha_printer_lookup_json();
    CHECK(strstr(json, "\"error\"") != NULL);
    cJSON_free(json);

    /* a stale or foreign answer is not ours; an unanswered request times out */
    ha_printer_lookup_request();
    r = next(now);
    CHECK(!ha_printer_lookup_handle_result(999999, true, NULL));
    cJSON_Delete(r);
    CHECK(!ha_printer_lookup_is_due(now + HA_PRINTER_LOOKUP_TIMEOUT_MS - 1));
    CHECK(!ha_printer_lookup_is_due(now + HA_PRINTER_LOOKUP_TIMEOUT_MS));
    json = ha_printer_lookup_json();
    CHECK(strstr(json, "\"error\"") != NULL);
    cJSON_free(json);

    /* a dropped connection ends a running lookup; a failed send is retried */
    ha_printer_lookup_request();
    r = next(now);
    ha_printer_lookup_send_failed();
    cJSON_Delete(r);
    CHECK(ha_printer_lookup_is_due(now));
    r = next(now);
    cJSON_Delete(r);
    ha_printer_lookup_cancel();
    json = ha_printer_lookup_json();
    CHECK(strstr(json, "\"error\"") != NULL);
    cJSON_free(json);
}

int main(void)
{
    test_full_lookup();
    test_no_integration_and_failures();
    printf("%d checks, %d failed\n", s_checks, s_failures);
    if (s_failures == 0) printf("PASS: printer lookup steps, batches, keys and failures\n");
    return s_failures == 0 ? 0 : 1;
}
