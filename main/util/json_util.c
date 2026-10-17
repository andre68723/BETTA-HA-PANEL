/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/json_util.h"

#include <stdlib.h>

cJSON *json_util_parse(const char *json)
{
    if (json == NULL) {
        return NULL;
    }
    return cJSON_Parse(json);
}

char *json_util_print_unformatted(const cJSON *json)
{
    if (json == NULL) {
        return NULL;
    }
    return cJSON_PrintUnformatted(json);
}

bool json_util_get_string(const cJSON *obj, const char *key, const char **out_value)
{
    if (obj == NULL || key == NULL || out_value == NULL) {
        return false;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive((cJSON *)obj, key);
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    *out_value = item->valuestring;
    return true;
}

bool json_util_get_int(const cJSON *obj, const char *key, int *out_value)
{
    if (obj == NULL || key == NULL || out_value == NULL) {
        return false;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive((cJSON *)obj, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    *out_value = item->valueint;
    return true;
}

void json_util_safe_free(char **ptr)
{
    if (ptr == NULL || *ptr == NULL) {
        return;
    }
    free(*ptr);
    *ptr = NULL;
}

uint32_t json_util_number_u32(const cJSON *item)
{
    if (!cJSON_IsNumber(item)) {
        return 0;
    }
    const double d = item->valuedouble;
    if (!(d >= 1.0)) {
        return 0; /* also NaN */
    }
    return d >= 4294967295.0 ? UINT32_MAX : (uint32_t)d;
}
