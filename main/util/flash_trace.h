/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * One log line per flash write ("flash write: graph history 4120 B, 38 ms"), so a visible display flash can be matched
 * against the wall-clock time of /api/log. Kept on purpose until the white/cyan flashes of the 10-inch panel are explained
 * (docs/development/learnings.md, "Display flashes"); a flash write disables the cache and the DSI refresh interrupt is
 * not cache-safe. On the host the macros compile to nothing.
 */
#pragma once

#ifdef ESP_PLATFORM
#include <stddef.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"

#define FLASH_TRACE_BEGIN(t) const int64_t t = esp_timer_get_time()
#define FLASH_TRACE_END(t, what, bytes) \
    ESP_LOGI("flash_trace", "flash write: %s %u B, %d ms", (what), (unsigned)(bytes), (int)((esp_timer_get_time() - (t)) / 1000))
#else
#define FLASH_TRACE_BEGIN(t) ((void)0)
#define FLASH_TRACE_END(t, what, bytes) ((void)(bytes))
#endif
