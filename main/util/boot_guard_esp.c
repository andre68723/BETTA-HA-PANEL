/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "util/boot_guard.h"
#include "util/log_tags.h"

static RTC_NOINIT_ATTR boot_guard_state_t s_state;
static bool s_safe_boot = false;
static bool s_safe_layout_pending = false;

static void boot_guard_stable_cb(void *arg)
{
    (void)arg;
    s_state.stable = 1;
    s_state.crashes = 0;
}

void boot_guard_init(void)
{
    const esp_reset_reason_t reason = esp_reset_reason();
    const bool power_on = (reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT);
    const bool crash = (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT ||
                        reason == ESP_RST_WDT);
    boot_guard_next(&s_state, power_on, crash);
    s_safe_boot = boot_guard_safe(&s_state);
    s_safe_layout_pending = s_safe_boot;
    if (s_safe_boot) {
        ESP_LOGE(TAG_APP, "Crash loop detected (%u crashes in a row): starting with the built-in layout, the stored layout is untouched",
            (unsigned)s_state.crashes);
    }
    static esp_timer_handle_t timer = NULL;
    const esp_timer_create_args_t args = {.callback = boot_guard_stable_cb, .name = "boot_guard"};
    if (esp_timer_create(&args, &timer) == ESP_OK) {
        (void)esp_timer_start_once(timer, (uint64_t)BOOT_GUARD_STABLE_S * 1000000ULL);
    }
}

bool boot_guard_take_safe_layout(void)
{
    const bool take = s_safe_layout_pending;
    s_safe_layout_pending = false;
    return take;
}

bool boot_guard_safe_mode(void)
{
    return s_safe_boot;
}
