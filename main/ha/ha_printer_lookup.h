/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Finds the Bambu Lab printers of Home Assistant and, for each, the entity of every binding of the Bambu printer widget
 * (layout/bambu_printer_slots.h), so the editor can offer "choose the printer" instead of ten entity fields. Three small
 * WebSocket steps, no registry dump: config_entries/get (domain bambu_lab: one entry per printer, its title is the
 * printer's name), search/related (the entities of that entry) and config/entity_registry/get_entries (their
 * translation_key, which names what an entity is whatever language Home Assistant speaks). ha_client.c sends the
 * requests on the HA task; the answer is read by GET /api/ha/printers. Requests and answers run on the HA task, the
 * snapshot may be read from any task.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"

#define HA_PRINTER_LOOKUP_MAX_PRINTERS 4
/* Entities per get_entries request; a registry entry is under 1 KB. */
#define HA_PRINTER_LOOKUP_BATCH 24
/* An answer that has not arrived by then (connection lost meanwhile) ends the lookup with an error. */
#define HA_PRINTER_LOOKUP_TIMEOUT_MS 15000

/* Any task: starts a new lookup (a running one is restarted). */
void ha_printer_lookup_request(void);
/* HA task: the connection dropped; a running lookup ends as an error. */
void ha_printer_lookup_cancel(void);
/* HA task: whether a request is waiting to be sent. */
bool ha_printer_lookup_is_due(int64_t now_ms);
/* HA task: the next request with msg_id, or NULL when nothing is due (the caller deletes it). */
cJSON *ha_printer_lookup_next_request(uint32_t msg_id, int64_t now_ms);
/* HA task: forgets the request of a failed send. */
void ha_printer_lookup_send_failed(void);
/* HA task: stores an answer; false if msg_id is not ours. */
bool ha_printer_lookup_handle_result(uint32_t msg_id, bool success, const cJSON *result);

/* Any task: {"status":"idle|pending|ready|error","printers":[{"id","name","entities":{<layout key>:<entity id>}}]}.
 * Caller frees with cJSON_free(); NULL without memory. */
char *ha_printer_lookup_json(void);
