/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
typedef struct {
    bool enabled, sensor_ready;
    float x_g, y_g, z_g;
    int boot_degrees, saved_degrees;
} display_orientation_status_t;
bool display_orientation_supported(void);
void display_orientation_configure(bool enabled);
/* After BSP/I2C initialization, before creating the first splash; bounded sensor read with saved fallback. */
int display_orientation_prepare_boot(void);
esp_err_t display_orientation_start(void);
void display_orientation_get_status(display_orientation_status_t *out);
/* UI task only, with the display lock held. */
bool display_orientation_apply(int degrees);
