/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/chart_scale.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static float nice_step(float raw)
{
    const float magnitude = powf(10.0f, floorf(log10f(raw)));
    const float fraction = raw / magnitude;
    const float nice = fraction <= 1.0f ? 1.0f : fraction <= 2.0f ? 2.0f : fraction <= 2.5f ? 2.5f : fraction <= 5.0f ? 5.0f : 10.0f;
    return nice * magnitude;
}

void chart_scale_nice(float lowest, float highest, int intervals, chart_scale_t *out)
{
    if (intervals < 1) intervals = 1;
    if (intervals > CHART_SCALE_MAX_INTERVALS) intervals = CHART_SCALE_MAX_INTERVALS;
    if (!(highest >= lowest)) highest = lowest;
    float span = highest - lowest;
    if (span < 1e-6f * (fabsf(highest) + 1.0f)) {
        /* flat series: a window of its own magnitude, at least 1 unit */
        span = fabsf(highest) * 0.1f;
        if (span < 1.0f) span = 1.0f;
        lowest -= span / 2.0f;
        highest += span / 2.0f;
        span = highest - lowest;
    }
    float step = nice_step(span / (float)intervals);
    float first = floorf(lowest / step + 1e-4f) * step;
    float last = ceilf(highest / step - 1e-4f) * step;
    int count = (int)lroundf((last - first) / step);
    if (count < 1) { count = 1; last = first + step; }
    int decimals = 0;
    while (decimals < 3 && fabsf(step * powf(10.0f, (float)decimals) - roundf(step * powf(10.0f, (float)decimals))) > 1e-3f) {
        decimals++;
    }
    out->min = first;
    out->max = last;
    out->step = step;
    out->intervals = count;
    out->decimals = decimals;
}

char *chart_scale_format(char *out, unsigned size, float value, int decimals, const char *unit)
{
    if (size == 0) return out;
    if (decimals < 0) decimals = 0;
    if (decimals > 3) decimals = 3;
    const float scale = powf(10.0f, (float)decimals);
    if (fabsf(roundf(value * scale)) < 0.5f) value = 0.0f;
    if (unit != NULL && unit[0] != '\0') snprintf(out, size, "%.*f %s", decimals, (double)value, unit);
    else snprintf(out, size, "%.*f", decimals, (double)value);
    return out;
}

void chart_scale_unit_of(const char *reading, char *out, unsigned size)
{
    if (size == 0) return;
    out[0] = '\0';
    if (reading == NULL) return;
    const char *end = reading + strlen(reading);
    while (end > reading && (end[-1] == ' ')) end--;
    const char *start = end;
    while (start > reading && start[-1] != ' ') start--;
    const unsigned len = (unsigned)(end - start);
    if (start == reading || len == 0 || len > 8 || len >= size) return; /* a bare value has no separate unit token */
    for (const char *p = start; p < end; p++) {
        if (*p >= '0' && *p <= '9') return;
    }
    memcpy(out, start, len);
    out[len] = '\0';
}
