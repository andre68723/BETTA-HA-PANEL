/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Where LVGL's allocations land. With CONFIG_LV_USE_CUSTOM_MALLOC (config/sdkconfig.defaults) LVGL allocates through
 * ui/lvgl_alloc.c, which counts live bytes per pool (internal RAM versus PSRAM) and can prefer PSRAM
 * (CONFIG_APP_LVGL_ALLOC_PSRAM_FIRST). Without it the counters stay zero and `active` is false.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool active;               /* the counting hooks are compiled in */
    bool psram_first;          /* policy: PSRAM first, internal as fallback */
    size_t internal_bytes;     /* live bytes of LVGL allocations in internal RAM */
    size_t psram_bytes;        /* ... in PSRAM */
    size_t internal_peak;
    size_t psram_peak;
    uint32_t live_allocations;
    uint32_t internal_allocations; /* cumulative allocation count by pool since boot */
    uint32_t psram_allocations;
} lvgl_alloc_stats_t;

void lvgl_alloc_get_stats(lvgl_alloc_stats_t *out);
