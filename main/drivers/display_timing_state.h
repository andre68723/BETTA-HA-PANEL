/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DISPLAY_TIME_LOCK_WAIT, DISPLAY_TIME_LOCK_HOLD, DISPLAY_TIME_REFRESH,
    DISPLAY_TIME_ROTATION, DISPLAY_TIME_FLUSH, DISPLAY_TIME_SUBMIT,
    DISPLAY_TIME_TRANSFER, DISPLAY_TIME_COUNT
} display_time_kind_t;

#define DISPLAY_TIMING_EVENTS 16
#define DISPLAY_TIMING_SLOW_US 100000
typedef struct {
    uint64_t count, total_us, maximum_us, slow, errors;
    uintptr_t maximum_caller;
} display_time_metric_t;
typedef struct {
    int64_t at_us;
    uint64_t duration_us;
    uintptr_t caller;
    uintptr_t owner_caller;
    char owner_name[16];
    uint8_t kind;
    bool error, owner_known, refreshing;
} display_time_event_t;
typedef struct {
    display_time_metric_t metric[DISPLAY_TIME_COUNT];
    display_time_event_t event[DISPLAY_TIMING_EVENTS];
    uint64_t events;
    uintptr_t owner_task, owner_caller;
    uint32_t depth;
    char owner_name[16];
    int64_t hold_start_us, refresh_start_us, transfer_start_us;
    bool refreshing, transfer_pending;
} display_timing_state_t;

/* Also used by the DMA completion ISR: fixed storage, no division/allocation. */
static inline __attribute__((always_inline)) void display_timing_record(
    display_timing_state_t *s, display_time_kind_t kind, int64_t now, int64_t duration,
    bool error, uintptr_t caller)
{
    if (duration < 0) duration = 0;
    display_time_metric_t *m = &s->metric[kind];
    ++m->count;
    m->total_us += (uint64_t)duration;
    if ((uint64_t)duration > m->maximum_us) {
        m->maximum_us = (uint64_t)duration;
        m->maximum_caller = caller;
    }
    if (error) ++m->errors;
    if (duration >= DISPLAY_TIMING_SLOW_US) ++m->slow;
    if (error || duration >= DISPLAY_TIMING_SLOW_US) {
        display_time_event_t *e = &s->event[s->events % DISPLAY_TIMING_EVENTS];
        e->at_us = now;
        e->duration_us = (uint64_t)duration;
        e->caller = caller;
        e->owner_caller = s->owner_caller;
        for (unsigned i = 0; i < sizeof(e->owner_name); ++i) e->owner_name[i] = s->owner_name[i];
        e->owner_known = s->depth != 0;
        e->refreshing = s->refreshing;
        e->kind = kind;
        e->error = error;
        ++s->events;
    }
}
