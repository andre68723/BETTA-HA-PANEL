/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#pragma once

#include <stddef.h>

#include "esp_heap_caps.h"

static inline void *ui_calloc_prefer_psram(size_t count, size_t size)
{
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
    void *ptr = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr != NULL) {
        return ptr;
    }
#endif
    return heap_caps_calloc(count, size, MALLOC_CAP_8BIT);
}
