/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/coredump_diagnostics.h"
#include "esp_attr.h"
#include "esp_system.h"

static RTC_NOINIT_ATTR coredump_diagnostic_marker_t s_marker;

void coredump_diagnostics_mark(const coredump_diagnostic_t *data)
{
    coredump_diagnostic_seal(&s_marker, data);
}

bool coredump_diagnostics_take(coredump_diagnostic_t *out)
{
    return coredump_diagnostic_take(&s_marker, esp_reset_reason() == ESP_RST_PANIC, out);
}
