/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Shared JSON body -> app_notification_t parsing/validation, extracted out
 * of api_notifications_post_handler() so both the REST endpoint
 * (POST /api/notifications) and the MQTT command-topic handler (added in a
 * later phase) parse identical payload shapes with identical validation.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "app_events.h"

/* Parses a notification JSON body (the same `{"message":..., "title":...,
 * "message_id":..., "timeout_sec":...}` shape accepted by
 * POST /api/notifications) into a heap-allocated app_notification_t.
 *
 * `json` must be a NUL-terminated buffer holding the JSON document; `len`
 * is currently informational only (kept for callers that already know the
 * body length, e.g. an HTTP content-length or an MQTT payload length).
 *
 * On success, returns true and sets *out to a newly calloc'd
 * app_notification_t; ownership transfers to the caller (same ownership
 * semantics api_notifications_post_handler already used: free it yourself
 * on failure to hand it off, otherwise pass ownership to
 * app_events_publish()).
 *
 * On failure, returns false and leaves *out untouched (do not read it).
 * If `err_msg` is non-NULL:
 *   - *err_msg is set to a static, non-owned string describing a JSON
 *     parsing/validation problem (e.g. "Invalid JSON", "Missing message")
 *     suitable for a 400 Bad Request response, or
 *   - *err_msg is set to NULL when the failure is an allocation failure
 *     (out of memory) rather than a bad payload -- callers should treat a
 *     false return with *err_msg == NULL as an internal/500-class error.
 */
bool notification_parse_json(const char *json, size_t len, app_notification_t **out, const char **err_msg);

/* Clears an action entity that is not a plain id in an action domain (see util/entity_id.h) and a camera entity that is not
 * a camera; called on every notification, whether it came from REST, MQTT or an alert of the layout. */
void notification_sanitize_entities(app_notification_t *notification);
