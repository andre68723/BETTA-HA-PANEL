/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/distance_units.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define METRES_PER_KM 1000.0
#define KM_PER_MILE 1.609344

bool distance_to_km(const char *unit, double *value)
{
    if (value == NULL) {
        return false;
    }
    if (unit == NULL || unit[0] == '\0' || strcmp(unit, "km") == 0) {
        return true;
    }
    if (strcmp(unit, "m") == 0) {
        *value /= METRES_PER_KM;
        return true;
    }
    if (strcmp(unit, "mi") == 0) {
        *value *= KM_PER_MILE;
        return true;
    }
    return false;
}

bool distance_readout_km(const char *device_class, const char *unit, const char *value_text, char *out, size_t out_len)
{
    if (device_class == NULL || unit == NULL || value_text == NULL || out == NULL || out_len == 0 ||
        strcmp(device_class, "distance") != 0 || strcmp(unit, "m") != 0) {
        return false;
    }
    char *end = NULL;
    const double metres = strtod(value_text, &end);
    if (end == value_text || *end != '\0' || !isfinite(metres) || fabs(metres) < METRES_PER_KM) {
        return false;
    }
    const double km = metres / METRES_PER_KM;
    char number[32];
    snprintf(number, sizeof(number), "%.1f", km);
    const size_t n = strlen(number);
    if (n >= 2 && strcmp(number + n - 2, ".0") == 0) {
        number[n - 2] = '\0';
    }
    snprintf(out, out_len, "%s km", number);
    return true;
}
