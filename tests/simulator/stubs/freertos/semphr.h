/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include "freertos/FreeRTOS.h"

typedef void *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, int ticks)
{ (void)semaphore; (void)ticks; return pdTRUE; }
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{ (void)semaphore; return pdTRUE; }
static inline void vSemaphoreDelete(SemaphoreHandle_t semaphore) { (void)semaphore; }
