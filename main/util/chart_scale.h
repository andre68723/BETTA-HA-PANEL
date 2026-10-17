/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * A readable value axis for a chart: round tick values (1, 2, 2.5 or 5 times a power of ten) that enclose the data,
 * and the number of decimals they need. Pure, so the host tests cover it.
 */
#pragma once

#include <stdbool.h>

#define CHART_SCALE_MAX_INTERVALS 8

typedef struct {
    float min;     /* first tick, at or below the lowest value */
    float max;     /* last tick, at or above the highest value */
    float step;    /* distance between ticks */
    int intervals; /* (max - min) / step, so intervals + 1 ticks */
    int decimals;  /* digits after the point that show every tick exactly, 0..3 */
} chart_scale_t;

/* lowest <= highest; `intervals` is the wanted number of gaps between ticks (1..CHART_SCALE_MAX_INTERVALS), the result may
 * have fewer or one more. A flat series gets a range of its own magnitude around it instead of a zero-height chart. */
void chart_scale_nice(float lowest, float highest, int intervals, chart_scale_t *out);

/* Formats `value` with `decimals` digits (a value that rounds to zero prints without a minus sign) and, when `unit` is
 * not empty, appends " <unit>". Returns out. */
char *chart_scale_format(char *out, unsigned size, float value, int decimals, const char *unit);

/* The unit of a displayed reading like "506 W", "33.4 %" or "25.7 °C": the last whitespace-separated token when it has no
 * digit and is at most 8 bytes long, else "". */
void chart_scale_unit_of(const char *reading, char *out, unsigned size);
