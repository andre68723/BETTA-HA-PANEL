/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stddef.h>
#include "freertos/FreeRTOS.h"

typedef void *QueueHandle_t;
static inline QueueHandle_t xQueueCreate(unsigned length, unsigned item_size)
{ (void)length; (void)item_size; return NULL; }
static inline BaseType_t xQueueReceive(QueueHandle_t queue, void *item, int ticks)
{ (void)queue; (void)item; (void)ticks; return pdFALSE; }
static inline BaseType_t xQueueSend(QueueHandle_t queue, const void *item, int ticks)
{ (void)queue; (void)item; (void)ticks; return pdFALSE; }
static inline void vQueueDelete(QueueHandle_t queue) { (void)queue; }
