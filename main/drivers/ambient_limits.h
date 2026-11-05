/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723
 */
#pragma once
#include <stdint.h>

/* A repeat request retains the device-owned deadline. */
static inline int64_t ambient_test_deadline(int64_t current, int64_t now, uint32_t duration_ms)
{
    if (current > now) return current;
    if (duration_ms > 30000) duration_ms = 30000;
    return now + duration_ms;
}

static inline int64_t ambient_capture_deadline(int64_t now, int64_t test_end, int test_only)
{
    int64_t end = now + 2000;
    return test_only && test_end < end ? test_end : end;
}
