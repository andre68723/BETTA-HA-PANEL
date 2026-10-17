/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * The options of a select / input_select entity, read from its attributes_json, and the service
 * that picks one. Pure C, host tested; used by the select tile.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define HA_SELECT_MAX_OPTIONS 24
#define HA_SELECT_OPTION_LEN 48

typedef struct {
    char option[HA_SELECT_MAX_OPTIONS][HA_SELECT_OPTION_LEN];
    size_t count;
    /* The attributes had no usable "options" list (e.g. dropped because the attributes were too
     * large): the tile says so instead of offering an empty picker. */
    bool missing;
} ha_select_options_t;

/* Fills `out` from the entity's attributes JSON. Options longer than HA_SELECT_OPTION_LEN - 1 are
 * left out (picking a shortened one would fail in Home Assistant), and at most
 * HA_SELECT_MAX_OPTIONS are kept. */
void ha_select_options_parse(const char *attributes_json, ha_select_options_t *out);
/* Index of `state` among the options, or -1. */
int ha_select_options_find(const ha_select_options_t *options, const char *state);
/* The service domain for an entity: "input_select" for input_select.*, else "select". */
const char *ha_select_service_domain(const char *entity_id);
/* The select_option call's JSON data; false when `out` is too small. */
bool ha_select_option_payload(const char *entity_id, const char *option, char *out, size_t out_len);
