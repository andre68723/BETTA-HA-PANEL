/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"

#define HA_CONTROL_MAX_OPTIONS 24
#define HA_CONTROL_OPTION_LEN 96
typedef struct {
    char values[HA_CONTROL_MAX_OPTIONS][HA_CONTROL_OPTION_LEN];
    char current[HA_CONTROL_OPTION_LEN];
    size_t count;
} ha_control_options_t;

/* Keeps complete service values only; invalid, overlong and duplicate names are omitted. */
void ha_control_options_parse(const char *json, const char *list_key, const char *current_key, ha_control_options_t *out);
int ha_control_options_find(const ha_control_options_t *options, const char *value);
bool ha_control_option_payload(const char *entity_id, const char *key, const char *value, char *out, size_t out_len);
/* Adds a bounded list; printing drops trailing choices as needed, preserving other attributes. */
void ha_control_options_copy(cJSON *dst, const cJSON *src, const char *key);
bool ha_control_options_print(cJSON *object, const char *list_key, char *out, size_t out_len);
