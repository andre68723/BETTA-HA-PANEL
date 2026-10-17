/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t touch_init(void);
bool touch_is_ready(void);

#if defined(CONFIG_APP_PANEL_VARIANT_10INCH_JC)
#include "driver/i2c_master.h"
/* Shared bus; callers must not delete it or install a second driver. */
i2c_master_bus_handle_t touch_paneljc_i2c_bus(void);
#endif
