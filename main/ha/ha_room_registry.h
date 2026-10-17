/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Area and floor registry of Home Assistant for the room cards, the house page and the editor's
 * room import: the temperature/humidity sensor an area designates and the floor levels. Holds
 * only state and parsing; ha_client.c sends the WebSocket requests. Not thread-safe on its own:
 * every call must hold the caller's lock (ha_client's mutex).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"

/* An answer that has not arrived by then (connection lost meanwhile) no longer blocks a new request. */
#define HA_ROOM_REGISTRY_TIMEOUT_MS 15000

/* Asks for the registry to be read (again) at the next opportunity. */
void ha_room_registry_request(void);
/* True while a request is asked for but not sent yet. */
bool ha_room_registry_is_due(void);
/* Marks both requests as sent and clears the due flag in one step; false without memory. */
bool ha_room_registry_begin(uint32_t area_req_id, uint32_t floor_req_id, int64_t now_ms);
/* Forgets the requests of a failed send. */
void ha_room_registry_cancel(void);
/* Stores an area_registry/floor_registry answer; false if msg_id is not one of ours. */
bool ha_room_registry_handle_result(uint32_t msg_id, bool success, const cJSON *result);
/* For the areas-only editor call: asks for a read when there are no areas yet or refresh is
 * set and nothing is under way; returns whether an answer is still to come. */
bool ha_room_registry_poll(bool connected, bool refresh, int64_t now_ms);
bool ha_room_registry_has_areas(void);
/* Adds "areas": [{name, temperature_entity_id, humidity_entity_id, floor, floor_level?}] to root. */
void ha_room_registry_add_areas_json(cJSON *root);
