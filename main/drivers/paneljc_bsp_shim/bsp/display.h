/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 *
 * Minimal shim of the ESP-BSP `bsp/display.h` API for the paneljc
 * (Guition JC8012P4A1) variant. No upstream BSP package exists for this
 * board, so only the symbols the application actually uses are exposed.
 * Real implementations live in main/drivers/display_init_paneljc.c.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Turn the panel backlight off before destructive OTA/settings operations. */
esp_err_t bsp_display_backlight_off(void);

#ifdef __cplusplus
}
#endif
