/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool available;
    float celsius;
    int64_t sampled_us;
    esp_err_t error;
} chip_temperature_snapshot_t;

/* Boot-only initialization; diagnostic failure must never prevent startup. */
esp_err_t chip_temperature_init(void);
chip_temperature_snapshot_t chip_temperature_snapshot(void);
