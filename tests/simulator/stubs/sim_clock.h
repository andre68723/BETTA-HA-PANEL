/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

/* Force-included into every simulator source (tests/simulator/CMakeLists.txt). With BETTA_SIM_NOW set (Unix seconds)
 * the simulator runs on a fixed clock, so a render is the same on every run:
 * - time() returns BETTA_SIM_NOW: dates, ages, forecast days and price windows;
 * - LVGL's tick and esp_timer_get_time() follow a virtual time that advances only by the simulator's own delays, so
 *   animations and timeouts reach the same point however busy the machine is.
 * Without it, all three are the real clocks. */
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

time_t sim_time(time_t *out);
bool sim_clock_fixed(void);
void sim_clock_set(time_t now); /* test scenarios: 0 restores the real clock; elapsed ticks stay monotonic */
uint32_t sim_virtual_tick(void);
void sim_virtual_delay(uint32_t ms);
int64_t sim_monotonic_us(void);
#define time(out) sim_time(out)
