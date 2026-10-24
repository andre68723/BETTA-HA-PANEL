/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Layout-driven Home Assistant subscriptions, independent of storage and transport.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "app_config.h"

/* Transport and registry must cover the model, including valid multi-page layouts with eight quotes per tile.
 * An average per-widget budget silently truncated layouts that still fit the 32 KiB file limit. */
#define HA_LAYOUT_ENTITY_CAPACITY APP_HA_MAX_ENTITIES

typedef void (*ha_layout_entity_overflow_fn)(size_t capacity, const char *entity_id);

typedef struct {
    /* Caller-owned capacity * APP_MAX_ENTITY_ID_LEN bytes, zero-initialized. */
    char *ids;
    size_t capacity;
    size_t count;
    bool need_weather_forecast;
    ha_layout_entity_overflow_fn on_overflow;
} ha_layout_entity_set_t;

/* Collects alert/page/widget entities from parsed layout JSON; resets count and forecast flag. */
void ha_layout_entities_collect(cJSON *root, ha_layout_entity_set_t *set);
/* Adds settings-derived top-bar entities after collecting the layout. */
void ha_layout_entities_add(ha_layout_entity_set_t *set, const char *entity_id);
bool ha_layout_entities_contains(const char *entity_ids, size_t count, const char *entity_id);
bool ha_layout_needs_ha_energy(cJSON *root);
/* Sorts the caller-owned array in place, then hashes it for change detection. */
uint32_t ha_layout_entities_signature(char *entity_ids, size_t entity_count);
