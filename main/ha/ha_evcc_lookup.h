/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723
 * Bounded evcc config-entry/related/get_entries lookup. HA-task-owned requests; HTTP only requests a restart. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"

#define HA_EVCC_LOOKUP_MAX_LOADPOINTS 8
/* Entities per get_entries request; a registry entry is under 1 KB. */
#define HA_EVCC_LOOKUP_BATCH 24
/* An answer that has not arrived by then (connection lost meanwhile) ends the lookup with an error. */
#define HA_EVCC_LOOKUP_TIMEOUT_MS 15000

/* Any task: starts a new lookup (a running one is restarted). */
void ha_evcc_lookup_request(void);
/* HA task: the connection dropped; a running lookup ends as an error. */
void ha_evcc_lookup_cancel(void);
/* HA task: whether a request is waiting to be sent. */
bool ha_evcc_lookup_is_due(int64_t now_ms);
/* HA task: the next request with msg_id, or NULL when nothing is due (the caller deletes it). */
cJSON *ha_evcc_lookup_next_request(uint32_t msg_id, int64_t now_ms);
/* HA task: forgets the request of a failed send. */
void ha_evcc_lookup_send_failed(void);
/* HA task: stores an answer; false if msg_id is not ours. */
bool ha_evcc_lookup_handle_result(uint32_t msg_id, bool success, const cJSON *result);

/* Any task: {"status":"idle|pending|ready|error","loadpoints":[{"id","name","entities":{<layout key>:<entity id>}}]}.
 * Caller frees with cJSON_free(); NULL without memory. */
char *ha_evcc_lookup_json(void);
