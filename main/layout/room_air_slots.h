/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include "app_config.h"
#include "cJSON.h"
#include "ui/widgets/room_air_model.h"

typedef struct { const char *key, *domains; int slot; } air_binding_t;
/* Negative slots: primary/secondary/tertiary; nonnegative slots: extra_entity_id[]. */
extern const air_binding_t air_bindings[AIR_ROLE_COUNT];
void air_slots_from_json(const cJSON *json, char *primary, char *secondary, char *tertiary,
                         char (*extra)[APP_MAX_ENTITY_ID_LEN]);
/* Returns false for malformed custom thresholds; always initializes out. */
bool air_config_from_json(const cJSON *json, air_config_t *out);
