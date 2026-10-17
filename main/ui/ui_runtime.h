/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 * Modified 2026 by andre68723 in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once

#include <stdint.h>

/* Shared condition-to-icon mapping for the top bar and weather saver. */
uint32_t ui_runtime_weather_icon_codepoint_for_key(const char *key);

#include "esp_err.h"

esp_err_t ui_runtime_init(void);
esp_err_t ui_runtime_load_layout(const char *layout_json);
esp_err_t ui_runtime_reload_layout(void);
esp_err_t ui_runtime_start(void);
