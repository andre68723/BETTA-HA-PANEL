/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HA_HISTORY_BUCKETS 288
#define HA_HISTORY_EVENTS 512
#define HA_HISTORY_OBJECT_MAX 1024

typedef struct {
    int64_t epoch;
    char state[32];
} ha_history_event_t;

typedef struct {
    float min;
    float max;
    float sum;
    float last;    /* the newest value seen in this bucket */
    uint16_t count;
    uint8_t gap;   /* the bucket's newest event was not a number (unavailable, unknown): the value stops here */
} ha_history_bucket_t;

typedef struct {
    bool numeric;
    bool shortened;
    int64_t start_epoch;
    int64_t end_epoch;
    size_t event_count;
    size_t event_head;
    ha_history_event_t events[HA_HISTORY_EVENTS];
    ha_history_bucket_t buckets[HA_HISTORY_BUCKETS];
    size_t valid_count;
} ha_history_snapshot_t;

typedef struct {
    ha_history_snapshot_t *snapshot;
    char object[HA_HISTORY_OBJECT_MAX];
    size_t object_len;
    int depth;
    bool in_string;
    bool escaped;
    bool object_overflow;
} ha_history_reducer_t;

void ha_history_reduce_init(ha_history_reducer_t *reducer, ha_history_snapshot_t *snapshot,
                            bool numeric, int64_t start_epoch, int64_t end_epoch);
/* Call once after the last chunk of a numeric history. A sensor keeps its value until it changes, but a bucket
 * only holds the events inside it: every empty bucket after the first value takes the last known value, until
 * an unavailable/unknown event ends it. Without this a slow sensor draws as isolated fragments. */
void ha_history_reduce_finish(ha_history_snapshot_t *snapshot);
bool ha_history_reduce_feed(ha_history_reducer_t *reducer, const char *data, size_t len);
const ha_history_event_t *ha_history_event_at(const ha_history_snapshot_t *snapshot, size_t ordered_index);
