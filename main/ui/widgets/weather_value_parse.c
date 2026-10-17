/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Stateless attribute parsing for the weather widget.
 */
#include "ui/widgets/weather_value_parse.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool weather_parse_float_token(const char *text, float *out)
{
    if (text == NULL || out == NULL) {
        return false;
    }
    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }
    if (*text == '\0') {
        return false;
    }
    char *end = NULL;
    float value = strtof(text, &end);
    if (end == text) {
        return false;
    }
    *out = value;
    return true;
}

bool weather_parse_int_token(const char *text, int *out)
{
    if (text == NULL || out == NULL) {
        return false;
    }
    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }
    if (*text == '\0') {
        return false;
    }
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (end == text) {
        return false;
    }
    *out = (int)value;
    return true;
}

bool weather_json_item_to_float(cJSON *item, float *out)
{
    if (item == NULL || out == NULL) {
        return false;
    }
    if (cJSON_IsNumber(item)) {
        *out = (float)item->valuedouble;
        return true;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return weather_parse_float_token(item->valuestring, out);
    }
    return false;
}

bool weather_json_item_to_int(cJSON *item, int *out)
{
    if (item == NULL || out == NULL) {
        return false;
    }
    if (cJSON_IsNumber(item)) {
        *out = item->valueint;
        return true;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return weather_parse_int_token(item->valuestring, out);
    }
    return false;
}

static const char *weather_find_json_key(const char *json, const char *key)
{
    if (json == NULL || key == NULL || key[0] == '\0') {
        return NULL;
    }

    char needle[64] = {0};
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    return strstr(json, needle);
}

bool weather_extract_raw_number_attr(const char *json, const char *key, float *out)
{
    const char *pos = weather_find_json_key(json, key);
    if (pos == NULL) {
        return false;
    }

    pos = strchr(pos, ':');
    if (pos == NULL) {
        return false;
    }
    pos++;
    while (*pos != '\0' && isspace((unsigned char)*pos)) {
        pos++;
    }

    if (*pos == '"') {
        pos++;
        return weather_parse_float_token(pos, out);
    }
    return weather_parse_float_token(pos, out);
}

bool weather_extract_raw_int_attr(const char *json, const char *key, int *out)
{
    const char *pos = weather_find_json_key(json, key);
    if (pos == NULL) {
        return false;
    }

    pos = strchr(pos, ':');
    if (pos == NULL) {
        return false;
    }
    pos++;
    while (*pos != '\0' && isspace((unsigned char)*pos)) {
        pos++;
    }

    if (*pos == '"') {
        pos++;
        return weather_parse_int_token(pos, out);
    }
    return weather_parse_int_token(pos, out);
}

bool weather_extract_raw_string_attr(const char *json, const char *key, char *out, size_t out_size)
{
    const char *pos = weather_find_json_key(json, key);
    if (pos == NULL || out == NULL || out_size == 0) {
        return false;
    }

    pos = strchr(pos, ':');
    if (pos == NULL) {
        return false;
    }
    pos++;
    while (*pos != '\0' && isspace((unsigned char)*pos)) {
        pos++;
    }

    if (*pos != '"') {
        return false;
    }
    pos++;

    size_t written = 0;
    while (*pos != '\0' && *pos != '"' && written + 1 < out_size) {
        if (*pos == '\\' && pos[1] != '\0') {
            pos++;
        }
        out[written++] = *pos++;
    }
    out[written] = '\0';
    return written > 0;
}
