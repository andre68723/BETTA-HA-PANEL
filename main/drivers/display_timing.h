/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"

#if CONFIG_APP_PANEL_VARIANT_10INCH_JC && !CONFIG_APP_PANELJC_PANEL_V2
#include "cJSON.h"
#include "lvgl.h"
#include "drivers/display_timing_state.h"
bool display_timing_lock(uint32_t timeout_ms, uintptr_t caller);
void display_timing_unlock(void);
void display_timing_attach(lv_display_t *display);
void display_timing_measure(display_time_kind_t kind, int64_t start_us, bool error);
void display_timing_transfer_begin(void);
void display_timing_transfer_failed(void);
void display_timing_transfer_done_isr(void);
void display_timing_json(cJSON *root);
void display_timing_summary(void);
#endif
