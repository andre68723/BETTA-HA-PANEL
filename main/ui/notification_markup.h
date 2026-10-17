/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A bounded bold-only Markdown subset, after notification UTF-8/emoji
 * sanitization. Paired ** / __ with non-whitespace edges become emphasis.
 * Unmatched, empty and triple markers remain literal. Escapes and backtick
 * code retain literal markers. Underscores inside words are not delimiters.
 * At the run budget, the entire source is shown literally rather than lost.
 * Output is bounded to UINT16_MAX bytes. No allocations; source/destination must not overlap. */
#define NOTIF_MARKUP_MAX_RUNS 32
typedef struct {
    uint16_t offset;
    uint16_t length;
    bool bold;
} notif_markup_run_t;

typedef struct {
    size_t length;
    size_t count;
    bool bold;
} notif_markup_result_t;

notif_markup_result_t notif_markup_parse(const char *src, char *dst, size_t capacity,
                                        notif_markup_run_t *runs, size_t run_capacity);
