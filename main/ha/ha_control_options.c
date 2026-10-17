/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_control_options.h"
#include <string.h>

static bool usable(const char *value)
{
    return value != NULL && value[0] != '\0' && strlen(value) < HA_CONTROL_OPTION_LEN;
}

int ha_control_options_find(const ha_control_options_t *options, const char *value)
{
    for (size_t i = 0; options != NULL && value != NULL && i < options->count; i++) {
        if (strcmp(options->values[i], value) == 0) return (int)i;
    }
    return -1;
}

void ha_control_options_parse(const char *json, const char *list_key, const char *current_key, ha_control_options_t *out)
{
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    cJSON *attrs = json != NULL ? cJSON_Parse(json) : NULL;
    const char *current = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(attrs, current_key));
    if (usable(current)) strcpy(out->current, current);
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(attrs, list_key);
    if (cJSON_IsArray(list)) {
        const cJSON *item;
        cJSON_ArrayForEach(item, list) {
            const char *value = cJSON_GetStringValue(item);
            if (out->count >= HA_CONTROL_MAX_OPTIONS) break;
            if (usable(value) && ha_control_options_find(out, value) < 0) strcpy(out->values[out->count++], value);
        }
    }
    cJSON_Delete(attrs);
}

bool ha_control_option_payload(const char *entity_id, const char *key, const char *value, char *out, size_t out_len)
{
    if (entity_id == NULL || key == NULL || !usable(value) || out == NULL || out_len == 0) return false;
    cJSON *object = cJSON_CreateObject();
    char *json = NULL;
    if (object != NULL && cJSON_AddStringToObject(object, "entity_id", entity_id) != NULL &&
        cJSON_AddStringToObject(object, key, value) != NULL) json = cJSON_PrintUnformatted(object);
    bool ok = json != NULL && strlen(json) < out_len;
    if (ok) strcpy(out, json);
    cJSON_free(json);
    cJSON_Delete(object);
    return ok;
}

void ha_control_options_copy(cJSON *dst, const cJSON *src, const char *key)
{
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(src, key);
    if (!cJSON_IsArray(list)) return;
    cJSON *copy = cJSON_AddArrayToObject(dst, key);
    if (copy == NULL) return;
    const cJSON *item;
    cJSON_ArrayForEach(item, list) {
        if (cJSON_GetArraySize(copy) >= HA_CONTROL_MAX_OPTIONS) break;
        const char *value = cJSON_GetStringValue(item);
        if (!usable(value)) continue;
        bool duplicate = false;
        const cJSON *kept;
        cJSON_ArrayForEach(kept, copy) {
            if (strcmp(cJSON_GetStringValue(kept), value) == 0) { duplicate = true; break; }
        }
        if (!duplicate) cJSON_AddItemToArray(copy, cJSON_CreateString(value));
    }
}

bool ha_control_options_print(cJSON *object, const char *list_key, char *out, size_t out_len)
{
    cJSON *list = cJSON_GetObjectItemCaseSensitive(object, list_key);
    for (;;) {
        char *json = cJSON_PrintUnformatted(object);
        if (json == NULL) return false;
        bool fits = strlen(json) < out_len;
        if (fits) memcpy(out, json, strlen(json) + 1);
        cJSON_free(json);
        if (fits) return true;
        int count = cJSON_IsArray(list) ? cJSON_GetArraySize(list) : 0;
        if (count == 0) return false;
        cJSON_DeleteItemFromArray(list, count - 1);
    }
}
