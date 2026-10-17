/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Host stub for the worker test (tests/host/test_history_week.c): pthread-backed FreeRTOS primitives, a counting heap and
 * a synchronous LVGL async call, so the real main/ha/ha_history_week.c runs on the host.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define MALLOC_CAP_SPIRAM 1u
#define MALLOC_CAP_8BIT 2u
#define MALLOC_CAP_INTERNAL 4u
void *heap_caps_calloc(size_t n, size_t size, uint32_t caps);
void heap_caps_free(void *ptr);
size_t heap_caps_get_free_size(uint32_t caps);
/* strlcpy is not in every libc the host tests run on */
size_t host_strlcpy(char *dst, const char *src, size_t size);
#undef strlcpy
#define strlcpy(d, s, n) host_strlcpy((d), (s), (n))
