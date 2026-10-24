/* SPDX-License-Identifier: LicenseRef-FNCL-1.1 | Copyright (c) 2026 andre68723 */
#pragma once
#include "../../week_stubs/freertos/queue.h"
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item);
void vQueueDelete(QueueHandle_t queue);
