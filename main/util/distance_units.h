/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Distances shown in kilometres. Some cars report a range in metres (the VW ID.4 integration: 66000 m) or miles;
 * a reading of "66000.0 m" on a gauge is correct but useless. One place decides, so the car widget and the gauge
 * agree.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Converts `value` from "m" or "mi" to kilometres in place and returns true; "km" (or an empty unit) is left as
 * it is and returns true too. Any other unit returns false and leaves the value alone. For a widget that
 * always shows kilometres, such as a car's range. */
bool distance_to_km(const char *unit, double *value);

/* The text a general readout should show for a distance in metres: when `device_class` is "distance", `unit` is
 * "m" and the value is at least 1000 m, writes "66 km" (at most one decimal, no trailing ".0") to `out` and
 * returns true. Everything else (a real 3 m distance, another unit, another class) returns false and the caller
 * keeps its own text. `value_text` is Home Assistant's state text. */
bool distance_readout_km(const char *device_class, const char *unit, const char *value_text, char *out, size_t out_len);
