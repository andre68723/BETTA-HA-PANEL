/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdint.h>

#include "sim_clock.h"

/* Monotonic microseconds; virtual on the simulator's fixed clock (sim_clock.h). */
static inline int64_t esp_timer_get_time(void)
{
    return sim_monotonic_us();
}
