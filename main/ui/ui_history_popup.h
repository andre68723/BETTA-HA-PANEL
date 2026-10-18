/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Open on the UI task. The worker fetches Recorder history and delivers a reduced snapshot. */
void ui_history_popup_open(const char *entity_id, const char *title, const char *current,
                           bool numeric, const char *device_class);

/* The last 30 days (daily means) or 12 months (weekly means) of a quote or any other measurement, from the Recorder statistics; a selector
 * switches between them. current is the value text with its unit ("199.22 EUR"), shown with the range's minimum and maximum. */
void ui_history_popup_open_range(const char *entity_id, const char *title, const char *current);

/* Selects the 30-day or the 12-month view of the open range popup, as a tap on its button does (device tests without touch). */
void ui_history_popup_range_select(bool year);

#ifdef BETTA_SIMULATOR
#include "ha/ha_history_stats.h"
/* Inject a parsed Recorder response into the open range popup for simulator regressions. */
void ui_history_popup_simulator_set_range(const ha_range_series_t *series);
#endif

/* Daily consumption of cumulative counters (the Water or Gas source of the Energy page): seven bars for the last
 * seven local days, from Home Assistant statistics. ids are statistic IDs (up to four, summed); bar_color is 0xRRGGBB. */
void ui_history_popup_open_days(const char *title, uint32_t bar_color, const char *const *ids, size_t count);

/* evcc completed charging sessions, using the same activity popup frame. */
void ui_history_popup_open_evcc(const char *mode_entity);
void ui_history_popup_close_evcc(const char *mode_entity);
