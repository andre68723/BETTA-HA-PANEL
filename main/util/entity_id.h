/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Home Assistant entity ids: one place for what a valid id looks like and which domains a notification
 * action may call. An id ends up inside a JSON service call, so anything that is not [a-z0-9_] around a single
 * dot must never get that far.
 */
#pragma once

#include <stdbool.h>

/* "domain.object_id": one dot, lowercase letters, digits and underscores, 3..95 characters. */
bool entity_id_valid(const char *entity_id);
/* Valid and in `domain`. */
bool entity_id_in_domain(const char *entity_id, const char *domain);
/* An id a notification or alert action may act on (press a button, unlock a lock, turn on a light, switch, scene,
 * script, input_boolean or fan): valid and in one of those domains. */
bool entity_id_action_allowed(const char *entity_id);
