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

#include "ha/ha_quote.h"
#include "ha/ha_quote_series.h"

/* A quote of the Stocks & ETFs widget: price, the last session's change, market state and times, and its history over
 * 1D, 1W, 30D, YTD or 1Y (period is the first), from ui/quote_history_feed.c with the source named. */
void ui_history_popup_open_quote(const char *entity_id, ha_quote_provider_t provider, const ha_quote_t *quote,
                                 ha_quote_period_t period);
/* Selects a period of the open quote popup, as a tap on its button does (device tests without touch). */
void ui_history_popup_quote_select(ha_quote_period_t period);

#ifdef BETTA_SIMULATOR
#include "ha/ha_history_stats.h"
/* Inject a parsed Recorder response into the open range popup for simulator regressions. */
void ui_history_popup_simulator_set_range(const ha_range_series_t *series);
#endif

/* Daily totals of cumulative counters (Water, Gas or Solar): seven bars for the last seven local days, from HA
 * statistics. ids are statistic IDs (up to four, summed); production selects solar-yield wording. */
void ui_history_popup_open_days(const char *title, uint32_t bar_color, const char *const *ids, size_t count, bool production);

/* evcc completed charging sessions, using the same activity popup frame. */
void ui_history_popup_open_evcc(const char *mode_entity);
void ui_history_popup_close_evcc(const char *mode_entity);
