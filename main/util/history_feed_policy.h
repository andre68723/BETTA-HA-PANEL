/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Scheduling of the Recorder histories that graph widgets show (ui/graph_history_feed.c). The History request queue
 * has one place and silently replaces a waiting request, and a popup may take it over, so a widget's answer can be
 * lost. The policy therefore keeps exactly one request outstanding, forgets one that stays unanswered, and picks the
 * next widget that is on screen and due: never fetched, or older than the refresh interval, with a pause after a
 * failure. Pure (host tested).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define HISTORY_FEED_MAX 8
#define HISTORY_FEED_REFRESH_MS (15LL * 60LL * 1000LL) /* a shown history is refreshed every 15 minutes */
#define HISTORY_FEED_RETRY_MS (30LL * 1000LL)          /* after a failed or unanswered request */
#define HISTORY_FEED_TIMEOUT_MS (45LL * 1000LL)        /* an unanswered request is forgotten after this */

typedef struct {
    bool used;
    bool visible;        /* the widget's page is on screen */
    int64_t last_ok_ms;  /* 0 = never fetched */
    int64_t last_try_ms; /* 0 = never tried */
} history_feed_slot_t;

typedef struct {
    int inflight;            /* slot index of the outstanding request, -1 for none */
    int64_t inflight_since_ms;
} history_feed_state_t;

typedef struct {
    int timed_out; /* slot whose request was just forgotten, -1 for none: the caller records the failed try */
    int next;      /* slot to request now, -1 for none */
} history_feed_decision_t;

void history_feed_state_init(history_feed_state_t *state);
/* One evaluation at `now_ms`. Does not change the slots: the caller applies timed_out and next. */
history_feed_decision_t history_feed_decide(const history_feed_slot_t *slots, int count, const history_feed_state_t *state,
                                            int64_t now_ms);
