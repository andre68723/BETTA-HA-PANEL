/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "drivers/orientation_policy.h"
#include <math.h>

bool orientation_rotation_valid(int degrees)
{
    return degrees == 0 || degrees == 90 || degrees == 180 || degrees == 270;
}

int orientation_boot_fallback(bool enabled, int saved)
{
    return enabled && orientation_rotation_valid(saved) ? saved : 90;
}

static int orientation_from_sample(float x, float y, float z)
{
    const float ax = fabsf(x), ay = fabsf(y);
    const float norm = x*x + y*y + z*z;
    int candidate = -1;
    if (isfinite(norm) && norm >= 0.65f*0.65f && norm <= 1.35f*1.35f) {
        if (ax >= 0.65f && ax >= ay + 0.20f && ax >= fabsf(z) + 0.10f)
            candidate = x > 0 ? 90 : 270;
        else if (ay >= 0.65f && ay >= ax + 0.20f && ay >= fabsf(z) + 0.10f)
            candidate = y > 0 ? 0 : 180;
    }
    return candidate;
}

bool orientation_boot_update(orientation_boot_policy_t *p, float x, float y, float z, int *requested)
{
    if (!p || !requested) return false;
    const int candidate = orientation_from_sample(x, y, z);
    if (candidate < 0) { p->candidate = -1; p->samples = 0; return false; }
    if (candidate != p->candidate) { p->candidate = candidate; p->samples = 1; return false; }
    if (p->samples < 3) p->samples++;
    if (p->samples < 3) return false;
    *requested = candidate;
    return true;
}

bool orientation_memory_should_save(orientation_memory_policy_t *p, bool enabled, int current,
                                    int saved, uint64_t now)
{
    if (!p) return false;
    if (!enabled || !orientation_rotation_valid(current) || current == saved) {
        p->candidate = -1; p->since_ms = now; return false;
    }
    if (p->candidate != current || now < p->since_ms) {
        p->candidate = current; p->since_ms = now; return false;
    }
    return now - p->since_ms >= 5000 && now >= p->retry_after_ms;
}

void orientation_memory_save_result(orientation_memory_policy_t *p, uint64_t now, bool success)
{
    if (p) p->retry_after_ms = success ? 0 : now + 30000;
}

bool orientation_policy_update(orientation_policy_t *p, float x, float y, float z,
                               uint64_t now, int current, int *requested)
{
    if (!p || !requested) return false;
    const int candidate = orientation_from_sample(x, y, z);
    if (candidate < 0 || candidate == current) {
        p->candidate = -1;
        p->since_ms = now;
        return false;
    }
    if (candidate != p->candidate || now < p->since_ms) {
        p->candidate = candidate;
        p->since_ms = now;
        return false;
    }
    if (now - p->since_ms < 1000) return false;
    *requested = candidate;
    return true;
}
