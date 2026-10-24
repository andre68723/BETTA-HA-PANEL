/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#pragma once
#include "../week_stubs/esp_heap_caps.h"
void *heap_caps_malloc(size_t size, uint32_t caps);
size_t heap_caps_get_minimum_free_size(uint32_t caps);
