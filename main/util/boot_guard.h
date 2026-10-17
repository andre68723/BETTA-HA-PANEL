/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Crash-loop guard. A layout that crashes the firmware while it is built puts the panel into an endless panic
 * loop that only USB can break. The guard counts consecutive crash resets (panic or watchdog) of boots that did not
 * run for BOOT_GUARD_STABLE_S seconds; from BOOT_GUARD_MAX_CRASHES on, the next boot loads the built-in default
 * layout once instead of the stored one ("safe mode"), so the editor stays reachable. The stored layout is not touched.
 *
 * boot_guard_next() / boot_guard_safe() are pure (host tested); boot_guard_esp.c keeps the counter in RTC memory,
 * which survives a panic and a watchdog reset but not a power cycle.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BOOT_GUARD_MAGIC 0xB007C0DEu
#define BOOT_GUARD_MAX_CRASHES 3
#define BOOT_GUARD_STABLE_S 60

typedef struct {
    uint32_t magic;
    uint32_t crashes; /* consecutive boots that ended in a crash before they were stable */
    uint32_t stable;  /* the boot that wrote this state has run long enough */
} boot_guard_state_t;

/* Update the state for the boot that just started. `power_on`: the RTC memory holds nothing valid (power cycle, brownout);
 * `crash_reset`: the reason of this reset is a panic or a watchdog. */
void boot_guard_next(boot_guard_state_t *state, bool power_on, bool crash_reset);
/* True when the current state asks for the built-in layout. */
bool boot_guard_safe(const boot_guard_state_t *state);

/* ESP side (boot_guard_esp.c): call once early in app_main. */
void boot_guard_init(void);
/* True for the first call of a boot that started in safe mode (the initial layout load takes it), false afterwards. */
bool boot_guard_take_safe_layout(void);
/* True during a boot that started in safe mode (for /api/version and the editor banner). */
bool boot_guard_safe_mode(void);
