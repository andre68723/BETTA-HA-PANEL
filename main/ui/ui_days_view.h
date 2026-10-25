/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Seven daily-consumption bars (Water, Gas, Solar of the Energy page), shared by the history popup and the Energy
 * page's right-hand detail view. The caller owns the body container and the status label.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#include "ha/ha_history_week.h"

typedef struct {
    lv_obj_t *body;     /* container the bars are drawn into; cleaned on every update */
    lv_obj_t *status;   /* one line of text above it: the title, a hint or an error */
    uint32_t color;     /* 0xRRGGBB of the bars */
    bool production;   /* solar yield; otherwise cumulative consumption */
} ui_days_view_t;

/* Replaces the content of the view with the answer: bars, or the message that fits the outcome (error, no statistics
 * for the source, nothing recorded in the last seven days). */
void ui_days_view_show(const ui_days_view_t *view, const ha_history_days_t *result);

/* The first two characters (not bytes) of a weekday name in the panel language: "Mo", "Tu". */
void ui_days_weekday_short(int wday, char *out, size_t out_len);

#ifdef BETTA_SIMULATOR
/* A synthetic week for screenshots and regressions: a gap on the third day and a lighter, incomplete today. The
 * caller frees it with free(). */
ha_history_days_t *ui_days_view_simulated(bool cubic_metres);
#endif
