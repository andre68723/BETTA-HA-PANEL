/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "settings/night_mode.h"

#include <time.h>

#include "drivers/display_init.h"
#include "esp_timer.h"

static bool s_enabled = false;
static int s_start_min = 22 * 60;
static int s_end_min = 6 * 60;
static int s_brightness_percent = 0;
static bool s_last_active = false;
static esp_timer_handle_t s_timer = NULL;

static bool night_now(void)
{
    if (!s_enabled || s_start_min == s_end_min) {
        return false;
    }
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    if (tm_now.tm_year < 124) { /* clock not set yet (before 2024) */
        return false;
    }
    const int minutes = tm_now.tm_hour * 60 + tm_now.tm_min;
    if (s_start_min < s_end_min) {
        return minutes >= s_start_min && minutes < s_end_min;
    }
    return minutes >= s_start_min || minutes < s_end_min; /* crosses midnight */
}

int night_mode_brightness_percent(void)
{
    return s_brightness_percent;
}

bool night_mode_active(void)
{
    return night_now();
}

static void night_mode_tick(void *arg)
{
    (void)arg;
    const bool active = night_now();
    if (active == s_last_active) {
        return;
    }
    s_last_active = active;
    if (active) {
        (void)display_set_brightness_percent(s_brightness_percent);
    } else {
        display_note_activity(); /* back to the normal brightness, restarts the dim timer */
    }
}

void night_mode_configure(bool enabled, int start_minutes, int end_minutes, int brightness_percent)
{
    s_enabled = enabled;
    s_brightness_percent = brightness_percent < 0 ? 0 : (brightness_percent > 100 ? 100 : brightness_percent);
    s_start_min = ((start_minutes % 1440) + 1440) % 1440;
    s_end_min = ((end_minutes % 1440) + 1440) % 1440;
    if (s_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = night_mode_tick,
            .name = "night_mode",
            .dispatch_method = ESP_TIMER_TASK,
            .skip_unhandled_events = true,
        };
        if (esp_timer_create(&args, &s_timer) == ESP_OK) {
            (void)esp_timer_start_periodic(s_timer, 15ULL * 1000ULL * 1000ULL);
        }
    }
    s_last_active = false;
    night_mode_tick(NULL);
}
