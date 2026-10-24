/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "sim_clock.h"

#include <stdlib.h>

#undef time

static int s_fixed = -1;
static time_t s_now;
static uint32_t s_virtual_ms;

void sim_clock_set(time_t now)
{
    s_fixed = now > 0;
    s_now = now;
}

bool sim_clock_fixed(void)
{
    if (s_fixed < 0) {
        const char *value = getenv("BETTA_SIM_NOW");
        char *end = NULL;
        const long long seconds = value != NULL ? strtoll(value, &end, 10) : 0;
        s_fixed = value != NULL && end != value && *end == '\0' && seconds > 0;
        s_now = (time_t)seconds;
    }
    return s_fixed;
}

time_t sim_time(time_t *out)
{
    const time_t result = sim_clock_fixed() ? s_now : time(NULL);
    if (out != NULL) *out = result;
    return result;
}

uint32_t sim_virtual_tick(void)
{
    return s_virtual_ms;
}

void sim_virtual_delay(uint32_t ms)
{
    s_virtual_ms += ms;
}

int64_t sim_monotonic_us(void)
{
    if (sim_clock_fixed()) return 1000000000LL + (int64_t)s_virtual_ms * 1000;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}
