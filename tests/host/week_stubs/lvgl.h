/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Host stub for the worker test (tests/host/test_history_week.c): pthread-backed FreeRTOS primitives, a counting heap and
 * a synchronous LVGL async call, so the real main/ha/ha_history_week.c runs on the host.
 */
#pragma once
typedef int lv_result_t;
#define LV_RESULT_OK 0
typedef void (*lv_async_cb_t)(void *user);
/* The firmware queues the call for the LVGL task; here it runs at once on the worker's thread. */
static inline lv_result_t lv_async_call(lv_async_cb_t cb, void *user)
{
    cb(user);
    return LV_RESULT_OK;
}
