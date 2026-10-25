/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * UI stall watchdog. The task watchdog only sees tasks that starve the idle task; a UI task that is *blocked* (waiting for a lock or
 * a driver that never answers) goes unnoticed. On the paneljc (2026-10-04) the screen froze at 13:41 and stayed frozen for hours
 * while Wi-Fi, Home Assistant and the web server kept working: the display lock was never released (a screenshot answered "could not
 * lock display"), the UI event queue stayed full and touch was dead. Two heartbeats prove the UI lives: a timer inside the LVGL task
 * and the loop of the ui_runtime task. When either is silent for UI_WD_STALL_S the panel writes a core dump of every task (which
 * shows who holds the lock) and restarts. Bounded like the network watchdog: never within UI_WD_BOOT_GRACE_S of boot and at most
 * UI_WD_MAX_RESTARTS in a row (cleared once the UI has been alive for UI_WD_CLEAR_AFTER_S).
 * Pure (host tested); util/ui_watchdog_esp.c gathers the heartbeats and restarts.
 */
#pragma once

#include <stdint.h>

#define UI_WD_BOOT_GRACE_S 120
#define UI_WD_STALL_S 90
#define UI_WD_CLEAR_AFTER_S 600
#define UI_WD_MAX_RESTARTS 3

typedef struct {
    int64_t alive_since; /* uptime seconds the UI has been alive without a break, -1 while it is stalled or still starting */
} ui_wd_state_t;

typedef struct {
    int64_t lvgl_age_s;    /* seconds since the LVGL timer last ran, -1 before its first beat */
    int64_t runtime_age_s; /* seconds since the ui_runtime loop last ran, -1 before its first beat */
    uint32_t restarts_in_a_row;
} ui_wd_input_t;

typedef enum {
    UI_WD_OK = 0,
    UI_WD_RESTART_LVGL,    /* the LVGL task stopped running its timers */
    UI_WD_RESTART_RUNTIME, /* the ui_runtime loop stopped (the event queue fills up) */
    UI_WD_SUPPRESSED,      /* a restart is due but the limit of restarts in a row is reached */
} ui_wd_decision_t;

void ui_wd_init(ui_wd_state_t *state);
/* One evaluation at `now_s` (seconds since boot). */
ui_wd_decision_t ui_wd_step(ui_wd_state_t *state, const ui_wd_input_t *in, int64_t now_s);
/* The 32-bit beat stamp for an uptime in microseconds: milliseconds plus one, never 0 because 0 means "no beat yet". The sum
 * would wrap to 0 once every 49.7 days (at 0xFFFFFFFF ms); that millisecond maps to 1 instead. */
uint32_t ui_wd_stamp_ms(int64_t uptime_us);
/* True once the UI has been alive for UI_WD_CLEAR_AFTER_S: the counter of restarts in a row may be cleared. */
int ui_wd_may_clear_restarts(const ui_wd_state_t *state, int64_t now_s);
