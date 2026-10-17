/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Fixed-width crash evidence, not peripheral registers. Written just before a deliberate abort,
 * checked at the next boot. Append reasons; archived values must keep their meaning. */
enum { COREDUMP_CAUSE_NONE, COREDUMP_CAUSE_PPA_TIMEOUT, COREDUMP_CAUSE_LVGL_STALL, COREDUMP_CAUSE_RUNTIME_STALL };
typedef struct {
    uint32_t cause;
    uint32_t width, height;
    uint32_t input_x, input_y;
    uint32_t rotation; /* degrees */
    uint32_t input_buffer, output_buffer, output_capacity;
    uint32_t wait_ms;
} coredump_diagnostic_t;

typedef struct {
    uint32_t magic;
    coredump_diagnostic_t data;
    uint32_t checksum;
} coredump_diagnostic_marker_t;

void coredump_diagnostic_seal(coredump_diagnostic_marker_t *marker, const coredump_diagnostic_t *data);
/* Only a panic reset may consume a marker; it is cleared even when invalid. */
bool coredump_diagnostic_take(coredump_diagnostic_marker_t *marker, bool panic_reset, coredump_diagnostic_t *out);
/* ESP glue: no filesystem writes or allocation in the aborting task. */
void coredump_diagnostics_mark(const coredump_diagnostic_t *data);
bool coredump_diagnostics_take(coredump_diagnostic_t *out);
