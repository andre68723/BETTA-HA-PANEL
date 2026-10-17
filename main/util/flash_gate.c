/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "util/flash_gate.h"

#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static _Atomic(SemaphoreHandle_t) s_gate;

bool flash_gate_enter(uint32_t timeout_ms)
{
    if (s_gate == NULL) {
        /* First use: create under a critical section so two callers do not each make one. */
        static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
        SemaphoreHandle_t fresh = xSemaphoreCreateMutex();
        if (fresh == NULL) {
            return false; /* No flash operation may bypass mutual exclusion. */
        }
        taskENTER_CRITICAL(&lock);
        if (s_gate == NULL) {
            s_gate = fresh;
            fresh = NULL;
        }
        taskEXIT_CRITICAL(&lock);
        if (fresh != NULL) {
            vSemaphoreDelete(fresh);
        }
    }
    return xSemaphoreTake(s_gate, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void flash_gate_leave(void)
{
    if (s_gate != NULL) {
        xSemaphoreGive(s_gate);
    }
}
