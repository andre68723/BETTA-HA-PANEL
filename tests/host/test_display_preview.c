/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "drivers/display_policy.h"
#define CHECK(value) assert(value)
#define CHECK_INT(actual, expected) assert((actual) == (expected))

static void test_display_preview(void)
{
    display_test_t test = {0};
    CHECK_INT(display_test_remaining_seconds(&test, 1000), 0);
    CHECK(!display_test_update(&test, 1000, 7));
    display_test_start(&test, 1000, 0); /* A black display must still expire. */
    CHECK_INT(test.percent, 0);
    CHECK_INT(display_test_remaining_seconds(&test, 1000), 30);
    CHECK(display_test_update(&test, 11000, 7));
    CHECK_INT(test.percent, 7);
    CHECK_INT(display_test_remaining_seconds(&test, 11000), 20); /* edits do not restart */
    CHECK_INT(display_test_remaining_seconds(&test, 30999), 1);
    CHECK_INT(display_test_remaining_seconds(&test, 31000), 0);
    CHECK(!display_test_update(&test, 31000, 80)); /* late requests cannot restart a test */
    display_test_start(&test, UINT32_MAX - 10000, 50);
    CHECK_INT(display_test_remaining_seconds(&test, 9999), 10); /* clock wrap */
    display_test_stop(&test); /* button or touch */
    CHECK_INT(display_test_remaining_seconds(&test, 9999), 0);
    CHECK(!display_test_update(&test, 9999, 100));
}

int main(void)
{
    test_display_preview();
    puts("display preview: black level, fixed timeout, stop, late edits and clock wrap passed");
    return 0;
}
