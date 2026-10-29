/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Display precision of numeric entities, as Home Assistant's own frontend rounds them: the entity
 * registry's options.sensor.display_precision (set by the user) or else suggested_display_precision
 * (set by the integration). The state itself carries neither. Reads the registry entries of the
 * layout's sensor/number entities in small batches (config/entity_registry/get_entries); ha_client.c
 * sends the requests. The same entries name the integration (platform) of each entity, which the
 * Stocks & ETFs widget needs to know how to read a quote (ha_quote.h). Requests and answers run on
 * the HA task; the lookups and formatting may be called from any task.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#define HA_DISPLAY_PRECISION_UNKNOWN (-1)
/* Entities per request; a full registry entry is under 1 KB, so a batch stays far below the WS buffer. */
#define HA_DISPLAY_PRECISION_BATCH 16
/* An answer that has not arrived by then (connection lost meanwhile) no longer blocks the next batch. */
#define HA_DISPLAY_PRECISION_TIMEOUT_MS 15000

typedef void (*ha_display_precision_changed_fn)(const char *entity_id);

/* HA task: the layout's entities (entity_ids holds count ids, APP_MAX_ENTITY_ID_LEN apart). Keeps the
 * sensor/number/input_number ones and asks for all of them again. False on allocation failure; the caller retries. */
bool ha_display_precision_set_wanted(const char *entity_ids, size_t count);
/* HA task: whether a batch is waiting to be asked for. */
bool ha_display_precision_is_due(int64_t now_ms);
/* HA task: the next request with msg_id, or NULL when nothing is due (the caller deletes it). */
cJSON *ha_display_precision_next_request(uint32_t msg_id, int64_t now_ms);
/* HA task: forgets the request of a failed send; its entities are asked for again. */
void ha_display_precision_cancel(void);
/* HA task: stores a get_entries answer and reports each entity whose precision changed; false if
 * msg_id is not ours. */
bool ha_display_precision_handle_result(
    uint32_t msg_id, bool success, const cJSON *result, ha_display_precision_changed_fn changed);

/* Any task: the integration of entity_id from its registry entry: NULL while not answered yet, "" when the
 * lookup failed or timed out, "easy_stock", or "other" for any other integration (and for
 * an entity without a registry entry). */
const char *ha_display_precision_platform(const char *entity_id);

/* Any task: the precision of entity_id, or HA_DISPLAY_PRECISION_UNKNOWN. */
int ha_display_precision_get(const char *entity_id);
/* Any task: writes state rounded to the entity's precision; false (out untouched) when there is no
 * precision or state is not a plain number, so the caller shows state as it is. */
bool ha_display_precision_format(const char *entity_id, const char *state, char *out, size_t out_len);
