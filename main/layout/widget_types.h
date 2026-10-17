/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * The widget types in one table: which types exist, their size limits on this variant and which entity
 * domains their primary entity may come from. The layout validator and the runtime (clamping) both read it,
 * so a new type is one row here instead of a branch in each of them.
 */
#pragma once

#include <stdbool.h>
#include "cJSON.h"

typedef struct {
    int min_w;
    int min_h;
    int max_w;
    int max_h;
} widget_size_limits_t;

/* True for a known type, including the legacy name "roborock_tile" of the vacuum tile. */
/* Linear gauge, segmented look: number of LED blocks (layout field gauge_segment_count). */
#define GAUGE_SEGMENT_COUNT_MIN 4
#define GAUGE_SEGMENT_COUNT_MAX 30
#define GAUGE_SEGMENT_COUNT_DEFAULT 10

bool widget_type_known(const char *type);
/* All current widget types have a separable display name; unknown types do not. */
bool widget_type_supports_name(const char *type);
/* show_name defaults to true (false for the weather tiles). For legacy sensor layouts, sensor_show_label is the fallback. */
bool widget_show_name(const cJSON *widget);
/* The limits of a type; a generic 60x60 .. content box for an unknown one. */
widget_size_limits_t widget_type_size_limits(const char *type);
/* The limits for a widget with this look (style_variant, may be NULL): a look with a different shape,
 * such as the flat horizontal gauge, has its own; any other uses its type's. */
widget_size_limits_t widget_size_limits_for(const char *type, const char *style_variant);
/* True when entity_id is in one of the domains the type accepts (always true for types that take any). */
bool widget_type_entity_domain_ok(const char *type, const char *entity_id);
/* The accepted domains as "a.* or b.*" for an error message ("?" for an unknown type). */
void widget_type_domain_hint(const char *type, char *out, unsigned out_len);
