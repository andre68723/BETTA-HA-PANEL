/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/user_activity.h"

#include <stdbool.h>

static uint32_t s_last_s;
static bool s_seen;

void user_activity_note(uint32_t now_s)
{
    __atomic_store_n(&s_last_s, now_s, __ATOMIC_RELAXED);
    __atomic_store_n(&s_seen, true, __ATOMIC_RELAXED);
}

int64_t user_activity_idle_s(uint32_t now_s)
{
    if (!__atomic_load_n(&s_seen, __ATOMIC_RELAXED)) {
        return (int64_t)now_s;
    }
    const uint32_t last = __atomic_load_n(&s_last_s, __ATOMIC_RELAXED);
    return now_s >= last ? (int64_t)(now_s - last) : 0;
}
