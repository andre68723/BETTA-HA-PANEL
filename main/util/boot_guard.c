/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/boot_guard.h"

#include <stddef.h>

void boot_guard_next(boot_guard_state_t *state, bool power_on, bool crash_reset)
{
    if (state == NULL) {
        return;
    }
    if (power_on || state->magic != BOOT_GUARD_MAGIC) {
        state->magic = BOOT_GUARD_MAGIC;
        state->crashes = 0;
    } else if (crash_reset && state->stable == 0) {
        /* The previous boot crashed before it had run long enough: a crash loop. */
        state->crashes++;
    } else {
        state->crashes = 0;
    }
    state->stable = 0;
}

bool boot_guard_safe(const boot_guard_state_t *state)
{
    return state != NULL && state->magic == BOOT_GUARD_MAGIC && state->crashes >= BOOT_GUARD_MAX_CRASHES;
}
