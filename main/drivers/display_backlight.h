/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 *
 * The seam between display_brightness.c (the brightness behaviour every board shares) and the board file that owns
 * the backlight hardware.
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int gpio_pin, requested_percent;
    uint32_t frequency_hz, duty, max_duty, samples, high_samples, elapsed_us;
    uint32_t gpio_function, gpio_signal;
    bool gpio_input, gpio_output, gpio_peripheral_output, gpio_open_drain, stable;
} display_backlight_diagnostics_t;

/* Implemented by each board's display_init_*.c: drive the backlight to `percent` (0..100, already clamped, and only
 * called when it differs from the last value written). Log failures here; the shared code only remembers success. */
esp_err_t display_backlight_write(int percent);

/* Optional bounded pin-level sample, called with the display lock held. Does not measure light output. */
bool display_backlight_diagnostics(display_backlight_diagnostics_t *out);

/* Called by the board once the display is up, before it announces itself ready: creates the dim timer (a failure is
 * not fatal: the display then never dims). */
esp_err_t display_brightness_start(void);
