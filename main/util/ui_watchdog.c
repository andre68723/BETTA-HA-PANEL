/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/ui_watchdog.h"

void ui_wd_init(ui_wd_state_t *state)
{
    state->alive_since = -1;
}

uint32_t ui_wd_stamp_ms(int64_t uptime_us)
{
    const uint32_t stamp = (uint32_t)(uptime_us / 1000) + 1U;
    return stamp == 0U ? 1U : stamp;
}

ui_wd_decision_t ui_wd_step(ui_wd_state_t *state, const ui_wd_input_t *in, int64_t now_s)
{
    if (now_s < UI_WD_BOOT_GRACE_S) {
        return UI_WD_OK;
    }
    const int lvgl_stalled = in->lvgl_age_s >= UI_WD_STALL_S;
    const int runtime_stalled = in->runtime_age_s >= UI_WD_STALL_S;
    if (lvgl_stalled || runtime_stalled) {
        state->alive_since = -1;
        if (in->restarts_in_a_row >= UI_WD_MAX_RESTARTS) {
            return UI_WD_SUPPRESSED;
        }
        return lvgl_stalled ? UI_WD_RESTART_LVGL : UI_WD_RESTART_RUNTIME;
    }
    if (state->alive_since < 0 && in->lvgl_age_s >= 0 && in->runtime_age_s >= 0) {
        state->alive_since = now_s;
    }
    return UI_WD_OK;
}

int ui_wd_may_clear_restarts(const ui_wd_state_t *state, int64_t now_s)
{
    return state->alive_since >= 0 && now_s - state->alive_since >= UI_WD_CLEAR_AFTER_S;
}
