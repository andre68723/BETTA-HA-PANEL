/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <math.h>
#include <string.h>
#include "ha/ha_history_reduce.h"

#define GRAPH_HISTORY_KEEP_LIVE 256U

typedef struct {
    uint32_t bucket_ts;
    float value;
} graph_history_sample_t;

typedef struct {
    size_t recorder_count;
    size_t live_count;
    bool applied;
} graph_history_merge_result_t;

/* first_live_ts separates actual state updates from previously imported buckets.
 * Move it to the oldest retained live sample after each refresh, so imported
 * Recorder samples cannot become live samples on the following refresh.
 * Pure/bounded: shared by the widget and the host regression test. */
static inline graph_history_merge_result_t graph_history_merge(graph_history_sample_t *samples, size_t count,
    size_t capacity, uint32_t *first_live_ts, const ha_history_snapshot_t *snapshot, uint32_t min_epoch)
{
    graph_history_merge_result_t result = {0};
    if (samples == NULL || first_live_ts == NULL || snapshot == NULL || !snapshot->numeric ||
        snapshot->end_epoch <= snapshot->start_epoch || count > capacity || capacity == 0) return result;

    bool usable = false;
    for (size_t i = 0; i < HA_HISTORY_BUCKETS; i++) {
        const ha_history_bucket_t *b = &snapshot->buckets[i];
        if (b->count > 0 && isfinite(b->sum / (float)b->count)) usable = true;
    }
    if (!usable) return result;

    size_t first = count;
    if (*first_live_ts != 0) {
        first = 0;
        while (first < count && samples[first].bucket_ts < *first_live_ts) first++;
    }
    size_t live_count = count - first;
    if (live_count > GRAPH_HISTORY_KEEP_LIVE) live_count = GRAPH_HISTORY_KEEP_LIVE;
    graph_history_sample_t live[GRAPH_HISTORY_KEEP_LIVE];
    memcpy(live, samples + count - live_count, live_count * sizeof(*live));
    uint32_t cutoff = live_count > 0 ? live[0].bucket_ts : UINT32_MAX;
    const int64_t span = snapshot->end_epoch - snapshot->start_epoch;
    size_t recorder_count = 0;
    for (size_t i = 0; i < HA_HISTORY_BUCKETS && recorder_count < capacity - live_count; i++) {
        const ha_history_bucket_t *b = &snapshot->buckets[i];
        if (b->count == 0) continue;
        const float value = b->sum / (float)b->count;
        const int64_t ts = snapshot->start_epoch + span * (int64_t)(2U * i + 1U) / (2 * HA_HISTORY_BUCKETS);
        if (!isfinite(value) || ts < min_epoch || ts >= (int64_t)cutoff) continue;
        samples[recorder_count++] = (graph_history_sample_t){.bucket_ts = (uint32_t)ts, .value = value};
    }
    memcpy(samples + recorder_count, live, live_count * sizeof(*live));
    *first_live_ts = live_count > 0 ? live[0].bucket_ts : 0;
    result.recorder_count = recorder_count;
    result.live_count = live_count;
    result.applied = true;
    return result;
}
