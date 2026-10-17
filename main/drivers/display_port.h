/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- see docs/release-notes.md
 */
#pragma once

#include "esp_lv_adapter.h"

esp_err_t display_port_init(void);
esp_err_t display_port_start(void);
bool display_port_lock(uint32_t timeout_ms);
void display_port_unlock(void);
lv_display_t *display_port_add_dsi(esp_lcd_panel_handle_t panel,
                                  esp_lcd_panel_io_handle_t io,
                                  uint16_t native_width, uint16_t native_height,
                                  bool rotate, bool swap_bytes);
