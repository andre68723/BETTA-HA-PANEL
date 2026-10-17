/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#pragma once
#include <stdint.h>
#include "esp_err.h"
esp_err_t esp_efuse_mac_get_default(uint8_t *);
