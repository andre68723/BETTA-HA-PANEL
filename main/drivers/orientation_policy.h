/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int candidate;
    uint64_t since_ms;
} orientation_policy_t;

typedef struct { int candidate; unsigned samples; } orientation_boot_policy_t;
typedef struct { int candidate; uint64_t since_ms, retry_after_ms; } orientation_memory_policy_t;

bool orientation_rotation_valid(int degrees);
int orientation_boot_fallback(bool enabled, int saved);
/* Startup uses three consecutive clear sensor readings; normal runtime still requires 1 s. */
bool orientation_boot_update(orientation_boot_policy_t *policy, float x, float y, float z, int *requested);
bool orientation_memory_should_save(orientation_memory_policy_t *policy, bool enabled, int current,
                                    int saved, uint64_t now_ms);
void orientation_memory_save_result(orientation_memory_policy_t *policy, uint64_t now_ms, bool success);

/* Native portrait axes, in g. Ambiguous tilt, flat placement and acceleration
 * retain the current orientation. A clear, stable position must last 1 s. */
bool orientation_policy_update(orientation_policy_t *policy, float x, float y, float z,
                               uint64_t now_ms, int current, int *requested);
