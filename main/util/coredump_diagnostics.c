/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/coredump_diagnostics.h"
#include <stddef.h>
#include <string.h>

#define DIAGNOSTIC_MAGIC 0x43445031u
static uint32_t checksum(const coredump_diagnostic_marker_t *marker)
{
    uint32_t hash = 2166136261u;
    const uint8_t *bytes = (const uint8_t *)marker;
    for (size_t i = 0; i < offsetof(coredump_diagnostic_marker_t, checksum); i++) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

void coredump_diagnostic_seal(coredump_diagnostic_marker_t *marker, const coredump_diagnostic_t *data)
{
    memset(marker, 0, sizeof(*marker));
    marker->data = *data;
    marker->magic = DIAGNOSTIC_MAGIC;
    marker->checksum = checksum(marker);
}

bool coredump_diagnostic_take(coredump_diagnostic_marker_t *marker, bool panic_reset, coredump_diagnostic_t *out)
{
    memset(out, 0, sizeof(*out));
    bool valid = panic_reset && marker->magic == DIAGNOSTIC_MAGIC && marker->checksum == checksum(marker) &&
                 marker->data.cause > COREDUMP_CAUSE_NONE && marker->data.cause <= COREDUMP_CAUSE_RUNTIME_STALL;
    if (valid) *out = marker->data;
    memset(marker, 0, sizeof(*marker));
    return valid;
}
