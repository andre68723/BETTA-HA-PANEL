/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * The generic attributes_json overflow fallback: pure cJSON logic with no ESP-IDF dependency, kept
 * in its own file specifically so it has host tests (tools/test_host.sh), unlike ha_client.c itself.
 * See docs/release-notes.md (v0.9.0, Tiles and pages) for the user-visible effect.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"

/* Most Home Assistant domains have their own compact attribute serializer in ha_attr_serialize.c (weather,
 * light, climate, media_player, cover, fan, ...), each keeping only the fields that domain's widgets
 * actually read. Any entity outside those domains still falls back to a verbatim copy of its raw
 * `attributes` object, which a handful of oversized integrations (large forecast/list payloads) can
 * overflow the fixed attributes_json buffer -- previously the buffer was left holding an invalid,
 * truncated JSON string in that case, which is silently unparseable by every reader.
 *
 * Call this only once that verbatim copy is known not to fit `out_json_size`. It keeps a small,
 * audited set of fields that the generic (non-domain-specific) widgets are known to read --
 * "device_class", "unit_of_measurement", "friendly_name" and "entity_picture" -- and always leaves
 * `out_json` holding valid JSON (either the compact object, or "{}" if none of those fields were
 * present or even the compact form somehow does not fit). Returns true if at least one field was
 * kept, false if `out_json` ended up as "{}".
 *
 * One series is kept as well: a Solcast-style "detailedForecast" becomes "fc_t0", "fc_step" and "fc_kw" for the
 * energy timeline's forecast line (see ha_attrs_compact.c).
 *
 * Other widget-specific fields (a cover's current_position, a fan's preset_modes, ...) are deliberately not
 * handled here: a domain that needs more than these four fields should get its own compact serializer
 * in ha_attr_serialize.c instead, following the existing per-domain pattern.
 */
bool ha_attrs_build_generic_overflow_fallback(cJSON *src_attrs, char *out_json, size_t out_json_size);
