/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdint.h>
#include <time.h>
#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;
static inline void vTaskDelay(TickType_t ticks)
{
    const struct timespec delay = {.tv_sec = ticks / 1000, .tv_nsec = (ticks % 1000) * 1000000L};
    nanosleep(&delay, NULL);
}
static inline BaseType_t xTaskCreate(void (*task)(void *), const char *name, uint32_t stack,
                                     void *arg, unsigned priority, TaskHandle_t *handle)
{
    (void)task; (void)name; (void)stack; (void)arg; (void)priority; (void)handle;
    return pdFAIL;
}
