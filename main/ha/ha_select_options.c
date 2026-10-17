/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ha/ha_select_options.h"

#include <string.h>

#include "cJSON.h"

void ha_select_options_parse(const char *attributes_json, ha_select_options_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->missing = true;
    cJSON *attrs = attributes_json != NULL ? cJSON_Parse(attributes_json) : NULL;
    const cJSON *options = cJSON_GetObjectItemCaseSensitive(attrs, "options");
    if (cJSON_IsArray(options)) {
        out->missing = false;
        const cJSON *option = NULL;
        cJSON_ArrayForEach(option, options) {
            if (out->count >= HA_SELECT_MAX_OPTIONS) {
                break;
            }
            if (cJSON_IsString(option) && option->valuestring[0] != '\0' &&
                strlen(option->valuestring) < HA_SELECT_OPTION_LEN) {
                strcpy(out->option[out->count++], option->valuestring);
            }
        }
    }
    cJSON_Delete(attrs);
}

int ha_select_options_find(const ha_select_options_t *options, const char *state)
{
    for (size_t i = 0; options != NULL && state != NULL && i < options->count; i++) {
        if (strcmp(options->option[i], state) == 0) {
            return (int)i;
        }
    }
    return -1;
}

const char *ha_select_service_domain(const char *entity_id)
{
    return entity_id != NULL && strncmp(entity_id, "input_select.", 13) == 0 ? "input_select" : "select";
}

bool ha_select_option_payload(const char *entity_id, const char *option, char *out, size_t out_len)
{
    if (entity_id == NULL || option == NULL || out == NULL || out_len == 0) {
        return false;
    }
    /* cJSON escapes quotes and backslashes in option names. */
    cJSON *payload = cJSON_CreateObject();
    char *text = NULL;
    if (payload != NULL && cJSON_AddStringToObject(payload, "entity_id", entity_id) != NULL &&
        cJSON_AddStringToObject(payload, "option", option) != NULL) {
        text = cJSON_PrintUnformatted(payload);
    }
    const bool ok = text != NULL && strlen(text) < out_len;
    if (ok) {
        strcpy(out, text);
    }
    cJSON_free(text);
    cJSON_Delete(payload);
    return ok;
}
