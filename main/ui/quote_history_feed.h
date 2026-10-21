/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Price histories for the Stocks & ETFs widget and its detail popup. Each chart or popup registers a slot and says which
 * quote and period it shows; the feed fetches one history at a time, only for slots on screen, refreshes them every
 * 15 minutes and asks again when an answer is lost (util/history_feed_policy.h, as the graph feed does). An open detail
 * popup goes first. Sources: Easy Stock's daily closes for 30D, YTD and 1Y (one fetch per symbol serves every period,
 * cached 15 minutes), otherwise Home Assistant: recorded states for 1D, hourly statistics for 1W, daily or weekly
 * statistics means for longer periods (also when Easy Stock's endpoint is missing). An answer goes to every slot that
 * shows the same quote and period. Everything runs on the LVGL task.
 */
#pragma once

#include <stdbool.h>

#include "lvgl.h"

#include "ha/ha_quote.h"
#include "ha/ha_quote_series.h"

typedef enum {
    QUOTE_FEED_LOADING = 0,
    QUOTE_FEED_READY,        /* series is valid */
    QUOTE_FEED_EMPTY,        /* the source answered without data for the period */
    QUOTE_FEED_FAILED,       /* no answer or an error; the feed asks again */
} quote_feed_status_t;

/* The series is only valid during the call. */
typedef void (*quote_history_feed_cb_t)(void *user, quote_feed_status_t status, const ha_quote_series_t *series);

/* `obj` decides visibility; priority slots (a detail popup) are fetched first. NULL when all slots are taken. */
void *quote_history_feed_register(lv_obj_t *obj, bool priority, quote_history_feed_cb_t cb, void *user);
/* What the slot shows. A change is fetched as soon as possible; the same quote and period keep the slot's answer.
 * price_hint is the current price (only the simulator's synthetic history uses it). */
void quote_history_feed_set(void *handle, const char *entity_id, ha_quote_provider_t provider, const char *symbol,
                            ha_quote_period_t period, double price_hint);
void quote_history_feed_unregister(void *handle);
