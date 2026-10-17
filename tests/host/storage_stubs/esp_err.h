/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include "../stubs/esp_err.h"
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_INVALID_LENGTH 0x1106
static inline const char *esp_err_to_name(esp_err_t err) { (void)err; return "injected error"; }

#define ESP_ERR_INVALID_CRC 0x109
