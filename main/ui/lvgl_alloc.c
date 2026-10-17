/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "ui/lvgl_alloc.h"

#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"

#if defined(CONFIG_LV_USE_CUSTOM_MALLOC) && CONFIG_LV_USE_CUSTOM_MALLOC

#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "lvgl.h"

#ifdef CONFIG_APP_LVGL_ALLOC_PSRAM_FIRST
#define LVGL_ALLOC_PSRAM_FIRST 1
#else
#define LVGL_ALLOC_PSRAM_FIRST 0
#endif

static size_t s_bytes[2];       /* [0] internal, [1] PSRAM */
static size_t s_peak[2];
static uint32_t s_live;
static uint32_t s_total[2];

static void book(int pool, size_t size, int dir)
{
    if (dir > 0) {
        const size_t now = __atomic_add_fetch(&s_bytes[pool], size, __ATOMIC_RELAXED);
        size_t peak = __atomic_load_n(&s_peak[pool], __ATOMIC_RELAXED);
        while (now > peak && !__atomic_compare_exchange_n(&s_peak[pool], &peak, now, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
        }
        __atomic_add_fetch(&s_live, 1, __ATOMIC_RELAXED);
        __atomic_add_fetch(&s_total[pool], 1, __ATOMIC_RELAXED);
    } else {
        __atomic_sub_fetch(&s_bytes[pool], size, __ATOMIC_RELAXED);
        __atomic_sub_fetch(&s_live, 1, __ATOMIC_RELAXED);
    }
}

static void account(void *p, int dir)
{
    if (p == NULL) return;
    book(esp_ptr_internal(p) ? 0 : 1, heap_caps_get_allocated_size(p), dir);
}

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}
lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes) { (void)mem; (void)bytes; return NULL; }
void lv_mem_remove_pool(lv_mem_pool_t pool) { (void)pool; }
void lv_mem_monitor_core(lv_mem_monitor_t *mon) { (void)mon; }
lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }

void *lv_malloc_core(size_t size)
{
    void *p = NULL;
#if LVGL_ALLOC_PSRAM_FIRST
    p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    if (p == NULL) p = malloc(size);
    account(p, +1);
    return p;
}

void *lv_realloc_core(void *p, size_t new_size)
{
    /* read the old block's pool and size before realloc may free it */
    const int old_pool = (p != NULL && !esp_ptr_internal(p)) ? 1 : 0;
    const size_t old_size = p != NULL ? heap_caps_get_allocated_size(p) : 0;
    void *np = NULL;
#if LVGL_ALLOC_PSRAM_FIRST
    np = heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    if (np == NULL) np = realloc(p, new_size);
    if (np != NULL) { /* a failed realloc leaves the old block allocated and counted */
        if (old_size > 0) book(old_pool, old_size, -1);
        account(np, +1);
    }
    return np;
}

void lv_free_core(void *p)
{
    account(p, -1);
    free(p);
}

void lvgl_alloc_get_stats(lvgl_alloc_stats_t *out)
{
    memset(out, 0, sizeof(*out));
    out->active = true;
    out->psram_first = LVGL_ALLOC_PSRAM_FIRST;
    out->internal_bytes = s_bytes[0];
    out->psram_bytes = s_bytes[1];
    out->internal_peak = s_peak[0];
    out->psram_peak = s_peak[1];
    out->live_allocations = s_live;
    out->internal_allocations = s_total[0];
    out->psram_allocations = s_total[1];
}

#else

void lvgl_alloc_get_stats(lvgl_alloc_stats_t *out)
{
    memset(out, 0, sizeof(*out));
}

#endif
