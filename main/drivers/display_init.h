/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 Cpt_Kirk
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t display_init(void);
bool display_is_ready(void);
bool display_lock(uint32_t timeout_ms);
void display_unlock(void);
esp_err_t display_set_brightness_percent(int percent);
void display_note_activity(void);
