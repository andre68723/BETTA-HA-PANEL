/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include <assert.h>
#include <stdio.h>
#include "ui/graph_history_merge.h"

static ha_history_snapshot_t snapshot;
static graph_history_sample_t samples[4096];
static const uint32_t start = 1791158400U;

static void recorder(uint32_t offset)
{
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.numeric = true;
    snapshot.start_epoch = start + offset;
    snapshot.end_epoch = snapshot.start_epoch + 86400;
    for (size_t i = 0; i < HA_HISTORY_BUCKETS; i++) {
        snapshot.buckets[i].count = 1;
        snapshot.buckets[i].sum = 18.0f + (float)i / 100;
    }
}

int main(void)
{
    uint32_t first_live = start + 86400;
    samples[0] = (graph_history_sample_t){first_live, 23.0f};
    recorder(0);
    graph_history_merge_result_t r = graph_history_merge(samples, 1, 4096, &first_live, &snapshot, start);
    assert(r.applied && r.recorder_count == 288 && r.live_count == 1);
    size_t count = r.recorder_count + r.live_count;
    /* The old callback treated all 289 samples as live on this refresh,
     * discarded all Recorder buckets, and kept only the newest 256. */
    recorder(900);
    r = graph_history_merge(samples, count, 4096, &first_live, &snapshot, start);
    assert(r.applied && r.recorder_count == 285 && r.live_count == 1);
    assert(samples[0].bucket_ts == start + 1050);
    assert(samples[r.recorder_count].value == 23.0f);
    count = r.recorder_count + r.live_count;
    recorder(1800);
    r = graph_history_merge(samples, count, 4096, &first_live, &snapshot, start);
    assert(r.recorder_count == 282 && r.live_count == 1);
    count = r.recorder_count + r.live_count;

    /* Retaining only 256 real live samples moves the cutoff forward;
     * the refreshed Recorder fills the newly freed older interval. */
    for (unsigned i = 1; i <= 300; i++) {
        samples[count++] = (graph_history_sample_t){start + 86400 + i * 2, 30.0f + i};
    }
    r = graph_history_merge(samples, count, 4096, &first_live, &snapshot, start);
    assert(r.live_count == 256 && r.recorder_count > 280);
    assert(first_live == start + 86400 + 45 * 2);
    count = r.recorder_count + r.live_count;
    graph_history_sample_t oldest = samples[0];
    r = graph_history_merge(samples, count, 4096, &first_live, &snapshot, start);
    assert(r.live_count == 256 && r.recorder_count > 280);
    assert(samples[0].bucket_ts == oldest.bucket_ts && samples[0].value == oldest.value);
    for (size_t i = 1; i < r.recorder_count + r.live_count; i++) {
        assert(samples[i].bucket_ts > samples[i-1].bucket_ts);
    }

    /* Empty refresh keeps existing history intact. */
    count = r.recorder_count + r.live_count;
    memset(snapshot.buckets, 0, sizeof(snapshot.buckets));
    r = graph_history_merge(samples, count, 4096, &first_live, &snapshot, start);
    assert(!r.applied && samples[0].bucket_ts == oldest.bucket_ts);
    /* No live samples: repeated imports must stay Recorder-only. */
    first_live = 0;
    recorder(0);
    r = graph_history_merge(samples, 0, 4096, &first_live, &snapshot, start);
    assert(r.applied && r.recorder_count == 288 && r.live_count == 0);
    r = graph_history_merge(samples, 288, 4096, &first_live, &snapshot, start);
    assert(r.applied && r.recorder_count == 288 && r.live_count == 0 && first_live == 0);
    puts("graph history merge: PASS (repeat refresh, real live cutoff, cap, ordering, empty refresh)");
}
