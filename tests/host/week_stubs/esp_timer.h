/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Host stub for the worker test (tests/host/test_history_week.c): pthread-backed FreeRTOS primitives, a counting heap and
 * a synchronous LVGL async call, so the real main/ha/ha_history_week.c runs on the host.
 */
#pragma once
#include <stdint.h>
#include <time.h>
static inline int64_t esp_timer_get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
