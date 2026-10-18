/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
/* Touch input of the 10-inch panel: the contact tracker with its counters and the queue that hands every tap to LVGL. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "drivers/touch_latch.h"
#include "drivers/touch_track.h"

static int s_checks = 0;
static int s_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        s_checks++;                                                        \
        if (!(cond)) {                                                     \
            s_failures++;                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

#define CHECK_INT(actual, expected)                                                                    \
    do {                                                                                               \
        const int a_ = (actual);                                                                       \
        const int e_ = (expected);                                                                     \
        s_checks++;                                                                                    \
        if (a_ != e_) {                                                                                \
            s_failures++;                                                                              \
            printf("FAIL %s:%d: %s is %d, expected %d\n", __FILE__, __LINE__, #actual, a_, e_);        \
        }                                                                                              \
    } while (0)

static touch_track_result_t tt(touch_track_t *t, int64_t ms, int n, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                              uint16_t *ox, uint16_t *oy)
{
    uint16_t xs[2] = {x0, x1};
    uint16_t ys[2] = {y0, y1};
    return touch_track_update(t, xs, ys, n, ms * 1000, ox, oy);
}

static void test_touch_track(void)
{
    touch_track_t t;
    uint16_t ox = 0, oy = 0;
    touch_track_reset(&t);

    /* a plain tap: reported from the second frame on, at the point the finger is at */
    CHECK_INT(tt(&t, 0, 1, 400, 300, 0, 0, &ox, &oy), TOUCH_TRACK_NONE);
    CHECK_INT(tt(&t, 10, 1, 402, 301, 0, 0, &ox, &oy), TOUCH_TRACK_REPORT);
    CHECK(ox == 402 && oy == 301);
    CHECK_INT(tt(&t, 20, 1, 403, 301, 0, 0, &ox, &oy), TOUCH_TRACK_REPORT);

    /* the ghost the owner hit: the first frame of a contact is a stray point at the edge, then the real finger */
    touch_track_reset(&t);
    CHECK_INT(tt(&t, 1000, 1, 799, 664, 0, 0, &ox, &oy), TOUCH_TRACK_NONE);   /* ghost: only a candidate */
    CHECK_INT(tt(&t, 1010, 1, 435, 672, 0, 0, &ox, &oy), TOUCH_TRACK_NONE);   /* real finger: far from the ghost, waits */
    CHECK_INT(tt(&t, 1020, 1, 436, 672, 0, 0, &ox, &oy), TOUCH_TRACK_REPORT); /* confirmed */
    CHECK(ox == 436 && oy == 672);
    CHECK_INT(tt(&t, 1030, 1, 437, 671, 0, 0, &ox, &oy), TOUCH_TRACK_REPORT);

    /* a stray jump while the finger is down is held, the finger keeps its place */
    CHECK_INT(tt(&t, 1040, 1, 780, 100, 0, 0, &ox, &oy), TOUCH_TRACK_HELD);
    CHECK(ox == 437 && oy == 671);
    CHECK_INT(tt(&t, 1050, 1, 438, 670, 0, 0, &ox, &oy), TOUCH_TRACK_REPORT);
    CHECK(ox == 438 && oy == 670);

    /* two points reported, the second one is a stray: the one near the finger is followed, even when listed second */
    CHECK_INT(tt(&t, 1060, 2, 700, 900, 439, 670, &ox, &oy), TOUCH_TRACK_REPORT);
    CHECK(ox == 439 && oy == 670);

    /* a fast drag stays inside the allowed speed: 300 px in 40 ms (7.5 px/ms) is no jump */
    touch_track_reset(&t);
    (void)tt(&t, 0, 1, 100, 100, 0, 0, &ox, &oy);
    (void)tt(&t, 10, 1, 100, 100, 0, 0, &ox, &oy);
    CHECK_INT(tt(&t, 50, 1, 100, 400, 0, 0, &ox, &oy), TOUCH_TRACK_REPORT);
    CHECK(oy == 400);

    /* a real jump that repeats is accepted after the hold limit (a second finger that stays) */
    CHECK_INT(tt(&t, 60, 1, 700, 1200, 0, 0, &ox, &oy), TOUCH_TRACK_HELD);
    CHECK_INT(tt(&t, 70, 1, 700, 1200, 0, 0, &ox, &oy), TOUCH_TRACK_HELD);
    CHECK_INT(tt(&t, 80, 1, 700, 1200, 0, 0, &ox, &oy), TOUCH_TRACK_REPORT);
    CHECK(ox == 700 && oy == 1200);

    /* no usable point: held for two frames, then the contact ends */
    CHECK_INT(tt(&t, 90, 0, 0, 0, 0, 0, &ox, &oy), TOUCH_TRACK_HELD);
    CHECK_INT(tt(&t, 100, 0, 0, 0, 0, 0, &ox, &oy), TOUCH_TRACK_HELD);
    CHECK_INT(tt(&t, 110, 0, 0, 0, 0, 0, &ox, &oy), TOUCH_TRACK_NONE);
    CHECK(!t.tracking);

    /* after a pause the next report is a new contact, not a jump */
    touch_track_reset(&t);
    (void)tt(&t, 0, 1, 100, 100, 0, 0, &ox, &oy);
    (void)tt(&t, 10, 1, 100, 100, 0, 0, &ox, &oy);
    CHECK_INT(tt(&t, 500, 1, 600, 1000, 0, 0, &ox, &oy), TOUCH_TRACK_NONE);      /* candidate of a new contact */
    CHECK_INT(tt(&t, 510, 1, 601, 1001, 0, 0, &ox, &oy), TOUCH_TRACK_REPORT);

    /* a point that keeps changing is accepted after the start frames */
    touch_track_reset(&t);
    CHECK_INT(tt(&t, 0, 1, 100, 100, 0, 0, &ox, &oy), TOUCH_TRACK_NONE);
    CHECK_INT(tt(&t, 10, 1, 400, 700, 0, 0, &ox, &oy), TOUCH_TRACK_NONE);
    CHECK_INT(tt(&t, 20, 1, 700, 200, 0, 0, &ox, &oy), TOUCH_TRACK_REPORT);

    /* raw range: the saturated edge value is a ghost marker, the end of the valid range is not */
    CHECK(touch_track_raw_valid(1650, 890, 1650, 890));
    CHECK(!touch_track_raw_valid(1651, 100, 1650, 890));
    CHECK(!touch_track_raw_valid(100, 891, 1650, 890));
    CHECK(!touch_track_raw_valid(0, 0, 1650, 890));
    CHECK(touch_track_raw_valid(20, 20, 1650, 890));
    CHECK_INT(touch_track_update(NULL, NULL, NULL, 0, 0, &ox, &oy), TOUCH_TRACK_NONE);

    /* weak-signal flag: the owner's log shows raw Y 16408, 16584 and 17265 = 0x4018, 0x40C8, 0x4371 (flag + coordinate) */
    CHECK_INT(16408 & TOUCH_TRACK_Y_MASK, 24);
    CHECK_INT(16584 & TOUCH_TRACK_Y_MASK, 200);
    CHECK_INT(17265 & TOUCH_TRACK_Y_MASK, 881);
    CHECK((16408 & TOUCH_TRACK_Y_FLAG) != 0 && (200 & TOUCH_TRACK_Y_FLAG) == 0);
    CHECK(touch_track_raw_valid(398, 16408 & TOUCH_TRACK_Y_MASK, 1650, 890));
    CHECK(touch_track_raw_valid(1329, 17265 & TOUCH_TRACK_Y_MASK, 1650, 890));

    /* a weak frame while a contact waits keeps the candidate: quick tap, first and third frame usable */
    touch_track_stats_clear();
    touch_track_reset(&t);
    CHECK_INT(tt(&t, 0, 1, 400, 300, 0, 0, &ox, &oy), TOUCH_TRACK_NONE);
    CHECK_INT(tt(&t, 10, 0, 0, 0, 0, 0, &ox, &oy), TOUCH_TRACK_NONE);     /* weak frame: skipped */
    CHECK(t.pending);
    CHECK_INT(tt(&t, 20, 1, 402, 301, 0, 0, &ox, &oy), TOUCH_TRACK_REPORT);
    CHECK(ox == 402 && oy == 301);
    CHECK_INT((int)touch_track_stats()->contacts, 1);
    CHECK_INT((int)touch_track_stats()->confirmed, 1);
    CHECK_INT((int)touch_track_stats()->lost, 0);

    /* a candidate that is never confirmed (finger lifted, or stale) is counted as lost */
    touch_track_reset(&t);
    (void)tt(&t, 100, 1, 400, 300, 0, 0, &ox, &oy);
    touch_track_reset(&t);                                              /* driver: controller reports no finger */
    CHECK_INT((int)touch_track_stats()->lost, 1);
    (void)tt(&t, 200, 1, 400, 300, 0, 0, &ox, &oy);
    (void)tt(&t, 900, 1, 700, 900, 0, 0, &ox, &oy);                     /* stale candidate replaced */
    CHECK_INT((int)touch_track_stats()->lost, 2);
    CHECK_INT((int)touch_track_stats()->contacts, 4);

    /* a weak frame that comes after a long gap does not keep a stale candidate alive */
    touch_track_reset(&t);
    (void)tt(&t, 1000, 1, 400, 300, 0, 0, &ox, &oy);
    CHECK_INT(tt(&t, 1500, 0, 0, 0, 0, 0, &ox, &oy), TOUCH_TRACK_NONE);
    CHECK(!t.pending);

    /* polls: a gap above the late limit and the longest gap are counted, an I2C error is not an empty poll */
    touch_track_stats_clear();
    touch_track_note_read(1000000, false, true);
    touch_track_note_read(1010000, false, false);
    touch_track_note_read(1200000, true, false);
    CHECK_INT((int)touch_track_stats()->reads, 3);
    CHECK_INT((int)touch_track_stats()->empty, 1);
    CHECK_INT((int)touch_track_stats()->i2c_errors, 1);
    CHECK_INT((int)touch_track_stats()->late, 1);
    CHECK_INT((int)touch_track_stats()->max_gap_ms, 190);

    touch_track_stats_clear();
    touch_track_note_frame(2, 1);
    CHECK_INT((int)touch_track_stats()->flagged, 2);
    CHECK_INT((int)touch_track_stats()->invalid, 1);
    CHECK_INT((int)touch_track_stats()->frames, 1);
}

static void test_touch_latch(void)
{
    touch_latch_t l;
    uint16_t x = 0, y = 0;
    touch_latch_reset(&l);
    CHECK(!touch_latch_read(&l, 0, &x, &y));                    /* nothing scanned */

    /* a tap that starts and ends between two LVGL reads is delivered: pressed once, then released */
    touch_latch_scan(&l, 1000, true, 400, 300);
    touch_latch_scan(&l, 1005, true, 402, 301);
    touch_latch_scan(&l, 1010, false, 0, 0);
    CHECK(touch_latch_read(&l, 1020, &x, &y) && x == 402 && y == 301);
    CHECK(!touch_latch_read(&l, 1030, &x, &y));                 /* release */
    CHECK(!touch_latch_read(&l, 1040, &x, &y));

    /* a long press stays pressed for every read and follows the finger */
    touch_latch_scan(&l, 2000, true, 100, 200);
    CHECK(touch_latch_read(&l, 2010, &x, &y) && x == 100);
    CHECK(touch_latch_read(&l, 2020, &x, &y) && x == 100);
    touch_latch_scan(&l, 2025, true, 110, 205);
    CHECK(touch_latch_read(&l, 2030, &x, &y) && x == 110 && y == 205);
    touch_latch_scan(&l, 2035, false, 0, 0);
    CHECK(!touch_latch_read(&l, 2040, &x, &y));                 /* release */
    CHECK(!touch_latch_read(&l, 2050, &x, &y));

    /* several quick taps between two reads stay separate: press, release, press, release, in order (the owner's tab-bar
     * test: 33 taps gave 30 contacts but only 22 presses when taps were merged) */
    touch_latch_reset(&l);
    touch_latch_scan(&l, 3000, true, 10, 20);
    touch_latch_scan(&l, 3010, false, 0, 0);
    touch_latch_scan(&l, 3020, true, 30, 40);
    touch_latch_scan(&l, 3030, false, 0, 0);
    touch_latch_scan(&l, 3040, true, 50, 60);                   /* the third one is still down */
    CHECK(touch_latch_read(&l, 3050, &x, &y) && x == 10 && y == 20);
    CHECK(!touch_latch_read(&l, 3060, &x, &y));
    CHECK(touch_latch_read(&l, 3070, &x, &y) && x == 30 && y == 40);
    CHECK(!touch_latch_read(&l, 3080, &x, &y));
    CHECK(touch_latch_read(&l, 3090, &x, &y) && x == 50 && y == 60);
    CHECK(touch_latch_read(&l, 3100, &x, &y) && x == 50);       /* still pressed */
    touch_latch_scan(&l, 3110, false, 0, 0);
    CHECK(!touch_latch_read(&l, 3120, &x, &y));
    CHECK(!touch_latch_read(&l, 3130, &x, &y));

    /* a finished tap that waits too long is dropped, a finger that is still down never is */
    touch_latch_reset(&l);
    touch_latch_scan(&l, 0, true, 1, 2);
    touch_latch_scan(&l, 10, false, 0, 0);
    touch_latch_scan(&l, 20, true, 3, 4);
    touch_latch_scan(&l, 30, false, 0, 0);
    touch_latch_scan(&l, TOUCH_LATCH_MAX_AGE_US + 15, true, 5, 6);
    CHECK(touch_latch_read(&l, TOUCH_LATCH_MAX_AGE_US + 100, &x, &y) && x == 5 && y == 6);  /* the two old taps are gone */
    CHECK(touch_latch_read(&l, TOUCH_LATCH_MAX_AGE_US * 3, &x, &y) && x == 5);               /* still down: kept */
    touch_latch_scan(&l, TOUCH_LATCH_MAX_AGE_US * 3 + 5, false, 0, 0);
    CHECK(!touch_latch_read(&l, TOUCH_LATCH_MAX_AGE_US * 3 + 10, &x, &y));

    /* a full queue drops the newest taps, not the ones LVGL waits for */
    touch_latch_reset(&l);
    for (int i = 0; i < TOUCH_LATCH_CAPACITY + 2; i++) {
        touch_latch_scan(&l, 100 + i * 20, true, (uint16_t)(10 + i), 0);
        touch_latch_scan(&l, 110 + i * 20, false, 0, 0);
    }
    int delivered = 0;
    uint16_t first = 0, last = 0;
    for (int i = 0; i < 40; i++) {
        if (touch_latch_read(&l, 1000 + i, &x, &y)) {
            if (delivered == 0) first = x;
            last = x;
            delivered++;
        }
    }
    CHECK_INT(delivered, TOUCH_LATCH_CAPACITY);
    CHECK(first == 10 && last == 10 + TOUCH_LATCH_CAPACITY - 1);

    CHECK(!touch_latch_read(NULL, 0, &x, &y) && !touch_latch_read(&l, 0, NULL, &y));
    touch_latch_scan(NULL, 0, true, 1, 1);
}

int main(void)
{
    test_touch_latch();
    test_touch_track();
    printf("touch: %d checks, %d failed\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
