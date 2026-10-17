/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "app_config.h"
#include "cJSON.h"

#define HA_REGISTRY_ID_LEN 40
typedef struct {
    char id[HA_REGISTRY_ID_LEN];
    char area_id[HA_REGISTRY_ID_LEN];
    char parent_id[HA_REGISTRY_ID_LEN];
    char name[APP_MAX_NAME_LEN];
} ha_registry_device_t;

typedef struct {
    char text[APP_MAX_NAME_LEN];
    bool prefix_device;
} ha_registry_name_t;

bool ha_registry_device_parse(const cJSON *entry, ha_registry_device_t *out);
/* Walks parent links only while no explicit area is set. Missing parents and cycles return "". */
const char *ha_registry_device_area(const ha_registry_device_t *devices, size_t count, const char *id);
void ha_registry_name_parse(const cJSON *entry, bool display, ha_registry_name_t *out);
void ha_registry_name_resolve(const ha_registry_name_t *name, const char *entity_id, const char *device_id,
    const ha_registry_device_t *devices, size_t count, char *out, size_t out_len);
