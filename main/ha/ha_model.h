/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "app_config.h"

typedef struct {
    char id[APP_MAX_ENTITY_ID_LEN];
    char name[APP_MAX_NAME_LEN];
    char domain[APP_MAX_NAME_LEN];
    char unit[APP_MAX_UNIT_LEN];
    char device_class[APP_MAX_NAME_LEN];
    char state_class[24];
    bool has_price_series;
    bool has_solar_forecast;
    uint32_t supported_features;
    bool supports_dimming;
    bool supports_color;
    bool supports_color_temp;
    char icon[APP_MAX_ICON_LEN];
} ha_entity_info_t;

typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    char state[APP_MAX_STATE_LEN];
    char attributes_json[APP_HA_ATTRS_MAX_LEN];
    int64_t last_changed_unix_ms;
    int64_t ha_changed_s; /* Home Assistant "last changed" as Unix seconds, 0 = unknown */
} ha_state_t;

esp_err_t ha_model_init(void);
void ha_model_reset(void);
esp_err_t ha_model_upsert_entity(const ha_entity_info_t *entity);
esp_err_t ha_model_upsert_state(const ha_state_t *state);
/* Updates only the discovery name, preserving state/capability metadata. */
esp_err_t ha_model_set_entity_name(const char *entity_id, const char *name);
/* Copies the shared display name when available; leaves out unchanged otherwise. */
bool ha_model_get_entity_name(const char *entity_id, char *out, size_t out_len);
bool ha_model_get_state(const char *entity_id, ha_state_t *out_state);
typedef struct {
    size_t slot; /* Stable until reset; revision remains monotonic across resets. */
    uint32_t revision;
} ha_state_version_t;
typedef struct {
    char entity_id[APP_MAX_ENTITY_ID_LEN];
    ha_state_version_t version;
} ha_model_change_t;
bool ha_model_get_state_versioned(const char *entity_id, ha_state_t *out_state, ha_state_version_t *version);
/* Latest cached changes after a cursor, ordered by revision (modulo uint32).
 * A partial batch advances only through its last entry. Copy outside the display lock;
 * advance the consumer's cursor only after processing the entire batch. */
esp_err_t ha_model_changes(uint32_t after, ha_model_change_t *out, size_t capacity,
                           size_t *count, uint32_t *next);
uint32_t ha_model_state_revision(void);
size_t ha_model_list_entities(
    const char *domain_filter, const char *search, ha_entity_info_t *out_entities, size_t max_out);
size_t ha_model_list_states(ha_state_t *out_states, size_t max_out);
/* One page of the list from offset; total (may be NULL) is the number of matches, so a caller such as the
 * editor's HTTP API can ask for the following pages. */
size_t ha_model_list_entities_page(const char *domain_filter, const char *search, size_t offset,
    ha_entity_info_t *out_entities, size_t max_out, size_t *total);
size_t ha_model_list_states_page(size_t offset, ha_state_t *out_states, size_t max_out, size_t *total);
