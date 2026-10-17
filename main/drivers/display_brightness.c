/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Brightness behaviour shared by every board: the configured levels, the dim timer, the wake on activity, the
 * room-light limit and night mode. The decisions are in display_policy.c (host-tested); this file holds the state,
 * the esp_timer and the public API of display_init.h, and talks to the hardware through display_backlight_write().
 * Before this file the same ~75 lines were copied into the panel4, panel10, Tab5 and paneljc board files.
 */
#include "drivers/display_backlight.h"
#include "drivers/display_init.h"
#include "drivers/display_policy.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "app_config.h"
#include "settings/night_mode.h"
#include "ui/ui_display_protection.h"
#include "util/log_tags.h"

static display_policy_t s_policy = {
    .active_percent = APP_DISPLAY_ACTIVE_BRIGHTNESS_PERCENT,
    .dim_percent = APP_DISPLAY_DIM_BRIGHTNESS_PERCENT,
    .dim_timeout_ms = APP_DISPLAY_DIM_TIMEOUT_MS,
    .ambient_limit_percent = 100,
    .dimmed = false,
};
static int s_backlight_percent = -1; /* last value written; -1 before the first write */
static esp_timer_handle_t s_dim_timer = NULL;

__attribute__((weak)) bool display_backlight_diagnostics(display_backlight_diagnostics_t *out)
{
    (void)out;
    return false;
}

static display_policy_env_t display_env(void)
{
    const bool night = night_mode_active();
    const display_policy_env_t env = {
        .protection_active = ui_display_protection_is_active(),
        .night_active = night,
        .night_percent = night ? night_mode_brightness_percent() : 0,
    };
    return env;
}

esp_err_t display_set_brightness_percent(int percent)
{
    /* Night timer, camera and settings updates cannot interrupt a deliberate preview. */
    const int test_percent = ui_display_brightness_test_percent();
    if (test_percent >= 0) percent = test_percent;
    const int next = display_policy_clamp_percent(percent);
    if (s_backlight_percent == next) {
        return ESP_OK;
    }
    const esp_err_t err = display_backlight_write(next);
    if (err == ESP_OK) {
        s_backlight_percent = next;
    }
    return err;
}

static void display_restart_dim_timer(void)
{
    if (s_dim_timer == NULL) {
        return;
    }
    if (esp_timer_is_active(s_dim_timer)) {
        (void)esp_timer_stop(s_dim_timer);
    }
    const esp_err_t err = esp_timer_start_once(s_dim_timer, (uint64_t)s_policy.dim_timeout_ms * 1000ULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_DISPLAY, "Could not start display dim timer: %s", esp_err_to_name(err));
    }
}

void display_configure_brightness(int active_brightness_percent, int dim_brightness_percent, int dim_timeout_seconds)
{
    display_policy_configure(&s_policy, active_brightness_percent, dim_brightness_percent, dim_timeout_seconds);
    if (display_is_ready() && !ui_display_protection_is_active()) {
        (void)display_set_brightness_percent(display_policy_wake_level(&s_policy));
        display_restart_dim_timer();
    }
}

static void display_dim_timer_cb(void *arg)
{
    (void)arg;
    if (!display_is_ready() || ui_display_protection_is_active()) {
        return;
    }
    s_policy.dimmed = true;
    const display_policy_env_t env = display_env();
    (void)display_set_brightness_percent(display_policy_dim_level(&s_policy, &env));
}

esp_err_t display_brightness_start(void)
{
    if (s_dim_timer != NULL) {
        return ESP_OK;
    }
    const esp_timer_create_args_t timer_args = {
        .callback = display_dim_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "display_dim",
        .skip_unhandled_events = true,
    };
    const esp_err_t err = esp_timer_create(&timer_args, &s_dim_timer);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_DISPLAY, "Could not create display dim timer: %s", esp_err_to_name(err));
    }
    return err;
}

void display_note_activity(void)
{
    if (ui_display_protection_note_activity()) {
        return;
    }
    if (!display_is_ready()) {
        return;
    }
    s_policy.dimmed = false;
    (void)display_set_brightness_percent(display_policy_wake_level(&s_policy));
    display_restart_dim_timer();
}

void display_set_ambient_limit_percent(int percent)
{
    display_policy_set_ambient(&s_policy, percent);
    if (!display_is_ready()) {
        return;
    }
    const display_policy_env_t env = display_env();
    const int level = display_policy_ambient_level(&s_policy, &env);
    if (level >= 0) {
        (void)display_set_brightness_percent(level);
    }
}

int display_get_brightness_percent(void)
{
    return s_backlight_percent;
}

int display_get_ambient_limit_percent(void)
{
    return s_policy.ambient_limit_percent;
}

bool display_is_dimmed(void)
{
    /* Camera presence wake must not end a test; a real touch still reaches its overlay. */
    if (ui_display_brightness_test_percent() >= 0) return false;
    const display_policy_env_t env = display_env();
    return display_policy_is_dimmed(&s_policy, &env, s_backlight_percent);
}
