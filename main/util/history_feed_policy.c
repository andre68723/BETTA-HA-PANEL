/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/history_feed_policy.h"

void history_feed_state_init(history_feed_state_t *state)
{
    state->inflight = -1;
    state->inflight_since_ms = 0;
}

history_feed_decision_t history_feed_decide(const history_feed_slot_t *slots, int count, const history_feed_state_t *state,
                                            int64_t now_ms)
{
    history_feed_decision_t decision = {.timed_out = -1, .next = -1};
    if (state->inflight >= 0) {
        if (now_ms - state->inflight_since_ms < HISTORY_FEED_TIMEOUT_MS) {
            return decision; /* one request at a time */
        }
        decision.timed_out = state->inflight;
    }
    int64_t oldest_ok = 0;
    for (int i = 0; i < count; i++) {
        const history_feed_slot_t *slot = &slots[i];
        if (!slot->used || !slot->visible || i == decision.timed_out) {
            continue; /* the timed-out one waits for its retry pause */
        }
        const bool tried_recently = slot->last_try_ms != 0 && now_ms - slot->last_try_ms < HISTORY_FEED_RETRY_MS;
        if (tried_recently) {
            continue;
        }
        const bool due = slot->last_ok_ms == 0 || now_ms - slot->last_ok_ms >= HISTORY_FEED_REFRESH_MS;
        if (!due) {
            continue;
        }
        /* the one waiting longest first; never fetched (0) comes before everything */
        if (decision.next < 0 || slot->last_ok_ms < oldest_ok) {
            decision.next = i;
            oldest_ok = slot->last_ok_ms;
        }
    }
    return decision;
}
