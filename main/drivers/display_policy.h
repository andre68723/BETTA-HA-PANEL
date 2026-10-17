/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Backlight policy of the display: which brightness the panel shows when it is awake, dimmed, in night mode, under
 * the room-light limit or covered by the screen protection. Pure functions on a small state (no hardware, no
 * FreeRTOS), shared by every board through display_brightness.c and host-tested (tests/host).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int active_percent;         /* configured level while in use, 1..100 */
    int dim_percent;            /* configured level after the dim timeout, 1..100 */
    uint32_t dim_timeout_ms;    /* inactivity until the display dims */
    int ambient_limit_percent;  /* room-light factor, 10..100 (100 = no limit; boards without a camera stay there) */
    bool dimmed;                /* the dim timer fired and no activity has happened since */
} display_policy_t;

/* What the policy needs to know about the rest of the device at the moment of a decision. */
typedef struct {
    bool protection_active;     /* screen protection (weather, page cycling, off) owns the backlight */
    bool night_active;          /* night mode is in effect */
    int night_percent;          /* its backlight, 0..100 (0 = off) */
} display_policy_env_t;

void display_policy_init(display_policy_t *policy, int active_percent, int dim_percent, uint32_t dim_timeout_ms);
/* New configuration from the settings: levels clamped to 1..100, timeout to 5..3600 seconds. */
void display_policy_configure(display_policy_t *policy, int active_percent, int dim_percent, int dim_timeout_seconds);
/* 0..100; a camera-less board never calls it. */
void display_policy_set_ambient(display_policy_t *policy, int percent);
int display_policy_clamp_percent(int percent);

/* Backlight for an awake display: the active level under the room-light limit. */
int display_policy_wake_level(const display_policy_t *policy);
/* Backlight when the dim timer fires: the night level in night mode, else the dim level under the room-light limit. */
int display_policy_dim_level(const display_policy_t *policy, const display_policy_env_t *env);
/* Backlight to show after the room light changed, or -1 to leave it alone (night mode and the screen protection set
 * their own level). */
int display_policy_ambient_level(const display_policy_t *policy, const display_policy_env_t *env);
/* True while the display waits for someone to wake it: dimmed, covered by the protection, or at the night level. */
bool display_policy_is_dimmed(const display_policy_t *policy, const display_policy_env_t *env, int current_percent);

/* A fixed backlight preview is temporary; edits never extend its deadline. */
#define DISPLAY_TEST_DURATION_MS 30000U
typedef struct {
    uint32_t started_ms;
    int percent;
    bool active;
} display_test_t;
void display_test_start(display_test_t *test, uint32_t now_ms, int percent);
bool display_test_update(display_test_t *test, uint32_t now_ms, int percent);
int display_test_remaining_seconds(const display_test_t *test, uint32_t now_ms);
void display_test_stop(display_test_t *test);
