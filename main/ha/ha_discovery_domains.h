/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * The Home Assistant domains the editor's entity discovery (GET /api/entities) can list, in one
 * place: the directly requested domains, the "any" pseudo domain (add a widget from an entity)
 * and the "room" pseudo domain (room cards and the room import). Pure C, host tested; ha_client.c
 * uses these for its filters and builds the Jinja discovery templates from the same lists.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

/* True for a domain the discovery accepts, including the "any" and "room" pseudo domains. */
bool ha_discovery_domain_supported(const char *domain);
/* `domain` when supported, else "light" (the historical default of the light picker). */
const char *ha_discovery_domain_or_default(const char *domain);
/* True when `entity_id` belongs to `domain` (after ha_discovery_domain_or_default()); for "any"
 * and "room" when it belongs to one of their domains. */
bool ha_discovery_entity_matches(const char *entity_id, const char *domain);
/* The "any" or "room" domain list as a Jinja list literal, e.g. ['sensor','light'], for the
 * discovery templates. Returns false when `out` is too small or `pseudo` is neither. */
bool ha_discovery_domains_jinja(const char *pseudo, char *out, size_t out_len);
