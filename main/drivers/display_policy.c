/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "drivers/display_policy.h"

#include <stddef.h>

#include "settings/brightness_model.h"

#define DISPLAY_POLICY_TIMEOUT_MIN_S 5
#define DISPLAY_POLICY_TIMEOUT_MAX_S 3600

int display_policy_clamp_percent(int percent)
{
    if (percent < 0) {
        return 0;
    }
    return percent > 100 ? 100 : percent;
}

void display_policy_init(display_policy_t *policy, int active_percent, int dim_percent, uint32_t dim_timeout_ms)
{
    if (policy == NULL) {
        return;
    }
    policy->active_percent = active_percent;
    policy->dim_percent = dim_percent;
    policy->dim_timeout_ms = dim_timeout_ms;
    policy->ambient_limit_percent = 100;
    policy->dimmed = false;
}

void display_policy_configure(display_policy_t *policy, int active_percent, int dim_percent, int dim_timeout_seconds)
{
    if (policy == NULL) {
        return;
    }
    policy->active_percent = brightness_clamp_level(active_percent);
    policy->dim_percent = brightness_clamp_level(dim_percent);
    if (dim_timeout_seconds < DISPLAY_POLICY_TIMEOUT_MIN_S) {
        dim_timeout_seconds = DISPLAY_POLICY_TIMEOUT_MIN_S;
    }
    if (dim_timeout_seconds > DISPLAY_POLICY_TIMEOUT_MAX_S) {
        dim_timeout_seconds = DISPLAY_POLICY_TIMEOUT_MAX_S;
    }
    policy->dim_timeout_ms = (uint32_t)dim_timeout_seconds * 1000U;
}

void display_policy_set_ambient(display_policy_t *policy, int percent)
{
    if (policy != NULL) {
        policy->ambient_limit_percent = display_policy_clamp_percent(percent);
    }
}

int display_policy_wake_level(const display_policy_t *policy)
{
    return brightness_scale_for_room_light(policy->active_percent, policy->ambient_limit_percent);
}

int display_policy_dim_level(const display_policy_t *policy, const display_policy_env_t *env)
{
    if (env->night_active) {
        return env->night_percent;
    }
    return brightness_scale_for_room_light(policy->dim_percent, policy->ambient_limit_percent);
}

int display_policy_ambient_level(const display_policy_t *policy, const display_policy_env_t *env)
{
    if (env->night_active || env->protection_active) {
        return -1;
    }
    return brightness_scale_for_room_light(policy->dimmed ? policy->dim_percent : policy->active_percent,
        policy->ambient_limit_percent);
}

bool display_policy_is_dimmed(const display_policy_t *policy, const display_policy_env_t *env, int current_percent)
{
    return env->protection_active || policy->dimmed || (env->night_active && current_percent <= env->night_percent);
}

int display_test_remaining_seconds(const display_test_t *test, uint32_t now_ms)
{
    if (!test->active) return 0;
    const uint32_t elapsed = now_ms - test->started_ms;
    if (elapsed >= DISPLAY_TEST_DURATION_MS) return 0;
    return (int)((DISPLAY_TEST_DURATION_MS - elapsed + 999U) / 1000U);
}

void display_test_start(display_test_t *test, uint32_t now_ms, int percent)
{
    test->started_ms = now_ms;
    test->percent = display_policy_clamp_percent(percent);
    test->active = true;
}

bool display_test_update(display_test_t *test, uint32_t now_ms, int percent)
{
    if (!display_test_remaining_seconds(test, now_ms)) return false;
    test->percent = display_policy_clamp_percent(percent);
    return true;
}

void display_test_stop(display_test_t *test)
{
    test->active = false;
}
