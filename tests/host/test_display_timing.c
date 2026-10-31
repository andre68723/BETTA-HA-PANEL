/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723
 */
#include <assert.h>
#include <stdio.h>
#include "drivers/display_timing_state.h"

int main(void)
{
    display_timing_state_t s = {0};
    display_timing_record(&s, DISPLAY_TIME_LOCK_WAIT, 20, 10, false, 100);
    assert(s.metric[DISPLAY_TIME_LOCK_WAIT].count == 1 && s.events == 0);
    s.depth = 2;
    s.owner_caller = 200;
    s.refreshing = true;
    display_timing_record(&s, DISPLAY_TIME_LOCK_WAIT, 30000, 20000, true, 300);
    assert(s.events == 1 && s.event[0].error && s.event[0].owner_known);
    assert(s.event[0].owner_caller == 200 && s.event[0].caller == 300 && s.event[0].refreshing);
    assert(s.metric[DISPLAY_TIME_LOCK_WAIT].errors == 1 && s.metric[DISPLAY_TIME_LOCK_WAIT].slow == 0);
    display_timing_record(&s, DISPLAY_TIME_REFRESH, 100000, 99999, false, 0);
    assert(s.events == 1);
    display_timing_record(&s, DISPLAY_TIME_REFRESH, 200000, 100000, false, 400);
    assert(s.events == 2 && s.metric[DISPLAY_TIME_REFRESH].slow == 1);
    assert(s.metric[DISPLAY_TIME_REFRESH].maximum_caller == 400);
    for (int i = 0; i < 32; ++i)
        display_timing_record(&s, DISPLAY_TIME_ROTATION, 1000000 + i, 200000, false, 500);
    assert(s.events == 34 && s.metric[DISPLAY_TIME_ROTATION].count == 32);
    for (uint64_t i = s.events - DISPLAY_TIMING_EVENTS; i < s.events; ++i)
        assert(s.event[i % DISPLAY_TIMING_EVENTS].at_us == 1000000 + (int64_t)i - 2);
    s.metric[DISPLAY_TIME_TRANSFER].total_us = UINT32_MAX;
    display_timing_record(&s, DISPLAY_TIME_TRANSFER, 9000000, 100, false, 0);
    assert(s.metric[DISPLAY_TIME_TRANSFER].total_us == UINT64_C(4294967395));
    display_timing_record(&s, DISPLAY_TIME_SUBMIT, 10, -1, false, 0);
    assert(s.metric[DISPLAY_TIME_SUBMIT].total_us == 0);
    puts("Display timing: thresholds, errors, owner context, ring overwrite and 64-bit totals passed");
    return 0;
}
