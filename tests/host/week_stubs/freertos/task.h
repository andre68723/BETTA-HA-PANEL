/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * Host stub for the worker test (tests/host/test_history_week.c): pthread-backed FreeRTOS primitives, a counting heap and
 * a synchronous LVGL async call, so the real main/ha/ha_history_week.c runs on the host.
 */
#pragma once
#include "freertos/FreeRTOS.h"
typedef void *TaskHandle_t;
void vTaskDelay(TickType_t ticks);
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg, int priority,
                                   TaskHandle_t *handle, int core);
